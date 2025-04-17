# ESP32 ArtNet Controller: Network Stability Enhancement

## Task Context

We're working on improving the network stability and error recovery for an ESP32-based ArtNet LED controller. The system currently experiences lwIP TCP/IP stack assertion failures during network initialization:

```
assert failed: tcpip_send_msg_wait_sem IDF/components/lwip/lwip/src/api/tcpip.c:455 (Invalid mbox)
```

We've implemented basic error handling and crash recovery, but need to enhance it further with more robust connection management, recovery mechanisms, and diagnostics.

## Current Implementation

The current implementation includes:
- Network initialization in safe mode with extensive error handling
- Persistent failure tracking to prevent crash loops
- Try/catch blocks around network operations
- Graceful degradation when network components fail

## Enhancement Goals

1. **Improved Network Reconnection**: Implement a robust reconnection mechanism that tries to restore network connectivity periodically but safely.

2. **Enhanced Diagnostics**: Add detailed network diagnostic information to the OLED display and debug logs.

3. **OTA Update Support**: Implement over-the-air update capability for remote recovery.

4. **Configuration Persistence**: Ensure settings are properly persisted across failures and reboots.

## Code Structure Guidelines

The implementation should:

1. **Maintain Component Isolation**: Keep LED functionality completely independent from network operations.

2. **Use State Machines**: Implement clear state transitions for network connection status.

3. **Implement Timeouts**: Add appropriate timeouts for all network operations.

4. **Add Recovery Backoff**: Use exponential backoff for reconnection attempts.

## Implementation Examples

### Connection State Machine

```cpp
enum NetworkState {
  NETWORK_DISABLED,
  NETWORK_INITIALIZING,
  NETWORK_CONNECTING,
  NETWORK_CONNECTED,
  NETWORK_RECONNECTING,
  NETWORK_FAILED
};

NetworkState networkState = NETWORK_DISABLED;

void updateNetworkState() {
  switch (networkState) {
    case NETWORK_DISABLED:
      // Do nothing, network is intentionally disabled
      break;
      
    case NETWORK_INITIALIZING:
      // Attempt safe initialization
      if (initializeWiFiHardware()) {
        networkState = NETWORK_CONNECTING;
      } else {
        networkState = NETWORK_FAILED;
      }
      break;
      
    case NETWORK_CONNECTING:
      // Attempt connection with timeout
      if (connectToWiFi()) {
        networkState = NETWORK_CONNECTED;
        initializeNetworkServices();
      } else {
        networkState = NETWORK_RECONNECTING;
      }
      break;
      
    // Additional states...
  }
}
```

### Safe Reconnection With Backoff

```cpp
unsigned long lastReconnectAttempt = 0;
int reconnectBackoff = 1000; // Start with 1 second

void attemptReconnection() {
  unsigned long currentMillis = millis();
  
  // Only attempt reconnection after backoff period
  if (currentMillis - lastReconnectAttempt < reconnectBackoff) {
    return;
  }
  
  debugLog("Attempting WiFi reconnection...");
  lastReconnectAttempt = currentMillis;
  
  // Disable WiFi first to ensure clean state
  WiFi.disconnect(true);
  delay(100);
  
  // Try to reconnect
  WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
  
  // Implement exponential backoff (max 5 minutes)
  reconnectBackoff = min(reconnectBackoff * 2, 300000);
}

// Reset backoff when successfully connected
void onWiFiConnected() {
  reconnectBackoff = 1000; // Reset to initial value
}
```

### OTA Update Implementation

```cpp
#include <ArduinoOTA.h>

void setupOTA() {
  // Only setup OTA if network is available
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  
  ArduinoOTA.setHostname(settings.nodeName.c_str());
  
  ArduinoOTA.onStart([]() {
    // Disable LED updates during OTA
    otaInProgress = true;
    debugLog("OTA update starting...");
  });
  
  ArduinoOTA.onEnd([]() {
    debugLog("OTA update complete");
  });
  
  ArduinoOTA.onError([](ota_error_t error) {
    debugLog("OTA Error: " + String(error));
  });
  
  ArduinoOTA.begin();
}

// Call in loop
void handleOTA() {
  if (WiFi.status() == WL_CONNECTED) {
    ArduinoOTA.handle();
  }
}
```

## Testing Procedures

To validate the implementation:

1. **Reconnection Testing**: Disconnect the WiFi router temporarily and verify automatic reconnection.

2. **Power Cycle Testing**: Validate settings persistence and recovery after power loss.

3. **OTA Update Testing**: Perform OTA updates in different states (idle, LED animation active).

4. **Long-Term Stability**: Run for extended periods with periodic network interruptions.

## Key Considerations

- **Memory Management**: Be careful with string operations in network code.
- **Task Priority**: Ensure network tasks don't interfere with LED timing.
- **Power Impact**: Network reconnection attempts can drain battery-powered systems.
- **User Feedback**: Provide clear status indicators during reconnection.

## Resources

- [ESP32 WiFi Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_wifi.html)
- [ArduinoOTA Library](https://github.com/esp8266/Arduino/tree/master/libraries/ArduinoOTA)
- [ESP32 LwIP Implementation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/lwip.html)
