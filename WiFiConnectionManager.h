#ifndef WIFI_CONNECTION_MANAGER_H
#define WIFI_CONNECTION_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include "ESP_GPT_I2C_Common.h"

// Function declarations
void setupWiFiWithLogging();
void monitorWiFiConnection();
String wifiStatusToString(wl_status_t status);
void reconnectIfNeeded();
bool initializeNetwork();

// WiFi status monitoring info
struct WiFiStatusInfo {
  bool isConnected;
  int reconnectAttempts;
  unsigned long lastReconnectAttempt;
  int signalStrength;
  String ipAddress;
  String macAddress;
};

extern WiFiStatusInfo wifiStatus;

#endif // WIFI_CONNECTION_MANAGER_H
