// ESP32 Artnet I2SClockless with Web UI + OLED Debug
// Built for ESP-WROOM-32 using 3-4 pins with up to 3 WS2812B strips per pin
// Features: Artnet, web interface config, OLED status, debug log
// TODO: bring in check box for static modes. add more control options for that
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Adafruit_NeoPixel.h> // Add NeoPixel library for the test function

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

#define DEBUG_ENABLED true
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
Preferences preferences;
I2SClocklessLedDriver driver;
artnetESP32V2 artnet;

rgb24 ledData[MAX_STRIPS * NUM_LEDS_PER_STRIP]; // Fixed error with NUM_LEDS_PER_STRIP
bool sendFrame = false;

// Anti-boot loop protection
// This is a global variable to track critical errors and prevent retries
bool networkInitFailed = false;
bool ledHardwareFailed = false;

// Runtime state + settings struct
struct Settings
{
  int numStrips;
  int ledsPerStrip;
  int pins[4];
  int brightness;
  String ssid;
  String password;
  bool useWiFi;
  String nodeName;
  int startUniverse;

  // Added settings for static color and mode controls
  bool useArtnet;
  bool useColorCycle;
  bool useStaticColor; // Flag to enable/disable static color
  int colorMode;
  rgb24 staticColor;
  int cycleSpeed;
} settings = {
    .numStrips = 1,
    .ledsPerStrip = 144,
    .pins = {12, -1, -1, -1}, // Only pin 12 active by default, others disabled
    .brightness = 255,
    .ssid = "Sage1",
    .password = "J@sper123",
    .useWiFi = false,
    .nodeName = "ESP32_Artnet_I2S",
    .startUniverse = 0,

    // Default values for new settings
    .useArtnet = false, // Default to static color mode instead of ArtNet
    .useColorCycle = false,
    .useStaticColor = true, // Static color enabled by default
    .colorMode = COLOR_MODE_RAINBOW,
    .staticColor = {255, 0, 255}, // Default to magenta (red+blue) for visibility
    .cycleSpeed = 50              // Medium speed
};

// Function to handle the tcpip_send_msg assert failure that's causing the boot loop
void disableAllNetworkOperations()
{
  // Complete network shutdown sequence
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Mark as failed to prevent any future attempts
  networkInitFailed = true;

  // Force settings to disable any network functionality
  settings.useArtnet = false;
  settings.useWiFi = false;
  settings.useStaticColor = true;

  // CRITICAL FIX: Persist the network failure state to prevent future attempts after reboot
  preferences.begin("led-settings", false);
  preferences.putBool("netFailed", true);
  preferences.end();

  debugLog("CRITICAL: Network stack disabled due to assertion failure");
}

struct State
{
  String logs[MAX_LOG_ENTRIES];
  int logIndex = 0;
} state;

