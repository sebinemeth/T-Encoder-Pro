/*
 * @Description: Home Assistant WebSocket Manager
 * @Author: WARP
 * @Date: 2024-10-08
 * @License: GPL 3.0
 */
#pragma once

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include "ha_config.h"
#include "ha_websocket_config.h"
#include "entities.h"

class HAWebSocketManager {
private:
    AsyncClient* client;
    WebSocketState currentState;
    unsigned long lastHeartbeat;
    unsigned long lastReconnectAttempt;
    unsigned long connectionStartTime;
    int nextMessageId;
    bool subscriptionActive;
    String lastError;
    
    // WebSocket frame parsing
    bool isFrameComplete;
    uint8_t frameBuffer[WS_MAX_MESSAGE_SIZE];
    size_t frameBufferPos;
    size_t expectedFrameLength;
    bool frameMasked;
    uint8_t maskKey[4];
    
    // Message queue for sending
    struct PendingMessage {
        String message;
        unsigned long timestamp;
        bool sent;
    };
    std::vector<PendingMessage> pendingMessages;
    
public:
    HAWebSocketManager() : client(nullptr), currentState(WS_STATE_DISCONNECTED), 
                          lastHeartbeat(0), lastReconnectAttempt(0), 
                          connectionStartTime(0), nextMessageId(10), 
                          subscriptionActive(false), isFrameComplete(false),
                          frameBufferPos(0), expectedFrameLength(0), frameMasked(false) {}
    
    ~HAWebSocketManager() {
        disconnect();
    }
    
    // Public interface
    bool begin() {
        Serial.println("[WS] Initializing WebSocket manager");
        currentState = WS_STATE_DISCONNECTED;
        return true;
    }
    
    void loop() {
        unsigned long now = millis();
        
        switch (currentState) {
            case WS_STATE_DISCONNECTED:
                if (WiFi.status() == WL_CONNECTED && 
                    (now - lastReconnectAttempt > WS_RECONNECT_INTERVAL)) {
                    connect();
                    lastReconnectAttempt = now;
                }
                break;
                
            case WS_STATE_CONNECTING:
                if (now - connectionStartTime > WS_CONNECTION_TIMEOUT) {
                    Serial.println("[WS] Connection timeout");
                    disconnect();
                }
                break;
                
            case WS_STATE_READY:
                // Send heartbeat
                if (now - lastHeartbeat > WS_HEARTBEAT_INTERVAL) {
                    sendPing();
                    lastHeartbeat = now;
                }
                break;
                
            default:
                // Check for authentication/subscription timeouts
                if (now - connectionStartTime > WS_RESPONSE_TIMEOUT) {
                    Serial.println("[WS] Response timeout in state: " + String(getWebSocketStateString(currentState)));
                    disconnect();
                }
                break;
        }
        
        // Process pending messages
        processPendingMessages();
    }
    
    bool isReady() const {
        return currentState == WS_STATE_READY;
    }
    
    bool isConnected() const {
        return currentState >= WS_STATE_CONNECTED;
    }
    
    WebSocketState getState() const {
        return currentState;
    }
    
    String getStateString() const {
        return String(getWebSocketStateString(currentState));
    }
    
    String getLastError() const {
        return lastError;
    }
    
    void disconnect() {
        Serial.println("[WS] Disconnecting");
        if (client) {
            client->close();
            delete client;
            client = nullptr;
        }
        currentState = WS_STATE_DISCONNECTED;
        subscriptionActive = false;
        clearPendingMessages();
    }

private:
    bool connect() {
        if (currentState != WS_STATE_DISCONNECTED) {
            return false;
        }
        
        Serial.println("[WS] Connecting to Home Assistant");
        currentState = WS_STATE_CONNECTING;
        connectionStartTime = millis();
        
        // Parse HA_URL to get host and port
        String url = String(HA_URL);
        if (url.startsWith("http://")) {
            url = url.substring(7);
        } else if (url.startsWith("https://")) {
            url = url.substring(8);
        }
        
        // Remove trailing slash
        if (url.endsWith("/")) {
            url = url.substring(0, url.length() - 1);
        }
        
        // Find port
        int portIndex = url.indexOf(':');
        String host;
        int port = 8123; // Default HA port
        
        if (portIndex > 0) {
            host = url.substring(0, portIndex);
            port = url.substring(portIndex + 1).toInt();
        } else {
            host = url;
        }
        
        Serial.println("[WS] Connecting to " + host + ":" + String(port));
        
        client = new AsyncClient();
        
        client->onConnect([this](void* arg, AsyncClient* c) {
            this->onConnected();
        });
        
        client->onData([this](void* arg, AsyncClient* c, void* data, size_t len) {
            this->onData((uint8_t*)data, len);
        });
        
        client->onDisconnect([this](void* arg, AsyncClient* c) {
            this->onDisconnected();
        });
        
        client->onError([this](void* arg, AsyncClient* c, int8_t error) {
            this->onError(error);
        });
        
        return client->connect(host.c_str(), port);
    }
    
