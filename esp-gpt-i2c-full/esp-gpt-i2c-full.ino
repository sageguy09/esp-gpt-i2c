// ESP32 Artnet I2SClockless with Web UI + OLED Debug
// Built for ESP-WROOM-32 using 3-4 pins with up to 3 WS2812B strips per pin
// Features: Artnet, web interface config, OLED status, debug log

#include <WiFi.h>
#include <WebServer.h>
#include <Adafruit_NeoPixel.h> // Add NeoPixel library for the test function

// Include the common code
#include "ESP_GPT_I2C_Common.h"

// Define constants that are used early in the code
#define UNIVERSE_SIZE 510
#define MAX_STRIPS 12       // 4 pins × 3 strips per pin
#define PACKET_TIMEOUT 5000 // 5 seconds without packets is a timeout

// This must be defined before including I2SClocklessLedDriver.h
#ifndef NUM_LEDS_PER_STRIP
#define NUM_LEDS_PER_STRIP 144 // Define this before library includes
#endif

#include "I2SClocklessLedDriver.h"
#include "artnetESP32V2.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include "UARTCommunicationBridge.h"

// Define the UART pin configuration for the communication bridge
#define UART_RX_PIN 16 // GPIO pin for UART RX
#define UART_TX_PIN 17 // GPIO pin for UART TX

// Create a UART bridge instance using Serial2
UARTCommunicationBridge uartBridge(Serial2);

// UART command handler callback
void handleUARTCommand(uint8_t cmd, uint8_t *data, uint16_t length);

// DMX data buffer for storing the most recent ArtNet data
uint8_t dmxData[UNIVERSE_SIZE];
unsigned long lastPacketTime = 0;
uint32_t packetsReceived = 0;

// ====== CONFIGURATION ======
#define STATUS_LED_PIN 16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define NUMSTRIPS 1
#define NB_CHANNEL_PER_LED 3
#define STRIPS_PER_PIN 3

// Extended from common code
#undef MAX_LOG_ENTRIES
#define MAX_LOG_ENTRIES 50

// Color cycle modes
#define COLOR_MODE_STATIC 0
#define COLOR_MODE_RAINBOW 1
#define COLOR_MODE_PULSE 2
#define COLOR_MODE_FIRE 3

// ====== LED COLOR TYPE ======
typedef struct
{
  uint8_t r;
  uint8_t g;
  uint8_t b;
} rgb24;

// ====== GLOBAL OBJECTS ======
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
WebServer server(80);
I2SClocklessLedDriver driver;
artnetESP32V2 artnet;

rgb24 ledData[MAX_STRIPS * NUM_LEDS_PER_STRIP]; // Fixed error with NUM_LEDS_PER_STRIP
bool sendFrame = false;

// Anti-boot loop protection
// These are global variables to track critical errors and prevent retries
bool ledHardwareFailed = false;
bool inSafeMode = false;        // Global variable for safe mode
int bootCount = 0;              // Boot counter for loop detection
unsigned long lastBootTime = 0; // Time of last boot attempt

// Extended settings structure for full version
struct FullSettings : Settings
{
  int numStrips;
  int ledsPerStrip;
  int pins[4];
  int brightness;
  int startUniverse;

  // Added settings for static color and mode controls
  bool useArtnet;
  bool useColorCycle;
  bool useStaticColor; // Flag to enable/disable static color
  int colorMode;
  rgb24 staticColor;
  int cycleSpeed;
};

// Initialize the settings with default values
FullSettings fullSettings;

void initializeDefaultSettings()
{
  // Base settings initialization
  fullSettings.ssid = "Sage1";
  fullSettings.password = "J@sper123";
  fullSettings.useWiFi = false;
  fullSettings.nodeName = "ESP32_Artnet_I2S";

  // Extended settings initialization
  fullSettings.numStrips = 1;
  fullSettings.ledsPerStrip = 144;
  fullSettings.pins[0] = 12;
  fullSettings.pins[1] = -1;
  fullSettings.pins[2] = -1;
  fullSettings.pins[3] = -1;
  fullSettings.brightness = 255;
  fullSettings.startUniverse = 0;
  fullSettings.useArtnet = false;
  fullSettings.useColorCycle = false;
  fullSettings.useStaticColor = true;
  fullSettings.colorMode = COLOR_MODE_RAINBOW;
  fullSettings.staticColor.r = 255;
  fullSettings.staticColor.g = 0;
  fullSettings.staticColor.b = 255;
  fullSettings.cycleSpeed = 50;
}

