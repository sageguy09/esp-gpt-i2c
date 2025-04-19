/**
 * NetworkManager.cpp
 * WiFi connection handling implementation
 * 
 * Part of ESP32 ArtNet DMX LED Controller
 * Created: April 2025
 */

#include "NetworkManager.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_err.h"

// Initialize static members
TaskHandle_t NetworkManager::_networkTaskHandle = NULL;
hw_timer_t* NetworkManager::_watchdogTimer = NULL;
NetworkState NetworkManager::_networkState = NETWORK_INITIALIZING;
bool NetworkManager::_apFallbackEnabled = WIFI_AP_FALLBACK_ENABLED;
bool NetworkManager::_networkInitialized = false;
bool NetworkManager::_networkInitFailed = false;
unsigned long NetworkManager::_lastReconnectAttempt = 0;
unsigned long NetworkManager::_apStartTime = 0;
String NetworkManager::_hostname = MDNS_DEVICE_NAME;
String NetworkManager::_ssid = "";
String NetworkManager::_password = "";
SystemSettings* NetworkManager::_settings = nullptr;
SystemStatus* NetworkManager::_status = nullptr;
std::mutex NetworkManager::_networkMutex;

// Initialize the network manager
bool NetworkManager::init(SystemSettings* settings, SystemStatus* status) {
    // Store pointers to settings and status
    _settings = settings;
    _status = status;
    
    // Log initialization
    LOG_INFO("Initializing NetworkManager");
    
    // Check if we should skip initialization due to previous failures
    if (_networkInitFailed) {
        LOG_WARNING("Network initialization skipped - previously failed");
        _networkState = NETWORK_DISABLED;
        updateStatus();
        return false;
    }
    
    // Check if WiFi is enabled in settings
    if (settings != nullptr && !settings->useWiFi) {
        LOG_INFO("WiFi disabled in settings");
        _networkState = NETWORK_DISABLED;
        updateStatus();
        return false;
    }
    
    // Set credentials from settings if available
    if (settings != nullptr) {
        _ssid = settings->ssid;
        _password = settings->password;
        _hostname = settings->deviceName;
        _apFallbackEnabled = settings->createAP;
    }
    
    // Try to initialize the network
    try {
        // Initialize the TCP/IP stack (critical for preventing assertion failures)
        if (!initTcpIpStack()) {
            LOG_ERROR("TCP/IP stack initialization failed");
            disableNetworkOperations();
            return false;
        }
        
        // Configure WiFi
        WiFi.persistent(false);
        WiFi.disconnect(true);
        delay(200); // Stabilization delay
        
        // Set up WiFi event handler
        WiFi.onEvent(WiFiEvent);
        
        // Set hostname
        if (!_hostname.isEmpty()) {
            WiFi.setHostname(_hostname.c_str());
        }
        
        // Mark as initialized
        _networkInitialized = true;
        _networkState = NETWORK_DISCONNECTED;
        updateStatus();
        
        LOG_INFO("NetworkManager initialized successfully");
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception during network initialization: " + String(e.what()));
        disableNetworkOperations();
        return false;
    }
    catch (...) {
        LOG_ERROR("Unknown exception during network initialization");
        disableNetworkOperations();
        return false;
    }
}

// Start network operations
bool NetworkManager::start() {
    // Check if already initialized
    if (!_networkInitialized) {
        LOG_ERROR("Cannot start network - not initialized");
        return false;
    }
    
    LOG_INFO("Starting network operations");
    
    // Create network task on NETWORK_CORE
    BaseType_t result = xTaskCreatePinnedToCore(
        networkTask,            // Task function
        "NetworkTask",          // Name
        4096,                   // Stack size
        NULL,                   // Parameters
        WIFI_TASK_PRIORITY,     // Priority
        &_networkTaskHandle,    // Task handle
        NETWORK_CORE            // Core
    );
    
    if (result != pdPASS || _networkTaskHandle == NULL) {
        LOG_ERROR("Failed to create network task");
        return false;
    }
    
    LOG_INFO("Network task created on core " + String(NETWORK_CORE));
    return true;
}

