#include "WebServerManager.h"

// Define global web server
AsyncWebServer server(80);

// Debug logging function with timestamps
void logWithTimestamp(const String &message)
{
  unsigned long uptime = millis();
  unsigned long seconds = uptime / 1000;
  unsigned long minutes = seconds / 60;
  seconds %= 60;

  String formattedMsg = "[" + String(minutes) + ":" +
                        (seconds < 10 ? "0" : "") + String(seconds) + "." +
                        String(uptime % 1000) + "] " + message;

  debugLog(formattedMsg);
}

// Main function to set up the web server
void setupWebServer()
{
  // Initialize SPIFFS first
  bool spiffsInitialized = SPIFFS.begin(true);
  if (!spiffsInitialized)
  {
    logWithTimestamp("ERROR: SPIFFS mount failed, will use embedded HTML fallback");
  }
  else
  {
    logWithTimestamp("SPIFFS mounted successfully");

    // List files in SPIFFS for debugging
    File root = SPIFFS.open("/");
    if (root && root.isDirectory())
    {
      File file = root.openNextFile();
      logWithTimestamp("SPIFFS content:");
      int fileCount = 0;
      while (file)
      {
        String fileName = file.name();
        int fileSize = file.size();
        logWithTimestamp("  • " + fileName + " (" + String(fileSize) + " bytes)");
        file = root.openNextFile();
        fileCount++;
      }

      if (fileCount == 0)
      {
        logWithTimestamp("  • No files found in SPIFFS");
      }
    }
    else
    {
      logWithTimestamp("Failed to open SPIFFS root directory");
    }
  }

  // Set up root route handler based on whether SPIFFS is available
  if (spiffsInitialized && SPIFFS.exists("/index.html"))
  {
    logWithTimestamp("Web UI source: SPIFFS files");

    // Serve the root page from SPIFFS
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      logWithTimestamp("Serving index.html from SPIFFS");
      request->send(SPIFFS, "/index.html", "text/html"); });

    // Serve CSS from SPIFFS
    server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      logWithTimestamp("Serving style.css from SPIFFS");
      request->send(SPIFFS, "/style.css", "text/css"); });

    // Serve JavaScript from SPIFFS
    server.on("/script.js", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      logWithTimestamp("Serving script.js from SPIFFS");
      request->send(SPIFFS, "/script.js", "application/javascript"); });
  }
  else
  {
    logWithTimestamp("Web UI source: Embedded HTML (SPIFFS files not found)");

    // Serve the root page using embedded HTML
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
      logWithTimestamp("Serving embedded HTML content");
      request->send(200, "text/html", generateEmbeddedHTML()); });
  }

  // Always set up the /settings endpoint for JSON API
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    logWithTimestamp("Serving /settings endpoint (JSON)");
    handleSettings(request); });

  // Set up endpoint for debug logs
  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    logWithTimestamp("Serving /logs endpoint (JSON)");
    handleLog(request); });

  // Set up POST handler for config updates
  server.on("/config", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    logWithTimestamp("Received configuration POST request");
    handleConfigPost(request); },
            // Handle file uploads if needed
            NULL,
            // Handle body data for POST requests
            [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
              // This handler processes POST body data - implement if needed for JSON data
            });

  // Set up a handler for 404 errors
  server.onNotFound([](AsyncWebServerRequest *request)
                    {
    logWithTimestamp("404 Not Found: " + request->url());
    request->send(404, "text/plain", "Not found"); });

  // Start the server
  server.begin();
  logWithTimestamp("AsyncWebServer started on port 80");
}

