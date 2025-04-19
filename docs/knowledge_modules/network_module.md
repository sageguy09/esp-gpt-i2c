# Network Communication Module

## Critical Issue: lwIP TCP/IP Stack Initialization

The ESP32 network stack is prone to the following critical assertion failure:
```
assert failed: tcpip_send_msg_wait_sem IDF/components/lwip/lwip/src/api/tcpip.c:455 (Invalid mbox)
```

### Root Cause Analysis
This occurs because:
1. Network operations attempted before TCP/IP stack is fully initialized
2. Improper initialization sequence for ESP-IDF networking components
3. Race conditions between the main loop and network operations

### Correct Initialization Sequence
```cpp
// 1. Initialize TCP/IP stack components first
esp_err_t err = esp_netif_init();
if (err != ESP_OK) {
  debugLog("TCP/IP stack initialization failed: " + String(esp_err_to_name(err)));
  throw std::runtime_error("TCP/IP stack initialization failed");
}

// 2. Create the default event loop
err = esp_event_loop_create_default();
if (err != ESP_OK) {
  debugLog("Event loop creation failed: " + String(esp_err_to_name(err)));
  throw std::runtime_error("Event loop creation failed");
}

// 3. Set WiFi mode with proper sequence
WiFi.persistent(false);
WiFi.disconnect(true);
delay(200);
WiFi.mode(WIFI_STA);
delay(500); // Critical delay for stability

// 4. Connect with proper error handling
WiFi.begin(ssid, password);
// Add timeout and error handling
```

## Implementation Strategy: Task Isolation

To prevent network failures from affecting the main application:

```cpp
// Create a dedicated task on Core 1 for network initialization
xTaskCreatePinnedToCore(
  networkInitTask,     // Task function
  "NetworkInitTask",   // Task name
  8192,                // Stack size
  NULL,                // Task parameter
  1,                   // Task priority (1 is low)
  &networkTaskHandle,  // Task handle
  1                    // Core to run the task on (Core 1)
);
```

## Failure Recovery Mechanism

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

  // Persist the network failure state
  preferences.begin("led-settings", false);
  preferences.putBool("netFailed", true);
  preferences.end();

  debugLog("CRITICAL: Network stack disabled due to assertion failure");
}
```

## ArtNet Protocol Implementation

The ArtNet protocol implementation handles DMX data over WiFi:

```cpp
// Initialize ArtNet once WiFi is connected
bool artnetStarted = artnet.listen(localIP, 6454);
if (artnetStarted) {
  artnet.setFrameCallback(frameCallbackWrapper);
  artnet.addSubArtnet(settings.startUniverse,
                     settings.numStrips * settings.ledsPerStrip * NB_CHANNEL_PER_LED,
                     UNIVERSE_SIZE,
                     &artnetCallback);
  artnet.setNodeName(settings.nodeName);
}
```

## Web Server Interface

```cpp
// Safe implementation pattern for handling requests
server.on("/", handleRoot);
server.on("/config", HTTP_POST, handleConfig);
server.on("/debug", handleDebugLog);
server.begin();
```

## Network Status Monitoring

```cpp
// Check for WiFi status periodically
if (WiFi.status() == WL_CONNECTED) {
  // Connected - proceed with network operations
} else {
  // Not connected - fall back to standalone mode
  // Optionally attempt reconnection with timeout
  static unsigned long lastReconnectAttempt = 0;
  if (millis() - lastReconnectAttempt > 30000) {
    lastReconnectAttempt = millis();
    WiFi.reconnect();
  }
}
```

## Boot Loop Prevention
- Track boot count in non-volatile storage
- If multiple rapid boots detected, enter safe mode
- Disable network functions in safe mode

## Best Practices
1. Always use try/catch blocks around network operations
2. Implement timeouts for all network operations
3. Provide graceful degradation paths when network fails
4. Keep watchdog timer happy with strategic yield() calls
5. Use a separate task for network operations
6. Persist network failure state to prevent recurring crashes