// Stop network operations
bool NetworkManager::stop() {
    // Check if task is running
    if (_networkTaskHandle != NULL) {
        LOG_INFO("Stopping network task");
        vTaskDelete(_networkTaskHandle);
        _networkTaskHandle = NULL;
    }
    
    // Disconnect from WiFi
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    // Update state
    _networkState = NETWORK_DISABLED;
    updateStatus();
    
    LOG_INFO("Network operations stopped");
    return true;
}

// Connect to WiFi in station mode
bool NetworkManager::connectToWiFi() {
    // Lock the mutex to ensure thread safety
    std::lock_guard<std::mutex> lock(_networkMutex);
    
    // Check if already connected
    if (WiFi.status() == WL_CONNECTED) {
        LOG_DEBUG("Already connected to WiFi");
        return true;
    }
    
    // Check if we have credentials
    if (_ssid.isEmpty()) {
        LOG_ERROR("Cannot connect to WiFi - SSID not set");
        return false;
    }
    
    LOG_INFO("Connecting to WiFi SSID: " + _ssid);
    
    // Update state
    _networkState = NETWORK_CONNECTING;
    updateStatus();
    
    // Set WiFi mode
    WiFi.mode(WIFI_STA);
    delay(100);
    
    // Start connection with timeout
    _lastReconnectAttempt = millis();
    WiFi.begin(_ssid.c_str(), _password.c_str());
    
    // Non-blocking connection check to be handled in update()
    return true;
}

// Start AP mode
bool NetworkManager::startAccessPoint() {
    // Lock the mutex to ensure thread safety
    std::lock_guard<std::mutex> lock(_networkMutex);
    
    // Generate a unique AP name
    String apName = generateAPName();
    
    LOG_INFO("Starting Access Point: " + apName);
    
    // Disconnect from any existing network
    WiFi.disconnect(true);
    delay(100);
    
    // Set WiFi mode
    WiFi.mode(WIFI_AP);
    delay(100);
    
    // Configure AP
    bool result = WiFi.softAP(
        apName.c_str(),                // AP name
        WIFI_AP_PASSWORD,              // Password
        WIFI_AP_CHANNEL,               // Channel
        0,                             // Hidden SSID (0 = visible)
        WIFI_AP_MAX_CONNECTIONS        // Max connections
    );
    
    if (!result) {
        LOG_ERROR("Failed to start Access Point");
        return false;
    }
    
    // Get AP IP address
    IPAddress apIP = WiFi.softAPIP();
    LOG_INFO("Access Point started. IP address: " + apIP.toString());
    
    // Update state
    _networkState = NETWORK_AP_MODE;
    _apStartTime = millis();
    updateStatus();
    
    return true;
}

// Reconnect to WiFi if disconnected
bool NetworkManager::reconnect() {
    // Don't attempt to reconnect if network is disabled or failed
    if (_networkState == NETWORK_DISABLED || _networkState == NETWORK_FAILED) {
        return false;
    }
    
    // Check if it's time to attempt reconnection
    unsigned long currentMillis = millis();
    if (currentMillis - _lastReconnectAttempt < WIFI_RECONNECT_INTERVAL_MS) {
        return false; // Not time to reconnect yet
    }
    
    // Attempt to reconnect
    LOG_INFO("Attempting WiFi reconnection");
    return connectToWiFi();
}

