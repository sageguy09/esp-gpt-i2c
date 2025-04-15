# ESP32 ArtNet I2SClockless Web UI with OLED Debug

This project integrates the ESP32 with the ArtNet protocol, I2SClockless LED driver, and a Web UI for configuration. It allows you to control WS2812B LED strips over the ArtNet protocol with features like web-based configuration, OLED display status, and debug logs.

## Project Overview

### Features:
- **ArtNet Protocol**: Handle ArtNet data over Wi-Fi, allowing communication between the ESP32 and ArtNet-compatible software like Resolume.
- **I2SClockless LED Driver**: Controls WS2812B LEDs using the I2S interface for smooth performance.
- **Web UI Configuration**: A simple web interface for setting up the Wi-Fi SSID, password, and LED configurations.
- **OLED Display Debug**: A small OLED display for real-time status updates, such as the current IP address and LED configuration.
- **Static Color Mode**: Apply a single color to all LEDs without requiring ArtNet data.
- **Color Cycle Effects**: Three built-in color effects (Rainbow, Pulse, Fire) with adjustable speed.
- **Individual Pin Configuration**: Configure each of the 4 output pins separately through the web UI.

### Dependencies:
- **WiFi.h**: Wi-Fi connectivity for the ESP32.
- **WebServer.h**: Web server to handle incoming HTTP requests for the configuration page.
- **Preferences.h**: Local settings storage for storing Wi-Fi credentials and ArtNet parameters.
- **I2SClocklessLedDriver.h**: Library for controlling the WS2812B LEDs using I2S.
- **artnetESP32V2.h**: ArtNet library for the ESP32 to listen and handle ArtNet data.
- **Adafruit_SSD1306.h**: Library for controlling the OLED display.
- **Adafruit_GFX.h**: Graphics library for the OLED display.

## Installation Instructions

### Install Required Libraries:
- I2SClocklessLedDriver
- Artnetesp32v2 (note the casing for folder name vs. header file)
- Adafruit_SSD1306
- Adafruit_GFX
- WebServer
- WiFi
- Preferences

### Arduino IDE Setup:
1. Open the Arduino IDE and go to File > Preferences > Additional Boards Manager URLs and add the ESP32 URL: https://dl.espressif.com/dl/package_esp32_index.json
2. Go to Tools > Board > Boards Manager, search for "ESP32" and install the latest version.
3. Install required libraries using Sketch > Include Library > Manage Libraries.

### Upload the Sketch:
1. Select ESP32 Dev Module in the Tools > Board menu.
2. Upload the code to your ESP32 device.

## Application Functionality

### 1. Web UI Configuration:
The web UI allows you to configure the following settings:
- **LEDs per Strip**: Number of LEDs on each strip (e.g., 144).
- **Number of Strips**: The number of strips connected (e.g., 3).
- **Brightness**: Set the maximum brightness for the LEDs (0-255).
- **Pin Configuration**: Set GPIO pins for each of the 4 outputs (default: 12, 14, 2, 4).
- **SSID and Password**: Connect to Wi-Fi by entering your SSID and password.
- **Web Interface URL**: Once the ESP32 is connected to Wi-Fi, open the ESP32's IP address in a browser to access the configuration page.

### 2. Operation Modes:
The controller supports three mutually exclusive operating modes:
- **ArtNet Mode**: Receive and display color data sent over the network using the ArtNet protocol.
- **Static Color Mode**: Display a single, user-selected color on all LEDs.
- **Color Cycle Mode**: Run one of three built-in color effects (Rainbow, Pulse, Fire) at an adjustable speed.

### 3. ArtNet Data Reception:
- The ESP32 listens for ArtNet data over the network (default port 6454).
- When data is received, it is processed in the callback function (artnetCallback) which updates the LEDs using the I2SClocklessLedDriver.

### 4. Static Color:
- Allows setting a fixed color for all LEDs without needing ArtNet data.
- Color is selected using a color picker in the web UI.
- Settings are saved to non-volatile memory and persist across reboots.

### 5. Color Cycle Effects:
- **Rainbow**: Smooth color transitions across all LEDs.
- **Pulse**: A pulsing effect with changing colors.
- **Fire**: A realistic fire simulation with random flickering.
- Speed is adjustable through the web UI.

### 6. OLED Debug Display:
- Displays the current status of the ESP32, including the connected IP address and other relevant debug information such as the number of LEDs, strips, and current settings.
- Shows the current operation mode (ArtNet, Static Color, or Color Cycle).
- Updates in real-time as settings change.

## Code Explanation

### Key Variables:
- **NUM_LEDS_PER_STRIP**: Defines the number of LEDs on each strip. **IMPORTANT: Must be defined before including I2SClocklessLedDriver.h**.
- **NUMSTRIPS**: Defines the total number of strips (maximum of 12, with 3 strips per pin).
- **ledData**: Holds the LED data that is sent to the I2SClocklessLedDriver.
- **settings**: Stores the configuration settings like Wi-Fi SSID, password, LED configurations, and mode preferences.
- **cyclePosition**: Tracks the position in animation cycles for color effects.

### Operation Modes:
- **useArtnet**: When enabled, the controller listens for ArtNet data and displays it on the LEDs.
- **useStaticColor**: When enabled, all LEDs display the same, user-selected color.
- **useColorCycle**: When enabled, the controller runs one of the built-in color cycle effects.

These modes are mutually exclusive - enabling one will automatically disable the others.

### Main Components:

#### Pin Configuration:
- The controller supports up to 4 output pins that can each drive multiple LED strips.
- Pins are configured through the web UI with individual settings for each pin.
- Default pin configuration: 12, 14, 2, 4.

