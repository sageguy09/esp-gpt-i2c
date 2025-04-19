/**
 * SystemManager.cpp
 * Core initialization and configuration management
 * 
 * Part of ESP32 ArtNet DMX LED Controller
 * Created: April 2025
 */

#include "SystemManager.h"
#include <esp_task_wdt.h>
#include <esp_system.h>

// Initialize static members
SystemSettings SystemManager::_settings;
SystemStatus SystemManager::_status;
Preferences SystemManager::_preferences;
std::mutex SystemManager::_systemMutex;
bool SystemManager::_initialized = false;
bool SystemManager::_running = false;
bool SystemManager::_inSafeMode = false;
hw_timer_t* SystemManager::_watchdogTimer = nullptr;
unsigned long SystemManager::_startTime = 0;
uint32_t SystemManager::_bootCount = 0;
unsigned long SystemManager::_lastBootTime = 0;

// Initialize the system manager
bool SystemManager::init() {
    // Set start time
    _startTime = millis();
    
    // Initialize the logger first
    Logger::init();
    LOG_INFO("SystemManager initializing...");
    
    // Get chip information
    esp_chip_info_t chipInfo;
    esp_chip_info(&chipInfo);
    LOG_INFO("ESP32 Chip: " + String((chipInfo.model == CHIP_ESP32S3) ? "ESP32-S3" : 
                                     (chipInfo.model == CHIP_ESP32) ? "ESP32" : "Unknown"));
    LOG_INFO("CPU Cores: " + String(chipInfo.cores));
    LOG_INFO("CPU Frequency: " + String(ESP.getCpuFreqMHz()) + " MHz");
    LOG_INFO("Free Heap: " + String(ESP.getFreeHeap()) + " bytes");
    
    // Check reset reason
    esp_reset_reason_t resetReason = esp_reset_reason();
    LOG_INFO("Reset reason: " + String(resetReason));
    
    // Initialize hardware
    if (!initHardware()) {
        LOG_ERROR("Hardware initialization failed");
        return false;
    }
    
    // Check for boot loops
    checkBootLoops();
    
    // Load settings
    if (!loadSettings()) {
        LOG_WARNING("Failed to load settings, using defaults");
        loadDefaultSettings();
    }
    
    // Initialize Network Manager only if not in safe mode
    if (!_inSafeMode && _settings.useWiFi) {
        if (!NetworkManager::init(&_settings, &_status)) {
            LOG_ERROR("Network manager initialization failed");
            
            // We can continue without network if necessary
            _settings.useWiFi = false;
            _settings.useArtnet = false;
        }
    }
    
    // Update status
    updateStatus();
    
    // Mark as initialized
    _initialized = true;
    LOG_INFO("SystemManager initialized successfully");
    
    return true;
}

// Start the system
bool SystemManager::start() {
    // Check if initialized
    if (!_initialized) {
        LOG_ERROR("Cannot start system - not initialized");
        return false;
    }
    
    LOG_INFO("Starting system...");
    
    // Set up watchdog
    if (!setupWatchdog()) {
        LOG_WARNING("Watchdog setup failed");
    }
    
    // Start network manager if enabled
    if (!_inSafeMode && _settings.useWiFi) {
        if (!NetworkManager::start()) {
            LOG_ERROR("Failed to start NetworkManager");
        }
    }
    
    // TODO: Initialize and start LED Manager when implemented
    
    // Mark as running
    _running = true;
    LOG_INFO("System started");
    
    return true;
}

// Stop the system
bool SystemManager::stop() {
    LOG_INFO("Stopping system...");
    
    // Stop network manager
    if (!_inSafeMode && _settings.useWiFi) {
        NetworkManager::stop();
    }
    
    // TODO: Stop LED Manager when implemented
    
    // Disable watchdog
    if (_watchdogTimer != nullptr) {
        timerAlarmDisable(_watchdogTimer);
    }
    
    // Mark as not running
    _running = false;
    LOG_INFO("System stopped");
    
    return true;
}

// Update the system (call from main loop)
void SystemManager::update() {
    // Skip if not running
    if (!_running) {
        return;
    }
    
    // Feed watchdog
    feedWatchdog();
    
    // Update network manager if enabled
    if (!_inSafeMode && _settings.useWiFi) {
        NetworkManager::update();
    }
    
    // TODO: Update LED Manager when implemented
    
    // Update system status
    updateStatus();
}

