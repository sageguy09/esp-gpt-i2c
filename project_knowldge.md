# ESP32 ArtNet I2SClockless Web UI with OLED Debug

This project integrates the ESP32 with the ArtNet protocol, I2SClockless LED driver, and a Web UI for configuration. It allows you to control WS2812B LED strips over the ArtNet protocol with features like web-based configuration, OLED display status, and debug logs.

## Project Overview

### Features:
- **ArtNet Protocol**: Handle ArtNet data over Wi-Fi, allowing communication between the ESP32 and ArtNet-compatible software like Resolume.
- **I2SClockless LED Driver**: Controls WS2812B LEDs using the I2S interface for smooth performance.
- **Web UI Configuration**: A simple web interface for setting up the Wi-Fi SSID, password, and LED configurations.
- **OLED Display Debug**: A small OLED display for real-time status updates, such as the current IP address and LED configuration.

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
- **SSID and Password**: Connect to Wi-Fi by entering your SSID and password.
- **Web Interface URL**: Once the ESP32 is connected to Wi-Fi, open the ESP32's IP address in a browser to access the configuration page.

### 2. ArtNet Data Reception:
- The ESP32 listens for ArtNet data over the network (default port 6454).
- When data is received, it is processed in the callback function (artnetCallback) which updates the LEDs using the I2SClocklessLedDriver.

### 3. OLED Debug Display:
- Displays the current status of the ESP32, including the connected IP address and other relevant debug information such as the number of LEDs, strips, and current settings.
- Updates in real-time as ArtNet data is received.

## Code Explanation

### Key Variables:
- **NUM_LEDS_PER_STRIP**: Defines the number of LEDs on each strip. **IMPORTANT: Must be defined before including I2SClocklessLedDriver.h**.
- **NUMSTRIPS**: Defines the total number of strips (maximum of 12, with 3 strips per pin).
- **ledData**: Holds the LED data that is sent to the I2SClocklessLedDriver.
- **sendFrame**: Flag to indicate when a frame should be sent to the LEDs.
- **settings**: Stores the configuration settings like Wi-Fi SSID, password, and LED configurations.

### Main Components:

#### ArtNet Setup:
- The ArtNet server is set up with `artnet.listen(localIP, 6454)` to listen for incoming ArtNet packets.
- The ArtNet class requires two callback approaches:
  1. A global frame callback with no parameters: `frameCallbackWrapper()`
  2. A subArtnet callback that receives a void pointer: `artnetCallback(void* param)`

#### LED Control:
- The LEDs are controlled using the I2SClocklessLedDriver library. The data is passed to the driver with `driver.showPixels()`.

#### Web Server:
- The web server is set up to listen on port 80 and serve the configuration page.
- The configuration data is read from the web UI and saved to memory (no SPIFFS is involved).

#### OLED Debug:
- An OLED display is used to show the current IP address and other status information.
- It also displays a status of the LED configuration and updates in real-time.

## Code Overview

Here's a breakdown of the important code sections:

### 1. Setting Up ArtNet:
```cpp
IPAddress localIP = WiFi.localIP();
artnet.listen(localIP, 6454);  // Start listening for ArtNet data
artnet.setFrameCallback(frameCallbackWrapper);  // Set the callback to handle ArtNet frames
```
- `artnet.listen()`: Starts listening for ArtNet packets.
- `artnet.setFrameCallback()`: Sets a wrapper callback function that complies with the required function signature.

### 2. ArtNet Callbacks:
```cpp
// Wrapper function for the frame callback (required by artnetESP32V2)
void frameCallbackWrapper() {
  // Empty function - actual processing happens in the subArtnet callback
}

// Actual data processing function (called by subArtnet)
void artnetCallback(void* param) {
  subArtnet* sub = (subArtnet*)param;
  uint8_t* data = sub->getData();
  int dataLength = sub->_nb_data;
  
  // Process data and update LEDs...
}
```

### 3. Web UI Configuration:
```cpp
void handleRoot() {
  String html = "<html><head><title>ESP32 Artnet</title></head><body><h2>ESP32 Artnet I2SClockless</h2>";
  html += "<form method='POST' action='/config'>";
  html += "<label>LEDs/Strip:</label><input name='leds' value='" + String(settings.ledsPerStrip) + "'><br>";
  html += "<label>Strips:</label><input name='strips' value='" + String(settings.numStrips) + "'><br>";
  html += "<label>Brightness:</label><input name='bright' value='" + String(settings.brightness) + "'><br>";
  html += "<label>SSID:</label><input name='ssid' value='" + settings.ssid + "'><br>";
  html += "<label>Password:</label><input name='pass' value='" + settings.password + "'><br>";
  html += "<input type='submit'></form></body></html>";
  server.send(200, "text/html", html);
}
```
- `handleRoot()`: Generates an HTML form for the user to input their configuration.

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

### Multiple Libraries for SSD1306:
- If you encounter multiple versions of the Adafruit_SSD1306 library, ensure you only have one version installed. Remove any redundant libraries in the Arduino IDE.

## Further Improvements

- **Add Support for More LED Types**: Extend the code to handle additional types of LEDs, such as WS2811 or APA102.
- **Enhanced Web UI**: Improve the UI with more options like custom color controls or effects for the LEDs.
- **Error Handling**: Add error handling for cases where the ESP32 fails to connect to Wi-Fi or ArtNet communication is interrupted.
- **Use SPIFFS or Preferences for persistent storage**: Currently settings are only stored in memory and lost on reboot.

## Final Notes

This project provides a robust way to control WS2812B LEDs via ArtNet, using an ESP32. The combination of I2SClockless LED control, ArtNet communication, and the Web UI configuration makes it flexible and easy to use for various interactive lighting setups.

The artnetESP32V2 library has a specific callback structure that needs to be properly implemented for the ArtNet data to flow correctly. Make sure to follow the pattern described in this document when making changes to the code.