// ESP32 Artnet I2SClockless with Web UI + OLED Debug
// Built for ESP-WROOM-32 using 3-4 pins with up to 3 WS2812B strips per pin
// Features: Artnet, web interface config, OLED status, debug log
// TODO: bring in check box for static modes. add more control options for that
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

// This must be defined before including I2SClocklessLedDriver.h
#ifndef NUM_LEDS_PER_STRIP
#define NUM_LEDS_PER_STRIP 144 // Define this before library includes
#endif

#include "I2SClocklessLedDriver.h"
#include "artnetESP32V2.h"
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>

// ====== CONFIGURATION ======
#define STATUS_LED_PIN 16
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define MAX_STRIPS 12 // 4 pins × 3 strips per pin
#define NUMSTRIPS 1
#define NB_CHANNEL_PER_LED 3
#define UNIVERSE_SIZE 510
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
    .pins = {12, 14, 2, 4},
    .brightness = 255,
    .ssid = "Sage1",
    .password = "J@sper123",
    .useWiFi = false,
    .nodeName = "ESP32_Artnet_I2S",
    .startUniverse = 0,

    // Default values for new settings
    .useArtnet = true,
    .useColorCycle = false,
    .useStaticColor = false, // Static color disabled by default
    .colorMode = COLOR_MODE_RAINBOW,
    .staticColor = {255, 0, 0}, // Default to red
    .cycleSpeed = 50            // Medium speed
};

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
  html += "</style>";
  html += "</head><body>";
  html += "<h2>ESP32 Artnet LED Controller</h2>";

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

  // Process mode settings
  settings.useArtnet = server.hasArg("useArtnet");
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

  // Process WiFi settings
  if (server.hasArg("ssid"))
    settings.ssid = server.arg("ssid");
  if (server.hasArg("pass"))
    settings.password = server.arg("pass");
  if (server.hasArg("nodeName"))
    settings.nodeName = server.arg("nodeName");

  // Save settings to preferences
  saveSettings();

  // Update LED driver with new brightness
  driver.setBrightness(settings.brightness);

  // For pin changes, we need to re-initialize the LED driver
  driver.initled(NULL, settings.pins, 3, settings.ledsPerStrip * STRIPS_PER_PIN);

  debugLog("Settings updated - Pins: " + String(settings.pins[0]) + "," +
           String(settings.pins[1]) + "," + String(settings.pins[2]) + "," +
           String(settings.pins[3]));

  // Redirect back to root page
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Settings Updated");
}

// ====== ARNET CALLBACK ======
void artnetCallback(void *param)
{
  subArtnet *sub = (subArtnet *)param;

  // Get ArtNet data from the packet and update LED colors
  uint8_t *data = sub->getData();
  int dataLength = sub->_nb_data;

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

  // Update the LEDs with the new data
  driver.showPixels(NO_WAIT);
  sendFrame = true;
}

// Wrapper function for the frame callback to match the expected signature
void frameCallbackWrapper()
{
  // This is just a wrapper to match the signature expected by setFrameCallback
  // The actual callback processing happens in artnetCallback when called by the subArtnet
}

// ====== COLOR FUNCTIONS ======
unsigned long lastColorUpdate = 0;
uint8_t cyclePosition = 0;

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
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++)
  {
    ledData[i] = settings.staticColor;
  }
}

// Main function to update LEDs based on current mode
void updateLEDsBasedOnMode()
{
  unsigned long currentMillis = millis();

  // Only update at the rate determined by cycleSpeed for animations
  // Static color should be applied immediately when changed
  if (!settings.useStaticColor && currentMillis - lastColorUpdate < (101 - settings.cycleSpeed))
  {
    return; // Not time to update yet for animations
  }

  lastColorUpdate = currentMillis;

  if (settings.useArtnet)
  {
    // If ArtNet is enabled, don't overwrite LED data here
    // It will be handled by ArtNet callback
    return;
  }

  if (settings.useStaticColor)
  {
    // Static color has priority over color cycle
    applyStaticColor();
    debugLog("Applied static color: R=" + String(settings.staticColor.r) +
             ", G=" + String(settings.staticColor.g) +
             ", B=" + String(settings.staticColor.b));
  }
  else if (settings.useColorCycle)
  {
    // Apply the selected color cycling mode
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
      rainbowCycle(); // Default to rainbow if mode is invalid
    }
  }
  else
  {
    // If neither static color nor color cycling is enabled,
    // still apply static color as a fallback
    applyStaticColor();
  }

  // Update the LEDs with the new data
  driver.showPixels(NO_WAIT);
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

  preferences.end();

  debugLog("Settings saved to preferences");
}