// Set up a watchdog timer for system reliability
bool SystemManager::setupWatchdog(uint32_t timeoutMs) {
    LOG_INFO("Setting up watchdog timer with timeout: " + String(timeoutMs) + "ms");
    
    try {
        // Initialize hardware timer for watchdog
        _watchdogTimer = timerBegin(0, 80, true); // Timer 0, divider 80, count up
        
        // Set alarm handler
        timerAttachInterrupt(_watchdogTimer, &watchdogCallback, true);
        
        // Set alarm value (timeout)
        timerAlarmWrite(_watchdogTimer, timeoutMs * 1000, false);
        
        // Enable alarm
        timerAlarmEnable(_watchdogTimer);
        
        LOG_INFO("Watchdog timer started");
        return true;
    }
    catch (...) {
        LOG_ERROR("Exception during watchdog setup");
        return false;
    }
}

// Reset the watchdog timer (call regularly to prevent reboot)
void SystemManager::feedWatchdog() {
    if (_watchdogTimer != nullptr) {
        timerWrite(_watchdogTimer, 0);
    }
}

// Load settings from persistent storage
bool SystemManager::loadSettings() {
    LOG_INFO("Loading settings from persistent storage");
    
    // Lock mutex for thread safety
    std::lock_guard<std::mutex> lock(_systemMutex);
    
    // Load default settings first
    loadDefaultSettings();
    
    try {
        // Open preferences
        if (!_preferences.begin(PREFERENCES_NAMESPACE, false)) {
            LOG_ERROR("Failed to open preferences");
            return false;
        }
        
        // Load boot count
        _bootCount = _preferences.getUInt(BOOT_COUNT_KEY, 0);
        _lastBootTime = _preferences.getULong("lastBootTime", 0);
        
        // Load network settings
        _settings.useWiFi = _preferences.getBool("useWiFi", _settings.useWiFi);
        _settings.useArtnet = _preferences.getBool("useArtnet", _settings.useArtnet);
        _settings.createAP = _preferences.getBool("createAP", _settings.createAP);
        _settings.ssid = _preferences.getString("ssid", _settings.ssid);
        _settings.password = _preferences.getString("password", _settings.password);
        _settings.deviceName = _preferences.getString("deviceName", _settings.deviceName);
        _settings.artnetUniverse = _preferences.getUShort("artnetUni", _settings.artnetUniverse);
        
        // Load LED settings
        _settings.mode = _preferences.getUChar("mode", _settings.mode);
        _settings.effectType = _preferences.getUChar("effectType", _settings.effectType);
        _settings.effectSpeed = _preferences.getUChar("effectSpeed", _settings.effectSpeed);
        _settings.numStrips = _preferences.getUShort("numStrips", _settings.numStrips);
        _settings.ledsPerStrip = _preferences.getUShort("ledsPerStrip", _settings.ledsPerStrip);
        _settings.brightness = _preferences.getUChar("brightness", _settings.brightness);
        
        // Load pins
        for (int i = 0; i < MAX_LED_STRIPS; i++) {
            String pinKey = "pin" + String(i);
            _settings.pins[i] = _preferences.getInt(pinKey.c_str(), _settings.pins[i]);
        }
        
        // Load static color
        _settings.staticColor.r = _preferences.getUChar("colorR", _settings.staticColor.r);
        _settings.staticColor.g = _preferences.getUChar("colorG", _settings.staticColor.g);
        _settings.staticColor.b = _preferences.getUChar("colorB", _settings.staticColor.b);
        
        // Load safe mode flag
        _settings.safeMode = _preferences.getBool("safeMode", _settings.safeMode);
        
        // Close preferences
        _preferences.end();
        
        // Check if we should enter safe mode
        if (_settings.safeMode) {
            enterSafeMode("Safe mode flag set in preferences");
        }
        
        LOG_INFO("Settings loaded successfully");
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception during loading settings: " + String(e.what()));
        _preferences.end();
        return false;
    }
    catch (...) {
        LOG_ERROR("Unknown exception during loading settings");
        _preferences.end();
        return false;
    }
}