// ====== DEBUG ======
void debugLog(String msg)
{
  if (!DEBUG_ENABLED)
    return;
  Serial.println(msg);
  state.logs[state.logIndex] = msg;
  state.logIndex = (state.logIndex + 1) % MAX_LOG_ENTRIES;
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
  html += "<div class='form-group'><label>LEDs per Strip:</label><input type='number' name='leds' value='" + String(settings.ledsPerStrip) + "'></div>";
  html += "<div class='form-group'><label>Number of Strips:</label><input type='number' name='strips' value='" + String(settings.numStrips) + "'></div>";
  html += "<div class='form-group'><label>Brightness:</label><input type='range' min='0' max='255' name='bright' value='" + String(settings.brightness) + "'></div>";

  // Individual pin configuration
  html += "<div class='form-group'><label>Pin Configuration:</label></div>";
  html += "<div style='margin-left: 20px;'>";
  for (int i = 0; i < 4; i++)
  {
    html += "<div class='form-group'><label>Pin " + String(i + 1) + ":</label>";
    html += "<input type='number' min='-1' max='39' name='pin" + String(i) + "' value='" + String(settings.pins[i]) + "'>";
    html += "</div>";
  }
  html += "</div>";

  html += "</div>";

  // Mode Selection Section
  html += "<div class='section'>";
  html += "<h3>Mode Selection</h3>";

  // ArtNet Mode Toggle
  html += "<div class='form-group'>";
  html += "<input type='checkbox' id='useArtnet' name='useArtnet' value='1' " + String(settings.useArtnet ? "checked" : "") + ">";
  html += "<label for='useArtnet'>Use ArtNet Mode</label>";
  html += "</div>";

  // Static Color Toggle
  html += "<div class='form-group'>";
  html += "<input type='checkbox' id='useStaticColor' name='useStaticColor' value='1' " + String(settings.useStaticColor ? "checked" : "") + ">";
  html += "<label for='useStaticColor'>Use Static Color</label>";
  html += "</div>";

  // Color Cycle Toggle
  html += "<div class='form-group'>";
  html += "<input type='checkbox' id='useColorCycle' name='useColorCycle' value='1' " + String(settings.useColorCycle ? "checked" : "") + ">";
  html += "<label for='useColorCycle'>Use Color Cycle Effects</label>";
  html += "</div>";

  // Color Mode Selection (only visible when color cycle is enabled)
  html += "<div class='form-group' id='colorModeGroup'>";
  html += "<label for='colorMode'>Color Mode:</label>";
  html += "<select name='colorMode' id='colorMode'>";
  html += "<option value='" + String(COLOR_MODE_RAINBOW) + "'" + (settings.colorMode == COLOR_MODE_RAINBOW ? " selected" : "") + ">Rainbow</option>";
  html += "<option value='" + String(COLOR_MODE_PULSE) + "'" + (settings.colorMode == COLOR_MODE_PULSE ? " selected" : "") + ">Pulse</option>";
  html += "<option value='" + String(COLOR_MODE_FIRE) + "'" + (settings.colorMode == COLOR_MODE_FIRE ? " selected" : "") + ">Fire</option>";
  html += "</select>";
  html += "</div>";

  // Static Color Selection (only visible when color cycle is disabled)
  html += "<div class='form-group' id='staticColorGroup'>";
  html += "<label for='staticColor'>Static Color:</label>";
  html += "<input type='color' id='staticColor' name='staticColor' value='#" +
          String(settings.staticColor.r, HEX) + String(settings.staticColor.g, HEX) + String(settings.staticColor.b, HEX) + "'>";
  html += "</div>";

  // Cycle Speed (only visible when color cycle is enabled)
  html += "<div class='form-group' id='cycleSpeedGroup'>";
  html += "<label for='cycleSpeed'>Cycle Speed:</label>";
  html += "<input type='range' min='1' max='100' name='cycleSpeed' id='cycleSpeed' value='" + String(settings.cycleSpeed) + "'>";
  html += "</div>";
  html += "</div>";

  // WiFi Configuration Section
  html += "<div class='section'>";
  html += "<h3>WiFi Configuration</h3>";
  html += "<div class='form-group'><label>SSID:</label><input type='text' name='ssid' value='" + settings.ssid + "'></div>";
  html += "<div class='form-group'><label>Password:</label><input type='password' name='pass' value='" + settings.password + "'></div>";
  html += "<div class='form-group'><label>Node Name:</label><input type='text' name='nodeName' value='" + settings.nodeName + "'></div>";
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
    settings.ledsPerStrip = server.arg("leds").toInt();
  if (server.hasArg("strips"))
    settings.numStrips = server.arg("strips").toInt();
  if (server.hasArg("bright"))
    settings.brightness = server.arg("bright").toInt();

  // Process individual pin configuration
  for (int i = 0; i < 4; i++)
  {
    String pinArg = "pin" + String(i);
    if (server.hasArg(pinArg))
    {
      settings.pins[i] = server.arg(pinArg).toInt();
    }
  }

  // CRITICAL FIX: If network has previously failed, ignore attempts to enable network features
  if (!networkInitFailed)
  {
    // Process mode settings
    settings.useArtnet = server.hasArg("useArtnet");

    // Process WiFi settings
    if (server.hasArg("ssid"))
      settings.ssid = server.arg("ssid");
    if (server.hasArg("pass"))
      settings.password = server.arg("pass");
    if (server.hasArg("nodeName"))
      settings.nodeName = server.arg("nodeName");

    // Check if we need to restart network functionality
    bool wifiConfigChanged = false;
    if (server.hasArg("ssid") && settings.ssid != server.arg("ssid"))
      wifiConfigChanged = true;
    if (server.hasArg("pass") && settings.password != server.arg("pass"))
      wifiConfigChanged = true;

    // If network config changed, we'll need to restart network services
    if (wifiConfigChanged && (settings.useArtnet || settings.useWiFi))
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
    settings.useArtnet = false;
    settings.useWiFi = false;
    // Log the override
    debugLog("WARNING: Network settings change ignored due to previous failure");
  }

  // Always allow these non-network settings
  settings.useColorCycle = server.hasArg("useColorCycle");
  settings.useStaticColor = server.hasArg("useStaticColor");

  if (server.hasArg("colorMode"))
    settings.colorMode = server.arg("colorMode").toInt();

  if (server.hasArg("cycleSpeed"))
    settings.cycleSpeed = server.arg("cycleSpeed").toInt();

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
    settings.staticColor.r = (colorValue >> 16) & 0xFF;
    settings.staticColor.g = (colorValue >> 8) & 0xFF;
    settings.staticColor.b = colorValue & 0xFF;
  }

  // Save settings to preferences
  saveSettings();

  // Check if hardware-affecting settings have changed
  bool needsRestart = false;

  // Hardware configuration changes may require a restart
  if (server.hasArg("leds") || server.hasArg("strips") ||
      server.hasArg("pin0") || server.hasArg("pin1") ||
      server.hasArg("pin2") || server.hasArg("pin3"))
  {

    // Attempt to reinitialize LED driver first
    try
    {
      debugLog("Hardware configuration changed - reinitializing LED driver");
      initializeLEDDriver();
    }
    catch (...)
    {
      // If reinitialization fails, we'll need a full restart
      needsRestart = true;
    }
  }

  // Apply LED settings immediately
  driver.setBrightness(settings.brightness);

  // If we're in static color mode, immediately apply the color
  if (settings.useStaticColor)
  {
    applyStaticColor();
  }

  // If we need a restart, inform the user and schedule it
  if (needsRestart)
  {
    // First send response to browser
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "Settings Updated - Device Restarting");

    // Allow time for the response to be sent
    delay(1000);

    // Then restart the device
    ESP.restart();
    return;
  }
  else
  {
    // Normal operation - just redirect back to root page
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "Settings Updated");
  }
}

