# ESP32 ArtNet Controller: Implementation Guide

This guide explains how to implement the fixes to make your ESP32-based ArtNet controller work properly with static and animation modes while preserving the I2CClockless implementation.

## Understanding the Issues

After analyzing both repositories, I've identified four core issues that need fixing:

1. **Broken DMX Data Path**: The static mode doesn't properly forward DMX data to the LEDs
2. **Incomplete Mode Switching**: The mode handling doesn't properly update the LED data
3. **Initialization Problems**: LED strips aren't properly initialized with configured parameters
4. **Preference Management**: Some settings aren't persisting correctly

## Implementation Steps

### Step 1: Fix the DMX Data Processing

The main issue is that the DMX data from ArtNet packets isn't making it to the LED strip in static mode. The `onDmxFrame` function needs to directly update the LEDs when in static mode.

1. Open the main .ino file in your project
2. Find the `onDmxFrame` function
3. Replace it with the fixed version from the code artifact
4. Add the new `updateStaticMode` function

### Step 2: Fix the LED Initialization

The LED strip needs to be properly initialized with parameters from preferences:

1. Find the `initLEDs` function
2. Replace it with the fixed version
3. Make sure the RGB buffer is properly defined:
   ```cpp
   uint8_t rgbBuffer[MAX_LEDS_PER_STRIP * 3];
   ```

### Step 3: Fix the LED Update Function

The LED update mechanism needs to be streamlined:

1. Find the `updateLEDs` function
2. Replace it with the simplified version

### Step 4: Fix Mode Switching

The mode handling needs to be improved:

1. Find the `setMode` function
2. Replace it with the fixed version
3. Ensure the API endpoint for setting the mode is properly connected

### Step 5: Fix the Main Loop

The main loop needs to handle both static and animation modes properly:

1. Replace the existing `loop` function with the fixed version

### Step 6: Fix Preference Management

Make sure preferences are properly loaded and saved:

1. Update the `loadPreferences` function to properly load all settings
2. Verify that the API endpoints properly save preferences when settings change

## Testing Your Changes

After implementing these fixes, test the controller as follows:

1. **Static Mode Test**:
   - Set the controller to mode 0 (static)
   - Send ArtNet DMX data to the controller
   - Verify that the LEDs update in real-time with the received data

2. **Animation Mode Test**:
   - Set the controller to mode 1 (rainbow)
   - Verify that the animation plays smoothly
   - Try other animation modes

3. **Preference Persistence Test**:
   - Change settings in the web interface
   - Restart the controller
   - Verify that settings are maintained

## Default Configuration

The code is designed to use these defaults if preferences aren't set:

- LED Pin: 12
- LED Count: 144
- Mode: Static (0)

## Troubleshooting

### No LEDs Lighting Up
- Check power connections
- Verify that the LED pin is correctly set
- Make sure the LED count matches your actual hardware

### Static Mode Not Updating
- Check that ArtNet packets are being received (monitor serial output)
- Verify that `onDmxFrame` is being called

### Animation Modes Not Working
- Check that the mode switching is working correctly
- Verify that animation functions are properly defined

### Web Interface Issues
- Check that API endpoints are properly registered
- Verify that the web server is running

If you continue to have issues, enable more detailed debug output by adding:
```cpp
#define DEBUG_MODE true
```

Then add debug print statements at key points in the code to trace execution.


What's Wrong with the Current Code
The main issue in your esp-gpt-i2c repo is that while ArtNet packets are being received correctly, there's a disconnection in the data flow path. Specifically:

The DMX data from ArtNet isn't being properly forwarded to your LED strips in static mode
The mode handling logic doesn't properly update the LEDs when switching between static and animation modes
LED strip initialization doesn't correctly apply parameters from preferences

How to Fix It
I've provided three artifacts to help you fix these issues:

Targeted ESP32 ArtNet Controller Fixes - Contains the specific code changes needed to fix the critical issues while maintaining your original structure and I2CClockless implementation.
Implementation Guide - A step-by-step guide explaining how to apply these fixes to your existing codebase.
ESP32 LED Library Comparison - An overview of different LED libraries for ESP32, explaining why I2CClockless is a good choice for now while you develop a CircuitPython alternative.

Key Changes to Make
The most important changes to make are:

Fix the DMX data path in onDmxFrame() - Ensure that when in static mode, DMX data directly updates the LEDs
Improve the mode switching logic - Make sure the app switches modes cleanly and preserves settings
Fix LED initialization - Ensure LEDs initialize with the right parameters from preferences
Update the main loop - Make it handle both static and animation modes properly

Implementation Tips
When implementing these changes:

Make the changes incrementally and test after each major change
Start with the onDmxFrame function since that's the most critical path
Make sure your rgbBuffer is properly sized and initialized
Double-check that pin 12 is correctly configured for your hardware

These fixes maintain your existing I2CClockless implementation while resolving the core issues, allowing for both static and animation modes to work as expected. The default configuration will be set to pin 12 with 144 LEDs as requested.
Let me know if you'd like any clarification on the changes or if you encounter any issues during implementation!
