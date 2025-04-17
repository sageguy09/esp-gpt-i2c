# ESP32 ArtNet Controller: Web UI Enhancement

## Task Context

The ESP32 ArtNet LED Controller features a web-based user interface for configuration and control. While the basic functionality works, we need to enhance the UI to provide better user experience, more detailed diagnostics, and improved controls.

## Current Implementation

The current web UI includes:
- LED configuration (strips, LEDs per strip, brightness)
- Pin configuration
- Mode selection (ArtNet, Static Color, Color Cycle)
- Color picker for static mode
- WiFi configuration

## Enhancement Goals

1. **Improved UI Responsiveness**: Make the UI more responsive and provide better feedback during operations.

2. **Enhanced Diagnostics Panel**: Add a dedicated diagnostics panel showing system status, memory usage, and network details.

3. **Live Preview**: Add a visual representation of the current LED state.

4. **Direct LED Control**: Allow direct control of individual LEDs for testing.

5. **Connection Status Indicator**: Show clear WiFi and ArtNet connection status.

## Implementation Guidelines

The implementation should:

1. **Maintain Compatibility**: Work with the existing backend API.

2. **Use Progressive Enhancement**: Start with basic functionality and add advanced features incrementally.

3. **Handle Network Issues**: Gracefully handle situations where the network connection drops.

4. **Optimize for Mobile**: Ensure the UI works well on mobile devices.

## Implementation Examples

### Enhanced Status Panel

```html
<div class="status-panel">
  <div class="status-item">
    <div class="status-label">WiFi:</div>
    <div class="status-value" id="wifi-status">Connecting...</div>
  </div>
  <div class="status-item">
    <div class="status-label">ArtNet:</div>
    <div class="status-value" id="artnet-status">Inactive</div>
  </div>
  <div class="status-item">
    <div class="status-label">Uptime:</div>
    <div class="status-value" id="uptime">0d 0h 0m</div>
  </div>
  <div class="status-item">
    <div class="status-label">Free Memory:</div>
    <div class="status-value" id="free-memory">0 KB</div>
  </div>
  <div class="status-item">
    <div class="status-label">LED Mode:</div>
    <div class="status-value" id="led-mode">Static</div>
  </div>
</div>
```

### API Endpoint for Status Updates

```cpp
void handleStatus() {
  StaticJsonDocument<512> doc;
  
  // System status
  doc["uptime"] = millis() / 1000;
  doc["freeMemory"] = ESP.getFreeHeap();
  
  // WiFi status
  doc["wifi"]["connected"] = (WiFi.status() == WL_CONNECTED);
  doc["wifi"]["rssi"] = WiFi.RSSI();
  doc["wifi"]["ip"] = WiFi.localIP().toString();
  
  // ArtNet status
  doc["artnet"]["enabled"] = settings.useArtnet;
  doc["artnet"]["packetsReceived"] = packetsReceived;
  doc["artnet"]["lastPacketTime"] = (millis() - lastPacketTime);
  
  // LED status
  doc["led"]["mode"] = settings.useArtnet ? "ArtNet" : 
                      (settings.useColorCycle ? "Cycle" : "Static");
  doc["led"]["brightness"] = settings.brightness;
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}
```

### AJAX Status Updates

```javascript
// Update status every 2 seconds
function updateStatus() {
  fetch('/status')
    .then(response => response.json())
    .then(data => {
      // Update WiFi status
      const wifiStatus = document.getElementById('wifi-status');
      if (data.wifi.connected) {
        wifiStatus.textContent = `Connected (${data.wifi.rssi} dBm)`;
        wifiStatus.className = 'status-value status-good';
      } else {
        wifiStatus.textContent = 'Disconnected';
        wifiStatus.className = 'status-value status-error';
      }
      
      // Update ArtNet status
      const artnetStatus = document.getElementById('artnet-status');
      if (data.artnet.enabled) {
        if (data.artnet.lastPacketTime < 5000) {
          artnetStatus.textContent = `Active (${data.artnet.packetsReceived} packets)`;
          artnetStatus.className = 'status-value status-good';
        } else {
          artnetStatus.textContent = 'No recent data';
          artnetStatus.className = 'status-value status-warning';
        }
      } else {
        artnetStatus.textContent = 'Disabled';
        artnetStatus.className = 'status-value status-neutral';
      }
      
      // Update other status elements...
    })
    .catch(error => {
      console.error('Error fetching status:', error);
      // Handle connection error - maybe show reconnect button
    });
}

// Start periodic updates
setInterval(updateStatus, 2000);
```

### Live LED Preview

```html
<div class="led-preview-container">
  <h3>LED Preview</h3>
  <div class="led-strip" id="led-preview"></div>
</div>

<script>
  function updateLEDPreview(numLEDs, mode) {
    const container = document.getElementById('led-preview');
    container.innerHTML = '';
    
    // Create LED elements
    for (let i = 0; i < numLEDs; i++) {
      const led = document.createElement('div');
      led.className = 'led';
      led.id = `led-${i}`;
      
      // For static mode, set all to the same color
      if (mode === 'static') {
        const colorPicker = document.getElementById('staticColor');
        led.style.backgroundColor = colorPicker.value;
      } 
      // For cycle mode, set colors based on position
      else if (mode === 'cycle') {
        const hue = (i / numLEDs * 360) % 360;
        led.style.backgroundColor = `hsl(${hue}, 100%, 50%)`;
      }
      
      container.appendChild(led);
    }
  }
</script>
```

## Testing Procedures

To validate the UI enhancements:

1. **Cross-Browser Testing**: Test on Chrome, Firefox, Safari, and Edge.

2. **Mobile Testing**: Verify functionality on mobile devices.

3. **Network Interruption Testing**: Test behavior when network connection is lost.

4. **Recovery Testing**: Verify UI recovers gracefully when connection is restored.

## Key Considerations

- **API Design**: Keep backend API endpoints simple and RESTful.
- **Progressive Enhancement**: Start with basic HTML and enhance with JavaScript.
- **Error Handling**: Provide clear error messages for all operations.
- **Responsive Design**: Use CSS flexbox/grid for responsive layouts.
- **Connection Management**: Handle connection drops gracefully.

## Resources

- [ESP32 Web Server Best Practices](https://randomnerdtutorials.com/esp32-web-server-arduino-ide/)
- [ArduinoJson Library](https://arduinojson.org/)
- [Modern CSS Techniques](https://developer.mozilla.org/en-US/docs/Web/CSS)
- [Fetch API](https://developer.mozilla.org/en-US/docs/Web/API/Fetch_API)