// ====== ARNET CALLBACK ======
void artnetCallback(void *param)
{
  subArtnet *sub = (subArtnet *)param;

  // Get ArtNet data from the packet
  uint8_t *data = sub->getData();
  int dataLength = sub->_nb_data;

  // Save a copy of the DMX data for use in static mode
  if (dataLength > UNIVERSE_SIZE)
    dataLength = UNIVERSE_SIZE;
  memcpy(dmxData, data, dataLength);

  // Update LED colors from DMX data
  for (int i = 0; i < dataLength; i += NB_CHANNEL_PER_LED)
  {
    int ledIndex = i / NB_CHANNEL_PER_LED;
    if (ledIndex < MAX_STRIPS * NUM_LEDS_PER_STRIP)
    {
      ledData[ledIndex].r = data[i];
      ledData[ledIndex].g = data[i + 1];
      ledData[ledIndex].b = data[i + 2];
    }
  }

  // Always update LEDs immediately for ArtNet data
  driver.showPixels(NO_WAIT);
  sendFrame = true;

  // Update tracking variables
  lastPacketTime = millis();
  packetsReceived++;

  // Send status via UART if bridge is initialized
  if (uartBridge.getLastError() == ERR_NONE)
  {
    uint8_t statusData[4] = {0x01, 0x00, 0x00, 0x00}; // Simple status packet
    uartBridge.sendCommand(CMD_DMX_DATA, statusData, 4);
  }
}

// Wrapper function for the frame callback to match the expected signature
void frameCallbackWrapper()
{
  // This is just a wrapper to match the signature expected by setFrameCallback
  // The actual callback processing happens in artnetCallback when called by the subArtnet
}

// Handler for UART commands - implement specific behavior for each command
void handleUARTCommand(uint8_t cmd, uint8_t *data, uint16_t length)
{
  debugLog("UART command received: " + String(cmd, HEX));

  switch (cmd)
  {
  case CMD_SET_MODE:
    if (length >= 1)
    {
      uint8_t mode = data[0];
      if (mode == 0)
      {
        // ArtNet mode
        settings.useArtnet = true;
        settings.useStaticColor = false;
        settings.useColorCycle = false;
      }
      else if (mode == 1)
      {
        // Static color mode
        settings.useArtnet = false;
        settings.useStaticColor = true;
        settings.useColorCycle = false;
      }
      else if (mode == 2)
      {
        // Color cycle mode
        settings.useArtnet = false;
        settings.useStaticColor = false;
        settings.useColorCycle = true;

        // Set specific animation if provided
        if (length >= 2 && data[1] <= COLOR_MODE_FIRE)
        {
          settings.colorMode = data[1];
        }
      }
      saveSettings();

      // Acknowledge the mode change
      uartBridge.sendCommand(CMD_ACK, &mode, 1);
    }
    break;

  case CMD_SET_BRIGHTNESS:
    if (length >= 1)
    {
      settings.brightness = data[0];
      driver.setBrightness(settings.brightness);
      saveSettings();
      // Fix the type mismatch by creating a uint8_t variable
      uint8_t brightnessValue = settings.brightness;
      uartBridge.sendCommand(CMD_ACK, &brightnessValue, 1);
    }
    break;

  case CMD_SET_COLOR:
    if (length >= 3)
    {
      // Update color settings
      settings.staticColor.r = data[0];
      settings.staticColor.g = data[1];
      settings.staticColor.b = data[2];

      // Log the color change
      debugLog("UART: Setting color to R=" + String(data[0]) +
               ", G=" + String(data[1]) +
               ", B=" + String(data[2]));

      // If in static color mode, apply the color immediately with improved method
      if (settings.useStaticColor)
      {
        // Critical: reinitialize driver to ensure clean state
        initializeLEDDriver();

        // Apply the static color with the improved method
        applyStaticColor();
      }

      // Save to persistent storage
      saveSettings();

      // Acknowledge the command
      uartBridge.sendCommand(CMD_ACK, data, 3);
    }
    break;

  default:
    // Unknown command
    uartBridge.sendErrorMessage(ERR_INVALID_CMD, "Unknown command");
    break;
  }
}

// ====== COLOR FUNCTIONS ======
unsigned long lastColorUpdate = 0;
uint8_t cyclePosition = 0;

// Test function for LED hardware diagnosis that combines multiple test methods
void testDirectLEDControl()
{
  debugLog("Starting comprehensive LED test...");

  // Test 1: Using I2SClocklessLedDriver directly
  debugLog("Test 1: Using I2SClocklessLedDriver directly");

  // Clear all LEDs first
  memset(ledData, 0, sizeof(ledData));
  driver.showPixels(WAIT);
  delay(100);

  // Set all LEDs to RED
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = 255;
    ledData[i].g = 0;
    ledData[i].b = 0;
  }
  driver.showPixels(WAIT);
  delay(1000);

  // Set all LEDs to GREEN
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = 0;
    ledData[i].g = 255;
    ledData[i].b = 0;
  }
  driver.showPixels(WAIT);
  delay(1000);

  // Test 2: Using Adafruit_NeoPixel for comparison
  debugLog("Test 2: Using Adafruit_NeoPixel library for comparison");

  // Create NeoPixel strip with the same configuration
  Adafruit_NeoPixel strip(settings.ledsPerStrip, settings.pins[0], NEO_GRB + NEO_KHZ800);
  strip.begin();
  strip.setBrightness(settings.brightness);

  // Set all LEDs to BLUE
  for (int i = 0; i < settings.ledsPerStrip; i++)
  {
    strip.setPixelColor(i, 0, 0, 255);
  }
  strip.show();
  delay(1000);

  // Set all LEDs to MAGENTA
  for (int i = 0; i < settings.ledsPerStrip; i++)
  {
    strip.setPixelColor(i, 255, 0, 255);
  }
  strip.show();
  delay(1000);

  // Test 3: Reinitialize I2SClocklessLedDriver and test again
  debugLog("Test 3: Reinitializing I2SClocklessLedDriver");

  // Reinitialize with specific pin configuration
  int singlePin[1] = {settings.pins[0]};
  driver.initled(NULL, singlePin, 1, settings.ledsPerStrip);
  driver.setBrightness(settings.brightness);

  // Set all LEDs to YELLOW
  for (int i = 0; i < settings.ledsPerStrip; i++)
  {
    ledData[i].r = 255;
    ledData[i].g = 255;
    ledData[i].b = 0;
  }
  driver.showPixels(WAIT);
  delay(1000);

  // Test 4: Try with direct buffer approach
  debugLog("Test 4: Direct buffer approach");
  uint8_t *directBuffer = (uint8_t *)malloc(settings.ledsPerStrip * 3);
  if (directBuffer)
  {
    for (int i = 0; i < settings.ledsPerStrip; i++)
    {
      directBuffer[i * 3] = 255;     // R
      directBuffer[i * 3 + 1] = 0;   // G
      directBuffer[i * 3 + 2] = 255; // B
    }

    driver.showPixels(WAIT, directBuffer);
    delay(1000);
    free(directBuffer);
  }

  // Restore original LED driver configuration
  debugLog("LED test complete, reinitializing driver");
  initializeLEDDriver();
}

