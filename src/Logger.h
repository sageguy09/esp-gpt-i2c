/**
 * Logger.h
 * Logging utilities with timestamp and severity levels
 * 
 * Part of ESP32 ArtNet DMX LED Controller
 * Created: April 2025
 * 
 * This file implements a flexible logging system with multiple verbosity
 * levels, timestamps, and support for both serial output and in-memory logs.
 */

#ifndef ESP32_ARTNET_LOGGER_H
#define ESP32_ARTNET_LOGGER_H

#include "Config.h"
#include <Arduino.h>
#include <vector>
#include <mutex>

class Logger {
public:
    // Initialize the logger
    static void init();
    
    // Log a message with specified level
    static void log(uint8_t level, const String& message);
    
    // Convenience methods for different log levels
    static void verbose(const String& message);
    static void debug(const String& message);
    static void info(const String& message);
    static void warning(const String& message);
    static void error(const String& message);
    
    // Get the most recent log entries as a JSON string
    static String getLogsAsJson(int count = MAX_LOG_ENTRIES);
    
    // Get the most recent log entries as a formatted string
    static String getLogsAsText(int count = MAX_LOG_ENTRIES);
    
    // Clear all log entries
    static void clearLogs();
    
    // Get the number of stored log entries
    static int getLogCount();
    
    // Enable/disable Serial output
    static void setSerialOutput(bool enabled);
    
    // Set log level (messages below this level will be ignored)
    static void setLogLevel(uint8_t level);

private:
    static std::vector<LogEntry> _logs;
    static int _logIndex;
    static uint8_t _logLevel;
    static bool _serialOutput;
    static std::mutex _logMutex;
    
    // Get log level as string
    static String levelToString(uint8_t level);
    
    // Format timestamp for display
    static String formatTimestamp(unsigned long timestamp);
};

// Macro for including file and line information in logs
#define LOG_VERBOSE(msg) Logger::verbose("[" + String(__FILE__) + ":" + String(__LINE__) + "] " + msg)
#define LOG_DEBUG(msg)   Logger::debug("[" + String(__FILE__) + ":" + String(__LINE__) + "] " + msg)
#define LOG_INFO(msg)    Logger::info(msg)
#define LOG_WARNING(msg) Logger::warning(msg)
#define LOG_ERROR(msg)   Logger::error("[" + String(__FILE__) + ":" + String(__LINE__) + "] " + msg)

#endif // ESP32_ARTNET_LOGGER_H