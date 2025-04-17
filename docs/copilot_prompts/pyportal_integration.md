# ESP32 ArtNet Controller: PyPortal Integration

## Task Context

We're looking to expand the ESP32 ArtNet LED Controller with external control capabilities via PyPortal, an Adafruit device with a touchscreen display. This integration will allow for enhanced control and visualization options without requiring a computer or smartphone.

## Current Implementation

The controller currently supports:
- Web UI for configuration
- UART Communication Bridge for external device communication
- ArtNet reception over WiFi
- Static and cycle animation modes

The UART Communication Bridge is already in place and provides the foundation for PyPortal communication.

## Integration Goals

1. **Bidirectional Communication**: Establish reliable communication between ESP32 and PyPortal via UART.

2. **Control Interface**: Implement a PyPortal touchscreen interface for LED control.

3. **Status Display**: Show ESP32 status, LED configuration, and network details on PyPortal.

4. **Standalone Operation**: Allow control of LED modes and colors without requiring WiFi.

## Implementation Guidelines

The implementation should:

1. **Leverage Existing UART Bridge**: Use the current UARTCommunicationBridge infrastructure.

2. **Use Protocol Buffers**: Implement a structured communication protocol for reliable data exchange.

3. **Handle Connection Issues**: Gracefully manage connection loss and recovery.

4. **Maintain Independence**: Ensure ESP32 can still function if PyPortal is disconnected.

## Implementation Examples

### ESP32-Side Enhancement

```cpp
// Enhanced UART command handler with PyPortal-specific commands
void handleUARTCommand(uint8_t cmd, uint8_t *data, uint16_t length) {
  debugLog("UART command received: " + String(cmd, HEX));

  switch (cmd) {
    // Existing commands...
    
    // PyPortal status request
    case CMD_GET_STATUS_DETAILED:
      sendDetailedStatus();
      break;
      
    // PyPortal configuration update
    case CMD_UPDATE_CONFIG:
      if (length >= sizeof(ConfigData)) {
        updateConfigFromPyPortal(data);
      }
      break;
      
    // PyPortal direct pixel control
    case CMD_SET_PIXEL:
      if (length >= 5) { // LED index (2 bytes) + RGB (3 bytes)
        uint16_t ledIndex = (data[0] << 8) | data[1];
        setPixelColor(ledIndex, data[2], data[3], data[4]);
      }
      break;
      
    default:
      // Unknown command
      uartBridge.sendErrorMessage(ERR_INVALID_CMD, "Unknown command");
      break;
  }
}

// Send detailed status information to PyPortal
void sendDetailedStatus() {
  // Create status buffer
  uint8_t statusData[32];
  int pos = 0;
  
  // Device operation mode
  statusData[pos++] = settings.useArtnet ? MODE_ARTNET : 
                      (settings.useColorCycle ? MODE_CYCLE : MODE_STATIC);
  
  // LED configuration
  statusData[pos++] = settings.numStrips;
  statusData[pos++] = (settings.ledsPerStrip >> 8) & 0xFF; // High byte
  statusData[pos++] = settings.ledsPerStrip & 0xFF;        // Low byte
  statusData[pos++] = settings.brightness;
  
  // Current color (if in static mode)
  statusData[pos++] = settings.staticColor.r;
  statusData[pos++] = settings.staticColor.g;
  statusData[pos++] = settings.staticColor.b;
  
  // WiFi status
  statusData[pos++] = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
  
  // ArtNet statistics
  statusData[pos++] = (packetsReceived >> 24) & 0xFF;
  statusData[pos++] = (packetsReceived >> 16) & 0xFF;
  statusData[pos++] = (packetsReceived >> 8) & 0xFF;
  statusData[pos++] = packetsReceived & 0xFF;
  
  // System uptime (seconds)
  uint32_t uptime = millis() / 1000;
  statusData[pos++] = (uptime >> 24) & 0xFF;
  statusData[pos++] = (uptime >> 16) & 0xFF;
  statusData[pos++] = (uptime >> 8) & 0xFF;
  statusData[pos++] = uptime & 0xFF;
  
  // Free memory
  uint32_t freeMem = ESP.getFreeHeap();
  statusData[pos++] = (freeMem >> 24) & 0xFF;
  statusData[pos++] = (freeMem >> 16) & 0xFF;
  statusData[pos++] = (freeMem >> 8) & 0xFF;
  statusData[pos++] = freeMem & 0xFF;
  
  // Send the status data
  uartBridge.sendCommand(CMD_STATUS_DETAILED, statusData, pos);
}

// Update configuration based on PyPortal command
void updateConfigFromPyPortal(uint8_t *data) {
  // Extract config data
  uint8_t pos = 0;
  
  // Operation mode
  uint8_t mode = data[pos++];
  switch (mode) {
    case MODE_ARTNET:
      settings.useArtnet = true;
      settings.useStaticColor = false;
      settings.useColorCycle = false;
      break;
      
    case MODE_STATIC:
      settings.useArtnet = false;
      settings.useStaticColor = true;
      settings.useColorCycle = false;
      break;
      
    case MODE_CYCLE:
      settings.useArtnet = false;
      settings.useStaticColor = false;
      settings.useColorCycle = true;
      break;
  }
  
  // LED configuration
  settings.brightness = data[pos++];
  
  // For static mode, update color
  if (settings.useStaticColor) {
    settings.staticColor.r = data[pos++];
    settings.staticColor.g = data[pos++];
    settings.staticColor.b = data[pos++];
  }
  
  // For cycle mode, update animation type
  if (settings.useColorCycle) {
    settings.colorMode = data[pos++];
    settings.cycleSpeed = data[pos++];
  }
  
  // Save settings and apply changes
  saveSettings();
  
  // Apply the new settings
  if (settings.useStaticColor) {
    applyStaticColor();
  }
  
  // Send acknowledgment
  uint8_t ack = 0x01;
  uartBridge.sendCommand(CMD_ACK, &ack, 1);
}
```

