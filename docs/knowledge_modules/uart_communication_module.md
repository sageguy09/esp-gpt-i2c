# UART Communication Bridge Module

## Overview
The UART Communication Bridge provides a bidirectional protocol for external control of the ESP32 ArtNet controller. This allows integration with external hardware like PyPortal displays or other microcontrollers.

## Implementation

### Initialization
```cpp
// Define the UART pin configuration
#define UART_RX_PIN 16  // GPIO pin for UART RX
#define UART_TX_PIN 17  // GPIO pin for UART TX

// Create a UART bridge instance using Serial2
UARTCommunicationBridge uartBridge(Serial2);

// Initialize in setup()
pinMode(UART_RX_PIN, INPUT);
pinMode(UART_TX_PIN, OUTPUT);
Serial2.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);

// Initialize UART bridge with error handling
bool uartOK = uartBridge.initializeCommunication();
if (uartOK) {
  uartBridge.setCommandCallback(handleUARTCommand);
}
```

## Protocol Structure

### Command Format
```
[START_BYTE][COMMAND][LENGTH][DATA...][CHECKSUM]
```

- **START_BYTE**: Fixed value (0xAA) marking packet start
- **COMMAND**: Command ID byte
- **LENGTH**: Length of DATA section
- **DATA**: Variable length command data
- **CHECKSUM**: Simple checksum for error detection

### Response Format
```
[START_BYTE][COMMAND/ACK/NACK][LENGTH][DATA...][CHECKSUM]
```

## Command Reference

### System Commands
- `CMD_PING (0x01)`: Simple ping to check communication
- `CMD_GET_VERSION (0x02)`: Get firmware version
- `CMD_RESET (0x03)`: Reset the ESP32

### LED Control Commands
- `CMD_SET_MODE (0x10)`: Set LED operation mode
  - Mode 0: ArtNet
  - Mode 1: Static color
  - Mode 2: Color cycle
- `CMD_SET_BRIGHTNESS (0x11)`: Set global brightness
- `CMD_SET_COLOR (0x12)`: Set static color (RGB)
- `CMD_SET_EFFECT (0x13)`: Set effect parameters

### Status Commands
- `CMD_GET_STATUS (0x20)`: Get general system status
- `CMD_GET_NETWORK_STATUS (0x21)`: Get network status
- `CMD_DMX_DATA (0x22)`: DMX data received notification

## Error Handling
- `ERR_NONE (0x00)`: No error
- `ERR_INVALID_CMD (0x01)`: Invalid command
- `ERR_INVALID_PARAM (0x02)`: Invalid parameter
- `ERR_CHECKSUM (0x03)`: Checksum error
- `ERR_BUFFER_OVERFLOW (0x04)`: Buffer overflow
- `ERR_TIMEOUT (0x05)`: Operation timeout

## Command Handling Example
```cpp
void handleUARTCommand(uint8_t cmd, uint8_t *data, uint16_t length) {
  switch (cmd) {
    case CMD_SET_MODE:
      if (length >= 1) {
        uint8_t mode = data[0];
        // Set the appropriate mode
        settings.useArtnet = (mode == 0);
        settings.useStaticColor = (mode == 1);
        settings.useColorCycle = (mode == 2);
        
        // Acknowledge the mode change
        uartBridge.sendCommand(CMD_ACK, &mode, 1);
      }
      break;
      
    case CMD_SET_BRIGHTNESS:
      if (length >= 1) {
        settings.brightness = data[0];
        driver.setBrightness(settings.brightness);
        
        // Acknowledge
        uint8_t brightnessValue = settings.brightness;
        uartBridge.sendCommand(CMD_ACK, &brightnessValue, 1);
      }
      break;
      
    // Other commands...
  }
}
```

## Integration with Main Loop
```cpp
void loop() {
  // Process UART communication with protection
  try {
    uartBridge.update();
  } catch (...) {
    // UART errors shouldn't crash the program
    debugLog("WARNING: UART update failed");
  }
  
  // Rest of the main loop...
}
```

## PyPortal Integration
The UART bridge is designed for easy integration with Adafruit PyPortal for creating an advanced touchscreen controller. The PyPortal would:

1. Display current status and mode information
2. Provide touch controls for LED parameters
3. Show color picker for static color mode
4. Display animations for cycle effects
5. Show network connection status

## Debugging
- Built-in error detection and reporting
- State tracking for protocol integrity
- Timeout handling for unresponsive transactions

## Best Practices
1. Always validate incoming command data
2. Implement proper error handling for all commands
3. Keep UART processing non-blocking
4. Provide acknowledgment for all commands
5. Implement checksums for data integrity