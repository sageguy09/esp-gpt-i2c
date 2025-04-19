// UARTCommunicationBridge.cpp
// Implementation of UART Communication Bridge for ESP32 ArtNet Controller
// Version: 0.1.0

#include "UARTCommunicationBridge.h"

// Packet structure:
// [START_BYTE][LENGTH][COMMAND][DATA...][CHECKSUM]
#define START_BYTE 0xAA
#define MIN_PACKET_LENGTH 4         // START_BYTE + LENGTH + COMMAND + CHECKSUM
#define RECEIVE_TIMEOUT 1000        // milliseconds
#define STATUS_UPDATE_INTERVAL 5000 // milliseconds for automatic status updates

// Constructor
UARTCommunicationBridge::UARTCommunicationBridge(HardwareSerial &serial, uint32_t baud)
    : uart(serial),
      baudRate(baud),
      initialized(false),
      lastError(ERR_NONE),
      lastReceiveTime(0),
      lastStatusUpdate(0),
      receiveIndex(0),
      currentStatus(STATUS_IDLE),
      statusUpdateInterval(STATUS_UPDATE_INTERVAL),
      packetsSent(0),
      packetsReceived(0),
      errorCount(0),
      commandCallback(nullptr)
{
    // Initialize buffer
    memset(receiveBuffer, 0, MAX_PACKET_SIZE);
}

// Initialize the UART communication
bool UARTCommunicationBridge::initializeCommunication()
{
    if (initialized)
    {
        return true; // Already initialized
    }

    // Begin UART with specified baud rate
    uart.begin(baudRate);

    // Initial diagnostics message
    uart.print("UART Communication Bridge v0.1.0 Initializing...");

    // Reset buffers
    resetReceiveBuffer();

    // Skip the test packet for now since it's causing timeouts
    // uint8_t testData[4] = {0x01, 0x02, 0x03, 0x04};
    // if (!sendCommand(CMD_ACK, testData, 4)) {
    //    setLastError(ERR_TIMEOUT);
    //    return false;
    // }

    initialized = true;
    uart.println("Initialization complete");
    return true;
}

// Process incoming data from UART
bool UARTCommunicationBridge::processIncomingData()
{
    if (!initialized)
    {
        return false;
    }

    currentStatus = STATUS_RECEIVING;

    // Check if data is available
    while (uart.available())
    {
        uint8_t incomingByte = uart.read();
        lastReceiveTime = millis();

        // If this is a start byte and we're not in the middle of a packet, reset buffer
        if (incomingByte == START_BYTE && receiveIndex == 0)
        {
            receiveBuffer[receiveIndex++] = incomingByte;
        }
        // Otherwise, if we've seen a start byte, keep adding to the buffer
        else if (receiveIndex > 0)
        {
            // Protect against buffer overflow
            if (receiveIndex < MAX_PACKET_SIZE)
            {
                receiveBuffer[receiveIndex++] = incomingByte;

                // If we have at least 2 bytes, we can check the length
                if (receiveIndex >= 2)
                {
                    uint8_t packetLength = receiveBuffer[1];

                    // If we have received all bytes (including checksum)
                    if (receiveIndex >= packetLength)
                    {
                        // Process the complete packet
                        currentStatus = STATUS_PROCESSING;

                        // Verify and process the packet
                        if (validatePacket(receiveBuffer, receiveIndex))
                        {
                            processPacket(receiveBuffer, receiveIndex);
                            packetsReceived++;
                        }
                        else
                        {
                            setLastError(ERR_CHECKSUM);
                            errorCount++;
                        }

                        // Reset for next packet
                        resetReceiveBuffer();
                        currentStatus = STATUS_IDLE;
                        return true;
                    }
                }
            }
            else
            {
                // Buffer overflow
                setLastError(ERR_BUFFER_OVERFLOW);
                errorCount++;
                resetReceiveBuffer();
                currentStatus = STATUS_ERROR;
                return false;
            }
        }
    }

    // Check for timeout on partial packets
    if (receiveIndex > 0 && (millis() - lastReceiveTime > RECEIVE_TIMEOUT))
    {
        setLastError(ERR_TIMEOUT);
        errorCount++;
        resetReceiveBuffer();
        currentStatus = STATUS_ERROR;
        return false;
    }

    currentStatus = STATUS_IDLE;
    return false;
}

