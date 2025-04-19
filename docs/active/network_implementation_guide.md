# ESP32 ArtNet Controller: Network Implementation Guide

## Overview

This guide provides comprehensive information about network implementation in the ESP32 ArtNet Controller project, including initialization, error handling, crash recovery, and best practices.

## Key Network Components

The ESP32 ArtNet Controller uses several networking components:

1. **ESP-IDF TCP/IP Stack**: Low-level networking components from the ESP-IDF framework
2. **Arduino WiFi Library**: High-level API for WiFi connectivity
3. **ArtNet Protocol**: UDP-based protocol for DMX lighting control over Ethernet/WiFi
4. **Web Server**: Local configuration interface

## Critical Network Issues Addressed

### TCP/IP Stack Assertion Failure

The project previously experienced critical assertion failures in the lwIP TCP/IP stack:

```
assert failed: tcpip_send_msg_wait_sem IDF/components/lwip/lwip/src/api/tcpip.c:455 (Invalid mbox)
```

This error occurs when network operations attempt to use the TCP/IP stack's message box (mbox) before it's properly initialized, typically during network initialization after LED driver setup.

### Root Causes

1. **Initialization Order Problem**: Network services attempting to start before TCP/IP stack is fully initialized
2. **Resource Conflict**: Competition between LED driver DMA operations and WiFi stack
3. **Error Handling Gaps**: Insufficient error handling causing crash loops
4. **Main Thread Blocking**: Network operations blocking the main thread during initialization

## Proper Network Initialization Sequence

### 1. Core TCP/IP Stack Initialization

The critical first step is initializing the low-level TCP/IP stack components:

```cpp
// Initialize TCP/IP stack first - CRITICAL for preventing assertion failures
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

### 2. WiFi Configuration

After the TCP/IP stack is initialized, WiFi can be configured:

```cpp
// Basic WiFi configuration with proper sequence
WiFi.persistent(false);
WiFi.disconnect(true);
delay(200);  // Stabilization delay

// Configure WiFi mode
WiFi.mode(WIFI_STA);
delay(500);  // Critical delay for stability
```

### 3. WiFi Connection

Connect to WiFi with proper timeout and error handling:

```cpp
// Start connection with timeout
WiFi.begin(settings.ssid.c_str(), settings.password.c_str());

// Non-blocking wait with proper timeout
unsigned long startAttempt = millis();
while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
  delay(100);
  yield();  // Keep watchdog happy
}

if (WiFi.status() != WL_CONNECTED) {
  debugLog("WiFi connection failed after timeout");
  throw std::runtime_error("WiFi connection timeout");
}
```

### 4. ArtNet Initialization

Only initialize ArtNet after WiFi is successfully connected:

```cpp
IPAddress localIP = WiFi.localIP();
bool artnetStarted = artnet.listen(localIP, 6454);
if (artnetStarted) {
  artnet.setFrameCallback(frameCallbackWrapper);
  artnet.addSubArtnet(settings.startUniverse,
                      settings.numStrips * settings.ledsPerStrip * NB_CHANNEL_PER_LED,
                      UNIVERSE_SIZE,
                      &artnetCallback);
  artnet.setNodeName(settings.nodeName);
} else {
  throw std::runtime_error("ArtNet initialization failed");
}
```

## Error Handling & Recovery

### 1. Network Initialization in Isolated Task

Network initialization is moved to a dedicated FreeRTOS task to isolate it from the main application:

```cpp
// Network initialization task function
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

### 2. Network Disable Function

When network initialization fails, this function completely disables network functionality:

```cpp
void disableAllNetworkOperations() {
  // Complete network shutdown sequence
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // Mark as failed to prevent any future attempts
  networkInitFailed = true;

  // Force settings to disable any network functionality
  settings.useArtnet = false;
  settings.useWiFi = false;
  settings.useStaticColor = true;

  // Persist the network failure state across reboots
  preferences.begin("led-settings", false);
  preferences.putBool("netFailed", true);
  preferences.end();

  debugLog("CRITICAL: Network stack disabled due to assertion failure");
}
```

### 3. Boot Loop Protection

A boot counter mechanism detects and prevents boot loops by entering safe mode:

```cpp
void checkBootLoopProtection() {
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

## Graceful Degradation

The system is designed to continue functioning even when network components fail:

1. **Operation Modes**:
   - **Full Operation**: All systems working (LEDs, WiFi, Web Server, ArtNet)
   - **Local Control Mode**: WiFi/ArtNet failed, but LEDs work with local control
   - **Safe Mode**: Minimal operation with basic LED functionality

2. **User Feedback**:
   - Status indicators in the Web UI about network state
   - Visual LED indication (red color) when in safe mode
   - Debug logs accessible through the Web UI

## Testing Network Functionality

### Test Procedures

1. **Normal Initialization**:
   - Verify proper boot sequence
   - Check all components initialize in correct order
   - Validate WiFi connection is established

2. **Error Recovery**:
   - Test with incorrect WiFi credentials
   - Verify graceful fallback to standalone mode
   - Ensure LED functionality continues

3. **Boot Loop Prevention**:
   - Test with repeated forced reboots
   - Verify safe mode is activated
   - Check settings persistence

### Test Verification

After implementation, the ESP32 ArtNet controller should:

1. Safely attempt network initialization in an isolated task
2. Properly detect and handle TCP/IP stack failures
3. Persist network failure state across reboots
4. Continue functioning in standalone mode when network fails
5. Provide clear feedback to users about the network state

## Best Practices

1. **Initialization Order**:
   - Always initialize the TCP/IP stack before any WiFi operations
   - Create event loops before starting network interfaces
   - Add sufficient delays between critical operations

2. **Task Management**:
   - Isolate network operations from LED operations
   - Use appropriate task priorities and core assignments
   - Implement proper synchronization between tasks

3. **Error Handling**:
   - Add try/catch blocks around all network operations
   - Use timeouts for all blocking operations
   - Implement graceful fallback mechanisms

4. **Memory Management**:
   - Monitor heap fragmentation
   - Handle allocation failures gracefully
   - Clean up resources after use

## Future Enhancements

1. **Network Diagnostics**:
   - Enhanced logging of network state transitions
   - WiFi signal strength monitoring
   - Packet statistics collection

2. **Recovery Mechanisms**:
   - Automatic reconnection after WiFi failures
   - Network watchdog for long-term monitoring
   - Schedule periodic connection health checks

3. **Safety Features**:
   - Physical reset button for safe mode
   - OTA updates for remote fixes
   - Configurable network timeouts and retry policies

4. **Performance Optimizations**:
   - Fine-tune task priorities and scheduling
   - Optimize memory usage in network operations
   - Reduce power consumption during network idle periods

## Technical References

1. [ESP-IDF TCP/IP Stack Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/tcpip_adapter.html)
2. [FreeRTOS Task Management](https://www.freertos.org/a00112.html)
3. [lwIP Documentation](https://www.nongnu.org/lwip/2_1_x/index.html)
4. [WiFi Library for ESP32](https://github.com/espressif/arduino-esp32/tree/master/libraries/WiFi)