// Handler for the /settings endpoint
void handleSettings(AsyncWebServerRequest *request)
{
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  StaticJsonDocument<512> doc;

  // Add basic settings
  doc["ssid"] = settings.ssid;
  doc["useWiFi"] = settings.useWiFi;
  doc["nodeName"] = settings.nodeName;
  doc["artnetUniverse"] = settings.artnetUniverse;
  doc["ledCount"] = settings.ledCount;
  doc["ledPin"] = settings.ledPin;
  doc["brightness"] = settings.brightness;
  doc["artnetEnabled"] = settings.artnetEnabled;

  // Add runtime information
  doc["ipAddress"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "Not connected";
  doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
  doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["uptime"] = millis() / 1000;
  doc["freeHeap"] = ESP.getFreeHeap();

  // Add ArtNet status
  doc["artnetRunning"] = state.artnetRunning;
  doc["artnetPacketCount"] = state.artnetPacketCount;
  doc["lastArtnetPacket"] = state.lastArtnetPacket;

  serializeJson(doc, *response);
  request->send(response);
}

// Handler for the /logs endpoint
void handleLog(AsyncWebServerRequest *request)
{
  AsyncResponseStream *response = request->beginResponseStream("application/json");
  StaticJsonDocument<2048> doc;
  JsonArray logs = doc.createNestedArray("logs");

  // Add logs from the circular buffer
  for (int i = 0; i < MAX_LOG_ENTRIES; i++)
  {
    int idx = (state.logIndex + i) % MAX_LOG_ENTRIES;
    if (state.logs[idx].length() > 0)
    {
      logs.add(state.logs[idx]);
    }
  }

  serializeJson(doc, *response);
  request->send(response);
}

// Generate embedded HTML for when SPIFFS is not available
String generateEmbeddedHTML()
{
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>ESP32 I2C Controller</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  // Embed minimal CSS here
  html += "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f5f5f5; }";
  html += ".container { max-width: 800px; margin: 0 auto; background-color: white; padding: 20px; border-radius: 5px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += ".form-group { margin-bottom: 15px; }";
  html += "label { display: inline-block; width: 150px; font-weight: bold; }";
  html += "input[type='number'], input[type='text'], input[type='password'] { width: 200px; padding: 8px; border: 1px solid #ddd; border-radius: 4px; }";
  html += "input[type='range'] { width: 200px; }";
  html += "input[type='checkbox'] { margin-right: 5px; }";
  html += "button { background: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }";
  html += "button:hover { background: #45a049; }";
  html += ".card { background: white; border-radius: 4px; padding: 15px; margin-bottom: 15px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }";
  html += ".card h3 { margin-top: 0; border-bottom: 1px solid #eee; padding-bottom: 10px; color: #333; }";
  html += ".status { font-size: 14px; color: #666; }";
  html += ".tabs { display: flex; margin-bottom: 20px; }";
  html += ".tab { padding: 10px 20px; cursor: pointer; border: 1px solid #ddd; border-bottom: none; border-radius: 4px 4px 0 0; background: #f8f8f8; }";
  html += ".tab.active { background: white; border-bottom: 2px solid white; margin-bottom: -1px; }";
  html += ".tab-content { display: none; border: 1px solid #ddd; padding: 20px; background: white; }";
  html += ".tab-content.active { display: block; }";
  html += ".note { background: #fffde7; padding: 10px; border-left: 5px solid #ffd600; margin-bottom: 20px; }";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h2>ESP32 I2C Controller</h2>";

  // Note about embedded interface
  html += "<div class='note'>";
  html += "<strong>Note:</strong> Using embedded HTML interface. For a full-featured interface, upload files to SPIFFS.";
  html += "</div>";

  // Tabs navigation
  html += "<div class='tabs'>";
  html += "<div class='tab active' onclick='showTab(\"config\")'>Configuration</div>";
  html += "<div class='tab' onclick='showTab(\"status\")'>Status</div>";
  html += "<div class='tab' onclick='showTab(\"logs\")'>Logs</div>";
  html += "</div>";

  // Configuration tab
  html += "<div id='config' class='tab-content active'>";
  html += "<form id='configForm' method='POST' action='/config'>";

  // WiFi section
  html += "<div class='card'>";
  html += "<h3>WiFi Settings</h3>";
  html += "<div class='form-group'><label>SSID:</label><input type='text' name='ssid' value='" + settings.ssid + "'></div>";
  html += "<div class='form-group'><label>Password:</label><input type='password' name='password' value='" + settings.password + "'></div>";

  // Fix string concatenation error
  String wifiChecked = settings.useWiFi ? "checked" : "";
  html += "<div class='form-group'><label>Enable WiFi:</label><input type='checkbox' name='useWiFi' " + wifiChecked + "></div>";

  html += "<div class='form-group'><label>Node Name:</label><input type='text' name='nodeName' value='" + settings.nodeName + "'></div>";
  html += "</div>";

  // LED section
  html += "<div class='card'>";
  html += "<h3>LED Configuration</h3>";
  html += "<div class='form-group'><label>LED Count:</label><input type='number' name='ledCount' value='" + String(settings.ledCount) + "'></div>";
  html += "<div class='form-group'><label>LED Pin:</label><input type='number' name='ledPin' value='" + String(settings.ledPin) + "'></div>";
  html += "<div class='form-group'><label>Brightness:</label><input type='range' min='0' max='255' name='brightness' value='" + String(settings.brightness) + "'></div>";
  html += "</div>";

  // ArtNet section
  html += "<div class='card'>";
  html += "<h3>ArtNet Settings</h3>";

  // Fix string concatenation error
  String artnetChecked = settings.artnetEnabled ? "checked" : "";
  html += "<div class='form-group'><label>Enable ArtNet:</label><input type='checkbox' name='artnetEnabled' " + artnetChecked + "></div>";

  html += "<div class='form-group'><label>Universe:</label><input type='number' name='artnetUniverse' value='" + String(settings.artnetUniverse) + "'></div>";
  html += "</div>";

  // Submit button
  html += "<button type='submit'>Save Configuration</button>";
  html += "</form>";
  html += "</div>";

  // Status tab
  html += "<div id='status' class='tab-content'>";
  html += "<div class='card'>";
  html += "<h3>System Status</h3>";
  html += "<div id='systemStatus'>Loading...</div>";
  html += "</div>";
  html += "<div class='card'>";
  html += "<h3>Network Status</h3>";
  html += "<div id='networkStatus'>Loading...</div>";
  html += "</div>";
  html += "<div class='card'>";
  html += "<h3>ArtNet Status</h3>";
  html += "<div id='artnetStatus'>Loading...</div>";
  html += "</div>";
  html += "<button onclick='refreshStatus()'>Refresh Status</button>";
  html += "</div>";

  // Logs tab
  html += "<div id='logs' class='tab-content'>";
  html += "<div class='card'>";
  html += "<h3>System Logs</h3>";
  html += "<div id='logEntries'>Loading logs...</div>";
  html += "</div>";
  html += "<button onclick='refreshLogs()'>Refresh Logs</button>";
  html += "</div>";

  // JavaScript for tab handling and status updates
  html += "<script>";

  // Tab switching function
  html += "function showTab(tabName) {";
  html += "  document.querySelectorAll('.tab-content').forEach(tab => tab.classList.remove('active'));";
  html += "  document.querySelectorAll('.tab').forEach(tab => tab.classList.remove('active'));";
  html += "  document.getElementById(tabName).classList.add('active');";
  html += "  document.querySelector('.tab[onclick=\"showTab(\\'' + tabName + '\\')\"]').classList.add('active');";
  html += "  if (tabName === 'status') refreshStatus();";
  html += "  if (tabName === 'logs') refreshLogs();";
  html += "}";

  // Status refresh function
  html += "function refreshStatus() {";
  html += "  fetch('/settings')";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      let systemHtml = `";
  html += "        <div class='status'>Uptime: ${formatUptime(data.uptime)}</div>";
  html += "        <div class='status'>Free Memory: ${formatBytes(data.freeHeap)}</div>";
  html += "      `;";
  html += "      document.getElementById('systemStatus').innerHTML = systemHtml;";

  html += "      let networkHtml = `";
  html += "        <div class='status'>WiFi Status: ${data.wifiConnected ? 'Connected' : 'Disconnected'}</div>";
  html += "        <div class='status'>IP Address: ${data.ipAddress}</div>";
  html += "        <div class='status'>RSSI: ${data.wifiConnected ? data.rssi + ' dBm' : 'N/A'}</div>";
  html += "      `;";
  html += "      document.getElementById('networkStatus').innerHTML = networkHtml;";

  html += "      let artnetHtml = `";
  html += "        <div class='status'>ArtNet Status: ${data.artnetRunning ? 'Running' : 'Stopped'}</div>";
  html += "        <div class='status'>Universe: ${data.artnetUniverse}</div>";
  html += "        <div class='status'>Packets Received: ${data.artnetPacketCount}</div>";
  html += "        <div class='status'>Last Packet: ${formatLastPacket(data.lastArtnetPacket)}</div>";
  html += "      `;";
  html += "      document.getElementById('artnetStatus').innerHTML = artnetHtml;";
  html += "    })";
  html += "    .catch(error => {";
  html += "      console.error('Error fetching status:', error);";
  html += "      document.getElementById('systemStatus').innerHTML = '<div class=\"status\">Error loading status</div>';";
  html += "    });";
  html += "}";

  // Logs refresh function
  html += "function refreshLogs() {";
  html += "  fetch('/logs')";
  html += "    .then(response => response.json())";
  html += "    .then(data => {";
  html += "      let logsHtml = '<div style=\"height: 300px; overflow-y: auto;\">';";
  html += "      if (data.logs && data.logs.length > 0) {";
  html += "        data.logs.forEach(log => {";
  html += "          logsHtml += `<div class=\"status\">${log}</div>`;";
  html += "        });";
  html += "      } else {";
  html += "        logsHtml += '<div class=\"status\">No logs available</div>';";
  html += "      }";
  html += "      logsHtml += '</div>';";
  html += "      document.getElementById('logEntries').innerHTML = logsHtml;";
  html += "    })";
  html += "    .catch(error => {";
  html += "      console.error('Error fetching logs:', error);";
  html += "      document.getElementById('logEntries').innerHTML = '<div class=\"status\">Error loading logs</div>';";
  html += "    });";
  html += "}";

  // Helper functions
  html += "function formatUptime(seconds) {";
  html += "  const hours = Math.floor(seconds / 3600);";
  html += "  const minutes = Math.floor((seconds % 3600) / 60);";
  html += "  const secs = seconds % 60;";
  html += "  return `${hours}h ${minutes}m ${secs}s`;";
  html += "}";

  html += "function formatBytes(bytes) {";
  html += "  if (bytes < 1024) return bytes + ' bytes';";
  html += "  else if (bytes < 1048576) return (bytes / 1024).toFixed(2) + ' KB';";
  html += "  else return (bytes / 1048576).toFixed(2) + ' MB';";
  html += "}";

  html += "function formatLastPacket(timestamp) {";
  html += "  if (!timestamp) return 'Never';";
  html += "  const now = new Date().getTime();";
  html += "  const diff = Math.floor((now - timestamp) / 1000);";
  html += "  if (diff < 60) return `${diff} seconds ago`;";
  html += "  else if (diff < 3600) return `${Math.floor(diff/60)} minutes ago`;";
  html += "  else return `${Math.floor(diff/3600)} hours ago`;";
  html += "}";

  // Initialize page
  html += "document.addEventListener('DOMContentLoaded', function() {";
  html += "  showTab('config');";
  html += "});";

  html += "</script>";
  html += "</div>"; // Close container
  html += "</body></html>";

  return html;
}

// Handle form submission
void handleConfigPost(AsyncWebServerRequest *request)
{
  logWithTimestamp("Processing configuration form submission");

  // Process form parameters
  int paramsNr = request->params();
  for (int i = 0; i < paramsNr; i++)
  {
    // Fix the invalid conversion by using const explicitly
    const AsyncWebParameter *p = request->getParam(i);
    logWithTimestamp("  • Parameter: " + p->name() + " = " + p->value());

    // Process each parameter
    String paramName = p->name();
    String paramValue = p->value();

    if (paramName == "ssid")
    {
      settings.ssid = paramValue;
    }
    else if (paramName == "password")
    {
      settings.password = paramValue;
    }
    else if (paramName == "useWiFi")
    {
      settings.useWiFi = (paramValue == "on" || paramValue == "1" || paramValue == "true");
    }
    else if (paramName == "nodeName")
    {
      settings.nodeName = paramValue;
    }
    else if (paramName == "ledCount")
    {
      settings.ledCount = paramValue.toInt();
    }
    else if (paramName == "ledPin")
    {
      settings.ledPin = paramValue.toInt();
    }
    else if (paramName == "brightness")
    {
      settings.brightness = paramValue.toInt();
    }
    else if (paramName == "artnetUniverse")
    {
      settings.artnetUniverse = paramValue.toInt();
    }
    else if (paramName == "artnetEnabled")
    {
      settings.artnetEnabled = (paramValue == "on" || paramValue == "1" || paramValue == "true");
    }
  }

  // If network settings changed, mark for restart
  preferences.begin("led-settings", false);
  preferences.putBool("netRestart", true);
  preferences.end();

  // Save settings
  // Here you would typically call some function to save the settings to preferences
  // This would need to be implemented based on your existing code structure

  // Redirect back to the root page
  request->redirect("/");
  logWithTimestamp("Configuration updated, redirecting to home page");
}
