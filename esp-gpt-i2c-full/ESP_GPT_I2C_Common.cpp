#include "ESP_GPT_I2C_Common.h"
#include <WiFi.h>
#include <stdexcept>
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_err.h"

// Define global variables
bool networkInitFailed = false;
TaskHandle_t networkTaskHandle = NULL;
bool espNetifInitialized = false;
SemaphoreHandle_t networkSemaphore = NULL;
Settings settings;
State state;
Preferences preferences;
AsyncUDP artnetUdp;
CRGB leds[NUM_LEDS];

void debugLog(String msg)
{
  if (!DEBUG_ENABLED)
    return;
  Serial.println(msg);
  state.logs[state.logIndex] = msg;
  state.logIndex = (state.logIndex + 1) % MAX_LOG_ENTRIES;
}

void disableAllNetworkOperations()
{
  // Complete network shutdown sequence
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Mark as failed to prevent any future attempts
  networkInitFailed = true;

  // Force settings to disable any network functionality
  settings.useWiFi = false;
  settings.artnetEnabled = false;

  // Persist the network failure state to prevent future attempts after reboot
  preferences.begin("led-settings", false);
  preferences.putBool("netFailed", true);
  preferences.end();

  debugLog("CRITICAL: Network stack disabled due to assertion failure");
}

void networkInitTask(void *parameter)
{
  debugLog("Network initialization task started on core " + String(xPortGetCoreID()));
  bool success = false;

  // Initialize the network with proper sequence and safeguards
  try
  {
    // CRITICAL: Wait a moment to ensure the main task is ready
    // This small delay helps prevent race conditions with the main CPU
    delay(100);

    // Start with a clean state
    WiFi.disconnect(true);
    delay(500);
    WiFi.mode(WIFI_OFF);
    delay(500);

    // **** CRITICAL FIX: Proper ESP-IDF component initialization sequence ****
    // This sequence is essential - the exact order matters
    debugLog("Initializing TCP/IP core components...");

    // First, initialize the TCP/IP adapter with a check to prevent double initialization
    if (!espNetifInitialized)
    {
      esp_err_t err = esp_netif_init();
      if (err != ESP_OK)
      {
        // More robust error reporting with error code protection
        String errorMsg = "TCP/IP adapter initialization failed: ";
        errorMsg += String((int)err); // Convert error code to integer instead of using esp_err_to_name
        debugLog(errorMsg);
        throw std::runtime_error("TCP/IP adapter initialization failed");
      }
      espNetifInitialized = true;
      debugLog("TCP/IP adapter initialized successfully");
    }
    else
    {
      debugLog("TCP/IP adapter already initialized");
    }

    // Then create the default event loop
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) // ESP_ERR_INVALID_STATE means it's already created, which is fine
    {
      // More robust error reporting
      String errorMsg = "Event loop creation failed: ";
      errorMsg += String((int)err);
      debugLog(errorMsg);
      throw std::runtime_error("Event loop creation failed");
    }
    debugLog("Event loop initialized successfully");

    // Strategic yield to ensure other threads have time to process
    yield();

    // Only after ESP-IDF initialization, initialize the WiFi station
    debugLog("ESP-IDF core components initialized, starting WiFi...");
    WiFi.mode(WIFI_STA);
    delay(300); // Extended delay after mode change - critical for stability

    // Connect with timeout
    debugLog("Attempting to connect to WiFi: " + settings.ssid);
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());

    // Wait for connection with timeout
    int attemptCount = 0;
    while (WiFi.status() != WL_CONNECTED && attemptCount < 20)
    {
      delay(500);
      Serial.print(".");
      // Strategic yield to keep watchdog happy
      yield();
      attemptCount++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      debugLog("WiFi connected successfully to: " + settings.ssid);
      debugLog("IP address: " + WiFi.localIP().toString());
      success = true;
    }
    else
    {
      debugLog("WiFi connection failed");
      // Don't throw here - just log and continue with degraded functionality
    }
  }
  catch (const std::exception &e)
  {
    // Specific exception handling
    debugLog("Network initialization exception: " + String(e.what()));
    disableAllNetworkOperations();
  }
  catch (...)
  {
    // Generic exception handling
    debugLog("Unknown network initialization error");
    disableAllNetworkOperations();
  }

  // Signal completion to the main task
  if (networkSemaphore != NULL)
  {
    xSemaphoreGive(networkSemaphore);
    debugLog("Network initialization semaphore released");
  }

  // Task is complete - delete itself
  debugLog("Network initialization task complete (success: " + String(success ? "true" : "false") + ")");
  networkTaskHandle = NULL;
  vTaskDelete(NULL);
}