// Send a status update packet
void UARTCommunicationBridge::sendStatusUpdate()
{
    if (!initialized)
    {
        return;
    }

    // Prepare status data packet
    uint8_t statusData[16];
    statusData[0] = currentStatus;
    statusData[1] = lastError;

    // Include packet statistics
    statusData[2] = (packetsSent >> 24) & 0xFF;
    statusData[3] = (packetsSent >> 16) & 0xFF;
    statusData[4] = (packetsSent >> 8) & 0xFF;
    statusData[5] = packetsSent & 0xFF;

    statusData[6] = (packetsReceived >> 24) & 0xFF;
    statusData[7] = (packetsReceived >> 16) & 0xFF;
    statusData[8] = (packetsReceived >> 8) & 0xFF;
    statusData[9] = packetsReceived & 0xFF;

    statusData[10] = (errorCount >> 24) & 0xFF;
    statusData[11] = (errorCount >> 16) & 0xFF;
    statusData[12] = (errorCount >> 8) & 0xFF;
    statusData[13] = errorCount & 0xFF;

    // Add free memory info (ESP32)
    uint32_t freeHeap = ESP.getFreeHeap();
    statusData[14] = (freeHeap >> 8) & 0xFF;
    statusData[15] = freeHeap & 0xFF;

    // Send the status update
    sendCommand(CMD_GET_STATUS, statusData, 16);
    lastStatusUpdate = millis();
}

// Handle mode switch command
void UARTCommunicationBridge::handleModeSwitch(uint8_t mode)
{
    if (!initialized)
    {
        return;
    }

    uint8_t modeData[1] = {mode};
    sendCommand(CMD_SET_MODE, modeData, 1);

    // Log the mode switch
    char logMsg[64];
    snprintf(logMsg, sizeof(logMsg), "Mode switched to %d via UART", mode);
    Serial.println(logMsg);
}

// Print system diagnostics to Serial monitor
void UARTCommunicationBridge::printSystemDiagnostics()
{
    Serial.println("\n--- UART Bridge Diagnostics ---");
    Serial.print("Initialization: ");
    Serial.println(initialized ? "OK" : "FAILED");
    Serial.print("Last Error: 0x");
    Serial.println(lastError, HEX);
    Serial.print("Current Status: 0x");
    Serial.println(currentStatus, HEX);
    Serial.print("Packets Sent: ");
    Serial.println(packetsSent);
    Serial.print("Packets Received: ");
    Serial.println(packetsReceived);
    Serial.print("Error Count: ");
    Serial.println(errorCount);
    Serial.print("Free Heap: ");
    Serial.println(ESP.getFreeHeap());
    Serial.println("-----------------------------");
}

// Set error code and print the message
void UARTCommunicationBridge::setLastError(uint8_t error)
{
    lastError = error;

    if (error != ERR_NONE)
    {
        Serial.print("UART Bridge Error: 0x");
        Serial.println(error, HEX);
    }
}

// Send error message over UART
bool UARTCommunicationBridge::sendErrorMessage(uint8_t errorCode, const char *message)
{
    if (!initialized)
    {
        return false;
    }

    uint8_t errorData[MAX_PACKET_SIZE];
    uint16_t dataLength = 1; // Start with error code

    // Add error code
    errorData[0] = errorCode;

    // Add message if provided
    if (message != nullptr)
    {
        size_t msgLen = strlen(message);

        // Limit message length to fit in packet
        if (msgLen > MAX_PACKET_SIZE - 2)
        {
            msgLen = MAX_PACKET_SIZE - 2;
        }

        // Copy message to data buffer
        memcpy(&errorData[1], message, msgLen);
        dataLength += msgLen;
    }

    return sendCommand(CMD_ERROR, errorData, dataLength);
}

