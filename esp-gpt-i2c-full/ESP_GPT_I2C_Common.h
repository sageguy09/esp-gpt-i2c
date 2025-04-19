#ifndef ESP_GPT_I2C_COMMON_H
#define ESP_GPT_I2C_COMMON_H

#include <Arduino.h>
#include <Preferences.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include <WiFi.h>
#include <AsyncUDP.h>
#include <FastLED.h>

// Define constants
#define DEBUG_ENABLED true
#define MAX_LOG_ENTRIES 20

// LED specific configuration
#define LED_PIN 2
#define NUM_LEDS 144
#define ARTNET_UNIVERSE 0
#define ARTNET_PORT 6454

// Basic settings structure
struct Settings
{
  String ssid = "Sage1";
  String password = "J@sper123";
  bool useWiFi = true;
  String nodeName = "ESP32_Test";

  // LED and ArtNet settings
  uint16_t artnetUniverse = ARTNET_UNIVERSE;
  uint16_t ledCount = NUM_LEDS;
  uint8_t ledPin = LED_PIN;
  uint8_t brightness = 255;
  bool artnetEnabled = true;
};

// Basic state for logs and status
struct State
{
  String logs[MAX_LOG_ENTRIES];
  int logIndex = 0;

  // ArtNet state tracking
  uint32_t artnetPacketCount = 0;
  unsigned long lastArtnetPacket = 0;
  bool artnetRunning = false;
};

// Global variables declaration (defined in implementation file)
extern bool networkInitFailed;
extern TaskHandle_t networkTaskHandle;
extern bool espNetifInitialized;
extern SemaphoreHandle_t networkSemaphore;
extern Settings settings;
extern State state;
extern Preferences preferences;
extern AsyncUDP artnetUdp;
extern CRGB leds[];

// Function declarations
void debugLog(String msg);
void disableAllNetworkOperations();
void networkInitTask(void *parameter);

// ArtNet specific functions
bool setupArtNet();
void processArtNetPacket(AsyncUDPPacket packet);
void updateLEDs(uint8_t *dmxData, uint16_t numChannels);
void startupAnimation();

#endif // ESP_GPT_I2C_COMMON_H