# ESP32 ArtNet Controller: Implementation Knowledge

## Current Project Status

### Overall Progress
- [x] Basic ArtNet reception
- [x] Web UI configuration
- [x] Multiple LED control modes
- [x] OLED Debug Display
- [x] UART Communication Bridge
- [x] LED Driver Fix - LEDs now lighting up
- [ ] Network Stability Fixes
- [ ] PyPortal Integration
- [ ] Advanced Error Handling

### Component Status

#### 1. LED Control
- **Current Implementation**: 
  - I2SClocklessLedDriver for WS2812B control
  - Multiple modes: ArtNet, Static Color, Color Cycle Effects
  - Single strip configuration on pin 12
  - Fixed initialization issues for proper LED operation
  
- **Implementation Details**:
  - The I2SClocklessLedDriver requires specific initialization based on the number of outputs
  - LED data format must match the driver expectations (RGB format)
  - The `showPixels(WAIT)` function is critical for proper updates
  - Timing is crucial for stable LED operation

- **Resolved Issues**:
  - Fixed LED initialization using the proper pin configuration arrays
  - Corrected data path for static color mode and cycle effects
  - Added debugging for LED operations
  - Implemented proper buffer management
  - Fixed color application in different modes

#### 2. Web Configuration
- **Features**:
  - Dynamic mode selection
  - Pin configuration
  - WiFi setup
  - Color mode selection
  - Static color picker
  
- **Current Issues**:
  - Network stability causing crashes on some operations
  - Color picker occasionally needs multiple attempts to update

#### 3. ArtNet Reception
- **Current Capabilities**:
  - UDP packet parsing
  - Single universe support
  - Callback-based processing
  - Proper handling of static mode data
  
- **Known Issues**:
  - Network initialization causing lwIP assertion failures
  - Need more robust error handling for network operations

#### 4. UART Communication Bridge
- **Current Implementation**:
  - Bidirectional communication protocol
  - Command-based operation
  - Error detection and reporting
  - Mode control capabilities
  - Optional initialization

## Current Critical Issue

### lwIP TCP/IP Stack Assertion Failure
The system is experiencing a critical assertion failure in the lwIP TCP/IP stack:
```
assert failed: tcpip_send_msg_wait_sem IDF/components/lwip/lwip/src/api/tcpip.c:455 (Invalid mbox)
```

This error occurs after successful LED initialization and static color application, suggesting that the crash happens specifically during network initialization.

### Root Causes
1. **Initialization Order Problem**: Network services attempting to start before TCP/IP stack is fully initialized
2. **Resource Conflict**: Competition between DMA operations for LEDs and WiFi stack
3. **Error Handling Gap**: Insufficient error handling causing crash loops

### Implemented Fixes
1. **Network Initialization Protection**:
   - Added complete network disabling function to prevent retries after failures
   - Implemented persistent tracking of network failures
   - Added extensive try/catch blocks around network operations

2. **Graceful Degradation**:
   - System now works even if network components fail
   - Falls back to standalone mode when network is unavailable
   - Preserves LED functionality regardless of network status

3. **Error Recovery**:
   - Added strategic yield() calls to keep watchdog timer happy
   - Wrapped all critical sections in try/catch blocks
   - Added persistent failure tracking across reboots

## Key Component Interactions

### I2SClocklessLedDriver
The I2SClocklessLedDriver is a specialized library for controlling WS2812B LEDs with the ESP32's I2S peripheral. Key aspects:

1. **Initialization**:
   ```cpp
   // For a single strip on pin 12
   int onePin[1] = { 12 };
   driver.initled(NULL, onePin, 1, numLedsPerStrip);
   
   // For multiple strips
   int pins[numStrips] = { pin1, pin2, ... };
   driver.initled(NULL, pins, numStrips, numLedsPerStrip);
   ```

2. **LED Updates**:
   ```cpp
   // Update LEDs without waiting (non-blocking)
   driver.showPixels(NO_WAIT);
   
   // Update LEDs and wait for completion (blocking)
   driver.showPixels(WAIT);
   ```

3. **Brightness Control**:
   ```cpp
   // Set global brightness (0-255)
   driver.setBrightness(brightness);
   ```

### ArtNet Integration
The ArtNet protocol integration handles DMX data over WiFi:

