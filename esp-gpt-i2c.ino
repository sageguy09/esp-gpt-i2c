// ESP32 Artnet I2SClockless with Web UI + OLED Debug
// Built for ESP-WROOM-32 using 3-4 pins with up to 3 WS2812B strips per pin
// Features: Artnet, web interface config, OLED status, debug log

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
#define NUMSTRIPS 2
#define NB_CHANNEL_PER_LED 3
#define UNIVERSE_SIZE 510
#define STRIPS_PER_PIN 3

#define DEBUG_ENABLED true
#define MAX_LOG_ENTRIES 50

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
} settings = {
    .numStrips = 6,
    .ledsPerStrip = 144,
    .pins = {2, 4, 12, 14},
    .brightness = 255,
    .ssid = "Sage1",
    .password = "J@sper123",
    .useWiFi = false,
    .nodeName = "ESP32_Artnet_I2S",
    .startUniverse = 0};

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
  String html = "<html><head><title>ESP32 Artnet</title></head><body><h2>ESP32 Artnet I2SClockless</h2>";
  html += "<form method='POST' action='/config'>";
  html += "<label>LEDs/Strip:</label><input name='leds' value='" + String(settings.ledsPerStrip) + "'><br>";
  html += "<label>Strips:</label><input name='strips' value='" + String(settings.numStrips) + "'><br>";
  html += "<label>Brightness:</label><input name='bright' value='" + String(settings.brightness) + "'><br>";
  html += "<label>SSID:</label><input name='ssid' value='" + settings.ssid + "'><br>";
  html += "<label>Password:</label><input name='pass' value='" + settings.password + "'><br>";
  html += "<input type='submit'></form></body></html>";
  server.send(200, "text/html", html);
}

void handleConfig()
{
  if (server.hasArg("leds"))
    settings.ledsPerStrip = server.arg("leds").toInt();
  if (server.hasArg("strips"))
    settings.numStrips = server.arg("strips").toInt();
  if (server.hasArg("bright"))
    settings.brightness = server.arg("bright").toInt();
  if (server.hasArg("ssid"))
    settings.ssid = server.arg("ssid");
  if (server.hasArg("pass"))
    settings.password = server.arg("pass");
  // Now only saves settings in memory, no SPIFFS involved
  server.sendHeader("Location", "/", true);
  server.send(302, "text/plain", "Updated");
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

// ====== SETUP ======
void setup()
{
  Serial.begin(115200);

  WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
  int timeout = 10;
  while (WiFi.status() != WL_CONNECTED && timeout--)
  {
    delay(1000);
    Serial.print(".");
  }

  server.on("/", handleRoot);
  server.on("/config", HTTP_POST, handleConfig);
  server.on("/debug", handleDebugLog);
  server.begin();

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))
  {
    Serial.println("OLED failed");
  }
  else
  {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.print("Artnet IP: ");
    display.println(WiFi.localIP());
    display.display();
  }

  driver.setBrightness(settings.brightness);
  driver.initled(NULL, settings.pins, 3, settings.ledsPerStrip * STRIPS_PER_PIN);

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
  }
  else
  {
    Serial.println("Failed to start Artnet listener");
  }
}

void loop()
{
  server.handleClient();
}
