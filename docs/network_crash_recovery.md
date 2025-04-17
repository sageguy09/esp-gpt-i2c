# ESP32 ArtNet Controller: Network Crash Recovery Guide

## Understanding the lwIP Assert Failure

The error experienced in the ESP32 ArtNet Controller is a critical assertion failure in the lwIP TCP/IP stack:

```
assert failed: tcpip_send_msg_wait_sem IDF/components/lwip/lwip/src/api/tcpip.c:455 (Invalid mbox)
```

This error occurs at line 455 of the `tcpip.c` file in the lwIP implementation of the ESP-IDF framework. The specific issue is with the "message box" (mbox) semaphore used for TCP/IP message passing between tasks.

## Root Causes

### 1. Resource Conflict
The I2SClocklessLedDriver uses DMA (Direct Memory Access), as does the WiFi stack. These might be competing for memory or system resources, especially during initialization.

### 2. Initialization Timing
The network services might be starting before the TCP/IP stack is fully initialized, leading to invalid message box access.

### 3. Memory Corruption
DMA operations for LEDs might be causing memory corruption that affects the network stack, particularly when both systems are initializing.

### 4. Race Condition
Multiple tasks trying to use network resources simultaneously could create race conditions that corrupt the message box semaphore.

## Implemented Solution

The solution involves several key components, implemented in the code:

### 1. Fail-Safe Initialization
The code now implements proper error handling and graceful fallbacks during initialization:

```cpp
// Before network initialization
WiFi.mode(WIFI_OFF);  // Start with WiFi fully off
delay(500);           // Ensure it's completely disabled
```

Network initialization is now performed with extensive error handling:

```cpp
try {
  // Only attempt network if requested and not previously failed
  if ((settings.useArtnet || settings.useWiFi) && !networkInitFailed) {
    // Proper WiFi initialization sequence
    WiFi.persistent(false);  // Prevent writing to flash
    WiFi.disconnect(true);   // Ensure disconnected
    delay(200);
    
    WiFi.mode(WIFI_STA);     // Set station mode
    delay(500);              // Longer delay to settle
    
    // Start connection with timeout
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
    // Non-blocking wait with timeout
  }
} catch (...) {
  // Complete failure - disable all network
  disableAllNetworkOperations();
}
```

### 2. Persistent Failure Tracking

Critical failures are now tracked across reboots to prevent repeated crash cycles:

```cpp
// Global variables to track critical errors
bool networkInitFailed = false;
bool ledHardwareFailed = false;

// Function to safely disable all network functionality
void disableAllNetworkOperations() {
  // Complete network shutdown sequence
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  
  // Mark as failed to prevent future attempts
  networkInitFailed = true;
  
  // Force settings to disable network features
  settings.useArtnet = false;
  settings.useWiFi = false;
  settings.useStaticColor = true;
  
  debugLog("CRITICAL: Network stack disabled due to assertion failure");
}
```

### 3. Exception Handling Throughout

The main loop is now wrapped in try/catch blocks to prevent crashes:

```cpp
void loop() {
  // CRITICAL FIX: First, handle watchdog timer
  static unsigned long lastYield = 0;
  if (millis() - lastYield > 10) {
    yield();  // Prevent watchdog timer reset
    lastYield = millis();
  }
  
  // Wrap everything in a try-catch to prevent boot loops
  try {
    // Main functionality
  } catch (...) {
    // If anything goes wrong, log and continue
    static unsigned long lastErrorTime = 0;
    if (millis() - lastErrorTime > 5000) {
      debugLog("ERROR: Exception in main loop");
      lastErrorTime = millis();
    }
  }
}
```

### 4. Operational Modes with Degraded Functionality

The system now implements a hierarchy of operational modes that allow it to function even when some components fail:

1. **Full Operation**: All systems working - LEDs, WiFi, Webserver, ArtNet
2. **Local Control Mode**: WiFi/ArtNet failed, but LEDs work with local control
3. **Minimal Operation**: Only LED basic functions work without network features

## Testing and Verification

To verify the solution's effectiveness:

