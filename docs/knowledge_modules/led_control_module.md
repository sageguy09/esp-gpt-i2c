# LED Control Module

## Library Information
The project uses `I2SClocklessLedDriver` for controlling WS2812B LED strips via ESP32's I2S peripheral with DMA.

## Key Features
- Hardware-driven timing eliminates flicker
- Supports multiple strips in parallel
- Resistant to WiFi/BT interruptions 
- Buffer management optimized for memory efficiency

## Initialization Patterns
```cpp
// For a single strip on pin 12
int onePin[1] = { 12 };
driver.initled(NULL, onePin, 1, numLedsPerStrip);

// For multiple strips
int pins[numStrips] = { pin1, pin2, ... };
driver.initled(NULL, pins, numStrips, numLedsPerStrip);
```

## Data Update Methods
```cpp
// Non-blocking update (returns immediately)
driver.showPixels(NO_WAIT);

// Blocking update (waits for completion)
driver.showPixels(WAIT);
```

## Color Modes
1. **ArtNet Mode**: Colors received from network DMX packets
2. **Static Color Mode**: Single RGB color applied to all LEDs
3. **Color Cycle Effects**:
   - Rainbow: Smooth hue transition across strip
   - Pulse: Breathing effect with varying intensity
   - Fire: Realistic fire simulation with random flicker

## Troubleshooting
1. **No LEDs lighting up**:
   - Check power supply (5V, sufficient current)
   - Verify data pin matches configuration
   - Ensure common ground between ESP32 and LEDs
   - Run direct test function to bypass normal code paths

2. **Flickering or unstable colors**:
   - Try WAIT mode for updates
   - Reduce update frequency
   - Isolate power supplies for ESP32 and LEDs
   - Check for signal integrity issues on long runs

3. **Color errors**:
   - Verify RGB order matches LED type
   - Check brightness settings
   - Ensure buffer is properly initialized

## Hardware Considerations
- WS2812B LEDs require 5V power and logic
- ESP32 outputs 3.3V logic (marginal but usually works)
- Each LED can draw up to 60mA at full brightness
- External power recommended for >8 LEDs
- Logic level shifter may improve reliability for long runs

## Recent Fixes
- Fixed LED initialization using proper pin configuration arrays
- Corrected data path for static color mode
- Improved buffer management for different modes
- Added extensive error handling

## Implementation Tips
- Use `WAIT` for critical updates to ensure completion
- Clear buffer before applying new colors
- Apply static colors with secondary verification update
- Use brightness control rather than scaling RGB values