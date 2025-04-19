#ifndef ESP_GPT_I2C_COMMON_H
#define ESP_GPT_I2C_COMMON_H

#include <Arduino.h>
#include <Preferences.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include <FastLED.h>
#include <AsyncUDP.h>

// Define constants
#define DEBUG_ENABLED true
#define MAX_LOG_ENTRIES 20

// ArtNet and LED constants
#define LED_PIN 2          // GPIO pin connected to the LED strip
#define NUM_LEDS 144       // Number of LEDs in your strip
#define ARTNET_UNIVERSE 0  // ArtNet universe to listen to
#define ARTNET_PORT 6454

// Basic settings structure
struct Settings {
  String ssid = "Sage1";
  String password = "J@sper123";
  bool useWiFi = true;
  String nodeName = "ESP32_Test";
  
  // ArtNet settings
  uint16_t artnetUniverse = 0;
  uint16_t ledCount = 144;
  uint8_t ledPin = 2;
  uint8_t brightness = 128;
  bool artnetEnabled = true;
};

// Basic state for logs
struct State {
  String logs[MAX_LOG_ENTRIES];
  int logIndex = 0;
  
  // ArtNet state
  uint32_t artnetPacketCount = 0;
  uint32_t lastArtnetPacket = 0;
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

// ArtNet globals
extern AsyncUDP artnetUdp;
extern CRGB leds[];

// Function declarations
void debugLog(String msg);
void disableAllNetworkOperations();
void networkInitTask(void *parameter);

// ArtNet function declarations
void setupArtNet();
void processArtNetPacket(AsyncUDPPacket packet);
void updateLEDs(uint8_t* dmxData, uint16_t length);
void startupAnimation();

#endif // ESP_GPT_I2C_COMMON_H