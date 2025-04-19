/**
 * Logger.cpp
 * Logging utilities implementation
 * 
 * Part of ESP32 ArtNet DMX LED Controller
 * Created: April 2025
 */

#include "Logger.h"
#include <ArduinoJson.h>

// Initialize static members
std::vector<LogEntry> Logger::_logs;
int Logger::_logIndex = 0;
uint8_t Logger::_logLevel = LOG_LEVEL;
bool Logger::_serialOutput = LOG_TO_SERIAL;
std::mutex Logger::_logMutex;

// Initialize the logger
void Logger::init() {
    // Reserve memory for logs to prevent reallocations
    _logs.reserve(MAX_LOG_ENTRIES);
    
    if (_serialOutput) {
        Serial.begin(115200);
        // Small delay to ensure Serial is ready
        delay(100);
        Serial.println(F("Logger initialized"));
    }
}

// Log a message with specified level
void Logger::log(uint8_t level, const String& message) {
    // Skip logging if message is below current log level
    if (level < _logLevel) {
        return;
    }
    
    // Get current timestamp
    unsigned long timestamp = millis();
    
    // Output to Serial if enabled
    if (_serialOutput) {
        Serial.print(formatTimestamp(timestamp));
        Serial.print(F(" ["));
        Serial.print(levelToString(level));
        Serial.print(F("] "));
        Serial.println(message);
    }
    
    // Lock the mutex to ensure thread safety
    std::lock_guard<std::mutex> lock(_logMutex);
    
    // Create a new log entry
    LogEntry entry;
    entry.timestamp = timestamp;
    entry.level = level;
    entry.message = message;
    
    // Store the log entry
    if (_logs.size() < MAX_LOG_ENTRIES) {
        _logs.push_back(entry);
    } else {
        // Replace oldest entry in circular buffer style
        _logs[_logIndex] = entry;
        _logIndex = (_logIndex + 1) % MAX_LOG_ENTRIES;
    }
}

// Convenience methods for different log levels
void Logger::verbose(const String& message) {
    log(LOG_LEVEL_VERBOSE, message);
}

void Logger::debug(const String& message) {
    log(LOG_LEVEL_DEBUG, message);
}

void Logger::info(const String& message) {
    log(LOG_LEVEL_INFO, message);
}

void Logger::warning(const String& message) {
    log(LOG_LEVEL_WARNING, message);
}

void Logger::error(const String& message) {
    log(LOG_LEVEL_ERROR, message);
}

// Get the most recent log entries as a JSON string
String Logger::getLogsAsJson(int count) {
    StaticJsonDocument<4096> doc;
    JsonArray logsArray = doc.createNestedArray("logs");
    
    // Lock the mutex to ensure thread safety
    std::lock_guard<std::mutex> lock(_logMutex);
    
    // Determine how many logs to return
    int logCount = std::min(static_cast<int>(_logs.size()), count);
    if (logCount == 0) {
        // Return empty array if no logs
        String result;
        serializeJson(doc, result);
        return result;
    }
    
    // Find the starting index for most recent logs
    int startIdx;
    if (_logs.size() < MAX_LOG_ENTRIES) {
        // Not filled up yet, start from 0
        startIdx = 0;
    } else {
        // Calculate starting index to get most recent entries
        startIdx = (_logIndex - logCount + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    }
    
    // Add log entries to JSON
    for (int i = 0; i < logCount; i++) {
        int idx = (startIdx + i) % std::min(static_cast<int>(_logs.size()), MAX_LOG_ENTRIES);
        JsonObject logObj = logsArray.createNestedObject();
        logObj["time"] = _logs[idx].timestamp;
        logObj["level"] = _logs[idx].level;
        logObj["levelStr"] = levelToString(_logs[idx].level);
        logObj["message"] = _logs[idx].message;
    }
    
    String result;
    serializeJson(doc, result);
    return result;
}

// Get the most recent log entries as a formatted string
String Logger::getLogsAsText(int count) {
    String result = "";
    
    // Lock the mutex to ensure thread safety
    std::lock_guard<std::mutex> lock(_logMutex);
    
    // Determine how many logs to return
    int logCount = std::min(static_cast<int>(_logs.size()), count);
    if (logCount == 0) {
        return "No logs available";
    }
    
    // Find the starting index for most recent logs
    int startIdx;
    if (_logs.size() < MAX_LOG_ENTRIES) {
        // Not filled up yet, start from 0
        startIdx = 0;
    } else {
        // Calculate starting index to get most recent entries
        startIdx = (_logIndex - logCount + MAX_LOG_ENTRIES) % MAX_LOG_ENTRIES;
    }
    
    // Add log entries to result string
    for (int i = 0; i < logCount; i++) {
        int idx = (startIdx + i) % std::min(static_cast<int>(_logs.size()), MAX_LOG_ENTRIES);
        result += formatTimestamp(_logs[idx].timestamp);
        result += " [";
        result += levelToString(_logs[idx].level);
        result += "] ";
        result += _logs[idx].message;
        result += "\n";
    }
    
    return result;
}

// Clear all log entries
void Logger::clearLogs() {
    std::lock_guard<std::mutex> lock(_logMutex);
    _logs.clear();
    _logIndex = 0;
}

// Get the number of stored log entries
int Logger::getLogCount() {
    std::lock_guard<std::mutex> lock(_logMutex);
    return _logs.size();
}

// Enable/disable Serial output
void Logger::setSerialOutput(bool enabled) {
    _serialOutput = enabled;
}

// Set log level (messages below this level will be ignored)
void Logger::setLogLevel(uint8_t level) {
    _logLevel = level;
}

// Get log level as string
String Logger::levelToString(uint8_t level) {
    switch (level) {
        case LOG_LEVEL_VERBOSE:
            return F("VERBOSE");
        case LOG_LEVEL_DEBUG:
            return F("DEBUG");
        case LOG_LEVEL_INFO:
            return F("INFO");
        case LOG_LEVEL_WARNING:
            return F("WARNING");
        case LOG_LEVEL_ERROR:
            return F("ERROR");
        default:
            return F("UNKNOWN");
    }
}

// Format timestamp for display
String Logger::formatTimestamp(unsigned long timestamp) {
    // Convert milliseconds to more readable format
    unsigned long seconds = timestamp / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    
    // Format as HH:MM:SS.mmm
    char buffer[16];
    sprintf(buffer, "%02lu:%02lu:%02lu.%03lu", 
            hours % 24, 
            minutes % 60, 
            seconds % 60, 
            timestamp % 1000);
    
    return String(buffer);
}