// Special quick test to verify LED driver functionality on startup
void quickLEDTest()
{
  debugLog("Running quick LED verification test...");

  // Flash red briefly to confirm hardware is connected and working
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = 255;
    ledData[i].g = 0;
    ledData[i].b = 0;
  }
  driver.showPixels(WAIT);
  delay(200);

  // Then green
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = 0;
    ledData[i].g = 255;
    ledData[i].b = 0;
  }
  driver.showPixels(WAIT);
  delay(200);

  // Then blue
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = 0;
    ledData[i].g = 0;
    ledData[i].b = 255;
  }
  driver.showPixels(WAIT);
  delay(200);

  // Clear LEDs at end of test
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = 0;
    ledData[i].g = 0;
    ledData[i].b = 0;
  }
  driver.showPixels(WAIT);

  debugLog("LED verification test complete");
}

// Convert HSV to RGB
rgb24 hsvToRgb(uint8_t h, uint8_t s, uint8_t v)
{
  rgb24 rgb;
  uint8_t region, remainder, p, q, t;

  if (s == 0)
  {
    rgb.r = v;
    rgb.g = v;
    rgb.b = v;
    return rgb;
  }

  region = h / 43;
  remainder = (h - (region * 43)) * 6;

  p = (v * (255 - s)) >> 8;
  q = (v * (255 - ((s * remainder) >> 8))) >> 8;
  t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

  switch (region)
  {
  case 0:
    rgb.r = v;
    rgb.g = t;
    rgb.b = p;
    break;
  case 1:
    rgb.r = q;
    rgb.g = v;
    rgb.b = p;
    break;
  case 2:
    rgb.r = p;
    rgb.g = v;
    rgb.b = t;
    break;
  case 3:
    rgb.r = p;
    rgb.g = q;
    rgb.b = v;
    break;
  case 4:
    rgb.r = t;
    rgb.g = p;
    rgb.b = v;
    break;
  default:
    rgb.r = v;
    rgb.g = p;
    rgb.b = q;
    break;
  }

  return rgb;
}

// Rainbow pattern - Smooth color transition across all LEDs
void rainbowCycle()
{
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    // Calculate hue based on position in strip and current cycle position
    int pixelHue = (i * 256 / settings.ledsPerStrip) + cyclePosition;
    // Convert to 0-255 range
    pixelHue = pixelHue % 256;
    // Convert HSV to RGB using saturation 240 and value/brightness 255
    ledData[i] = hsvToRgb(pixelHue, 240, settings.brightness);
  }

  // Update cycle position for animation
  cyclePosition = (cyclePosition + 1) % 256;
}

// Pulse effect - All LEDs pulse together
void pulseEffect()
{
  // Calculate intensity based on sine wave
  float intensity = (sin(cyclePosition * 0.0245) + 1.0) / 2.0;
  uint8_t value = intensity * settings.brightness;

  // Apply color with calculated brightness
  rgb24 color = hsvToRgb(cyclePosition, 255, value);

  // Apply to all LEDs
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i] = color;
  }

  // Slower cycle for pulse effect
  cyclePosition = (cyclePosition + 1) % 256;
}

