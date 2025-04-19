/**
 * SystemManager.h
 * Core initialization and configuration management
 * 
 * Part of ESP32 ArtNet DMX LED Controller
 * Created: April 2025
 * 
 * This file implements the main system manager that coordinates
 * initialization of all subsystems and manages configuration.
 */

#ifndef ESP32_ARTNET_SYSTEM_MANAGER_H
#define ESP32_ARTNET_SYSTEM_MANAGER_H

#include "Config.h"
#include "Logger.h"
#include "NetworkManager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <mutex>

class SystemManager {
public:
    // Initialize the system manager
    static bool init();
    
    // Start the system
    static bool start();
    
    // Stop the system
    static bool stop();
    
    // Update the system (call from main loop)
    static void update();
    
    // Set up a watchdog timer for system reliability
    static bool setupWatchdog(uint32_t timeoutMs = WATCHDOG_TIMEOUT_MS);
    
    // Reset the watchdog timer (call regularly to prevent reboot)
    static void feedWatchdog();
    
    // Load settings from persistent storage
    static bool loadSettings();
    
    // Save settings to persistent storage
    static bool saveSettings();
    
    // Reset settings to defaults
    static bool resetSettings();
    
    // Get system settings
    static SystemSettings* getSettings();
    
    // Get system status
    static SystemStatus* getStatus();
    
    // Check if system is in safe mode
    static bool isInSafeMode();
    
    // Enter safe mode
    static void enterSafeMode(const String& reason);
    
    // Check for and handle boot loops
    static void checkBootLoops();
    
    // Get uptime in milliseconds
    static unsigned long getUptime();
    
    // Get free heap memory
    static uint32_t getFreeHeap();

private:
    // System settings
    static SystemSettings _settings;
    
    // System status
    static SystemStatus _status;
    
    // Preferences for persistent storage
    static Preferences _preferences;
    
    // Mutex for thread safety
    static std::mutex _systemMutex;
    
    // State variables
    static bool _initialized;
    static bool _running;
    static bool _inSafeMode;
    static hw_timer_t* _watchdogTimer;
    static unsigned long _startTime;
    
    // Watchdog callback
    static void IRAM_ATTR watchdogCallback();
    
    // Initialize hardware
    static bool initHardware();
    
    // Load default settings
    static void loadDefaultSettings();
    
    // Update system status
    static void updateStatus();
    
    // Boot counter management
    static uint32_t _bootCount;
    static unsigned long _lastBootTime;
    static void incrementBootCount();
};

#endif // ESP32_ARTNET_SYSTEM_MANAGER_H