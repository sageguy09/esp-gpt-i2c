#include "WiFiConnectionManager.h"
#include "ESP_GPT_I2C_Common.h"

// Add logWithTimestamp function
void logWithTimestamp(String message)
{
  // Get current uptime in milliseconds
  unsigned long uptime = millis();

  // Convert to a readable format
  String timestamp = String(uptime / 3600000) + ":" +
                     String((uptime / 60000) % 60) + ":" +
                     String((uptime / 1000) % 60) + "." +
                     String(uptime % 1000);

  // Format with fixed width for better readability
  while (timestamp.length() < 12)
  {
    timestamp = " " + timestamp;
  }

  // Log with timestamp
  debugLog("[" + timestamp + "] " + message);
}

// Define WiFi status tracking structure
WiFiStatusInfo wifiStatus = {
    .isConnected = false,
    .reconnectAttempts = 0,
    .lastReconnectAttempt = 0,
    .signalStrength = 0,
    .ipAddress = "",
    .macAddress = ""};

// Convert WiFi status code to readable string
String wifiStatusToString(wl_status_t status)
{
  switch (status)
  {
  case WL_IDLE_STATUS:
    return "IDLE";
  case WL_NO_SSID_AVAIL:
    return "NO SSID AVAILABLE";
  case WL_SCAN_COMPLETED:
    return "SCAN COMPLETED";
  case WL_CONNECTED:
    return "CONNECTED";
  case WL_CONNECT_FAILED:
    return "CONNECTION FAILED";
  case WL_CONNECTION_LOST:
    return "CONNECTION LOST";
  case WL_DISCONNECTED:
    return "DISCONNECTED";
  default:
    return "UNKNOWN (" + String(status) + ")";
  }
}

// Initialize WiFi with detailed logging
void setupWiFiWithLogging()
{
  if (networkInitFailed || !settings.useWiFi)
  {
    logWithTimestamp("WiFi initialization skipped - network disabled");
    return;
  }

  logWithTimestamp("WiFi initialization starting");
  logWithTimestamp("Connecting to SSID: " + settings.ssid);

  // Set WiFi mode first
  WiFi.mode(WIFI_STA);
  logWithTimestamp("WiFi mode set to STATION (client) mode");

  // Disconnect before attempting to connect
  WiFi.disconnect(true);
  delay(100);
  logWithTimestamp("Previous WiFi connections cleared");

  // Set hostname for easier identification on the network
  WiFi.setHostname(settings.nodeName.c_str());
  logWithTimestamp("Hostname set to: " + settings.nodeName);

  // Log MAC address
  wifiStatus.macAddress = WiFi.macAddress();
  logWithTimestamp("MAC Address: " + wifiStatus.macAddress);

  // Start WiFi connection
  WiFi.begin(settings.ssid.c_str(), settings.password.c_str());
  logWithTimestamp("WiFi connection attempt initiated");

  // Wait for connection with detailed logging
  int attempts = 0;
  int maxAttempts = 20; // 10 seconds timeout

  while (WiFi.status() != WL_CONNECTED && attempts < maxAttempts)
  {
    delay(500);
    attempts++;

    // Log details about the connection attempt
    if (attempts % 2 == 0)
    { // Log every second
      wl_status_t status = WiFi.status();
      logWithTimestamp("WiFi status: " + wifiStatusToString(status) +
                       " (Attempt " + String(attempts / 2) + " of " + String(maxAttempts / 2) + ")");
    }
  }

  // Update WiFi status information
  if (WiFi.status() == WL_CONNECTED)
  {
    wifiStatus.isConnected = true;
    wifiStatus.ipAddress = WiFi.localIP().toString();
    wifiStatus.signalStrength = WiFi.RSSI();

    logWithTimestamp("WiFi connected successfully!");
    logWithTimestamp("IP Address: " + wifiStatus.ipAddress);
    logWithTimestamp("Signal strength (RSSI): " + String(wifiStatus.signalStrength) + " dBm");
    logWithTimestamp("Channel: " + String(WiFi.channel()));

    // Get the gateway IP
    IPAddress gateway = WiFi.gatewayIP();
    logWithTimestamp("Gateway IP: " + gateway.toString());

    // Get the subnet mask
    IPAddress subnet = WiFi.subnetMask();
    logWithTimestamp("Subnet Mask: " + subnet.toString());

    // Get the DNS server IP
    IPAddress dns = WiFi.dnsIP();
    logWithTimestamp("DNS Server: " + dns.toString());
  }
  else
  {
    wifiStatus.isConnected = false;
    logWithTimestamp("WiFi connection FAILED - operating in offline mode");
    logWithTimestamp("Last status: " + wifiStatusToString(WiFi.status()));
  }
}