// Initialize FastLED and set up ArtNet listener
bool setupArtNet()
{
  // Exit early if network is not available
  if (networkInitFailed || !settings.artnetEnabled)
  {
    debugLog("ArtNet setup skipped - network unavailable or disabled");
    return false;
  }

  // If WiFi is not connected, we can't set up ArtNet
  if (WiFi.status() != WL_CONNECTED)
  {
    debugLog("ArtNet setup failed - WiFi not connected");
    return false;
  }

  debugLog("Setting up FastLED on pin " + String(settings.ledPin));

  // Initialize the LED strip
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, settings.ledCount);
  FastLED.setBrightness(settings.brightness);

  // Clear all LEDs to black
  FastLED.clear();
  FastLED.show();

  // Run the startup animation to confirm LEDs are working
  startupAnimation();

  // Set up the UDP listener for ArtNet packets
  debugLog("Setting up ArtNet listener on port " + String(ARTNET_PORT));
  IPAddress localIP = WiFi.localIP();

  if (artnetUdp.listen(ARTNET_PORT))
  {
    debugLog("ArtNet UDP listener started on port " + String(ARTNET_PORT));

    // Set up the onPacket callback
    artnetUdp.onPacket([](AsyncUDPPacket packet)
                       { processArtNetPacket(packet); });

    state.artnetRunning = true;
    return true;
  }
  else
  {
    debugLog("Failed to start ArtNet UDP listener");
    state.artnetRunning = false;
    return false;
  }
}

// Process incoming ArtNet packet
void processArtNetPacket(AsyncUDPPacket packet)
{
  uint8_t *data = packet.data();
  size_t len = packet.length();

  // ArtNet packets must be at least 18 bytes (header + opcode + universe + length)
  if (len < 18)
  {
    return;
  }

  // Check for ArtNet header - "Art-Net" + null byte
  if (data[0] != 'A' || data[1] != 'r' || data[2] != 't' || data[3] != '-' ||
      data[4] != 'N' || data[5] != 'e' || data[6] != 't' || data[7] != 0)
  {
    return;
  }

  // Check for OpCode 0x5000 (DMX data, little endian)
  if (data[8] != 0x00 || data[9] != 0x50)
  {
    return;
  }

  // Extract universe (lower byte, little endian)
  uint16_t universe = data[14] | (data[15] << 8);

  // Only process if it's our universe
  if (universe != settings.artnetUniverse)
  {
    return;
  }

  // Extract data length (DMX channels, high byte first)
  uint16_t dataLength = (data[16] << 8) | data[17];

  // DMX data starts at offset 18
  uint8_t *dmxData = &data[18];

  // Update LEDs with the DMX data
  updateLEDs(dmxData, dataLength);

  // Update statistics
  state.artnetPacketCount++;
  state.lastArtnetPacket = millis();
}

// Update LEDs based on DMX data
void updateLEDs(uint8_t *dmxData, uint16_t numChannels)
{
  // Each LED uses 3 channels (R,G,B)
  uint16_t numLEDs = (numChannels / 3 < settings.ledCount) ? numChannels / 3 : settings.ledCount;

  // Update each LED with RGB values from DMX data
  for (uint16_t i = 0; i < numLEDs; i++)
  {
    uint16_t dmxOffset = i * 3;

    // Set RGB values from DMX data
    leds[i].r = dmxData[dmxOffset];
    leds[i].g = dmxData[dmxOffset + 1];
    leds[i].b = dmxData[dmxOffset + 2];
  }

  // Update the LEDs
  FastLED.show();
}

// Simple startup animation to confirm LEDs are working
void startupAnimation()
{
  debugLog("Running LED startup animation");

  // Red flash
  fill_solid(leds, settings.ledCount, CRGB::Red);
  FastLED.show();
  delay(300);

  // Green flash
  fill_solid(leds, settings.ledCount, CRGB::Green);
  FastLED.show();
  delay(300);

  // Blue flash
  fill_solid(leds, settings.ledCount, CRGB::Blue);
  FastLED.show();
  delay(300);

  // Clear all LEDs
  FastLED.clear();
  FastLED.show();

  debugLog("Startup animation complete");
}