    void onConnected() {
        Serial.println("[WS] TCP connected, performing WebSocket handshake");
        currentState = WS_STATE_CONNECTED;
        
        String host = String(HA_URL);
        if (host.startsWith("http://")) {
            host = host.substring(7);
        } else if (host.startsWith("https://")) {
            host = host.substring(8);
        }
        if (host.endsWith("/")) {
            host = host.substring(0, host.length() - 1);
        }
        if (host.indexOf(':') > 0) {
            host = host.substring(0, host.indexOf(':'));
        }
        
        // Send WebSocket handshake
        String handshake = "GET /api/websocket HTTP/1.1\r\n";
        handshake += "Host: " + host + "\r\n";
        handshake += "Upgrade: websocket\r\n";
        handshake += "Connection: Upgrade\r\n";
        handshake += "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n";
        handshake += "Sec-WebSocket-Version: 13\r\n";
        handshake += "\r\n";
        
        client->write(handshake.c_str(), handshake.length());
    }
    
    void onData(uint8_t* data, size_t len) {
        // Handle HTTP handshake response first
        if (currentState == WS_STATE_CONNECTED) {
            String response = String((char*)data);
            if (response.indexOf("HTTP/1.1 101") >= 0 && response.indexOf("websocket") >= 0) {
                Serial.println("[WS] WebSocket handshake successful");
                currentState = WS_STATE_AUTHENTICATING;
                connectionStartTime = millis(); // Reset timeout for authentication
                // Send authentication immediately after handshake
                sendAuthentication();
            } else {
                Serial.println("[WS] WebSocket handshake failed");
                Serial.println(response);
                lastError = "WebSocket handshake failed";
                disconnect();
            }
            return;
        }
        
        // Debug: Log raw WebSocket data
        Serial.print("[WS] Raw data (" + String(len) + " bytes): ");
        for (size_t i = 0; i < len && i < 50; i++) { // Log first 50 bytes
            Serial.printf("%02X ", data[i]);
        }
        if (len > 50) Serial.print("...");
        Serial.println();
        
        // Process WebSocket frames
        processWebSocketData(data, len);
    }
    
