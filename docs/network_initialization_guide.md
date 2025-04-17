# ESP32 Network Initialization Guide

## 🔍 Issue Analysis

The ESP32 can experience assertion failures in the lwIP TCP/IP stack, particularly this error:

```
assert failed: tcpip_send_msg_wait_sem IDF/components/lwip/lwip/src/api/tcpic.c:455 (Invalid mbox)
```

This occurs due to:
1. **Initialization Order Problems**: Components initialized in the wrong sequence
2. **Resource Conflicts**: Competition between peripherals (especially LED drivers and WiFi)
3. **Concurrency Issues**: FreeRTOS task/semaphore management issues
4. **Timing Problems**: Insufficient delays between critical operations
5. **Memory Management Issues**: Improper handling of memory allocation and deallocation

## 🛠️ Initialization Best Practices

### Correct Initialization Order

The optimal initialization sequence for ESP32 is:

1. **Serial** (for debugging)
2. **Non-volatile Storage** (NVS flash initialization)
3. **TCP/IP Stack** (core networking components)
4. **Network Interfaces** (WiFi/Ethernet)
5. **Communication** (UART bridge)
6. **Peripherals** (LED driver, displays)
7. **Web Server** (after all other components)

### Detailed Initialization Example

```cpp
void setup() {
  // 1. Serial debugging (always first)
  Serial.begin(115200);
  delay(100);  // Stabilization delay
  
  // 2. System-level initialization
  esp_err_t ret = nvs_flash_init(); // Initialize NVS for settings
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);
  
  // 3. Load settings and check boot history
  loadSettings();
  checkBootLoopProtection();
  
  // 4. TCP/IP stack initialization BEFORE any networking
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  
  // 5. Network interface initialization
  if (!inSafeMode && (settings.useWifi || settings.useArtnet)) {
    if (!initializeNetworkWithTimeout()) {
      Serial.println("Network initialization failed, entering safe mode");
      inSafeMode = true;
    }
  }
  
  // 6. Initialize remaining peripherals only after network is ready
  initializeUARTWithErrorHandling();
  initializeLEDDriverWithErrorHandling();
  initializeOLEDWithErrorHandling();
  
  // 7. Final services that depend on all previous components
  if (!inSafeMode) {
    startWebServer();
  }
  
  // 8. Mark successful boot
  updateBootCounter(true);
}
```

### Robust Network Initialization

For a robust network initialization function:

```cpp
bool initializeNetworkWithTimeout() {
  // First initialize the network interface
  esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
  if (sta_netif == NULL) {
    Serial.println("Failed to create network interface");
    return false;
  }
  
  // Initialize WiFi with proper error handling
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t err = esp_wifi_init(&cfg);
  if (err != ESP_OK) {
    Serial.printf("WiFi init failed: %s\n", esp_err_to_name(err));
    return false;
  }
  
  // Set WiFi mode and configuration
  err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (err != ESP_OK) return false;
  
  // Configure WiFi with settings
  wifi_config_t wifi_config = {};
  strncpy((char*)wifi_config.sta.ssid, settings.ssid, sizeof(wifi_config.sta.ssid));
  strncpy((char*)wifi_config.sta.password, settings.password, sizeof(wifi_config.sta.password));
  
  err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
  if (err != ESP_OK) return false;
  
  // Start WiFi with timeout
  err = esp_wifi_start();
  if (err != ESP_OK) return false;
  
  // Wait for connection with timeout
  unsigned long startTime = millis();
  const unsigned long timeout = 15000; // 15 seconds timeout
  
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < timeout) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Failed to connect to WiFi within timeout");
    return false;
  }
  
  Serial.print("Connected to WiFi. IP: ");
  Serial.println(WiFi.localIP());
  return true;
}
```

## 🛡️ Error Handling and Recovery

### Boot Loop Prevention

Implement boot loop detection and safe mode transition:

```cpp
void checkBootLoopProtection() {
  Preferences prefs;
  prefs.begin("system", false);
  
  // Get boot count and last boot time
  uint8_t bootCount = prefs.getUChar("bootCount", 0);
  uint32_t lastBootTime = prefs.getUInt("lastBootTime", 0);
  uint32_t currentTime = millis();
  
  // Check if we're in a boot loop (multiple boots in short time)
  if (bootCount >= 3 && (currentTime - lastBootTime < 60000)) {
    Serial.println("Boot loop detected, entering safe mode");
    inSafeMode = true;
    
    // Reset counter but keep safe mode for this boot
    prefs.putUChar("bootCount", 0);
  } else {
    // Increment boot count
    prefs.putUChar("bootCount", bootCount + 1);
  }
  
  // Save current boot time
  prefs.putUInt("lastBootTime", currentTime);
  prefs.end();
}

void updateBootCounter(bool success) {
  if (success) {
    Preferences prefs;
    prefs.begin("system", false);
    prefs.putUChar("bootCount", 0); // Reset on successful boot
    prefs.end();
  }
}
```

### Task Management

Proper FreeRTOS task management is crucial:

```cpp
TaskHandle_t networkTask = NULL;

void createTasks() {
  // Only create network-dependent tasks after network is ready
  if (!inSafeMode && WiFi.status() == WL_CONNECTED) {
    // Higher priority for network task (but not maximum)
    xTaskCreatePinnedToCore(
      networkTaskFunction,    // Task function
      "NetworkTask",          // Name
      8192,                   // Stack size (bytes)
      NULL,                   // Parameters
      3,                      // Priority (0-24, higher = more priority)
      &networkTask,           // Task handle
      0                       // Core (0 or 1)
    );
    
    // Lower priority for LED updates
    xTaskCreatePinnedToCore(
      ledUpdateTask,
      "LEDTask",
      4096,
      NULL,
      1,
      NULL,
      1  // Run on second core
    );
  }
}

// Network task with proper resource management
void networkTaskFunction(void *parameter) {
  while (true) {
    // Check if network is still connected
    if (WiFi.status() != WL_CONNECTED) {
      // Handle network disconnect
      vTaskDelay(1000 / portTICK_PERIOD_MS);
      continue;
    }
    
    // Network operations with proper error handling
    if (settings.useArtnet) {
      processArtnetPackets();
    }
    
    // Yield some time to other tasks
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
```

### Memory Management

Safe memory management prevents crashes:

```cpp
// Safe memory allocation with verification
void* safeAlloc(size_t size) {
  void* ptr = malloc(size);
  if (ptr == NULL) {
    Serial.printf("Memory allocation failed for %d bytes\n", size);
    // Log memory stats
    Serial.printf("Free heap: %d, min free: %d\n", 
                 ESP.getFreeHeap(), ESP.getMinFreeHeap());
  }
  return ptr;
}

// Example of proper memory handling in networking code
bool sendNetworkData(const uint8_t* data, size_t length) {
  // Allocate buffer with size checking
  if (length > 1472) { // Max safe UDP payload
    Serial.println("Packet too large");
    return false;
  }
  
  uint8_t* buffer = (uint8_t*)safeAlloc(length);
  if (buffer == NULL) {
    return false;
  }
  
  // Copy data and send
  memcpy(buffer, data, length);
  bool result = udp.beginPacket(targetIP, targetPort) && 
                udp.write(buffer, length) && 
                udp.endPacket();
  
  // Always free resources
  free(buffer);
  return result;
}
```

## 📊 Testing and Verification

### Comprehensive Test Plan

1. **Startup Testing**: 
   - Test each component in isolation
   - Verify initialization order works correctly
   - Test with different configuration combinations

2. **Stress Testing**:
   - Power cycle the device multiple times in quick succession
   - Test with WiFi AP appearing/disappearing
   - Test with high network traffic conditions

3. **Network Recovery**:
   - Verify behavior when WiFi is unavailable
   - Test reconnection after WiFi loss
   - Ensure LED functionality continues regardless of network state