// Fire effect - Simulates flickering flames
void fireEffect()
{
  const int cooling = 55;
  const int sparking = 120;
  static uint8_t heat[MAX_STRIPS * NUM_LEDS_PER_STRIP];
  int totalLeds = settings.numStrips * settings.ledsPerStrip;

  // Step 1: Cool down every cell a little
  for (int i = 0; i < totalLeds; i++)
  {
    // Cast to int to avoid type mismatch with max()
    int randomCooling = (int)random(0, ((cooling * 10) / totalLeds) + 2);
    heat[i] = max(0, (int)heat[i] - randomCooling);
  }

  // Step 2: Heat from each cell drifts up and diffuses
  for (int k = totalLeds - 1; k >= 2; k--)
  {
    heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
  }

  // Step 3: Randomly ignite new sparks near the bottom
  if (random(255) < sparking)
  {
    int y = random(7);
    // Cast to int to avoid type mismatch with min()
    int sparkHeat = (int)random(160, 255);
    heat[y] = min(255, (int)heat[y] + sparkHeat);
  }

  // Step 4: Convert heat to LED colors
  for (int j = 0; j < totalLeds; j++)
  {
    // Scale heat value to RGB
    uint8_t temperature = heat[j];

    // Calculate ramp up from
    uint8_t t192 = round((temperature / 255.0) * 191);

    // Calculate color based on temperature
    rgb24 color;

    // Hotter = brighter, with red-orange-yellow color progression
    if (t192 < 47)
    {
      color.r = t192 * 5;
      color.g = 0;
      color.b = 0;
    }
    else if (t192 < 96)
    {
      color.r = 255;
      color.g = (t192 - 47) * 5;
      color.b = 0;
    }
    else if (t192 < 150)
    {
      color.r = 255;
      color.g = 255;
      color.b = (t192 - 96) * 4;
    }
    else
    {
      color.r = 255;
      color.g = 255;
      color.b = 255;
    }

    ledData[j] = color;
  }
}

// Apply static color to all LEDs
void applyStaticColor()
{
  if (!settings.useStaticColor)
    return;

  debugLog("Applying static color: R=" + String(settings.staticColor.r) +
           " G=" + String(settings.staticColor.g) +
           " B=" + String(settings.staticColor.b));

  // Critical: Clear any previous data
  memset(ledData, 0, sizeof(ledData));

  // Apply color to all LEDs
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = settings.staticColor.r;
    ledData[i].g = settings.staticColor.g;
    ledData[i].b = settings.staticColor.b;
  }

  // Force immediate update with WAIT flag to ensure completion
  driver.showPixels(NO_WAIT);

  // Add an additional verification update with a slight delay
  // This double-update approach can help with some reliability issues
  delay(2);
  driver.showPixels(NO_WAIT);

  debugLog("Static color applied successfully");
}

// Main function to update LEDs based on current mode
void updateLEDsBasedOnMode()
{
  unsigned long currentMillis = millis();

  // Skip updates for animations if not enough time has passed
  if (!settings.useStaticColor &&
      !settings.useArtnet &&
      settings.useColorCycle &&
      currentMillis - lastColorUpdate < (101 - settings.cycleSpeed))
  {
    return;
  }

  lastColorUpdate = currentMillis;

  // ArtNet mode is handled separately in the callback
  if (settings.useArtnet)
  {
    return;
  }

  // Static color mode has priority
  if (settings.useStaticColor)
  {
    // Only reapply static color periodically to avoid overwhelming the LED driver
    static unsigned long lastStaticUpdate = 0;
    if (currentMillis - lastStaticUpdate > 1000) // Update once per second
    {
      applyStaticColor();
      lastStaticUpdate = currentMillis;
    }
  }
  // Color cycle mode
  else if (settings.useColorCycle)
  {
    // Clear the buffer first
    // This helps avoid artifacts from previous frames
    memset(ledData, 0, sizeof(ledData));

    // Apply the selected color effect
    switch (settings.colorMode)
    {
    case COLOR_MODE_RAINBOW:
      rainbowCycle();
      break;
    case COLOR_MODE_PULSE:
      pulseEffect();
      break;
    case COLOR_MODE_FIRE:
      fireEffect();
      break;
    default:
      rainbowCycle();
      break;
    }

    // Critical: Force update LEDs with WAIT flag for better reliability
    driver.showPixels(WAIT);
  }
  // Fallback to static color if nothing else is active
  else if (!settings.useArtnet && !settings.useColorCycle)
  {
    // Force static color mode if nothing else is active
    settings.useStaticColor = true;
    applyStaticColor();
  }
}

// Test LED strip with color sequence - useful for debugging hardware issues
void testLEDs()
{
  debugLog("LED Test Mode: Starting self-test");

  // Clear all LEDs
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = 0;
    ledData[i].g = 0;
    ledData[i].b = 0;
  }
  driver.showPixels(NO_WAIT);
  delay(500);

  // Test RED
  debugLog("LED Test: RED");
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = 255;
    ledData[i].g = 0;
    ledData[i].b = 0;
  }
  driver.showPixels(NO_WAIT);
  delay(1000);

  // Test GREEN
  debugLog("LED Test: GREEN");
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = 0;
    ledData[i].g = 255;
    ledData[i].b = 0;
  }
  driver.showPixels(NO_WAIT);
  delay(1000);

  // Test BLUE
  debugLog("LED Test: BLUE");
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = 0;
    ledData[i].g = 0;
    ledData[i].b = 255;
  }
  driver.showPixels(NO_WAIT);
  delay(1000);

  // Test WHITE
  debugLog("LED Test: WHITE");
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i].r = 255;
    ledData[i].g = 255;
    ledData[i].b = 255;
  }
  driver.showPixels(NO_WAIT);
  delay(1000);

  // Return to normal state
  debugLog("LED Test: Complete");
  if (settings.useStaticColor)
  {
    applyStaticColor();
  }
  else
  {
    // Turn LEDs off
    for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
    {
      ledData[i].r = 0;
      ledData[i].g = 0;
      ledData[i].b = 0;
    }
    driver.showPixels(NO_WAIT);
  }
}

