# ESP32 ArtNet Controller Project

This project implements an ArtNet DMX controller using ESP32 platforms to drive WS2812B LED installations. It features dual implementation options:

1. Full implementation with ArtNet, web UI, OLED display, and various LED effects
2. Minimal implementation focused on stable network initialization and basic functionality

## Project Structure

- **ESP_GPT_I2C_Common.h/cpp**: Common code shared between both implementations
  - Network initialization with anti-crash protection
  - Debug logging functions
  - Basic settings structure
  - Utility functions

- **esp-gpt-i2c-full/**: Full-featured implementation
  - ArtNet DMX reception
  - Web UI for configuration
  - OLED status display
  - Multiple LED effect modes
  - Static color mode
  - UART bridge for external control

- **esp-gpt-i2c-minimal/**: Minimal implementation for testing and debugging
  - Stable network initialization
  - Memory management optimization
  - Status monitoring
  - Debugging utilities

## Key Features

- **Reliable Network Stack**: ESP-IDF component initialization in correct sequence
- **ArtNet Reception**: Efficient processing of ArtNet packets
- **WS2812B Control**: Optimized LED control via I2SClocklessLedDriver
- **Web Configuration**: Simple browser-based setup
- **UART Bridge**: External control via PyPortal or other microcontroller
- **Boot Loop Protection**: Anti-crash mechanisms to prevent boot loops

## Usage

### Full Implementation
1. Open the `esp-gpt-i2c-full/esp-gpt-i2c-full.ino` sketch in Arduino IDE
2. Select your ESP32 board from the Boards menu
3. Upload the sketch to your ESP32 board
4. Access the web UI via your board's IP address for configuration

### Minimal Implementation
1. Open the `esp-gpt-i2c-minimal/esp-gpt-i2c-minimal.ino` sketch in Arduino IDE
2. Select your ESP32 board from the Boards menu
3. Upload the sketch to your ESP32 board
4. Monitor serial output for debugging information

## Hardware Requirements

- ESP32-WROOM or ESP32-S3 board
- WS2812B LED strips (up to 300 LEDs per universe)
- 5V power supply sized appropriately for LED count
- Optional: OLED display (SSD1306, 128×32)
- Optional: PyPortal or other microcontroller for external control

## License

MIT License