### PyPortal CircuitPython Implementation

```python
import board
import busio
import digitalio
import time
import displayio
import terminalio
from adafruit_display_text import label
import adafruit_touchscreen
from adafruit_button import Button
import struct

# Initialize UART communication
uart = busio.UART(board.TX, board.RX, baudrate=115200)

# Command codes (must match ESP32 definitions)
CMD_ACK = 0x01
CMD_ERROR = 0x02
CMD_SET_MODE = 0x10
CMD_SET_BRIGHTNESS = 0x11
CMD_SET_COLOR = 0x12
CMD_SET_ANIMATION = 0x13
CMD_GET_STATUS = 0x20
CMD_GET_STATUS_DETAILED = 0x21
CMD_STATUS_DETAILED = 0x22
CMD_UPDATE_CONFIG = 0x30

# Mode definitions
MODE_ARTNET = 0
MODE_STATIC = 1
MODE_CYCLE = 2

# Display setup
display = board.DISPLAY
ts = adafruit_touchscreen.Touchscreen(
    board.TOUCH_XL, board.TOUCH_XR,
    board.TOUCH_YD, board.TOUCH_YU,
    calibration=((5200, 59000), (5800, 57000)),
    size=(display.width, display.height))

# Create the display group
main_group = displayio.Group()
display.show(main_group)

# Status variables
device_status = {
    "mode": MODE_STATIC,
    "strips": 1,
    "leds_per_strip": 144,
    "brightness": 255,
    "color": (255, 0, 255),  # Default magenta
    "wifi_connected": False,
    "packets_received": 0,
    "uptime": 0,
    "free_memory": 0
}

# Send command to ESP32
def send_command(cmd, data=None):
    # Start byte
    packet = bytearray([0xAA])
    
    # Prepare data
    if data is None:
        data = bytearray()
    
    # Length (command + data + checksum)
    packet.append(len(data) + 3)
    
    # Command
    packet.append(cmd)
    
    # Data
    packet.extend(data)
    
    # Calculate checksum (XOR of all bytes)
    checksum = 0
    for b in packet:
        checksum ^= b
    packet.append(checksum)
    
    # Send packet
    uart.write(packet)

# Request detailed status
def request_status():
    send_command(CMD_GET_STATUS_DETAILED)

# Process received packets
def process_uart():
    if not uart.in_waiting:
        return
    
    # Look for start byte
    start_byte = uart.read(1)
    if not start_byte or start_byte[0] != 0xAA:
        return
    
    # Read length
    length_byte = uart.read(1)
    if not length_byte:
        return
    length = length_byte[0]
    
    # Read command and data
    packet_data = uart.read(length - 1)  # -1 for the start byte we already read
    if not packet_data or len(packet_data) < length - 1:
        return
    
    cmd = packet_data[0]
    data = packet_data[1:-1]  # Remove command and checksum
    
    # Process command
    if cmd == CMD_STATUS_DETAILED:
        process_status_data(data)
    elif cmd == CMD_ERROR:
        process_error(data)
    # Handle other commands...

# Process status data from ESP32
def process_status_data(data):
    pos = 0
    
    # Extract mode
    device_status["mode"] = data[pos]
    pos += 1
    
    # Extract LED configuration
    device_status["strips"] = data[pos]
    pos += 1
    device_status["leds_per_strip"] = (data[pos] << 8) | data[pos+1]
    pos += 2
    device_status["brightness"] = data[pos]
    pos += 1
    
    # Extract color
    device_status["color"] = (data[pos], data[pos+1], data[pos+2])
    pos += 3
    
    # Extract WiFi status
    device_status["wifi_connected"] = data[pos] == 1
    pos += 1
    
    # Extract ArtNet statistics
    device_status["packets_received"] = (data[pos] << 24) | (data[pos+1] << 16) | (data[pos+2] << 8) | data[pos+3]
    pos += 4
    
    # Extract uptime
    device_status["uptime"] = (data[pos] << 24) | (data[pos+1] << 16) | (data[pos+2] << 8) | data[pos+3]
    pos += 4
    
    # Extract free memory
    device_status["free_memory"] = (data[pos] << 24) | (data[pos+1] << 16) | (data[pos+2] << 8) | data[pos+3]
    
    # Update UI with new data
    update_display()

# Update the UI with current status
def update_display():
    # Clear display
    while len(main_group) > 0:
        main_group.pop()
    
    # Add status labels
    status_label = label.Label(
        terminalio.FONT,
        text=f"Mode: {['ArtNet', 'Static', 'Cycle'][device_status['mode']]}",
        x=10, y=10)
    main_group.append(status_label)
    
    wifi_label = label.Label(
        terminalio.FONT,
        text=f"WiFi: {'Connected' if device_status['wifi_connected'] else 'Disconnected'}",
        x=10, y=30)
    main_group.append(wifi_label)
    
    led_label = label.Label(
        terminalio.FONT,
        text=f"LEDs: {device_status['strips']} strips, {device_status['leds_per_strip']} LEDs",
        x=10, y=50)
    main_group.append(led_label)
    
    # Add mode control buttons
    artnet_button = Button(
        x=10, y=80, width=100, height=40,
        label="ArtNet", label_font=terminalio.FONT,
        style=Button.ROUNDRECT,
        selected=device_status["mode"] == MODE_ARTNET)
    main_group.append(artnet_button)
    
    static_button = Button(
        x=120, y=80, width=100, height=40,
        label="Static", label_font=terminalio.FONT,
        style=Button.ROUNDRECT,
        selected=device_status["mode"] == MODE_STATIC)
    main_group.append(static_button)
    
    cycle_button = Button(
        x=230, y=80, width=100, height=40,
        label="Cycle", label_font=terminalio.FONT,
        style=Button.ROUNDRECT,
        selected=device_status["mode"] == MODE_CYCLE)
    main_group.append(cycle_button)
    
    # Add color picker if in static mode
    if device_status["mode"] == MODE_STATIC:
        # Add color picker buttons
        colors = [
            ("Red", (255, 0, 0)),
            ("Green", (0, 255, 0)),
            ("Blue", (0, 0, 255)),
            ("Yellow", (255, 255, 0)),
            ("Cyan", (0, 255, 255)),
            ("Magenta", (255, 0, 255)),
            ("White", (255, 255, 255)),
        ]
        
        for i, (name, color) in enumerate(colors):
            x = 10 + (i % 3) * 110
            y = 130 + (i // 3) * 50
            color_button = Button(
                x=x, y=y, width=100, height=40,
                label=name, label_font=terminalio.FONT,
                style=Button.ROUNDRECT,
                fill_color=color,
                outline_color=0xFFFFFF)
            main_group.append(color_button)
    
    # Add animation controls if in cycle mode
    if device_status["mode"] == MODE_CYCLE:
        # Add animation type buttons
        animations = [
            ("Rainbow", 1),
            ("Pulse", 2),
            ("Fire", 3)
        ]
        
        for i, (name, anim_type) in enumerate(animations):
            x = 10 + i * 110
            y = 130
            anim_button = Button(
                x=x, y=y, width=100, height=40,
                label=name, label_font=terminalio.FONT,
                style=Button.ROUNDRECT,
                selected=device_status.get("animation_type", 1) == anim_type)
            main_group.append(anim_button)

# Main loop
last_status_request = 0
last_touch_check = 0

while True:
    current_time = time.monotonic()
    
    # Request status periodically
    if current_time - last_status_request > 2:  # Every 2 seconds
        request_status()
        last_status_request = current_time
    
    # Process UART data
    process_uart()
    
    # Check for touch events
    if current_time - last_touch_check > 0.1:  # 10 times per second
        p = ts.touch_point
        if p:
            # Handle touch events for buttons
            # (Implementation would depend on your specific UI layout)
            pass
        
        last_touch_check = current_time
    
    # Short delay to prevent CPU overload
    time.sleep(0.01)
```