// Save settings to persistent storage
bool SystemManager::saveSettings() {
    LOG_INFO("Saving settings to persistent storage");
    
    // Lock mutex for thread safety
    std::lock_guard<std::mutex> lock(_systemMutex);
    
    try {
        // Open preferences
        if (!_preferences.begin(PREFERENCES_NAMESPACE, false)) {
            LOG_ERROR("Failed to open preferences");
            return false;
        }
        
        // Save network settings
        _preferences.putBool("useWiFi", _settings.useWiFi);
        _preferences.putBool("useArtnet", _settings.useArtnet);
        _preferences.putBool("createAP", _settings.createAP);
        _preferences.putString("ssid", _settings.ssid);
        _preferences.putString("password", _settings.password);
        _preferences.putString("deviceName", _settings.deviceName);
        _preferences.putUShort("artnetUni", _settings.artnetUniverse);
        
        // Save LED settings
        _preferences.putUChar("mode", _settings.mode);
        _preferences.putUChar("effectType", _settings.effectType);
        _preferences.putUChar("effectSpeed", _settings.effectSpeed);
        _preferences.putUShort("numStrips", _settings.numStrips);
        _preferences.putUShort("ledsPerStrip", _settings.ledsPerStrip);
        _preferences.putUChar("brightness", _settings.brightness);
        
        // Save pins
        for (int i = 0; i < MAX_LED_STRIPS; i++) {
            String pinKey = "pin" + String(i);
            _preferences.putInt(pinKey.c_str(), _settings.pins[i]);
        }
        
        // Save static color
        _preferences.putUChar("colorR", _settings.staticColor.r);
        _preferences.putUChar("colorG", _settings.staticColor.g);
        _preferences.putUChar("colorB", _settings.staticColor.b);
        
        // Save safe mode flag
        _preferences.putBool("safeMode", _settings.safeMode);
        
        // Close preferences
        _preferences.end();
        
        LOG_INFO("Settings saved successfully");
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception during saving settings: " + String(e.what()));
        _preferences.end();
        return false;
    }
    catch (...) {
        LOG_ERROR("Unknown exception during saving settings");
        _preferences.end();
        return false;
    }
}

// Reset settings to defaults
bool SystemManager::resetSettings() {
    LOG_INFO("Resetting settings to defaults");
    
    // Lock mutex for thread safety
    std::lock_guard<std::mutex> lock(_systemMutex);
    
    try {
        // Open preferences
        if (!_preferences.begin(PREFERENCES_NAMESPACE, false)) {
            LOG_ERROR("Failed to open preferences");
            return false;
        }
        
        // Clear all preferences
        _preferences.clear();
        
        // Close preferences
        _preferences.end();
        
        // Load default settings
        loadDefaultSettings();
        
        LOG_INFO("Settings reset to defaults successfully");
        return true;
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception during resetting settings: " + String(e.what()));
        _preferences.end();
        return false;
    }
    catch (...) {
        LOG_ERROR("Unknown exception during resetting settings");
        _preferences.end();
        return false;
    }
}

// Get system settings
SystemSettings* SystemManager::getSettings() {
    return &_settings;
}

// Get system status
SystemStatus* SystemManager::getStatus() {
    return &_status;
}

// Check if system is in safe mode
bool SystemManager::isInSafeMode() {
    return _inSafeMode;
}

// Enter safe mode
void SystemManager::enterSafeMode(const String& reason) {
    LOG_WARNING("Entering safe mode: " + reason);
    
    // Set safe mode flag
    _inSafeMode = true;
    _settings.safeMode = true;
    
    // Disable network functionality
    _settings.useWiFi = false;
    _settings.useArtnet = false;
    
    // Set minimal LED configuration
    _settings.mode = MODE_STATIC;
    _settings.numStrips = 1;
    _settings.ledsPerStrip = 8; // Minimal LED count
    _settings.brightness = 64;  // Reduced brightness
    
    // Set static color to red to indicate safe mode
    _settings.staticColor.r = 255;
    _settings.staticColor.g = 0;
    _settings.staticColor.b = 0;
    
    // Save settings
    saveSettings();
    
    // Update status
    _status.isInSafeMode = true;
}

// Check for and handle boot loops
void SystemManager::checkBootLoops() {
    LOG_INFO("Checking for boot loops...");
    
    // Increment boot count
    incrementBootCount();
    
    // Get current time
    unsigned long currentTime = millis();
    
    // Open preferences
    if (!_preferences.begin(PREFERENCES_NAMESPACE, false)) {
        LOG_ERROR("Failed to open preferences");
        return;
    }
    
    // Read boot count and last boot time
    uint32_t bootCount = _preferences.getUInt(BOOT_COUNT_KEY, 1);
    unsigned long lastBootTime = _preferences.getULong(LAST_BOOT_TIME_KEY, 0);
    
    LOG_INFO("Boot count: " + String(bootCount) + 
             ", Last boot: " + String(lastBootTime) + "ms ago");
    
    // Check for rapid reboot pattern (3+ reboots within 1 minute)
    if (bootCount >= 3 && lastBootTime > 0 && lastBootTime < 60000) {
        LOG_WARNING("Boot loop detected! " + String(bootCount) + 
                   " reboots in " + String(lastBootTime) + "ms");
        
        // Enter safe mode
        enterSafeMode("Boot loop detected");
        
        // Reset boot count to prevent getting stuck in boot loop detection
        _preferences.putUInt(BOOT_COUNT_KEY, 1);
    }
    
    // Close preferences
    _preferences.end();
}

