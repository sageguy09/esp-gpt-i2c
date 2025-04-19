#include "ESP_GPT_I2C_Common.h"
#include <FastLED.h>
#include <WiFi.h>

// Define globals
AsyncUDP artnetUdp;
CRGB leds[NUM_LEDS];

// Setup ArtNet functionality
void setupArtNet() {
  debugLog("Initializing ArtNet controller...");
  
  // Initialize LED strip
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(settings.brightness);
  
  // Clear LEDs initially
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  
  // Only start ArtNet if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    // Start listening for ArtNet packets
    if(artnetUdp.listen(ARTNET_PORT)) {
      debugLog("Listening for ArtNet packets on port 6454, universe " + String(settings.artnetUniverse));
      
      // Set up packet handler
      artnetUdp.onPacket([](AsyncUDPPacket packet) {
        processArtNetPacket(packet);
      });
      
      state.artnetRunning = true;
      
      // Show startup animation
      startupAnimation();
    } else {
      debugLog("Failed to start ArtNet UDP listener");
      state.artnetRunning = false;
    }
  } else {
    debugLog("ArtNet not started - WiFi not connected");
    state.artnetRunning = false;
  }
}

void processArtNetPacket(AsyncUDPPacket packet) {
  uint8_t* data = packet.data();
  size_t length = packet.length();
  
  // Minimum packet size check
  if (length < 18) {
    return;
  }
  
  // Check for "Art-Net" header
  if (data[0] != 'A' || data[1] != 'r' || data[2] != 't' || data[3] != '-' ||
      data[4] != 'N' || data[5] != 'e' || data[6] != 't' || data[7] != 0) {
    return;
  }
  
  // Check OpCode for ArtDMX (0x5000)
  uint16_t opCode = data[8] | (data[9] << 8);
  if (opCode != 0x5000) {
    return;
  }
  
  // Extract universe
  uint16_t universe = data[14] | (data[15] << 8);
  
  // Only process if it's our universe
  if (universe != settings.artnetUniverse) {
    return;
  }
  
  // Extract data length
  uint16_t dataLength = (data[16] << 8) | data[17];
  
  // Update LEDs with DMX data
  updateLEDs(&data[18], dataLength);
  
  // Update statistics
  state.artnetPacketCount++;
  state.lastArtnetPacket = millis();
}

void updateLEDs(uint8_t* dmxData, uint16_t length) {
  // Calculate number of LEDs we can update (3 channels per LED for RGB)
  int ledCount = min(settings.ledCount, length / 3);
  
  // Map DMX data to LEDs
  for (int i = 0; i < ledCount; i++) {
    leds[i].r = dmxData[i * 3];
    leds[i].g = dmxData[i * 3 + 1];
    leds[i].b = dmxData[i * 3 + 2];
  }
  
  // Show the LEDs
  FastLED.show();
}

void startupAnimation() {
  // Clear all LEDs
  fill_solid(leds, settings.ledCount, CRGB::Black);
  FastLED.show();
  delay(500);
  
  // Red sweep
  for (int i = 0; i < settings.ledCount; i++) {
    leds[i] = CRGB::Red;
    if (i > 0) leds[i-1] = CRGB::Black;
    FastLED.show();
    delay(5);
  }
  leds[settings.ledCount-1] = CRGB::Black;
  FastLED.show();
  
  // Green sweep
  for (int i = 0; i < settings.ledCount; i++) {
    leds[i] = CRGB::Green;
    if (i > 0) leds[i-1] = CRGB::Black;
    FastLED.show();
    delay(5);
  }
  leds[settings.ledCount-1] = CRGB::Black;
  FastLED.show();
  
  // Blue sweep
  for (int i = 0; i < settings.ledCount; i++) {
    leds[i] = CRGB::Blue;
    if (i > 0) leds[i-1] = CRGB::Black;
    FastLED.show();
    delay(5);
  }
  leds[settings.ledCount-1] = CRGB::Black;
  FastLED.show();
  
  // Final clear
  fill_solid(leds, settings.ledCount, CRGB::Black);
  FastLED.show();
}