4. **Error Simulation**:
   - Intentionally cause errors (wrong passwords, etc.)
   - Verify graceful failure and fallback mechanisms
   - Test recovery from simulated crashes

## 📝 Diagnostic and Debugging

### Serial Logging

Implement comprehensive logging for diagnostics:

```cpp
void debugLog(const char* format, ...) {
  char buffer[256];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  
  // Print timestamp
  unsigned long ms = millis();
  unsigned long seconds = ms / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  
  Serial.printf("[%02lu:%02lu:%02lu.%03lu] %s\n", 
                hours % 24, minutes % 60, seconds % 60, ms % 1000,
                buffer);
                
  // Optionally log to OLED or other display
  if (oledAvailable) {
    // Update OLED with last log message
  }
}
```

### Memory Monitoring

Track memory usage to identify leaks:

```cpp
void reportMemoryStats() {
  static unsigned long lastReportTime = 0;
  
  // Report every 30 seconds
  if (millis() - lastReportTime > 30000) {
    debugLog("Memory - Free: %d, Min Free: %d, Max Alloc: %d", 
             ESP.getFreeHeap(),
             ESP.getMinFreeHeap(),
             ESP.getMaxAllocHeap());
    lastReportTime = millis();
  }
}
```

## 🚨 Recovery Strategy

### Safe Mode Implementation

Implement a safe mode with essential functionality:

```cpp
void enterSafeMode() {
  debugLog("ENTERING SAFE MODE");
  
  // Disable all network functionality
  if (WiFi.status() == WL_CONNECTED) {
    WiFi.disconnect(true);
  }
  WiFi.mode(WIFI_OFF);
  
  // Disable ArtNet
  settings.useArtnet = false;
  
  // Set LEDs to a safe static mode
  settings.useStaticColor = true;
  settings.staticRed = 0;
  settings.staticGreen = 0;
  settings.staticBlue = 64; // Dim blue to indicate safe mode
  
  // Mark that we're in safe mode
  inSafeMode = true;
  
  // Update LEDs to indicate safe mode
  updateLEDs();
  
  // Save these settings
  saveSettings();
}
```

### Recovery Button

Implement a physical reset button:

```cpp
void checkRecoveryButton() {
  // Check if recovery button is pressed (active low on GPIO0)
  if (digitalRead(0) == LOW) {
    static unsigned long pressStartTime = 0;
    
    if (pressStartTime == 0) {
      pressStartTime = millis();
    } else if (millis() - pressStartTime > 5000) {
      // Button held for 5 seconds - factory reset
      factoryReset();
      ESP.restart();
    }
  } else {
    pressStartTime = 0;
  }
}

void factoryReset() {
  debugLog("FACTORY RESET INITIATED");
  
  // Clear all settings
  Preferences prefs;
  prefs.begin("system", false);
  prefs.clear();
  prefs.end();
  
  // Reset boot counter
  Preferences bootPrefs;
  bootPrefs.begin("boot", false);
  bootPrefs.clear();
  bootPrefs.end();
}
```

## 🔑 Best Practices Summary

1. **Initialization Order Matters**:
   - Always follow the correct sequence: Serial → System → TCP/IP → Network → Peripherals → Services
   - Initialize low-level components before dependent high-level ones
   - Use strategic delays between critical initializations

2. **Error Handling is Critical**:
   - Never allow initialization to fail silently
   - Implement timeouts for all blocking operations
   - Have fallback modes for all critical functions

3. **Resource Management**:
   - Always check memory allocation results
   - Free resources as soon as they're no longer needed
   - Monitor heap fragmentation and memory usage

4. **Task Synchronization**:
   - Keep related operations in the same task
   - Use appropriate FreeRTOS synchronization primitives
   - Pin critical tasks to specific cores when needed
   - Use appropriate task priorities

5. **Persistent State Management**:
   - Save critical state information for recovery after crashes
   - Implement boot counters to detect crash loops
   - Have safe fallback modes for all critical systems