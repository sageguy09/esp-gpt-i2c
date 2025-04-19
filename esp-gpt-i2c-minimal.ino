// ESP32 Minimal Test - Fixing TCP/IP assertion issues
// This file isolates network initialization to resolve assertion failures

#include <WiFi.h>
#include <Preferences.h>
// Add ESP-IDF core includes
#include "esp_netif.h"
#include "esp_event.h"
#include <stdexcept> // For std::runtime_error
#include "esp_err.h" // For esp_err_to_name
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // For semaphores
#include "esp_system.h"      // For esp_reset_reason

// Define constants
#define DEBUG_ENABLED true
#define MAX_LOG_ENTRIES 20

// Global variables for tracking network initialization state
bool networkInitFailed = false;
TaskHandle_t networkTaskHandle = NULL;
bool espNetifInitialized = false;          // Track if ESP-IDF components are already initialized
SemaphoreHandle_t networkSemaphore = NULL; // Semaphore for network init synchronization

// Basic settings
struct Settings
{
  String ssid = "Sage1";
  String password = "J@sper123";
  bool useWiFi = true;
  String nodeName = "ESP32_Test";
} settings;

// Basic state for logs
struct State
{
  String logs[MAX_LOG_ENTRIES];
  int logIndex = 0;
} state;

// Preferences for persisting settings
Preferences preferences;

// ====== DEBUG ======
void debugLog(String msg)
{
  if (!DEBUG_ENABLED)
    return;
  Serial.println(msg);
  state.logs[state.logIndex] = msg;
  state.logIndex = (state.logIndex + 1) % MAX_LOG_ENTRIES;
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

// Completely disable network functionality
void disableAllNetworkOperations()
{
  // Complete network shutdown sequence
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Mark as failed to prevent any future attempts
  networkInitFailed = true;

  // Force settings to disable any network functionality
  settings.useWiFi = false;

  // Persist the network failure state to prevent future attempts after reboot
  preferences.begin("led-settings", false);
  preferences.putBool("netFailed", true);
  preferences.end();

  debugLog("CRITICAL: Network stack disabled due to assertion failure");
}

// Network initialization task function that runs on Core 1
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

void setup()
{
  // Start with bare minimum initialization
  Serial.begin(115200);
  delay(1000); // Longer delay to ensure serial is ready
  debugLog("ESP32 Minimal Test for Network Initialization");

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
        }
        else
        {
          debugLog("Skipping reconnect - TCP/IP stack not initialized");
        }
      }
    }
  }
}