#### ArtNet Setup:
- The ArtNet server is set up with `artnet.listen(localIP, 6454)` to listen for incoming ArtNet packets.
- The ArtNet class requires two callback approaches:
  1. A global frame callback with no parameters: `frameCallbackWrapper()`
  2. A subArtnet callback that receives a void pointer: `artnetCallback(void* param)`

#### LED Control:
- The LEDs are controlled using the I2SClocklessLedDriver library. The data is passed to the driver with `driver.showPixels()`.
- In static color mode, all LEDs are set to the same color value.
- In color cycle mode, LEDs are updated according to the selected effect pattern.

#### Web Server:
- The web server is set up to listen on port 80 and serve the configuration page.
- The page dynamically shows/hides controls based on the selected operation mode.
- All settings are saved to non-volatile memory using the Preferences library.

#### OLED Debug:
- An OLED display is used to show the current IP address and other status information.
- It also displays the current operation mode and updates in real-time.

## Code Overview

### 1. Setting Up ArtNet:
```cpp
IPAddress localIP = WiFi.localIP();
artnet.listen(localIP, 6454);  // Start listening for ArtNet data
artnet.setFrameCallback(frameCallbackWrapper);  // Set the callback to handle ArtNet frames
```

### 2. Static Color Implementation:
```cpp
// Apply static color to all LEDs
void applyStaticColor() {
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++) {
    ledData[i] = settings.staticColor;
  }
}
```

### 3. Color Cycle Effects:
```cpp
// Rainbow pattern
void rainbowCycle() {
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++) {
    int pixelHue = (i * 256 / settings.ledsPerStrip) + cyclePosition;
    pixelHue = pixelHue % 256;
    ledData[i] = hsvToRgb(pixelHue, 240, settings.brightness);
  }
  cyclePosition = (cyclePosition + 1) % 256;
}

// Pulse effect
void pulseEffect() {
  float intensity = (sin(cyclePosition * 0.0245) + 1.0) / 2.0;
  uint8_t value = intensity * settings.brightness;
  rgb24 color = hsvToRgb(cyclePosition, 255, value);
  
  for (int i = 0; i < settings.numStrips * settings.ledsPerStrip; i++) {
    ledData[i] = color;
  }
  cyclePosition = (cyclePosition + 1) % 256;
}

// Fire effect
void fireEffect() {
  // Complex fire simulation algorithm
  // Creates realistic flickering flames
}
```

### 4. LED Update Logic:
```cpp
void updateLEDsBasedOnMode() {
  // Only update at appropriate intervals based on cycle speed
  
  if (settings.useArtnet) {
    // ArtNet mode - data handled by callback
    return;
  }

  if (settings.useStaticColor) {
    // Static color mode
    applyStaticColor();
  } else if (settings.useColorCycle) {
    // Color cycle mode
    switch (settings.colorMode) {
      case COLOR_MODE_RAINBOW: rainbowCycle(); break;
      case COLOR_MODE_PULSE: pulseEffect(); break;
      case COLOR_MODE_FIRE: fireEffect(); break;
      default: rainbowCycle(); break;
    }
  } else {
    // Fallback to static color
    applyStaticColor();
  }
  
  // Update LEDs with new data
  driver.showPixels(NO_WAIT);
}
```

## Known Issues and Fixes

### Library Import Issues:
- **I2SClocklessLedDriver**: Must be included with double quotes `#include "I2SClocklessLedDriver.h"`, not angle brackets.
- **artnetESP32V2**: The library folder is named `Artnetesp32v2` but the header file is `artnetESP32V2.h` - be aware of this case sensitivity.
- **NUM_LEDS_PER_STRIP**: Must be defined *before* including the I2SClocklessLedDriver.h library to avoid the default value warning.

### ArtNet Callback Function Issues:
- The `artnetESP32V2` library requires a global frame callback that takes no parameters (`void function()`).
- For actual data processing, each subArtnet instance needs a callback that takes a void pointer parameter.
- Solution: Use a wrapper function with the correct signature for `setFrameCallback()` and let the actual processing happen in the subArtnet callback.

### ArtNet Data Access:
- Use `sub->getData()` to get the ArtNet data buffer instead of trying to access a non-existent `lengthData` property.
- Use `sub->_nb_data` to get the data length.

### Color Effects Timing:
- Color cycle effects use `millis()` for timing to ensure smooth, non-blocking animation.
- The animation speed is controlled by the `cycleSpeed` setting in the web UI.

### Type Casting for Math Functions:
- When using `max()` and `min()` functions with different types (like `int` and `long int`), explicit casting is required to avoid compilation errors.
- Example: `heat[i] = max(0, (int)heat[i] - (int)random(0, cooling));`

## Future Improvements

- **Add Support for More LED Types**: Extend the code to handle additional types of LEDs, such as WS2811 or APA102.
- **Enhanced Color Effects**: Add more complex patterns and effects to the color cycle options.
- **DMX Control**: Add support for direct DMX control over Ethernet or USB.
- **Scheduled Effects**: Allow setting up schedules for different effects to run at specific times.
- **Audio Reactive Effects**: Add support for audio input to create sound-reactive LED patterns.

## Final Notes

This project provides a robust way to control WS2812B LEDs via ArtNet, static color, or built-in effects, using an ESP32. The combination of I2SClockless LED control, ArtNet communication, and the web UI configuration makes it flexible and easy to use for various interactive lighting setups.

Settings are stored in non-volatile memory, so your configuration persists across power cycles. The mode selection (ArtNet, Static Color, or Color Cycle) allows for versatile usage scenarios from professional light installations to simple mood lighting.