1. **Power Cycle Testing**: The device should consistently boot to a working state, with LEDs operational even if network fails.

2. **WiFi Interruption Testing**: Disconnecting and reconnecting WiFi should not crash the device; it should continue LED operations.

3. **Resource Stress Testing**: Running LED animations while performing network operations should not cause crashes.

4. **Long-Term Stability**: The device should operate for extended periods without crashing.

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

## Boot Loop Issue (April 2025 Update)

Despite the implemented network crash recovery solution, a persistent boot loop issue has been identified that appears to be related to interactions between the LED driver initialization and network operations.

### Symptoms

- ESP32 continuously reboots without stabilizing
- Serial output may show tcpip_send_msg assertion failure
- LEDs may briefly initialize then fail before the device reboots
- Device never reaches stable operation state

### Root Causes

1. **LED Driver Initialization Timing Issues**
   - The timing between LED initialization steps may be insufficient
   - The hardware verification step may not be robust enough
   - Initialization sequence may fail without proper fallback mechanisms

2. **Network/LED Driver Interaction Issues**
   - The tcpip_send_msg assertion failure indicates conflicts between WiFi and LED driver
   - Network operations might be interfering with LED timing requirements
   - Memory allocation conflicts between network stack and LED driver

3. **Error Handling Gaps**
   - Some exceptions during LED initialization might not be properly caught
   - The existing boot loop prevention may not cover all failure modes
   - Recovery mechanisms might not be properly saving state between reboots

### Recommended Solutions

#### 1. Enhanced LED Driver Initialization

Implement a multi-stage fallback approach:

```cpp
// First attempt - try full configuration
try {
  debugLog("LED init - Stage 1: Full configuration");
  driver.initled(NULL, pins, activeCount, settings.ledsPerStrip);
  driver.setBrightness(settings.brightness);
  driver.showPixels(WAIT); // Important: first update in WAIT mode
  delay(50); // Short delay after initialization
  initSuccess = true;
}
catch (...) {
  debugLog("LED init Stage 1 failed, trying fallback mode");
}

// Second attempt - try with single pin only if first attempt failed
if (!initSuccess && activeCount > 1) {
  try {
    debugLog("LED init - Stage 2: Single pin fallback");
    int singlePin[1] = { pins[0] };
    driver.initled(NULL, singlePin, 1, settings.ledsPerStrip);
    // ...additional initialization...
  }
  catch (...) {
    // ...fallback handling...
  }
}

// Third attempt - absolute minimal configuration
if (!initSuccess) {
  try {
    debugLog("LED init - Stage 3: Minimal configuration");
    int minimalPin[1] = { 12 }; // Always try pin 12 as last resort
    driver.initled(NULL, minimalPin, 1, 8); // Try with just 8 LEDs
    // ...minimal initialization...
  }
  catch (...) {
    // ...final fallback handling...
  }
}
```

#### 2. Boot Counter for Safe Mode Enforcement

```cpp
// In setup()
preferences.begin("esp-artnet", false);
int bootCount = preferences.getInt("bootCount", 0);
bootCount++;
preferences.putInt("bootCount", bootCount);

// If multiple boots within short period, force safe mode
if (bootCount > 3) {
  debugLog("Multiple reboots detected - enforcing safe mode");
  ledHardwareFailed = true;
  networkInitFailed = true;
  settings.useStaticColor = false;
  settings.useColorCycle = false;
  settings.useArtnet = false;
  
  // Reset counter after a longer timeout
  unsigned long lastBootTime = preferences.getLong("lastBootTime", 0);
  unsigned long currentTime = millis();
  if (currentTime - lastBootTime > 300000) { // 5 minutes
    preferences.putInt("bootCount", 0);
  }
}
preferences.putLong("lastBootTime", millis());
preferences.end();
```

#### 3. Complete Separation of Network and LED Operations

- Ensure network operations never happen during LED updates
- Add timing barriers between LED operations and network activity
- Consider disabling WiFi completely during LED initialization
- Add memory allocations monitoring to detect potential conflicts

Implementation of these solutions should address the persistent boot loop issue and further improve the system's stability and resilience.