// Monitor WiFi connection and log status changes
void monitorWiFiConnection()
{
  static wl_status_t lastStatus = WL_IDLE_STATUS;
  static unsigned long lastCheck = 0;
  unsigned long currentMillis = millis();

  // Check every second
  if (currentMillis - lastCheck < 1000)
  {
    return;
  }

  lastCheck = currentMillis;
  wl_status_t currentStatus = WiFi.status();

  // Only log when status changes
  if (currentStatus != lastStatus)
  {
    logWithTimestamp("WiFi status changed: " + wifiStatusToString(lastStatus) +
                     " -> " + wifiStatusToString(currentStatus));

    // Update connection status
    wifiStatus.isConnected = (currentStatus == WL_CONNECTED);

    // If connected, update IP and signal strength
    if (currentStatus == WL_CONNECTED)
    {
      wifiStatus.ipAddress = WiFi.localIP().toString();
      wifiStatus.signalStrength = WiFi.RSSI();
      logWithTimestamp("Connected with IP: " + wifiStatus.ipAddress +
                       ", RSSI: " + String(wifiStatus.signalStrength) + " dBm");
    }

    lastStatus = currentStatus;
  }

  // Periodic logging of signal strength when connected
  static unsigned long lastSignalLog = 0;
  if (wifiStatus.isConnected && (currentMillis - lastSignalLog >= 30000))
  { // Every 30 seconds
    lastSignalLog = currentMillis;
    wifiStatus.signalStrength = WiFi.RSSI();
    logWithTimestamp("WiFi signal strength: " + String(wifiStatus.signalStrength) + " dBm");
  }
}

// Attempt to reconnect WiFi if disconnected
void reconnectIfNeeded()
{
  if (networkInitFailed || !settings.useWiFi)
  {
    return;
  }

  unsigned long currentMillis = millis();

  // Only attempt reconnect if we're disconnected and enough time has passed since last attempt
  if (!wifiStatus.isConnected &&
      (currentMillis - wifiStatus.lastReconnectAttempt >= 30000))
  { // 30 seconds between attempts

    wifiStatus.lastReconnectAttempt = currentMillis;
    wifiStatus.reconnectAttempts++;

    logWithTimestamp("Attempting WiFi reconnection (attempt #" +
                     String(wifiStatus.reconnectAttempts) + ")...");

    // Disconnect before reconnecting
    WiFi.disconnect(true);
    delay(100);

    // Set the mode again just to be safe
    WiFi.mode(WIFI_STA);
    delay(100);

    // Begin connection
    WiFi.begin(settings.ssid.c_str(), settings.password.c_str());

    // Wait briefly for connection
    int shortAttempts = 0;
    while (WiFi.status() != WL_CONNECTED && shortAttempts < 10)
    {
      delay(500);
      shortAttempts++;
    }

    // Check if connection was successful
    if (WiFi.status() == WL_CONNECTED)
    {
      wifiStatus.isConnected = true;
      wifiStatus.ipAddress = WiFi.localIP().toString();
      wifiStatus.signalStrength = WiFi.RSSI();

      logWithTimestamp("WiFi reconnected successfully!");
      logWithTimestamp("IP Address: " + wifiStatus.ipAddress);
      logWithTimestamp("Signal strength (RSSI): " + String(wifiStatus.signalStrength) + " dBm");

      // If you have ArtNet or other network services, you would restart them here
      if (settings.artnetEnabled)
      {
        logWithTimestamp("Restarting ArtNet after reconnection...");
        setupArtNet(); // Call your ArtNet setup function
      }
    }
    else
    {
      logWithTimestamp("WiFi reconnection failed: " + wifiStatusToString(WiFi.status()));
    }
  }
}

// Complete network initialization function
bool initializeNetwork()
{
  if (networkInitFailed || !settings.useWiFi)
  {
    logWithTimestamp("Network initialization skipped - disabled in settings or previously failed");
    return false;
  }

  // Start with WiFi setup
  setupWiFiWithLogging();

  // Setup ArtNet if WiFi is connected and ArtNet is enabled
  if (wifiStatus.isConnected && settings.artnetEnabled)
  {
    logWithTimestamp("Setting up ArtNet DMX...");
    bool artnetSetupResult = setupArtNet();

    if (artnetSetupResult)
    {
      logWithTimestamp("ArtNet setup successful!");
    }
    else
    {
      logWithTimestamp("ArtNet setup failed!");
    }

    return artnetSetupResult;
  }

  return wifiStatus.isConnected;
}
