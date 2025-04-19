# ESP32 ArtNet Controller Network Initialization Fix

## Problem Description

The ESP32 ArtNet Controller was experiencing a critical failure during initialization with the following error:

```
assert failed: tcpip_send_msg_wait_sem IDF/components/lwip/lwip/src/api/tcpip.c:455 (Invalid mbox)
```

This assertion failure occurs in the lwIP TCP/IP stack when there's an invalid message box (mbox) during network initialization. The error would trigger consistently after LED initialization and static color application, causing the device to crash and reboot repeatedly, creating a boot loop.

## Root Causes

1. **Incorrect Initialization Sequence**: The TCP/IP stack components were not initialized in the correct order, leading to assertion failures when network operations were attempted.

2. **Resource Conflicts**: Competition between LED driver DMA operations and WiFi stack for memory and system resources.

3. **Error Handling Gaps**: Insufficient error trapping and recovery mechanisms, causing the entire system to crash when network initialization failed.

4. **Main Thread Blocking**: Network operations were blocking the main thread during initialization, preventing proper error recovery.

## Implemented Solution

### 1. Proper TCP/IP Stack Initialization

Added explicit initialization of ESP-IDF TCP/IP stack components before any WiFi operations:

```cpp
// Initialize the TCP/IP stack first - CRITICAL for preventing assertion failures
esp_err_t err = esp_netif_init();
if (err != ESP_OK) {
  debugLog("TCP/IP stack initialization failed: " + String(esp_err_to_name(err)));
  throw std::runtime_error("TCP/IP stack initialization failed");
}

// Create the default event loop - MUST happen before WiFi initialization
err = esp_event_loop_create_default();
if (err != ESP_OK) {
  debugLog("Event loop creation failed: " + String(esp_err_to_name(err)));
  throw std::runtime_error("Event loop creation failed");
}
```

### 2. Isolation of Network Operations in FreeRTOS Task

Moved network initialization to a dedicated FreeRTOS task to isolate network operations from the main application thread:

```cpp
// Network initialization task function - isolates network operations in a separate core
void networkInitTask(void *parameter) {
  debugLog("Network initialization task started on core " + String(xPortGetCoreID()));
  
  try {
    // Initialize network with proper protection mechanisms
    initializeNetworkWithProtection();
  } 
  catch (...) {
    debugLog("Network initialization task encountered an error");
    disableAllNetworkOperations();
  }
  
  // Task is complete - delete itself
  debugLog("Network initialization task complete");
  networkTaskHandle = NULL;
  vTaskDelete(NULL);
}
```

### 3. Comprehensive Error Handling and Recovery

Added robust error handling throughout the network initialization process, with automatic fallback to static color mode when network fails:

```cpp
// Function to handle the tcpip_send_msg assert failure
void disableAllNetworkOperations()
{
  // Complete network shutdown sequence
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Mark as failed to prevent any future attempts
  networkInitFailed = true;

  // Force settings to disable any network functionality
  settings.useArtnet = false;
  settings.useWiFi = false;
  settings.useStaticColor = true;

  // CRITICAL FIX: Persist the network failure state to prevent future attempts after reboot
  preferences.begin("led-settings", false);
  preferences.putBool("netFailed", true);
  preferences.end();

  debugLog("CRITICAL: Network stack disabled due to assertion failure");
}
```

### 4. Boot Loop Prevention

Added a boot counter mechanism to detect and recover from boot loops by entering a safe mode:

```cpp
void checkBootLoopProtection()
{
  preferences.begin("led-settings", false);

  // Get boot count and last boot time
  bootCount = preferences.getInt("bootCount", 0);
  lastBootTime = preferences.getLong("lastBootTime", 0);
  unsigned long currentTime = millis();

  // Check for rapid reboots indicating a boot loop
  if (bootCount > 3 && (currentTime - lastBootTime < 300000)) { // 5 minutes
    // Enable safe mode if too many boots in short time
    inSafeMode = true;
    debugLog("SAFE MODE ENABLED: Multiple boot failures detected");

    // Use minimal configuration in safe mode
    settings.useArtnet = false;
    settings.useWiFi = false;
    settings.useColorCycle = false;
    settings.useStaticColor = true;
    settings.staticColor = {255, 0, 0}; // Red = safe mode
    settings.numStrips = 1;
    settings.ledsPerStrip = 8; // Minimal LED count
    settings.brightness = 64;  // Reduced brightness
  }

  // Increment boot counter and save time
  bootCount++;
  preferences.putInt("bootCount", bootCount);
  preferences.putLong("lastBootTime", currentTime);
  preferences.end();
}
```

## Verification Steps

1. **Normal Boot Sequence**: The device should start with LED initialization and then proceed to network initialization in a protected way.

2. **LED Functionality Check**: LED operations should continue to function properly in static color mode regardless of network status.

3. **Network Initialization Test**: WiFi and ArtNet should initialize successfully when correctly configured.

4. **Stability Test**: Device should remain stable and not crash when network operations are attempted.

5. **Recovery Test**: Device should gracefully handle network failures and continue operating in static color mode.

## Future Recommendations

1. **Memory Management**: Continue optimizing memory usage to prevent heap fragmentation.

2. **Task Prioritization**: Adjust FreeRTOS task priorities to ensure stable operation under heavy load.

3. **Diagnostics**: Add more detailed diagnostic logging, especially for network state transitions.

4. **Resource Separation**: Further isolate LED DMA operations from network operations with clear synchronization mechanisms.

## Technical References

1. ESP-IDF Documentation on TCP/IP stack initialization
2. FreeRTOS Task Management
3. ESP32 Network Stack Diagnostic Framework
4. I2SClocklessLedDriver resource allocation patterns