void handleDebugLog()
{
  String json = "[";
  for (int i = 0; i < MAX_LOG_ENTRIES; i++)
  {
    int idx = (state.logIndex + i) % MAX_LOG_ENTRIES;
    if (state.logs[idx].length())
    {
      if (i > 0)
        json += ",";
      json += "\"" + state.logs[idx] + "\"";
    }
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ====== WEB UI ======
void handleRoot()
{
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>ESP32 Artnet Controller</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; }";
  html += ".form-group { margin-bottom: 15px; }";
  html += "label { display: inline-block; width: 120px; }";
  html += "input[type='number'], input[type='text'], input[type='password'] { width: 100px; }";
  html += "input[type='color'] { width: 60px; height: 30px; }";
  html += "input[type='checkbox'] { margin-right: 5px; }";
  html += "select { width: 120px; }";
  html += ".section { background: #f5f5f5; padding: 10px; margin-bottom: 15px; border-radius: 5px; }";
  html += ".section h3 { margin-top: 0; }";
  html += "button { background: #4CAF50; color: white; padding: 10px 15px; border: none; border-radius: 4px; cursor: pointer; }";
  html += "button:hover { background: #45a049; }";
  html += ".error-message { background: #ffebee; color: #c62828; padding: 10px; border-radius: 5px; margin-bottom: 15px; border-left: 5px solid #c62828; }";
  html += "</style>";
  html += "</head><body>";
  html += "<h2>ESP32 Artnet LED Controller</h2>";

  // CRITICAL FIX: Show a warning message if network has been disabled due to failures
  if (networkInitFailed)
  {
    html += "<div class='error-message'>";
    html += "<strong>NOTICE:</strong> Network functionality has been permanently disabled due to critical failures. ";
    html += "This device will operate in standalone mode only. WiFi and ArtNet settings cannot be changed.";
    html += "</div>";
  }

  html += "<form method='POST' action='/config'>";

  // LED Configuration Section
  html += "<div class='section'>";
  html += "<h3>LED Configuration</h3>";
  html += "<div class='form-group'><label>LEDs per Strip:</label><input type='number' name='leds' value='" + String(fullSettings.ledsPerStrip) + "'></div>";
  html += "<div class='form-group'><label>Number of Strips:</label><input type='number' name='strips' value='" + String(fullSettings.numStrips) + "'></div>";
  html += "<div class='form-group'><label>Brightness:</label><input type='range' min='0' max='255' name='bright' value='" + String(fullSettings.brightness) + "'></div>";

  // Individual pin configuration
  html += "<div class='form-group'><label>Pin Configuration:</label></div>";
  html += "<div style='margin-left: 20px;'>";
  for (int i = 0; i < 4; i++)
  {
    html += "<div class='form-group'><label>Pin " + String(i + 1) + ":</label>";
    html += "<input type='number' min='-1' max='39' name='pin" + String(i) + "' value='" + String(fullSettings.pins[i]) + "'>";
    html += "</div>";
  }
  html += "</div>";

  html += "</div>";

  // Mode Selection Section
  html += "<div class='section'>";
  html += "<h3>Mode Selection</h3>";

  // ArtNet Mode Toggle
  html += "<div class='form-group'>";
  html += "<input type='checkbox' id='useArtnet' name='useArtnet' value='1' " + String(fullSettings.useArtnet ? "checked" : "") + ">";
  html += "<label for='useArtnet'>Use ArtNet Mode</label>";
  html += "</div>";

  // Static Color Toggle
  html += "<div class='form-group'>";
  html += "<input type='checkbox' id='useStaticColor' name='useStaticColor' value='1' " + String(fullSettings.useStaticColor ? "checked" : "") + ">";
  html += "<label for='useStaticColor'>Use Static Color</label>";
  html += "</div>";

  // Color Cycle Toggle
  html += "<div class='form-group'>";
  html += "<input type='checkbox' id='useColorCycle' name='useColorCycle' value='1' " + String(fullSettings.useColorCycle ? "checked" : "") + ">";
  html += "<label for='useColorCycle'>Use Color Cycle Effects</label>";
  html += "</div>";

  // Color Mode Selection (only visible when color cycle is enabled)
  html += "<div class='form-group' id='colorModeGroup'>";
  html += "<label for='colorMode'>Color Mode:</label>";
  html += "<select name='colorMode' id='colorMode'>";
  html += "<option value='" + String(COLOR_MODE_RAINBOW) + "'" + (fullSettings.colorMode == COLOR_MODE_RAINBOW ? " selected" : "") + ">Rainbow</option>";
  html += "<option value='" + String(COLOR_MODE_PULSE) + "'" + (fullSettings.colorMode == COLOR_MODE_PULSE ? " selected" : "") + ">Pulse</option>";
  html += "<option value='" + String(COLOR_MODE_FIRE) + "'" + (fullSettings.colorMode == COLOR_MODE_FIRE ? " selected" : "") + ">Fire</option>";
  html += "</select>";
  html += "</div>";

  // Static Color Selection (only visible when color cycle is disabled)
  html += "<div class='form-group' id='staticColorGroup'>";
  html += "<label for='staticColor'>Static Color:</label>";
  html += "<input type='color' id='staticColor' name='staticColor' value='#" +
          String(fullSettings.staticColor.r, HEX) + String(fullSettings.staticColor.g, HEX) + String(fullSettings.staticColor.b, HEX) + "'>";
  html += "</div>";

  // Cycle Speed (only visible when color cycle is enabled)
  html += "<div class='form-group' id='cycleSpeedGroup'>";
  html += "<label for='cycleSpeed'>Cycle Speed:</label>";
  html += "<input type='range' min='1' max='100' name='cycleSpeed' id='cycleSpeed' value='" + String(fullSettings.cycleSpeed) + "'>";
  html += "</div>";
  html += "</div>";

  // WiFi Configuration Section
  html += "<div class='section'>";
  html += "<h3>WiFi Configuration</h3>";
  html += "<div class='form-group'><label>SSID:</label><input type='text' name='ssid' value='" + fullSettings.ssid + "'></div>";
  html += "<div class='form-group'><label>Password:</label><input type='password' name='pass' value='" + fullSettings.password + "'></div>";
  html += "<div class='form-group'><label>Node Name:</label><input type='text' name='nodeName' value='" + fullSettings.nodeName + "'></div>";
  html += "</div>";

  // Submit Button
  html += "<button type='submit'>Save Settings</button>";
  html += "</form>";

  // Add JavaScript to show/hide elements based on toggles
  html += "<script>";
  html += "function updateVisibility() {";
  html += "  var useArtnet = document.getElementById('useArtnet').checked;";
  html += "  var useColorCycle = document.getElementById('useColorCycle').checked;";
  html += "  var useStaticColor = document.getElementById('useStaticColor').checked;";

  // Show/hide elements based on selected mode
  html += "  document.getElementById('colorModeGroup').style.display = (useColorCycle && !useArtnet) ? 'block' : 'none';";
  html += "  document.getElementById('staticColorGroup').style.display = (useStaticColor && !useArtnet) ? 'block' : 'none';";
  html += "  document.getElementById('cycleSpeedGroup').style.display = (useColorCycle && !useArtnet) ? 'block' : 'none';";

  // Handle mutual exclusivity
  html += "  if(useArtnet && (useColorCycle || useStaticColor)) {";
  html += "    if(this.id === 'useArtnet') {";
  html += "      document.getElementById('useColorCycle').checked = false;";
  html += "      document.getElementById('useStaticColor').checked = false;";
  html += "    } else {";
  html += "      document.getElementById('useArtnet').checked = false;";
  html += "    }";
  html += "  }";

  // Make static color and color cycle mutually exclusive
  html += "  if(useColorCycle && useStaticColor) {";
  html += "    if(this.id === 'useColorCycle') {";
  html += "      document.getElementById('useStaticColor').checked = false;";
  html += "    } else {";
  html += "      document.getElementById('useColorCycle').checked = false;";
  html += "    }";
  html += "  }";

  html += "}";
  html += "document.getElementById('useArtnet').addEventListener('change', updateVisibility);";
  html += "document.getElementById('useColorCycle').addEventListener('change', updateVisibility);";
  html += "document.getElementById('useStaticColor').addEventListener('change', updateVisibility);";
  html += "updateVisibility(); // Initial call";
  html += "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleConfig()
{
  // Process LED configuration
  if (server.hasArg("leds"))
    fullSettings.ledsPerStrip = server.arg("leds").toInt();
  if (server.hasArg("strips"))
    fullSettings.numStrips = server.arg("strips").toInt();
  if (server.hasArg("bright"))
    fullSettings.brightness = server.arg("bright").toInt();

  // Process individual pin configuration
  for (int i = 0; i < 4; i++)
  {
    String pinArg = "pin" + String(i);
    if (server.hasArg(pinArg))
    {
      fullSettings.pins[i] = server.arg(pinArg).toInt();
    }
  }

  // CRITICAL FIX: If network has previously failed, ignore attempts to enable network features
  if (!networkInitFailed)
  {
    // Process mode settings
    fullSettings.useArtnet = server.hasArg("useArtnet");

    // Process WiFi settings
    if (server.hasArg("ssid"))
      fullSettings.ssid = server.arg("ssid");
    if (server.hasArg("pass"))
      fullSettings.password = server.arg("pass");
    if (server.hasArg("nodeName"))
      fullSettings.nodeName = server.arg("nodeName");

    // Update the common settings
    settings.ssid = fullSettings.ssid;
    settings.password = fullSettings.password;
    settings.nodeName = fullSettings.nodeName;
    settings.useWiFi = fullSettings.useArtnet || fullSettings.useWiFi;

    // Check if we need to restart network functionality
    bool wifiConfigChanged = false;
    if (server.hasArg("ssid") && fullSettings.ssid != server.arg("ssid"))
      wifiConfigChanged = true;
    if (server.hasArg("pass") && fullSettings.password != server.arg("pass"))
      wifiConfigChanged = true;

    // If network config changed, we'll need to restart network services
    if (wifiConfigChanged && (fullSettings.useArtnet || settings.useWiFi))
    {
      debugLog("WiFi configuration changed - preparing to restart network services");
      // Save the flag for network restart
      preferences.begin("led-settings", false);
      preferences.putBool("netRestart", true);
      preferences.end();
    }
  }
  else
  {
    // Always force these to be disabled if network has failed
    fullSettings.useArtnet = false;
    settings.useWiFi = false;
    // Log the override
    debugLog("WARNING: Network settings change ignored due to previous failure");
  }

  // Always allow these non-network settings
  fullSettings.useColorCycle = server.hasArg("useColorCycle");
  fullSettings.useStaticColor = server.hasArg("useStaticColor");

  if (server.hasArg("colorMode"))
    fullSettings.colorMode = server.arg("colorMode").toInt();

  if (server.hasArg("cycleSpeed"))
    fullSettings.cycleSpeed = server.arg("cycleSpeed").toInt();

  // Process static color (convert from hex to RGB)
  if (server.hasArg("staticColor"))
  {
    String colorHex = server.arg("staticColor");
    // Remove '#' if present
    if (colorHex.startsWith("#"))
    {
      colorHex = colorHex.substring(1);
    }

    // Parse the hex color value
    long colorValue = strtol(colorHex.c_str(), NULL, 16);
    fullSettings.staticColor.r = (colorValue >> 16) & 0xFF;
    fullSettings.staticColor.g = (colorValue >> 8) & 0xFF;
    fullSettings.staticColor.b = colorValue & 0xFF;
  }

  // Save settings to preferences
  saveSettings();

  // Normal operation - just redirect back to root page
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Settings Updated");
}

// ====== SETTINGS FUNCTIONS ======
void loadSettings()
{
  preferences.begin("led-settings", false);

  // Load LED configuration
  fullSettings.numStrips = preferences.getInt("numStrips", fullSettings.numStrips);
  fullSettings.ledsPerStrip = preferences.getInt("ledsPerStrip", fullSettings.ledsPerStrip);
  fullSettings.brightness = preferences.getInt("brightness", fullSettings.brightness);

  // Load pins array
  String pinsStr = preferences.getString("pins", "");
  if (pinsStr.length() > 0)
  {
    int index = 0;
    int commaPos = 0;
    while (commaPos >= 0 && index < 4)
    {
      commaPos = pinsStr.indexOf(',');
      if (commaPos > 0)
      {
        fullSettings.pins[index++] = pinsStr.substring(0, commaPos).toInt();
        pinsStr = pinsStr.substring(commaPos + 1);
      }
      else if (pinsStr.length() > 0)
      {
        fullSettings.pins[index++] = pinsStr.toInt();
        break;
      }
    }
  }

  // Load WiFi configuration from preferences to both settings structures
  fullSettings.ssid = preferences.getString("ssid", fullSettings.ssid);
  fullSettings.password = preferences.getString("password", fullSettings.password);
  fullSettings.nodeName = preferences.getString("nodeName", fullSettings.nodeName);
  fullSettings.startUniverse = preferences.getInt("startUniverse", fullSettings.startUniverse);

  // Update common settings
  settings.ssid = fullSettings.ssid;
  settings.password = fullSettings.password;
  settings.nodeName = fullSettings.nodeName;

  // Load mode settings
  fullSettings.useArtnet = preferences.getBool("useArtnet", fullSettings.useArtnet);
  fullSettings.useColorCycle = preferences.getBool("useColorCycle", fullSettings.useColorCycle);
  fullSettings.useStaticColor = preferences.getBool("useStaticColor", fullSettings.useStaticColor);
  fullSettings.colorMode = preferences.getInt("colorMode", fullSettings.colorMode);
  fullSettings.cycleSpeed = preferences.getInt("cycleSpeed", fullSettings.cycleSpeed);

  // Load static color
  fullSettings.staticColor.r = preferences.getInt("staticColorR", fullSettings.staticColor.r);
  fullSettings.staticColor.g = preferences.getInt("staticColorG", fullSettings.staticColor.g);
  fullSettings.staticColor.b = preferences.getInt("staticColorB", fullSettings.staticColor.b);

  // Load network failure state - CRITICAL FIX for persistent boot-loop prevention
  networkInitFailed = preferences.getBool("netFailed", false);
  if (networkInitFailed)
  {
    debugLog("CRITICAL: Network previously failed and disabled permanently");
  }

  // Set common WiFi flag based on ArtNet setting
  settings.useWiFi = fullSettings.useArtnet || preferences.getBool("useWiFi", settings.useWiFi);

  preferences.end();

  debugLog("Settings loaded from preferences");
}

void saveSettings()
{
  preferences.begin("led-settings", false);

  // Save LED configuration
  preferences.putInt("numStrips", fullSettings.numStrips);
  preferences.putInt("ledsPerStrip", fullSettings.ledsPerStrip);
  preferences.putInt("brightness", fullSettings.brightness);

  // Save pins array as comma-separated string
  String pinsStr = String(fullSettings.pins[0]);
  for (int i = 1; i < 4; i++)
  {
    pinsStr += "," + String(fullSettings.pins[i]);
  }
  preferences.putString("pins", pinsStr);

  // Save WiFi configuration
  preferences.putString("ssid", fullSettings.ssid);
  preferences.putString("password", fullSettings.password);
  preferences.putString("nodeName", fullSettings.nodeName);
  preferences.putInt("startUniverse", fullSettings.startUniverse);
  preferences.putBool("useWiFi", settings.useWiFi);

  // Save mode settings
  preferences.putBool("useArtnet", fullSettings.useArtnet);
  preferences.putBool("useColorCycle", fullSettings.useColorCycle);
  preferences.putBool("useStaticColor", fullSettings.useStaticColor);
  preferences.putInt("colorMode", fullSettings.colorMode);
  preferences.putInt("cycleSpeed", fullSettings.cycleSpeed);

  // Save static color
  preferences.putInt("staticColorR", fullSettings.staticColor.r);
  preferences.putInt("staticColorG", fullSettings.staticColor.g);
  preferences.putInt("staticColorB", fullSettings.staticColor.b);

  // CRITICAL FIX: Always save the current network failure state to maintain across reboots
  preferences.putBool("netFailed", networkInitFailed);

  preferences.end();

  debugLog("Settings saved to preferences");
}

// Create a minimal setup function that delegates to the common code
void setup()
{
  // Start with bare minimum initialization
  Serial.begin(115200);
  delay(100);
  
  // Initialize default settings
  initializeDefaultSettings();
  
  debugLog("ESP32 ArtNet LED Controller Starting - Full Version");

  // Load any saved settings from preferences
  loadSettings();

  // Rest of setup implementation...
  // This would include LED driver initialization, web server setup, etc.
}

// Create a minimal loop function
void loop()
{
  // Keep the watchdog happy
  yield();
  delay(10);

  // Rest of loop implementation...
  // This would include LED updates, web server handling, etc.
}