    void processWebSocketData(uint8_t* data, size_t len) {
        Serial.println("[WS] Processing " + String(len) + " bytes of WebSocket data");
        
        for (size_t i = 0; i < len; i++) {
            frameBuffer[frameBufferPos++] = data[i];
            
            if (frameBufferPos >= WS_MAX_MESSAGE_SIZE) {
                Serial.println("[WS] Frame buffer overflow");
                frameBufferPos = 0;
                expectedFrameLength = 0;
                return;
            }
            
            // Parse WebSocket frame header - need at least 2 bytes
            if (frameBufferPos >= 2 && expectedFrameLength == 0) {
                uint8_t firstByte = frameBuffer[0];
                uint8_t secondByte = frameBuffer[1];
                
                bool fin = (firstByte & 0x80) != 0;
                uint8_t opcode = firstByte & 0x0F;
                frameMasked = (secondByte & 0x80) != 0;
                uint8_t payloadLen = secondByte & 0x7F;
                
                Serial.println("[WS] Frame: FIN=" + String(fin) + ", opcode=" + String(opcode) + ", masked=" + String(frameMasked) + ", len=" + String(payloadLen));
                
                size_t headerSize = 2;
                
                // Handle extended payload lengths
                if (payloadLen == 126) {
                    if (frameBufferPos >= 4) {
                        expectedFrameLength = (frameBuffer[2] << 8) | frameBuffer[3];
                        headerSize = 4;
                    } else {
                        continue; // Need more data for extended length
                    }
                } else if (payloadLen == 127) {
                    Serial.println("[WS] 64-bit length frames not supported");
                    frameBufferPos = 0;
                    expectedFrameLength = 0;
                    return;
                } else {
                    expectedFrameLength = payloadLen;
                }
                
                // Handle masking key (only for client-to-server frames)
                if (frameMasked) {
                    if (frameBufferPos >= headerSize + 4) {
                        memcpy(maskKey, &frameBuffer[headerSize], 4);
                        headerSize += 4;
                    } else {
                        continue; // Need more data for mask
                    }
                }
                
                Serial.println("[WS] Expected payload length: " + String(expectedFrameLength) + ", header size: " + String(headerSize));
            }
            
            // Check if we have a complete frame
            if (expectedFrameLength > 0) {
                size_t headerSize = frameMasked ? 6 : 2;
                if (expectedFrameLength > 125) {
                    headerSize = frameMasked ? 8 : 4; // Extended length
                }
                
                Serial.println("[WS] Frame check: bufferPos=" + String(frameBufferPos) + ", headerSize=" + String(headerSize) + ", expected=" + String(expectedFrameLength));
                
                if (frameBufferPos >= headerSize + expectedFrameLength) {
                    uint8_t* payload = &frameBuffer[headerSize];
                    
                    // Unmask payload if needed (server frames are typically unmasked)
                    if (frameMasked) {
                        for (size_t j = 0; j < expectedFrameLength; j++) {
                            payload[j] ^= maskKey[j % 4];
                        }
                    }
                    
                    uint8_t opcode = frameBuffer[0] & 0x0F;
                    Serial.println("[WS] Processing frame with opcode: " + String(opcode) + " (0x" + String(frameBuffer[0], HEX) + ")");
                    
                    // Process the complete frame
                    if (opcode == 1) { // Text frame
                        Serial.println("[WS] Processing text frame of " + String(expectedFrameLength) + " bytes");
                        processTextMessage(payload, expectedFrameLength);
                    } else if (opcode == 8) { // Close frame
                        Serial.println("[WS] Received close frame");
                        disconnect();
                        return;
                    } else if (opcode == 9) { // Ping frame
                        Serial.println("[WS] Received ping frame");
                        sendPong(payload, expectedFrameLength);
                    } else if (opcode == 10) { // Pong frame
                        Serial.println("[WS] Received pong frame");
                    } else {
                        Serial.println("[WS] Unknown opcode: " + String(opcode));
                    }
                    
                    // Reset for next frame
                    frameBufferPos = 0;
                    expectedFrameLength = 0;
                    frameMasked = false;
                    break; // Process one frame at a time
                }
            }
        }
    }
    
    void processTextMessage(uint8_t* payload, size_t length) {
        // Null-terminate the message
        char messageBuffer[WS_MAX_MESSAGE_SIZE];
        memcpy(messageBuffer, payload, length);
        messageBuffer[length] = '\0';
        
        Serial.println("[WS] Received: " + String(messageBuffer));
        
        // Parse JSON message
        DynamicJsonDocument doc(WS_MAX_MESSAGE_SIZE);
        DeserializationError error = deserializeJson(doc, messageBuffer);
        
        if (error) {
            Serial.println("[WS] JSON parse error: " + String(error.c_str()));
            return;
        }
        
        String type = doc["type"] | "";
        
        if (type == HA_WS_TYPE_AUTH_REQUIRED) {
            sendAuthentication();
        } else if (type == HA_WS_TYPE_AUTH_OK) {
            Serial.println("[WS] Authentication successful");
            currentState = WS_STATE_AUTHENTICATED;
            subscribeToStateChanges();
        } else if (type == HA_WS_TYPE_AUTH_INVALID) {
            Serial.println("[WS] Authentication failed");
            lastError = "Authentication failed - check token";
            currentState = WS_STATE_ERROR;
        } else if (type == HA_WS_TYPE_RESULT) {
            handleResultMessage(doc);
        } else if (type == HA_WS_TYPE_EVENT) {
            handleEventMessage(doc);
        }
    }
    
    void sendAuthentication() {
        Serial.println("[WS] Sending authentication");
        
        DynamicJsonDocument doc(512);
        doc["type"] = "auth";
        doc["access_token"] = String(HA_TOKEN);
        
        String message;
        serializeJson(doc, message);
        
        // Debug output (truncated token for security)
        String debugToken = String(HA_TOKEN);
        if (debugToken.length() > 10) {
            debugToken = debugToken.substring(0, 10) + "..." + debugToken.substring(debugToken.length() - 10);
        }
        Serial.println("[WS] Auth token: " + debugToken);
        Serial.println("[WS] Auth message: " + message);
        
        sendWebSocketMessage(message);
    }
    