// Get uptime in milliseconds
unsigned long SystemManager::getUptime() {
    return millis() - _startTime;
}

// Get free heap memory
uint32_t SystemManager::getFreeHeap() {
    return ESP.getFreeHeap();
}

// Watchdog callback
void IRAM_ATTR SystemManager::watchdogCallback() {
    // Force system restart
    esp_restart();
}

// Initialize hardware
bool SystemManager::initHardware() {
    LOG_INFO("Initializing hardware...");
    
    // Set up status LED if enabled
    if (STATUS_LED_ENABLED) {
        pinMode(STATUS_LED_PIN, OUTPUT);
        digitalWrite(STATUS_LED_PIN, HIGH); // Turn on status LED
    }
    
    // TODO: Initialize any other hardware components
    
    LOG_INFO("Hardware initialized successfully");
    return true;
}

// Load default settings
void SystemManager::loadDefaultSettings() {
    // WiFi settings
    _settings.ssid = "";
    _settings.password = "";
    _settings.useWiFi = true;
    _settings.createAP = WIFI_AP_FALLBACK_ENABLED;
    
    // Device identification
    _settings.deviceName = MDNS_DEVICE_NAME;
    
    // ArtNet settings
    _settings.useArtnet = true;
    _settings.artnetUniverse = ARTNET_UNIVERSE_START;
    
    // LED settings
    _settings.mode = MODE_ARTNET;
    _settings.effectType = EFFECT_RAINBOW;
    _settings.effectSpeed = 128; // Mid speed
    _settings.numStrips = 1;
    _settings.ledsPerStrip = DEFAULT_LED_COUNT;
    _settings.brightness = DEFAULT_BRIGHTNESS;
    
    // Initialize pins with default values
    int defaultPins[4] = DEFAULT_LED_PINS;
    for (int i = 0; i < MAX_LED_STRIPS; i++) {
        _settings.pins[i] = (i < 4) ? defaultPins[i] : -1;
    }
    
    // Static color (default to white)
    _settings.staticColor.r = 255;
    _settings.staticColor.g = 255;
    _settings.staticColor.b = 255;
    
    // System settings
    _settings.safeMode = false;
    _settings.bootCount = 0;
}

// Update system status
void SystemManager::updateStatus() {
    // Lock mutex for thread safety
    std::lock_guard<std::mutex> lock(_systemMutex);
    
    // Update general system statistics
    _status.uptime = getUptime();
    _status.freeHeap = ESP.getFreeHeap();
    _status.minFreeHeap = ESP.getMinFreeHeap();
    _status.isInSafeMode = _inSafeMode;
    
    // Try to get CPU temperature
    #ifdef TARGET_ESP32_S3
    _status.cpuTemperature = temperatureRead();
    #else
    _status.cpuTemperature = 0.0f;
    #endif
    
    // Update network status (already updated by NetworkManager)
    
    // TODO: Update LED status when implemented
}

// Increment boot count
void SystemManager::incrementBootCount() {
    // Open preferences
    if (!_preferences.begin(PREFERENCES_NAMESPACE, false)) {
        LOG_ERROR("Failed to open preferences");
        return;
    }
    
    // Read current boot count
    uint32_t bootCount = _preferences.getUInt(BOOT_COUNT_KEY, 0);
    unsigned long currentTime = millis();
    
    // Store last boot time (time since previous boot)
    unsigned long lastBootTime = _preferences.getULong(LAST_BOOT_TIME_KEY, 0);
    
    // Increment boot counter
    bootCount++;
    _bootCount = bootCount;
    _lastBootTime = lastBootTime;
    
    // Save updated values
    _preferences.putUInt(BOOT_COUNT_KEY, bootCount);
    _preferences.putULong(LAST_BOOT_TIME_KEY, currentTime);
    
    // Close preferences
    _preferences.end();
    
    LOG_INFO("Boot count: " + String(bootCount));
}