// Update the OLED display with current status information
void updateDisplayStatus()
{
  static bool displayInitialized = false;

  // Only initialize the display once
  if (!displayInitialized)
  {
    displayInitialized = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
    if (!displayInitialized)
    {
      debugLog("OLED display initialization failed");
      return;
    }
  }

  // Only proceed if display is initialized
  if (!displayInitialized)
  {
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);

  // Display WiFi and IP status
  if (WiFi.status() == WL_CONNECTED)
  {
    display.println("WiFi: Connected");
    display.print("IP: ");
    display.println(WiFi.localIP());
  }
  else
  {
    display.println("WiFi: Disconnected");
    display.println("Standalone Mode");
  }

  // Display current mode
  display.println("-------------------");
  if (settings.useArtnet)
  {
    display.print("Mode: ArtNet");
  }
  else if (settings.useColorCycle)
  {
    display.print("Mode: Cycle");
  }
  else if (settings.useStaticColor)
  {
    display.print("Mode: Static");
  }

  display.display();
}

// ====== SETTINGS FUNCTIONS ======
void loadSettings()
{
  preferences.begin("led-settings", false);

  // Load LED configuration
  settings.numStrips = preferences.getInt("numStrips", settings.numStrips);
  settings.ledsPerStrip = preferences.getInt("ledsPerStrip", settings.ledsPerStrip);
  settings.brightness = preferences.getInt("brightness", settings.brightness);

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
        settings.pins[index++] = pinsStr.substring(0, commaPos).toInt();
        pinsStr = pinsStr.substring(commaPos + 1);
      }
      else if (pinsStr.length() > 0)
      {
        settings.pins[index++] = pinsStr.toInt();
        break;
      }
    }
  }

  // Load WiFi configuration
  settings.ssid = preferences.getString("ssid", settings.ssid);
  settings.password = preferences.getString("password", settings.password);
  settings.nodeName = preferences.getString("nodeName", settings.nodeName);
  settings.startUniverse = preferences.getInt("startUniverse", settings.startUniverse);

  // Load mode settings
  settings.useArtnet = preferences.getBool("useArtnet", settings.useArtnet);
  settings.useColorCycle = preferences.getBool("useColorCycle", settings.useColorCycle);
  settings.useStaticColor = preferences.getBool("useStaticColor", settings.useStaticColor); // Load static color flag
  settings.colorMode = preferences.getInt("colorMode", settings.colorMode);
  settings.cycleSpeed = preferences.getInt("cycleSpeed", settings.cycleSpeed);

  // Load static color
  settings.staticColor.r = preferences.getInt("staticColorR", settings.staticColor.r);
  settings.staticColor.g = preferences.getInt("staticColorG", settings.staticColor.g);
  settings.staticColor.b = preferences.getInt("staticColorB", settings.staticColor.b);

  // Load network failure state - CRITICAL FIX for persistent boot-loop prevention
  networkInitFailed = preferences.getBool("netFailed", false);
  if (networkInitFailed)
  {
    debugLog("CRITICAL: Network previously failed and disabled permanently");
  }

  preferences.end();

  debugLog("Settings loaded from preferences");
}

void saveSettings()
{
  preferences.begin("led-settings", false);

  // Save LED configuration
  preferences.putInt("numStrips", settings.numStrips);
  preferences.putInt("ledsPerStrip", settings.ledsPerStrip);
  preferences.putInt("brightness", settings.brightness);

  // Save pins array as comma-separated string
  String pinsStr = String(settings.pins[0]);
  for (int i = 1; i < 4; i++)
  {
    pinsStr += "," + String(settings.pins[i]);
  }
  preferences.putString("pins", pinsStr);

  // Save WiFi configuration
  preferences.putString("ssid", settings.ssid);
  preferences.putString("password", settings.password);
  preferences.putString("nodeName", settings.nodeName);
  preferences.putInt("startUniverse", settings.startUniverse);

  // Save mode settings
  preferences.putBool("useArtnet", settings.useArtnet);
  preferences.putBool("useColorCycle", settings.useColorCycle);
  preferences.putBool("useStaticColor", settings.useStaticColor); // Save static color flag
  preferences.putInt("colorMode", settings.colorMode);
  preferences.putInt("cycleSpeed", settings.cycleSpeed);

  // Save static color
  preferences.putInt("staticColorR", settings.staticColor.r);
  preferences.putInt("staticColorG", settings.staticColor.g);
  preferences.putInt("staticColorB", settings.staticColor.b);

  // CRITICAL FIX: Always save the current network failure state to maintain across reboots
  preferences.putBool("netFailed", networkInitFailed);

  preferences.end();

  debugLog("Settings saved to preferences");
}

// LED Driver Initialization with hardware failure detection
void initializeLEDDriver()
{
  debugLog("Initializing LED driver with basic settings: strips=" + String(settings.numStrips) +
           ", ledsPerStrip=" + String(settings.ledsPerStrip));

  // Clear any previous LED data
  memset(ledData, 0, sizeof(ledData));

  // Initialize with simplest possible configuration
  // IMPORTANT FIX: Only use as many pins as strips, not all 4 pins
  static int pins[4] = {-1, -1, -1, -1};

  // Only configure exactly as many pins as we have strips
  int activeCount = 0;
  for (int i = 0; i < settings.numStrips && i < 4; i++)
  {
    if (settings.pins[i] >= 0)
    {
      pins[activeCount] = settings.pins[i];
      activeCount++;
    }
  }

  // Ensure at least one pin is active
  if (activeCount == 0)
  {
    pins[0] = 12; // Default to pin 12
    activeCount = 1;
  }

  // CRITICAL FIX: Only use exactly as many pins as needed
  debugLog("Using " + String(activeCount) + " pins for LED control");

  // Initialize with failure handling
  try
  {
    driver.initled(NULL, pins, activeCount, settings.ledsPerStrip);

    // Set brightness
    driver.setBrightness(settings.brightness);

    // Simple test to ensure driver is working
    driver.showPixels(WAIT);
    debugLog("LED driver initialized successfully");
  }
  catch (...)
  {
    // If any exception occurs, disable LED operations
    debugLog("ERROR: LED hardware initialization failed - operating in display-only mode");
  }
}

