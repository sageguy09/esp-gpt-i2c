// UARTCommunicationBridge.h
// UART Communication Bridge for ESP32 ArtNet Controller
// Provides bidirectional communication between ESP32 and external devices
// Version: 0.1.0

#ifndef UART_COMMUNICATION_BRIDGE_H
#define UART_COMMUNICATION_BRIDGE_H

#include <Arduino.h>
#include <stdint.h>

// Command codes for communication protocol
#define CMD_ACK 0x01            // Acknowledgment
#define CMD_ERROR 0x02          // Error notification
#define CMD_SET_MODE 0x10       // Set operation mode
#define CMD_SET_BRIGHTNESS 0x11 // Set LED brightness
#define CMD_SET_COLOR 0x12      // Set static color
#define CMD_SET_ANIMATION 0x13  // Set animation parameters
#define CMD_GET_STATUS 0x20     // Request status information
#define CMD_DMX_DATA 0x30       // DMX data packet
#define CMD_SYSTEM_RESET 0x40   // Trigger system reset

// Error codes
#define ERR_NONE 0x00            // No error
#define ERR_INVALID_CMD 0x01     // Invalid command
#define ERR_INVALID_PARAM 0x02   // Invalid parameter
#define ERR_BUFFER_OVERFLOW 0x03 // Buffer overflow
#define ERR_TIMEOUT 0x04         // Communication timeout
#define ERR_CHECKSUM 0x05        // Checksum mismatch
#define ERR_LED_INIT 0x10        // LED initialization error
#define ERR_ARTNET_INIT 0x11     // ArtNet initialization error
#define ERR_WIFI_CONN 0x12       // WiFi connection error

// Maximum packet size
#define MAX_PACKET_SIZE 256

// Status codes for diagnostics
#define STATUS_IDLE 0x00
#define STATUS_RECEIVING 0x01
#define STATUS_PROCESSING 0x02
#define STATUS_SENDING 0x03
#define STATUS_ERROR 0xFF

class UARTCommunicationBridge
{
public:
    // Constructor takes UART port and baud rate
    UARTCommunicationBridge(HardwareSerial &serial = Serial2, uint32_t baudRate = 115200);

    // Initialization
    bool initializeCommunication();

    // Main processing functions
    bool processIncomingData();
    void sendStatusUpdate();
    void handleModeSwitch(uint8_t mode);

    // Diagnostic methods
    void printSystemDiagnostics();
    uint8_t getLastError() const { return lastError; }
    void clearErrors() { lastError = ERR_NONE; }

    // Callback registration for processing commands
    typedef void (*CommandCallback)(uint8_t cmd, uint8_t *data, uint16_t length);
    void setCommandCallback(CommandCallback callback) { commandCallback = callback; }

    // Error handling
    void setLastError(uint8_t error);
    bool sendErrorMessage(uint8_t errorCode, const char *message = nullptr);

    // Packet construction and parsing
    bool sendCommand(uint8_t command, uint8_t *data = nullptr, uint16_t length = 0);

    // Processing loop - call this in the main loop
    void update();

private:
    // Internal state variables
    HardwareSerial &uart;
    uint32_t baudRate;
    bool initialized;
    uint8_t lastError;
    unsigned long lastReceiveTime;
    unsigned long lastStatusUpdate;

    // Communication buffers
    uint8_t receiveBuffer[MAX_PACKET_SIZE];
    uint16_t receiveIndex;
    uint8_t packetBuffer[MAX_PACKET_SIZE];

    // Status tracking
    uint8_t currentStatus;
    uint32_t statusUpdateInterval; // in milliseconds
    uint32_t packetsSent;
    uint32_t packetsReceived;
    uint32_t errorCount;

    // Callback function
    CommandCallback commandCallback;

    // Internal helper methods
    uint8_t calculateChecksum(uint8_t *data, uint16_t length);
    bool validatePacket(uint8_t *packet, uint16_t length);
    void processPacket(uint8_t *packet, uint16_t length);
    void resetReceiveBuffer();
};

#endif // UART_COMMUNICATION_BRIDGE_H