    void subscribeToStateChanges() {
        Serial.println("[WS] Subscribing to state changes");
        currentState = WS_STATE_SUBSCRIBING;
        
        DynamicJsonDocument doc(512);
        doc["id"] = nextMessageId++;
        doc["type"] = "subscribe_events";
        doc["event_type"] = "state_changed";
        
        String message;
        serializeJson(doc, message);
        sendWebSocketMessage(message);
    }
    
    void handleResultMessage(const DynamicJsonDocument& doc) {
        int id = doc["id"] | 0;
        (void)id; // Suppress unused variable warning
        bool success = doc["success"] | false;
        
        if (success) {
            if (currentState == WS_STATE_SUBSCRIBING) {
                Serial.println("[WS] Successfully subscribed to events");
                currentState = WS_STATE_READY;
                subscriptionActive = true;
                // Get initial states for all entities
                getInitialStates();
            }
        } else {
            Serial.println("[WS] Command failed: " + String(doc["error"]["message"] | "Unknown error"));
            lastError = doc["error"]["message"] | "Command failed";
        }
    }
    
    void handleEventMessage(const DynamicJsonDocument& doc) {
        if (!doc["event"].is<JsonObject>()) return;
        
        String eventType = doc["event"]["event_type"] | "";
        
        if (eventType == "state_changed") {
            String entityId = doc["event"]["data"]["entity_id"] | "";
            
            // Find the entity in our list
            for (int i = 0; i < ENTITY_COUNT; i++) {
                if (entities[i].entity_id == entityId) {
                    // Create a new document for the new_state data
                    DynamicJsonDocument newStateDoc(1024);
                    newStateDoc = doc["event"]["data"]["new_state"];
                    updateEntityFromWebSocket(i, newStateDoc);
                    break;
                }
            }
        }
    }
    
    void updateEntityFromWebSocket(int entityIndex, const DynamicJsonDocument& doc) {
        if (entityIndex < 0 || entityIndex >= ENTITY_COUNT) return;
        
        String state = doc["state"] | "";
        Serial.println("[WS] Entity " + entities[entityIndex].name + " state: " + state);
        
        // Update entity based on type - similar to fetchEntityState but from WebSocket data
        switch (entities[entityIndex].type) {
            case ENTITY_TOGGLE: {
                bool newState = (state == "on");
                if (entities[entityIndex].state != newState) {
                    entities[entityIndex].state = newState;
                    entities[entityIndex].lastState = !newState; // Force UI update
                }
                break;
            }
            case ENTITY_COVER: {
                entities[entityIndex].coverState = state;
                if (doc["attributes"]["current_position"].is<int>()) {
                    int newPosition = doc["attributes"]["current_position"];
                    if (entities[entityIndex].position != newPosition) {
                        entities[entityIndex].position = newPosition;
                        entities[entityIndex].lastPosition = -1; // Force UI update
                    }
                }
                break;
            }
            case ENTITY_THERMOSTAT: {
                if (doc["attributes"]["current_temperature"].is<float>()) {
                    entities[entityIndex].currentTemp = doc["attributes"]["current_temperature"];
                }
                if (doc["attributes"]["temperature"].is<float>()) {
                    float newTarget = doc["attributes"]["temperature"];
                    if (abs(entities[entityIndex].targetTemp - newTarget) > 0.1) {
                        entities[entityIndex].targetTemp = newTarget;
                        entities[entityIndex].lastTargetTemp = -1; // Force UI update
                    }
                }
                entities[entityIndex].hvacMode = doc["attributes"]["hvac_mode"] | "off";
                entities[entityIndex].hvacAction = doc["attributes"]["hvac_action"] | "idle";
                break;
            }
            case ENTITY_SENSOR: {
                entities[entityIndex].sensorValue = state;
                entities[entityIndex].sensorUnit = doc["attributes"]["unit_of_measurement"] | "";
                break;
            }
            case ENTITY_FAN: {
                bool fanOn = (state == "on");
                int newSpeed = 0;
                if (fanOn && doc["attributes"]["percentage"].is<int>()) {
                    newSpeed = doc["attributes"]["percentage"];
                }
                if (entities[entityIndex].fanSpeed != newSpeed) {
                    entities[entityIndex].fanSpeed = newSpeed;
                    entities[entityIndex].lastFanSpeed = -1; // Force UI update
                }
                break;
            }
        }
        
        // Set icon if not already processed
        if (!entities[entityIndex].iconFetched) {
            if (entities[entityIndex].icon.length() == 0) {
                String entityType = getEntityType(entities[entityIndex].entity_id);
                // Set default icons based on type (same as REST implementation)
                if (entityType == "input_boolean") {
                    entities[entityIndex].icon = "mdi:toggle-switch";
                } else if (entityType == "switch") {
                    entities[entityIndex].icon = "mdi:light-switch";
                } else if (entityType == "light") {
                    entities[entityIndex].icon = "mdi:lightbulb";
                } else if (entityType == "cover") {
                    entities[entityIndex].icon = "mdi:window-shutter";
                } else if (entityType == "climate") {
                    entities[entityIndex].icon = "mdi:thermostat";
                } else if (entityType == "sensor") {
                    entities[entityIndex].icon = "mdi:gauge";
                } else if (entityType == "fan") {
                    entities[entityIndex].icon = "mdi:fan";
                } else {
                    entities[entityIndex].icon = "mdi:help-circle";
                }
            }
            entities[entityIndex].iconFetched = true;
        }
    }
    