## Communication Protocol

The communication protocol between ESP32 and PyPortal uses a simple packet structure:

```
[START_BYTE][LENGTH][COMMAND][DATA...][CHECKSUM]
```

Where:
- `START_BYTE`: Always 0xAA
- `LENGTH`: Total packet length (includes command, data, and checksum)
- `COMMAND`: Command code (e.g., status request, color change)
- `DATA`: Variable-length data specific to each command
- `CHECKSUM`: XOR of all preceding bytes

### Key Commands

| Command | Code | Description | Data Format |
|---------|------|-------------|-------------|
| ACK | 0x01 | Acknowledgment | [Status] |
| ERROR | 0x02 | Error notification | [ErrorCode][Message] |
| SET_MODE | 0x10 | Set operation mode | [Mode] |
| SET_BRIGHTNESS | 0x11 | Set LED brightness | [Brightness] |
| SET_COLOR | 0x12 | Set static color | [R][G][B] |
| SET_ANIMATION | 0x13 | Set animation parameters | [Type][Speed] |
| GET_STATUS | 0x20 | Request basic status | None |
| GET_STATUS_DETAILED | 0x21 | Request detailed status | None |
| STATUS_DETAILED | 0x22 | Detailed status response | [Mode][Strips][LEDs][Brightness][Color][WiFi][Stats]... |
| UPDATE_CONFIG | 0x30 | Update multiple settings | [Mode][Brightness][Color/Animation]... |

## Testing Procedures

To validate the integration:

1. **Communication Testing**: Verify reliable UART communication between ESP32 and PyPortal.

2. **UI Interaction**: Test all UI elements on the PyPortal touchscreen.

3. **State Synchronization**: Ensure ESP32 state changes are reflected on PyPortal and vice versa.

4. **Disconnection Handling**: Test behavior when UART connection is interrupted.

## Key Considerations

- **Buffer Management**: Be careful with buffer sizes on both sides of the connection.
- **Error Handling**: Implement robust error detection and recovery.
- **Touch Calibration**: PyPortal touchscreen may require calibration for accurate touch detection.
- **Power Considerations**: PyPortal can be powered via USB or battery; consider power requirements.

## Resources

- [Adafruit PyPortal Documentation](https://learn.adafruit.com/adafruit-pyportal)
- [CircuitPython UART Documentation](https://learn.adafruit.com/circuitpython-essentials/circuitpython-uart-serial)
- [Adafruit Display and Touch Libraries](https://learn.adafruit.com/making-a-pyportal-user-interface-displayio)
- [ESP32 UART Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/uart.html)
