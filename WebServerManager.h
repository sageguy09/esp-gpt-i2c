#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "ESP_GPT_I2C_Common.h"

// Forward declarations
void setupWebServer();
void logWithTimestamp(const String& message);
void handleSettings(AsyncWebServerRequest *request);
void handleLog(AsyncWebServerRequest *request);
void handleRootPage(AsyncWebServerRequest *request);
bool serveStaticFiles();
String generateEmbeddedHTML();
void handleConfigPost(AsyncWebServerRequest *request);

#endif // WEB_SERVER_MANAGER_H
