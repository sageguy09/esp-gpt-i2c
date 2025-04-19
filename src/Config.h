/**
 * Config.h
 * System-wide configuration parameters and structures
 * 
 * Part of ESP32 ArtNet DMX LED Controller
 * Created: April 2025
 * 
 * This file contains all the configuration parameters, constants,
 * and data structures used throughout the application.
 */

#ifndef ESP32_ARTNET_CONFIG_H
#define ESP32_ARTNET_CONFIG_H

#include <Arduino.h>
#include <FastLED.h>

// =========================================================================
// SYSTEM CONFIGURATION
// =========================================================================

// Hardware target configuration
#define TARGET_ESP32_S3 0       // Set to 1 if using ESP32-S3
#define TARGET_ESP32_WROOM 1    // Set to 1 if using ESP32-WROOM

// Debug and logging configuration
#define DEBUG_ENABLED true
#define LOG_TO_SERIAL true
#define LOG_LEVEL_VERBOSE 0
#define LOG_LEVEL_DEBUG 1
#define LOG_LEVEL_INFO 2
#define LOG_LEVEL_WARNING 3
#define LOG_LEVEL_ERROR 4
#define LOG_LEVEL LOG_LEVEL_INFO
#define MAX_LOG_ENTRIES 50

// Core allocation for tasks
#define NETWORK_CORE 0          // Core for network operations
#define LED_CONTROL_CORE 1      // Core for LED operations

// Task priorities
#define WIFI_TASK_PRIORITY 5
#define LED_TASK_PRIORITY 4
#define MDNS_TASK_PRIORITY 3

// Watchdog configuration
#define WATCHDOG_TIMEOUT_MS 5000

// =========================================================================
// NETWORK CONFIGURATION
// =========================================================================

// WiFi configuration
#define WIFI_RECONNECT_INTERVAL_MS 30000  // 30 seconds between reconnection attempts
#define WIFI_CONNECT_TIMEOUT_MS 15000     // 15 seconds timeout for WiFi connection
#define WIFI_AP_FALLBACK_ENABLED true     // Enable Access Point fallback if station connection fails
#define WIFI_AP_NAME_PREFIX "ESP32-ArtNet-"  // AP name prefix, will append chip ID
#define WIFI_AP_PASSWORD "artnet12345"    // Default AP password
#define WIFI_AP_CHANNEL 1                 // WiFi AP channel
#define WIFI_AP_MAX_CONNECTIONS 4         // Maximum connections to the AP
#define WIFI_AP_TIMEOUT_MS 3600000        // 1 hour timeout for AP mode (0 for no timeout)

// ArtNet configuration
#define ARTNET_PORT 6454
#define ARTNET_UNIVERSE_START 0
#define ARTNET_NUM_UNIVERSES 1
#define UNIVERSE_SIZE 510       // DMX universe size (512 - 2 start bytes)

// mDNS configuration
#define MDNS_SERVICE_NAME "artnet"
#define MDNS_PROTOCOL "udp"
#define MDNS_DEVICE_NAME "ESP32-ArtNet"

// =========================================================================
// LED CONFIGURATION
// =========================================================================

// LED hardware configuration
#define MAX_LED_STRIPS 4            // Maximum number of LED strips
#define MAX_LEDS_PER_STRIP 300      // Maximum number of LEDs per strip
#define STRIPS_PER_PIN 3            // Number of strips per output pin (for I2SClocklessLedDriver)
#define DEFAULT_LED_COUNT 144       // Default number of LEDs per strip
#define DEFAULT_BRIGHTNESS 128      // Default brightness (0-255)
#define NB_CHANNEL_PER_LED 3        // RGB LEDs use 3 channels per LED

// Recommended pins for LED data
#define DEFAULT_LED_PINS {12, 14, 2, 4}  // Default pins for LED data

// Status LED configuration (onboard LED or separate status LED)
#define STATUS_LED_PIN 16
#define STATUS_LED_ENABLED true

// =========================================================================
// STORAGE CONFIGURATION
// =========================================================================

// Preferences namespace for persistent storage
#define PREFERENCES_NAMESPACE "artnet-cfg"
#define BOOT_COUNT_KEY "bootCnt"
#define LAST_BOOT_TIME_KEY "lastBoot"
#define NETWORK_FAILURE_KEY "netFailed"

// =========================================================================
// SYSTEM STRUCTURES
// =========================================================================

// RGB color structure (3 bytes)
typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
} rgb24;

// System operating modes
enum OperatingMode {
  MODE_ARTNET = 0,       // Receive colors via ArtNet DMX
  MODE_STATIC = 1,       // Display a single static color
  MODE_EFFECT = 2,       // Run a built-in effect
  MODE_TEST = 3,         // Test/diagnostics mode
  MODE_SAFE = 4          // Safe mode (minimal functionality)
};

// Effect types for MODE_EFFECT
enum EffectType {
  EFFECT_RAINBOW = 0,    // Rainbow color cycle
  EFFECT_PULSE = 1,      // Pulsing brightness effect
  EFFECT_FIRE = 2,       // Fire simulation effect
  EFFECT_CHASE = 3,      // Chase effect
  EFFECT_TWINKLE = 4     // Twinkle/random effect
};

// Network connection states
enum NetworkState {
  NETWORK_INITIALIZING = 0,   // Network initialization in progress
  NETWORK_CONNECTING = 1,     // Attempting to connect
  NETWORK_CONNECTED = 2,      // Connected to WiFi network
  NETWORK_DISCONNECTED = 3,   // Disconnected, will try to reconnect
  NETWORK_AP_MODE = 4,        // Access point mode active
  NETWORK_FAILED = 5,         // Network initialization failed
  NETWORK_DISABLED = 6        // Network functionality disabled
};

// System settings structure (stored in preferences)
struct SystemSettings {
  // WiFi settings
  String ssid;
  String password;
  bool useWiFi;
  bool createAP;
  
  // Device identification
  String deviceName;
  
  // ArtNet settings
  bool useArtnet;
  uint16_t artnetUniverse;
  
  // LED settings
  uint8_t mode;                 // Current operating mode (OperatingMode enum)
  uint8_t effectType;           // Current effect type (EffectType enum)
  uint8_t effectSpeed;          // Effect speed (1-255)
  uint16_t numStrips;           // Number of LED strips
  uint16_t ledsPerStrip;        // LEDs per strip
  int pins[MAX_LED_STRIPS];     // Output pins for each strip
  uint8_t brightness;           // Global brightness (0-255)
  rgb24 staticColor;            // Static color for MODE_STATIC
  
  // System settings
  bool safeMode;                // Safe mode flag
  uint32_t bootCount;           // Number of boots
};

// System status structure
struct SystemStatus {
  // Network status
  NetworkState networkState;
  String ipAddress;
  int8_t rssi;                  // WiFi signal strength
  unsigned long lastConnectAttempt;
  bool artnetRunning;
  
  // ArtNet statistics
  uint32_t artnetPacketCount;
  unsigned long lastArtnetPacket;
  
  // System statistics
  unsigned long uptime;         // System uptime in milliseconds
  uint32_t freeHeap;            // Free heap memory
  uint32_t minFreeHeap;         // Minimum free heap memory
  float cpuTemperature;         // CPU temperature
  
  // Safe mode status
  bool isInSafeMode;
  
  // Task status
  bool ledTaskRunning;
  bool networkTaskRunning;
};

// Log entry structure
struct LogEntry {
  unsigned long timestamp;      // Time since boot in ms
  uint8_t level;                // Log level
  String message;               // Log message
};

#endif // ESP32_ARTNET_CONFIG_H