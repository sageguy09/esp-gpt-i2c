// ESP32 Minimal Test - With ArtNet DMX Support
// This implementation now includes ArtNet DMX protocol support for WS2812B LEDs

#include <WiFi.h>
// Add ESP-IDF core includes
#include "esp_netif.h"
#include "esp_event.h"
#include <stdexcept> // For std::runtime_error
#include "esp_err.h" // For esp_err_to_name
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // For semaphores
#include "esp_system.h"      // For esp_reset_reason

// Include common code
#include "ESP_GPT_I2C_Common.h"

// Function to verify TCP/IP components are initialized properly
bool verifyTcpIpInit()
{
  if (!espNetifInitialized)
  {
    debugLog("TCP/IP components not properly initialized!");
    return false;
  }

  // Additional verification could be added here
  return true;
}

// ====== MEMORY MANAGEMENT ======
// Safe memory allocation with verification and logging
void *safeAlloc(size_t size, const char *allocation_name = "unknown")
{
  void *ptr = malloc(size);
  if (ptr == NULL)
  {
    String errorMsg = "Memory allocation failed for ";
    errorMsg += allocation_name;
    errorMsg += " (";
    errorMsg += String(size);
    errorMsg += " bytes), free heap: ";
    errorMsg += String(ESP.getFreeHeap());
    debugLog(errorMsg);
  }
  return ptr;
}

void setup()
{
  // Start with bare minimum initialization
  Serial.begin(115200);
  delay(1000); // Longer delay to ensure serial is ready
  debugLog("ESP32 ArtNet DMX Controller - Minimal Implementation");

  // Check reset reason - important for diagnostics
  esp_reset_reason_t reason = esp_reset_reason();
  debugLog("Reset reason: " + String(reason));

  // Create synchronization semaphore
  networkSemaphore = xSemaphoreCreateBinary();
  if (networkSemaphore == NULL)
  {
    debugLog("ERROR: Failed to create network semaphore");
  }

  // Load network failure state from preferences
  preferences.begin("led-settings", false);
  networkInitFailed = preferences.getBool("netFailed", false);
  preferences.end();

  if (networkInitFailed)
  {
    debugLog("Network previously failed - staying in offline mode");
    return;
  }

  // **** CRITICAL FIX: Core scheduling for network initialization ****
  // Create a dedicated task on Core 1 for network initialization
  if (settings.useWiFi)
  {
    debugLog("Creating network initialization task on Core 1");

    // Ensure this task has sufficient stack size and higher priority
    xTaskCreatePinnedToCore(
        networkInitTask,    // Task function
        "NetworkInitTask",  // Task name
        16384,              // Stack size (bytes) - doubled from previous value
        NULL,               // Task parameter
        5,                  // Task priority (increased to 5 for network operations)
        &networkTaskHandle, // Task handle
        1                   // Core to run the task on (Core 1)
    );

    // Wait for network initialization to complete or timeout
    if (networkSemaphore != NULL)
    {
      if (xSemaphoreTake(networkSemaphore, pdMS_TO_TICKS(10000)) == pdTRUE)
      {
        debugLog("Network initialization completed successfully");
      }
      else
      {
        debugLog("Network initialization timed out");
      }
    }

    // Brief delay to allow task to start
    delay(200); // Increased delay to ensure task has time to initialize
  }
  else
  {
    debugLog("Network disabled in settings");
  }

  // Set up ArtNet after network is initialized
  if (WiFi.status() == WL_CONNECTED)
  {
    debugLog("Setting up ArtNet DMX...");
    setupArtNet();
  }
  else
  {
    debugLog("Skipping ArtNet setup - WiFi not connected");
  }

  debugLog("Setup complete - entering main loop");
}

void loop()
{
  // Keep the watchdog happy
  yield();
  delay(10);

  // Show periodic status and handle network monitoring
  static unsigned long lastStatus = 0;
  static unsigned long lastReconnectAttempt = 0;
  unsigned long currentMillis = millis();

  // Status update every 5 seconds
  if (currentMillis - lastStatus > 5000)
  {
    lastStatus = currentMillis;

    // Monitor memory usage
    debugLog("Free heap: " + String(ESP.getFreeHeap()) + " bytes");

    if (networkInitFailed)
    {
      debugLog("Status: Network disabled due to previous failure");
    }
    else if (WiFi.status() == WL_CONNECTED)
    {
      debugLog("Status: WiFi connected, IP: " + WiFi.localIP().toString());

      // Include ArtNet status
      if (state.artnetRunning)
      {
        debugLog("ArtNet: Active, Universe: " + String(settings.artnetUniverse) + 
                 ", Packets: " + String(state.artnetPacketCount) + 
                 ", Last: " + (state.lastArtnetPacket > 0 ? 
                                String((currentMillis - state.lastArtnetPacket) / 1000) + "s ago" : 
                                "never"));
      }
      else
      {
        debugLog("ArtNet: Not running");
      }
    }
    else
    {
      debugLog("Status: WiFi disconnected");

      // Attempt reconnection every 30 seconds if WiFi is disconnected
      if (currentMillis - lastReconnectAttempt > 30000 && !networkInitFailed && settings.useWiFi)
      {
        lastReconnectAttempt = currentMillis;
        debugLog("Attempting WiFi reconnection...");

        // Only try to reconnect if TCP/IP stack is properly initialized
        if (verifyTcpIpInit())
        {
          WiFi.disconnect(true);
          delay(100);
          WiFi.begin(settings.ssid.c_str(), settings.password.c_str());

          // If reconnected, restart ArtNet
          if (WiFi.status() == WL_CONNECTED && !state.artnetRunning)
          {
            debugLog("WiFi reconnected, restarting ArtNet...");
            setupArtNet();
          }
        }
        else
        {
          debugLog("Skipping reconnect - TCP/IP stack not initialized");
        }
      }
    }
  }
}
