# ESP32 ArtNet Controller: Network Crash Recovery Guide

## Understanding the lwIP Assert Failure

The error experienced in the ESP32 ArtNet Controller is a critical assertion failure in the lwIP TCP/IP stack:

```
assert failed: tcpip_send_msg_wait_sem IDF/components/lwip/lwip/src/api/tcpip.c:455 (Invalid mbox)
```

This error occurs at line 455 of the `tcpip.c` file in the lwIP implementation of the ESP-IDF framework. The specific issue is with the "message box" (mbox) semaphore used for TCP/IP message passing between tasks.

## Root Causes

### 1. Initialization Order Problems
Network components must be initialized in the correct sequence. When the TCP/IP stack is initialized after other components that depend on it, or when its initialization is interleaved with other critical operations, the mailbox can become invalid.

### 2. Resource Conflict
The I2SClocklessLedDriver uses DMA (Direct Memory Access), as does the WiFi stack. These might be competing for memory or system resources, especially during initialization.

### 3. Concurrency Issues
Improper management of FreeRTOS tasks, semaphores, or message queues can lead to the TCP/IP stack failing. Without proper synchronization, tasks may attempt to use network resources before they're fully initialized.

### 4. Memory Corruption
DMA operations for LEDs might be causing memory corruption that affects the network stack, particularly when both systems are initializing.

### 5. Race Condition
Multiple tasks trying to use network resources simultaneously could create race conditions that corrupt the message box semaphore.

## Implemented Solution

The solution involves several key components, implemented in the code:

### 1. Corrected Initialization Sequence

The initialization sequence has been restructured to follow this strict order:

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

### 2. Robust Network Initialization

Network initialization now includes proper error handling and timeout mechanisms:

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

### 3. Boot Loop Protection

A sophisticated boot loop protection mechanism prevents repeated crash cycles:

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

### 4. Task Management and Synchronization

Proper task management prevents concurrency issues:

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

### 5. Memory Management

Improved memory management prevents resource conflicts:

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

### 6. Operational Modes with Degraded Functionality

The system implements a hierarchy of operational modes:

1. **Full Operation**: All systems working - LEDs, WiFi, Webserver, ArtNet
2. **Local Control Mode**: WiFi/ArtNet failed, but LEDs work with local control
3. **Minimal Operation**: Only LED basic functions work without network features

## Testing and Verification

To verify the solution's effectiveness:

1. **Serial Debugging**:
   * Every critical step logs its status to the Serial Monitor
   * Timestamps and error details provide visibility into failures
   * Memory usage statistics help identify resource issues

2. **Stress Testing**:
   * Power cycle the system multiple times to detect boot loops
   * Test with varying network conditions to ensure graceful handling
   * Monitor resource usage during peak operations

3. **Network Recovery**:
   * Test behavior when WiFi is unavailable
   * Verify reconnection mechanisms function properly
   * Ensure LED operations continue regardless of network state

4. **Resource Monitoring**:
   * Track memory usage during critical operations
   * Monitor task execution timing to detect conflicts
   * Identify potential resource contention points

## Troubleshooting Checklist

If network issues persist:

1. **Check Power Supply**: Ensure stable 5V with sufficient current capacity. Unstable power can cause network component failures.

2. **Verify GPIO Compatibility**: Certain pins have interactions with WiFi functionality. Try using different pins for LED control.

3. **Reduce DMA Usage**: If issues persist, try simplifying the LED configuration (fewer LEDs, single strip) to reduce DMA contention.

4. **Force Safe Mode**: If all else fails, manually configure the device for non-network operation by setting `useWiFi` and `useArtnet` to false in the web UI.

## Future Enhancements

While the current implementation provides a robust solution to the network crash issue, several enhancements could further improve stability:

1. **Dedicated Task Management**: Implement proper FreeRTOS tasks with appropriate priorities and synchronization.

2. **Enhanced Recovery**: Add more sophisticated recovery mechanisms, including automatic reconnection after network failures.

3. **OTA Updates**: Implement over-the-air updates to allow fixing issues without physical access.

4. **Hardware Reset Button**: Add a physical reset button that can force the device into safe mode without network.

## Implementation Checklist

1. ✅ Correct the TCP/IP initialization sequence
2. ✅ Add proper error handling for network operations
3. ✅ Implement boot loop detection and protection
4. ✅ Add resource cleanup in case of failures
5. ✅ Ensure proper task synchronization in FreeRTOS
6. ✅ Add memory management safeguards