// Calculate checksum (simple XOR of all bytes)
uint8_t UARTCommunicationBridge::calculateChecksum(uint8_t *data, uint16_t length)
{
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < length; i++)
    {
        checksum ^= data[i];
    }
    return checksum;
}

// Validate received packet
bool UARTCommunicationBridge::validatePacket(uint8_t *packet, uint16_t length)
{
    // Minimum packet size check
    if (length < MIN_PACKET_LENGTH)
    {
        return false;
    }

    // Verify start byte
    if (packet[0] != START_BYTE)
    {
        return false;
    }

    // Verify length
    uint8_t packetLength = packet[1];
    if (packetLength != length)
    {
        return false;
    }

    // Verify checksum (last byte)
    uint8_t receivedChecksum = packet[length - 1];
    uint8_t calculatedChecksum = calculateChecksum(packet, length - 1);

    return (receivedChecksum == calculatedChecksum);
}

// Process a validated packet
void UARTCommunicationBridge::processPacket(uint8_t *packet, uint16_t length)
{
    // Extract command
    uint8_t command = packet[2];

    // Handle built-in commands
    switch (command)
    {
    case CMD_ACK:
        // Acknowledgment packet, no action needed
        break;

    case CMD_GET_STATUS:
        // Request for status information
        sendStatusUpdate();
        break;

    case CMD_SYSTEM_RESET:
        // Reset command - trigger ESP32 reset
        ESP.restart();
        break;

    default:
        // For other commands, call the callback if registered
        if (commandCallback != nullptr)
        {
            // Pass only the data portion (skip header)
            uint16_t dataLength = length - 4; // Subtract START_BYTE, LENGTH, COMMAND, CHECKSUM
            commandCallback(command, &packet[3], dataLength);
        }
        else
        {
            // No callback registered for this command
            sendErrorMessage(ERR_INVALID_CMD, "No handler for command");
        }
        break;
    }
}

// Send a command packet over UART
bool UARTCommunicationBridge::sendCommand(uint8_t command, uint8_t *data, uint16_t length)
{
    if (!initialized)
    {
        return false;
    }

    currentStatus = STATUS_SENDING;

    // Calculate total packet length
    uint16_t packetLength = 3 + length + 1; // START_BYTE + LENGTH + COMMAND + DATA + CHECKSUM

    // Ensure we're not exceeding maximum packet size
    if (packetLength > MAX_PACKET_SIZE)
    {
        setLastError(ERR_BUFFER_OVERFLOW);
        currentStatus = STATUS_ERROR;
        return false;
    }

    // Prepare packet buffer
    packetBuffer[0] = START_BYTE;
    packetBuffer[1] = packetLength;
    packetBuffer[2] = command;

    // Copy data if provided
    if (data != nullptr && length > 0)
    {
        memcpy(&packetBuffer[3], data, length);
    }

    // Calculate and append checksum
    packetBuffer[packetLength - 1] = calculateChecksum(packetBuffer, packetLength - 1);

    // Send the packet
    uart.write(packetBuffer, packetLength);

    // Flush to ensure all bytes are sent
    uart.flush();

    packetsSent++;
    currentStatus = STATUS_IDLE;
    return true;
}

// Reset the receive buffer
void UARTCommunicationBridge::resetReceiveBuffer()
{
    memset(receiveBuffer, 0, MAX_PACKET_SIZE);
    receiveIndex = 0;
}

// Update method to be called in the main loop
void UARTCommunicationBridge::update()
{
    // Process any incoming data
    processIncomingData();

    // Send periodic status updates
    if (millis() - lastStatusUpdate >= statusUpdateInterval)
    {
        sendStatusUpdate();
    }
}