// Update network status (call periodically)
void NetworkManager::update() {
    // Skip update if network is disabled or failed
    if (_networkState == NETWORK_DISABLED || _networkState == NETWORK_FAILED) {
        return;
    }
    
    // Get current time
    unsigned long currentMillis = millis();
    
    // Handle different network states
    switch (_networkState) {
        case NETWORK_CONNECTING:
            // Check if connection succeeded
            if (WiFi.status() == WL_CONNECTED) {
                LOG_INFO("WiFi connected. IP address: " + WiFi.localIP().toString());
                _networkState = NETWORK_CONNECTED;
                
                // Set up mDNS
                setupMDNS(_hostname.isEmpty() ? MDNS_DEVICE_NAME : _hostname);
            }
            // Check if connection timed out
            else if (currentMillis - _lastReconnectAttempt > WIFI_CONNECT_TIMEOUT_MS) {
                LOG_WARNING("WiFi connection timed out");
                
                // If AP fallback is enabled, start AP mode
                if (_apFallbackEnabled) {
                    LOG_INFO("Falling back to Access Point mode");
                    startAccessPoint();
                } else {
                    _networkState = NETWORK_DISCONNECTED;
                }
            }
            break;
            
        case NETWORK_CONNECTED:
            // Check if we lost connection
            if (WiFi.status() != WL_CONNECTED) {
                LOG_WARNING("WiFi connection lost");
                _networkState = NETWORK_DISCONNECTED;
                
                // Attempt to reconnect immediately
                reconnect();
            }
            break;
            
        case NETWORK_DISCONNECTED:
            // Try to reconnect if it's time
            reconnect();
            break;
            
        case NETWORK_AP_MODE:
            // Check if AP mode timeout has been reached (if enabled)
            if (WIFI_AP_TIMEOUT_MS > 0 && currentMillis - _apStartTime > WIFI_AP_TIMEOUT_MS) {
                LOG_INFO("AP mode timeout reached, attempting to reconnect to WiFi");
                connectToWiFi();
            }
            break;
            
        default:
            break;
    }
    
    // Update system status
    updateStatus();
}

// Get current network state
NetworkState NetworkManager::getState() {
    return _networkState;
}

// Get IP address as string
String NetworkManager::getIPAddress() {
    if (_networkState == NETWORK_CONNECTED) {
        return WiFi.localIP().toString();
    } else if (_networkState == NETWORK_AP_MODE) {
        return WiFi.softAPIP().toString();
    } else {
        return "0.0.0.0";
    }
}

// Check if connected to WiFi
bool NetworkManager::isConnected() {
    return _networkState == NETWORK_CONNECTED && WiFi.status() == WL_CONNECTED;
}

// Check if in AP mode
bool NetworkManager::isInAPMode() {
    return _networkState == NETWORK_AP_MODE;
}

// Set up mDNS responder
bool NetworkManager::setupMDNS(const String& hostname) {
    // Skip if not connected
    if (!isConnected()) {
        LOG_WARNING("Cannot setup mDNS - not connected to WiFi");
        return false;
    }
    
    LOG_INFO("Setting up mDNS responder with hostname: " + hostname);
    
    // Start mDNS responder
    if (!MDNS.begin(hostname.c_str())) {
        LOG_ERROR("Failed to start mDNS responder");
        return false;
    }
    
    // Add service to mDNS
    MDNS.addService(MDNS_SERVICE_NAME, MDNS_PROTOCOL, ARTNET_PORT);
    
    LOG_INFO("mDNS responder started successfully");
    return true;
}

// Disable all network operations (for critical failures)
void NetworkManager::disableNetworkOperations() {
    // Lock the mutex to ensure thread safety
    std::lock_guard<std::mutex> lock(_networkMutex);
    
    LOG_ERROR("Disabling all network operations due to critical failure");
    
    // Shutdown network interfaces
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    
    // Mark as failed
    _networkInitFailed = true;
    _networkState = NETWORK_FAILED;
    
    // Update settings if available
    if (_settings != nullptr) {
        _settings->useWiFi = false;
        _settings->useArtnet = false;
    }
    
    // Update status
    updateStatus();
}

// Get RSSI (WiFi signal strength)
int8_t NetworkManager::getRSSI() {
    if (isConnected()) {
        return WiFi.RSSI();
    } else {
        return 0;
    }
}