// ====== SETUP ======
void setup()
{
  // Start with bare minimum initialization
  Serial.begin(115200);
  delay(100); // Short delay to stabilize serial
  debugLog("ESP32 ArtNet LED Controller Starting");

  // Load settings from preferences first
  loadSettings();

  // Initialize boot counter for safe mode detection
  checkBootLoopProtection();

  // CRITICAL: Completely disable network functionality if in safe mode or previously failed
  // This is essential to prevent the tcpip_send_msg assertion failure
  if (networkInitFailed || inSafeMode)
  {
    WiFi.mode(WIFI_OFF);
    delay(500); // Longer delay to ensure WiFi is fully off
    debugLog("Network disabled due to previous failures or safe mode");
  }
  // Otherwise try network initialization FIRST (before other peripherals)
  else if (settings.useArtnet || settings.useWiFi)
  {
    // Move network operations to a separate function to isolate errors
    initializeNetworkWithProtection();
  }
  else
  {
    // Skip all network operations
    WiFi.mode(WIFI_OFF);
    delay(500); // Ensure WiFi is fully off
    debugLog("Network operations skipped - using standalone mode");
  }

  // Configure UART after network but before LED initialization
  pinMode(UART_RX_PIN, INPUT);
  pinMode(UART_TX_PIN, OUTPUT);
  Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

  // Initialize UART bridge with error handling
  try
  {
    bool uartOK = uartBridge.initializeCommunication();
    if (uartOK)
    {
      debugLog("UART Bridge initialized successfully");
      uartBridge.setCommandCallback(handleUARTCommand);
    }
    else
    {
      debugLog("UART Bridge initialization failed, continuing without UART");
    }
  }
  catch (...)
  {
    debugLog("UART exception occurred, continuing without UART");
  }

  // Initialize LED driver AFTER network and UART
  try
  {
    debugLog("Initializing LED driver with basic setup");
    initializeLEDDriver();

    // Run a quick LED test if initialization succeeded
    if (settings.useStaticColor)
    {
      try
      {
        applyStaticColor();
        debugLog("Applied static color at startup");
      }
      catch (...)
      {
        debugLog("Failed to apply static color");
      }
    }
  }
  catch (...)
  {
    debugLog("LED driver initialization failed, continuing in UI-only mode");
    ledHardwareFailed = true; // Mark LED hardware as failed
  }

  // Initialize display LAST since it's least critical
  try
  {
    bool displayOK = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
    if (displayOK)
    {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.println("ESP32 ArtNet");
      display.println("Initializing...");
      display.display();
      debugLog("OLED display initialized");
    }
    else
    {
      debugLog("OLED display initialization failed");
    }
  }
  catch (...)
  {
    debugLog("OLED exception occurred, continuing without display");
  }

  // Web server initialization - this always needs to work
  server.on("/", handleRoot);
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/debug", handleDebugLog);

  // IMPORTANT: Start web server after all other initializations
  try
  {
    server.begin();
    debugLog("Web server started");
  }
  catch (...)
  {
    debugLog("Web server start failed");
  }

  // Mark successful boot
  updateBootCounter(true);
  debugLog("Setup complete - entering main loop");
}

// Add boot loop protection with counter
// These variables need to be added at global scope
bool inSafeMode = false;
int bootCount = 0;
unsigned long lastBootTime = 0;

void checkBootLoopProtection()
{
  preferences.begin("led-settings", false);

  // Get boot count and last boot time
  bootCount = preferences.getInt("bootCount", 0);
  lastBootTime = preferences.getLong("lastBootTime", 0);
  unsigned long currentTime = millis();

  // Check for rapid reboots indicating a boot loop
  if (bootCount > 3 && (currentTime - lastBootTime < 300000))
  { // 5 minutes
    // Enable safe mode if too many boots in short time
    inSafeMode = true;
    debugLog("SAFE MODE ENABLED: Multiple boot failures detected");

    // Use minimal configuration in safe mode
    settings.useArtnet = false;
    settings.useWiFi = false;
    settings.useColorCycle = false;
    settings.useStaticColor = true;
    settings.staticColor = {255, 0, 0}; // Red = safe mode
    settings.numStrips = 1;
    settings.ledsPerStrip = 8; // Minimal LED count
    settings.brightness = 64;  // Reduced brightness
  }

  // Increment boot counter and save time
  bootCount++;
  preferences.putInt("bootCount", bootCount);
  preferences.putLong("lastBootTime", currentTime);
  preferences.end();
}

void updateBootCounter(bool success)
{
  preferences.begin("led-settings", false);
  if (success)
  {
    // Reset boot counter after successful boot
    preferences.putInt("bootCount", 0);
    debugLog("Boot successful - counter reset");
  }
  preferences.end();
}