1. **Initialization**:
   ```cpp
   artnet.addSubArtnet(
     startUniverse,
     totalChannels,
     universeSize,
     &artnetCallback
   );
   ```

2. **Callback Processing**:
   ```cpp
   void artnetCallback(void *param) {
     subArtnet *subartnet = (subArtnet *)param;
     // Process data from subartnet->data
     // Then update LEDs
     driver.showPixels(NO_WAIT);
   }
   ```

## Technical Insights

### I2SClockless Driver Technical Details
The I2SClocklessLedDriver uses ESP32's I2S peripheral with DMA to generate precise timing for WS2812B LEDs:

1. **Key Advantages**:
   - Hardware-driven timing eliminates flicker
   - Offloads timing-critical operations from CPU
   - Supports parallel output for multiple strips
   - Resistant to WiFi/BT interruptions

2. **Buffer Management**:
   - Creates DMA buffers for LED data
   - Translates RGB values into timing pulses
   - Handles memory efficiently

3. **Timing Requirements**:
   - WS2812B requires strict timing (800KHz)
   - 0 bit: ~0.4μs high, ~0.85μs low
   - 1 bit: ~0.8μs high, ~0.45μs low
   - Reset: >50μs low

### Color Handling
For WS2812B LEDs, color data needs specific formatting:

1. **RGB Format**:
   - 3 bytes per LED (R, G, B order)
   - Memory layout is contiguous
   - Total buffer size = numLEDs * 3

2. **Static Color Application**:
   ```cpp
   // Apply a static RGB color to all LEDs
   for (int i = 0; i < totalLEDs; i++) {
     ledData[i].r = red;
     ledData[i].g = green;
     ledData[i].b = blue;
   }
   driver.showPixels(WAIT);
   ```

## Known Limitations

1. **Pin Compatibility**:
   - Not all ESP32 pins work well with LED output
   - Recommended pins: 12, 14, 2, 4, 5, etc.
   - Avoid pins used by WiFi/BT (GPIO 1, 3)

2. **Power Requirements**:
   - WS2812B LEDs can draw significant current
   - Each LED can draw up to 60mA at full brightness
   - External power supply recommended for more than 8-10 LEDs

3. **Data Signal Integrity**:
   - 3.3V output from ESP32 might be marginal for 5V LEDs
   - Logic level shifter may be needed for longer runs
   - Keep data wires short, use proper grounding

## Test Procedures

1. **Basic LED Test**:
   - Sequential RGB colors test
   - Tests driver initialization and data path
   - Identifies potential hardware issues

2. **Mode Switching Test**:
   - Toggle between ArtNet, Static, and Cycle modes
   - Verifies clean transitions between modes
   - Validates persistence of settings

3. **Network Resiliency Test**:
   - Intentionally disconnect WiFi
   - Verify fallback to standalone mode
   - Ensure LED functionality continues

## Troubleshooting Guide

### LED Not Lighting Up
1. **Check Power**:
   - Verify 5V power to LEDs
   - Ensure common ground between ESP32 and LEDs
   - Check for sufficient power capacity

2. **Verify Data Connection**:
   - Confirm data pin matches code configuration
   - Try a different pin
   - Check signal with logic analyzer if possible

3. **Software Checks**:
   - Verify initialization parameters
   - Ensure RGB data format matches LEDs
   - Run test mode to bypass normal code path

### Network Issues
1. **Power Issues**:
   - Unstable power can cause WiFi to fail
   - Try powering ESP32 from a separate supply than LEDs

2. **Software Fixes**:
   - Use `WAIT` mode for critical updates
   - Add strategic delays around network operations
   - Add proper error handling for network operations

3. **Recovery Options**:
   - LED operations should continue even if network fails
   - Implement mode switching via hardware button if WiFi fails
   - Add self-recovery mechanisms for network failures

## Development Roadmap

### Immediate Priorities
1. ✅ Fix LED output issues
2. ✅ Implement network crash recovery
3. ✅ Add error handling for critical operations
4. ✅ Create independent operation modes

### Future Enhancements
1. PyPortal integration for external control
2. Multi-universe ArtNet support
3. Enhanced web UI with preview
4. More advanced animations
5. Audio-reactive effects

## Acknowledgments
- hpwit for the I2SClocklessLedDriver and artnetESP32V2 libraries
- All contributors to the ESP32 Arduino core
- Developers providing insights on LED control techniques