// Set hostname
void NetworkManager::setHostname(const String& hostname) {
    _hostname = hostname;
    
    // Update WiFi hostname if already initialized
    if (_networkInitialized) {
        WiFi.setHostname(hostname.c_str());
        
        // Update mDNS if connected
        if (isConnected()) {
            setupMDNS(hostname);
        }
    }
}

// Set credentials
void NetworkManager::setCredentials(const String& ssid, const String& password) {
    _ssid = ssid;
    _password = password;
    
    // If we're changing credentials and already connected, reconnect
    if (_networkInitialized && _networkState == NETWORK_CONNECTED) {
        LOG_INFO("Credentials changed, reconnecting to WiFi");
        connectToWiFi();
    }
}

// Set AP fallback
void NetworkManager::setApFallback(bool enable) {
    _apFallbackEnabled = enable;
}

// Network task handler
void NetworkManager::networkTask(void* parameter) {
    LOG_INFO("Network task started on core " + String(xPortGetCoreID()));
    
    // Initialize network
    if (NetworkManager::_networkInitialized) {
        // Attempt to connect to WiFi
        NetworkManager::connectToWiFi();
    }
    
    // Task loop
    while (true) {
        // Update network status
        NetworkManager::update();
        
        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// WiFi event handler
void NetworkManager::WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case SYSTEM_EVENT_STA_GOT_IP:
            LOG_INFO("WiFi connected with IP: " + WiFi.localIP().toString());
            _networkState = NETWORK_CONNECTED;
            break;
            
        case SYSTEM_EVENT_STA_DISCONNECTED:
            LOG_WARNING("WiFi disconnected");
            if (_networkState == NETWORK_CONNECTED) {
                _networkState = NETWORK_DISCONNECTED;
            }
            break;
            
        case SYSTEM_EVENT_AP_START:
            LOG_INFO("Access Point started");
            _networkState = NETWORK_AP_MODE;
            break;
            
        case SYSTEM_EVENT_AP_STOP:
            LOG_INFO("Access Point stopped");
            if (_networkState == NETWORK_AP_MODE) {
                _networkState = NETWORK_DISCONNECTED;
            }
            break;
            
        case SYSTEM_EVENT_AP_STACONNECTED:
            LOG_INFO("Station connected to Access Point");
            break;
            
        case SYSTEM_EVENT_AP_STADISCONNECTED:
            LOG_INFO("Station disconnected from Access Point");
            break;
            
        default:
            // Ignore other events
            break;
    }
    
    // Update status
    updateStatus();
}

// TCP/IP stack initialization
bool NetworkManager::initTcpIpStack() {
    LOG_INFO("Initializing TCP/IP stack");
    
    try {
        // Initialize TCP/IP stack
        esp_err_t err = esp_netif_init();
        if (err != ESP_OK) {
            LOG_ERROR("TCP/IP stack initialization failed: " + String(esp_err_to_name(err)));
            return false;
        }
        
        // Create the default event loop
        err = esp_event_loop_create_default();
        if (err != ESP_OK) {
            LOG_ERROR("Event loop creation failed: " + String(esp_err_to_name(err)));
            return false;
        }
        
        LOG_INFO("TCP/IP stack initialized successfully");
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception during TCP/IP stack initialization: " + String(e.what()));
        return false;
    }
    catch (...) {
        LOG_ERROR("Unknown exception during TCP/IP stack initialization");
        return false;
    }
}

// Calculate unique AP name with MAC address suffix
String NetworkManager::generateAPName() {
    // Get MAC address
    uint8_t mac[6];
    WiFi.macAddress(mac);
    
    // Format name with last 4 digits of MAC address
    char apName[32];
    sprintf(apName, "%s%02X%02X", WIFI_AP_NAME_PREFIX, mac[4], mac[5]);
    
    return String(apName);
}

// Update system status
void NetworkManager::updateStatus() {
    if (_status == nullptr) {
        return;
    }
    
    // Update status information
    _status->networkState = _networkState;
    _status->ipAddress = getIPAddress();
    _status->rssi = getRSSI();
    _status->lastConnectAttempt = _lastReconnectAttempt;
}