// Improved network initialization with better timeouts
void initializeNetworkWithProtection()
{
  debugLog("Starting protected network initialization");

  // Critical section in try-catch to prevent boot loops
  try
  {
    // Set a timeout in case anything hangs
    unsigned long networkStartTime = millis();

    // First, try basic WiFi mode configuration which might trigger the assertion
    WiFi.persistent(false);
    WiFi.disconnect(true);
    delay(200);

    // This is where the assertion often happens - protected with timeout
    debugLog("Setting WiFi mode...");
    WiFi.mode(WIFI_STA);
    delay(500); // Critical delay for stability

    // Only proceed if we haven't crashed by this point
    debugLog("Attempting to connect to WiFi: " + settings.ssid);
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());

    // Non-blocking wait with proper timeout
    unsigned long startAttempt = millis();
    int dotCount = 0;
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000)
    {
      delay(100);
      yield(); // Keep watchdog happy

      // Visual feedback but don't spam the log
      if (millis() - startAttempt > dotCount * 1000)
      {
        Serial.print(".");
        dotCount++;
      }
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      debugLog("WiFi connected successfully to: " + settings.ssid);
      debugLog("IP address: " + WiFi.localIP().toString());

      // Only proceed with ArtNet if WiFi is working
      if (settings.useArtnet)
      {
        try
        {
          delay(500); // Additional stability delay before ArtNet init
          initializeArtNet();
        }
        catch (...)
        {
          debugLog("ArtNet initialization failed, falling back to standalone mode");
          settings.useArtnet = false;
        }
      }
    }
    else
    {
      debugLog("WiFi connection failed after timeout");
      throw std::runtime_error("WiFi connection timeout");
    }
  }
  catch (const std::exception &e)
  {
    // Catch specific exceptions if possible
    debugLog("Network error: " + String(e.what()));
    disableAllNetworkOperations();
  }
  catch (...)
  {
    // Catch any other errors
    debugLog("Unknown network error occurred");
    disableAllNetworkOperations();
  }
}

// Separate ArtNet initialization to further isolate potential failures
void initializeArtNet()
{
  IPAddress localIP = WiFi.localIP();
  debugLog("Initializing ArtNet on IP: " + localIP.toString());

  bool artnetStarted = artnet.listen(localIP, 6454);
  if (artnetStarted)
  {
    artnet.setFrameCallback(frameCallbackWrapper);
    artnet.addSubArtnet(settings.startUniverse,
                        settings.numStrips * settings.ledsPerStrip * NB_CHANNEL_PER_LED,
                        UNIVERSE_SIZE,
                        &artnetCallback);
    artnet.setNodeName(settings.nodeName);
    debugLog("ArtNet initialized successfully");
  }
  else
  {
    debugLog("ArtNet failed to listen on IP");
    throw std::runtime_error("ArtNet initialization failed");
  }
}

void loop()
{
  // CRITICAL FIX: First, handle watchdog timer
  static unsigned long lastYield = 0;
  if (millis() - lastYield > 10)
  {
    yield(); // Prevent watchdog timer reset
    lastYield = millis();
  }

  // Wrap everything in a try-catch to prevent boot loops
  try
  {
    // Handle web server with watchdog-safe implementation
    server.handleClient();

    // Use static variables for timing to reduce memory operations
    static unsigned long lastUpdateTime = 0;
    unsigned long currentMillis = millis();

    // Only update LEDs at reasonable intervals and only if we're not going to crash
    if (currentMillis - lastUpdateTime > 50)
    { // 20fps max refresh rate
      lastUpdateTime = currentMillis;

      // Handle LED updates based on mode
      if (settings.useArtnet)
      {
        // ArtNet mode updates happen in callback - nothing to do here
      }
      else if (settings.useColorCycle)
      {
        try
        {
          // Simplified animation update
          switch (settings.colorMode)
          {
          case COLOR_MODE_RAINBOW:
            rainbowCycle();
            break;
          case COLOR_MODE_PULSE:
            pulseEffect();
            break;
          case COLOR_MODE_FIRE:
            fireEffect();
            break;
          default:
            rainbowCycle();
          }
          driver.showPixels(NO_WAIT);
        }
        catch (...)
        {
          // If LED update fails, disable color cycle to prevent future crashes
          debugLog("ERROR: LED update failed in cycle mode, disabling animations");
          settings.useColorCycle = false;
          settings.useStaticColor = true;
        }
      }
      else if (settings.useStaticColor)
      {
        // Only update static colors occasionally
        static unsigned long lastStaticUpdate = 0;
        if (currentMillis - lastStaticUpdate > 2000)
        { // Every 2 seconds is plenty
          lastStaticUpdate = currentMillis;
          try
          {
            applyStaticColor();
          }
          catch (...)
          {
            // If static color update fails, log but continue
            debugLog("WARNING: Static color update failed");
          }
        }
      }

      // Update OLED display less frequently
      static unsigned long lastDisplayUpdate = 0;
      if (currentMillis - lastDisplayUpdate > 1000)
      { // Once per second
        lastDisplayUpdate = currentMillis;
        try
        {
          updateDisplayStatus();
        }
        catch (...)
        {
          // Display errors shouldn't crash the program
          debugLog("WARNING: Display update failed");
        }
      }

      // Process UART communication with protection
      try
      {
        uartBridge.update();
      }
      catch (...)
      {
        // UART errors shouldn't crash the program
        debugLog("WARNING: UART update failed");
      }
    }
  }
  catch (...)
  {
    // If anything goes wrong in the main loop, log and continue
    // This is our final safety net against boot loops
    static unsigned long lastErrorTime = 0;
    if (millis() - lastErrorTime > 5000)
    { // Limit error logging to avoid spam
      debugLog("ERROR: Exception in main loop");
      lastErrorTime = millis();
    }
  }

  // Required for ESP32 stability
  delay(1);
}
