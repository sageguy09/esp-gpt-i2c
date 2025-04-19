/**
 * NetworkManager.h
 * WiFi connection handling with AP fallback
 * 
 * Part of ESP32 ArtNet DMX LED Controller
 * Created: April 2025
 * 
 * This file implements a robust WiFi connection manager that handles
 * station mode connections with automatic fallback to Access Point mode.
 */

#ifndef ESP32_ARTNET_NETWORK_MANAGER_H
#define ESP32_ARTNET_NETWORK_MANAGER_H

#include "Config.h"
#include "Logger.h"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <mutex>

class NetworkManager {
public:
    // Initialize the network manager
    static bool init(SystemSettings* settings, SystemStatus* status);
    
    // Start network operations
    static bool start();
    
    // Stop network operations
    static bool stop();
    
    // Connect to WiFi in station mode
    static bool connectToWiFi();
    
    // Start AP mode
    static bool startAccessPoint();
    
    // Reconnect to WiFi if disconnected
    static bool reconnect();
    
    // Update network status (call periodically)
    static void update();
    
    // Get current network state
    static NetworkState getState();
    
    // Get IP address as string
    static String getIPAddress();
    
    // Check if connected to WiFi
    static bool isConnected();
    
    // Check if in AP mode
    static bool isInAPMode();
    
    // Set up mDNS responder
    static bool setupMDNS(const String& hostname);
    
    // Disable all network operations (for critical failures)
    static void disableNetworkOperations();
    
    // Get RSSI (WiFi signal strength)
    static int8_t getRSSI();
    
    // Configuration methods
    static void setHostname(const String& hostname);
    static void setCredentials(const String& ssid, const String& password);
    static void setApFallback(bool enable);

private:
    // Network task handler
    static TaskHandle_t _networkTaskHandle;
    static void networkTask(void* parameter);
    
    // Watchdog handler
    static hw_timer_t* _watchdogTimer;
    static void IRAM_ATTR watchdogCallback();
    
    // Event handlers
    static void WiFiEvent(WiFiEvent_t event);
    
    // Internal state
    static NetworkState _networkState;
    static bool _apFallbackEnabled;
    static bool _networkInitialized;
    static bool _networkInitFailed;
    static unsigned long _lastReconnectAttempt;
    static unsigned long _apStartTime;
    static String _hostname;
    static String _ssid;
    static String _password;
    static SystemSettings* _settings;
    static SystemStatus* _status;
    static std::mutex _networkMutex;
    
    // TCP/IP stack initialization
    static bool initTcpIpStack();
    
    // Calculate unique AP name with MAC address suffix
    static String generateAPName();
    
    // Update system status
    static void updateStatus();
};

#endif // ESP32_ARTNET_NETWORK_MANAGER_H