// ====== SETUP ======
void setup()
{
  Serial.begin(115200);

  // Load settings from preferences
  loadSettings();

  // Initialize display first so we can show status
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println("OLED failed");
  }
  else
  {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("ESP32 ArtNet Controller");
    display.println("Connecting to WiFi...");
    display.display();
  }

  // Connect to WiFi
  WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
  int timeout = 10;
  while (WiFi.status() != WL_CONNECTED && timeout--)
  {
    delay(1000);
    Serial.print(".");

    // Update OLED with countdown
    if (display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
    {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("ESP32 ArtNet Controller");
      display.println("Connecting to WiFi...");
      display.println("Timeout: " + String(timeout));
      display.display();
    }
  }

  // Initialize web server
  server.on("/", handleRoot);
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/debug", handleDebugLog);
  server.begin();

  // Display connection status on OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    display.clearDisplay();
    display.setCursor(0, 0);

    if (WiFi.status() == WL_CONNECTED)
    {
      display.println("Connected to WiFi");
      display.print("IP: ");
      display.println(WiFi.localIP());
    }
    else
    {
      display.println("WiFi connection failed");
      display.println("Running in standalone mode");
    }

    // Show current mode
    display.println("-------------------");
    if (settings.useArtnet)
    {
      display.println("Mode: ArtNet");
    }
    else if (settings.useColorCycle)
    {
      display.print("Mode: Cycle (");
      switch (settings.colorMode)
      {
      case COLOR_MODE_RAINBOW:
        display.print("Rainbow");
        break;
      case COLOR_MODE_PULSE:
        display.print("Pulse");
        break;
      case COLOR_MODE_FIRE:
        display.print("Fire");
        break;
      }
      display.println(")");
    }
    else
    {
      display.println("Mode: Static Color");
    }

    display.display();
  }

  // Initialize LED driver
  driver.setBrightness(settings.brightness);
  driver.initled(NULL, settings.pins, 3, settings.ledsPerStrip * STRIPS_PER_PIN);

  // Only initialize ArtNet if it's enabled
  if (settings.useArtnet)
  {
    IPAddress localIP = WiFi.localIP();
    artnet.listen(localIP, 6454);                  // Start listening for ArtNet data
    artnet.setFrameCallback(frameCallbackWrapper); // Set the callback to handle ArtNet frames

    artnet.addSubArtnet(
        settings.startUniverse,
        settings.numStrips * settings.ledsPerStrip * NB_CHANNEL_PER_LED,
        UNIVERSE_SIZE,
        &artnetCallback);
    artnet.setNodeName(settings.nodeName);

    if (artnet.listen(localIP, 6454))
    {
      Serial.print("Artnet listening on IP: ");
      Serial.println(localIP);
      debugLog("ArtNet initialized successfully");
    }
    else
    {
      Serial.println("Failed to start Artnet listener");
      debugLog("Failed to initialize ArtNet");
    }
  }
  else
  {
    debugLog("ArtNet disabled in settings");
  }

  // Apply initial LED state based on settings
  if (!settings.useArtnet)
  {
    if (settings.useColorCycle)
    {
      // Set initial color cycle mode
      debugLog("Starting in color cycle mode: " + String(settings.colorMode));
    }
    else
    {
      // Set initial static color
      debugLog("Starting in static color mode");
      applyStaticColor();
      driver.showPixels(NO_WAIT);
    }
  }
}

void loop()
{
  server.handleClient();
  updateLEDsBasedOnMode();
}