    void getInitialStates() {
        Serial.println("[WS] Getting initial states");
        
        DynamicJsonDocument doc(512);
        doc["id"] = nextMessageId++;
        doc["type"] = "get_states";
        
        String message;
        serializeJson(doc, message);
        sendWebSocketMessage(message);
    }
    
    void sendPing() {
        Serial.println("[WS] Sending ping");
        
        DynamicJsonDocument doc(256);
        doc["id"] = HA_WS_PING_MESSAGE_ID;
        doc["type"] = "ping";
        
        String message;
        serializeJson(doc, message);
        sendWebSocketMessage(message);
    }
    
    void sendPong(uint8_t* payload, size_t length) {
        // Send WebSocket pong frame
        uint8_t frame[2 + length];
        frame[0] = 0x8A; // Pong frame
        frame[1] = length;
        memcpy(&frame[2], payload, length);
        
        if (client && client->connected()) {
            client->write((const char*)frame, 2 + length);
        }
    }
    
    void sendWebSocketMessage(const String& message) {
        if (!client || !client->connected()) {
            return;
        }
        
        size_t messageLen = message.length();
        uint8_t frame[10 + messageLen];
        size_t frameSize = 0;
        
        // WebSocket frame header
        frame[frameSize++] = 0x81; // Text frame, FIN=1
        
        if (messageLen < 126) {
            frame[frameSize++] = messageLen;
        } else if (messageLen < 65536) {
            frame[frameSize++] = 126;
            frame[frameSize++] = (messageLen >> 8) & 0xFF;
            frame[frameSize++] = messageLen & 0xFF;
        } else {
            Serial.println("[WS] Message too long");
            return;
        }
        
        // Copy message data
        memcpy(&frame[frameSize], message.c_str(), messageLen);
        frameSize += messageLen;
        
        client->write((const char*)frame, frameSize);
    }
    
    void onDisconnected() {
        Serial.println("[WS] Disconnected");
        currentState = WS_STATE_DISCONNECTED;
        subscriptionActive = false;
        lastReconnectAttempt = millis();
    }
    
    void onError(int8_t error) {
        Serial.println("[WS] Error: " + String(error));
        lastError = "Connection error: " + String(error);
        disconnect();
    }
    
    void processPendingMessages() {
        // Remove old pending messages
        unsigned long now = millis();
        pendingMessages.erase(
            std::remove_if(pendingMessages.begin(), pendingMessages.end(), 
                [now](const PendingMessage& msg) {
                    return (now - msg.timestamp > WS_RESPONSE_TIMEOUT);
                }), 
            pendingMessages.end()
        );
        
        // Send unsent messages
        for (auto& msg : pendingMessages) {
            if (!msg.sent && isReady()) {
                sendWebSocketMessage(msg.message);
                msg.sent = true;
            }
        }
    }
    
    void clearPendingMessages() {
        pendingMessages.clear();
    }
    
    // Helper function to extract entity type from entity_id
    String getEntityType(String entityId) {
        int dotIndex = entityId.indexOf('.');
        if (dotIndex > 0) {
            return entityId.substring(0, dotIndex);
        }
        return "unknown";
    }
};
