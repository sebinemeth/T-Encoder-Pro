/*
 * @Description: Home Assistant Hybrid Manager v2 (REST + WebSocket)
 * @Author: WARP
 * @Date: 2024-10-08
 * @License: GPL 3.0
 * 
 * This manager combines:
 * - REST API: Initial state fetching and fallback polling (15s)
 * - WebSocket API: Real-time updates and command sending
 */
#pragma once

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "ha_config.h"
#include "ha_websocket_config.h"
#include "entities.h"

class HAWebSocketManagerV2 {
private:
    // WebSocket components
    WebSocketsClient webSocket;
    WebSocketState currentState;
    unsigned long lastHeartbeat;
    unsigned long lastReconnectAttempt;
    unsigned long connectionStartTime;
    int nextMessageId;
    bool subscriptionActive;
    String lastError;
    
    // REST API components
    HTTPClient httpClient;
    unsigned long lastRestPoll;
    bool restInitialFetchDone;
    static const unsigned long REST_POLL_INTERVAL = 15000; // 15 seconds
    
    // Pending command queue for WebSocket
    struct PendingCommand {
        String entityId;
        String action;
        String value;
        unsigned long timestamp;
    };
    static const int MAX_PENDING_COMMANDS = 10;
    PendingCommand pendingCommands[MAX_PENDING_COMMANDS];
    int pendingCommandCount;
    
public:
    HAWebSocketManagerV2() : currentState(WS_STATE_DISCONNECTED), 
                            lastHeartbeat(0), lastReconnectAttempt(0), 
                            connectionStartTime(0), nextMessageId(10), 
                            subscriptionActive(false), lastRestPoll(0),
                            restInitialFetchDone(false), pendingCommandCount(0) {}
    
    ~HAWebSocketManagerV2() {
        disconnect();
    }
    
    // Public interface
    bool begin() {
        Serial.println("[WS] Initializing WebSocket manager v2");
        currentState = WS_STATE_DISCONNECTED;
        
        // Setup WebSocket event handler
        webSocket.onEvent([this](WStype_t type, uint8_t * payload, size_t length) {
            this->webSocketEvent(type, payload, length);
        });
        
        return true;
    }
    
    void loop() {
        unsigned long now = millis();
        
        // WebSocket management
        switch (currentState) {
            case WS_STATE_DISCONNECTED:
                if (WiFi.status() == WL_CONNECTED && 
                    (now - lastReconnectAttempt > WS_RECONNECT_INTERVAL)) {
                    connect();
                    lastReconnectAttempt = now;
                }
                break;
                
            case WS_STATE_READY:
                // Send heartbeat
                if (now - lastHeartbeat > WS_HEARTBEAT_INTERVAL) {
                    sendPing();
                    lastHeartbeat = now;
                }
                
                // Process pending commands via WebSocket
                processPendingCommands();
                break;
                
            default:
                // Check for authentication/subscription timeouts
                if (now - connectionStartTime > WS_RESPONSE_TIMEOUT) {
                    Serial.println("[WS] Response timeout in state: " + String(getWebSocketStateString(currentState)));
                    disconnect();
                }
                break;
        }
        
        // Process WebSocket events
        webSocket.loop();
        
        // REST API management
        handleRestPolling(now);
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
        webSocket.disconnect();
        currentState = WS_STATE_DISCONNECTED;
        subscriptionActive = false;
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
        
        // Connect to Home Assistant WebSocket API
        webSocket.begin(host, port, "/api/websocket");
        
        return true;
    }
    
    void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
        switch(type) {
            case WStype_DISCONNECTED:
                Serial.println("[WS] Disconnected");
                currentState = WS_STATE_DISCONNECTED;
                subscriptionActive = false;
                lastReconnectAttempt = millis();
                break;
                
            case WStype_CONNECTED: {
                Serial.printf("[WS] Connected to: %s\n", payload);
                currentState = WS_STATE_AUTHENTICATING;
                connectionStartTime = millis();
                break;
            }
            
            case WStype_TEXT: {
                Serial.printf("[WS] Received text: %s\n", payload);
                processTextMessage((char*)payload, length);
                break;
            }
            
            case WStype_BIN:
                Serial.printf("[WS] Received binary length: %u\n", length);
                break;
                
            case WStype_PING:
                Serial.println("[WS] Received ping");
                break;
                
            case WStype_PONG:
                Serial.println("[WS] Received pong");
                break;
                
            case WStype_ERROR:
                Serial.printf("[WS] Error: %s\n", payload);
                lastError = String((char*)payload);
                break;
                
            default:
                Serial.printf("[WS] Unknown event type: %d\n", type);
                break;
        }
    }
    
    void processTextMessage(const char* payload, size_t length) {
        Serial.println("[WS] Processing message of length: " + String(length));
        
        // Parse JSON message
        DynamicJsonDocument doc(WS_MAX_MESSAGE_SIZE);
        DeserializationError error = deserializeJson(doc, payload, length);
        
        if (error) {
            Serial.println("[WS] JSON parse error: " + String(error.c_str()));
            Serial.println("[WS] Message length: " + String(length) + ", Buffer size: " + String(WS_MAX_MESSAGE_SIZE));
            Serial.println("[WS] First 200 chars: " + String(payload).substring(0, min(200, (int)length)));
            return;
        }
        
        Serial.println("[WS] JSON parsed successfully, memory usage: " + String(doc.memoryUsage()) + "/" + String(doc.capacity()) + " bytes");
        
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
            Serial.println("[WS] Received event message");
            handleEventMessage(doc);
        } else {
            Serial.println("[WS] Unknown message type: " + type);
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
        
        webSocket.sendTXT(message);
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
        webSocket.sendTXT(message);
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
                // Get initial states via REST API (not WebSocket)
                fetchInitialStatesViaREST();
            } else {
                Serial.println("[WS] Command successful: " + String(id));
            }
        } else {
            Serial.println("[WS] Command failed: " + String(doc["error"]["message"] | "Unknown error"));
            lastError = doc["error"]["message"] | "Command failed";
        }
    }
    
    void handleEventMessage(const DynamicJsonDocument& doc) {
        Serial.println("[WS] Processing event message");
        Serial.println("[WS] Document memory usage: " + String(doc.memoryUsage()) + "/" + String(doc.capacity()) + " bytes");
        
        // Check if doc contains "event" key
        if (!doc.containsKey("event")) {
            Serial.println("[WS] Error: Document missing 'event' key");
            return;
        }
        
        // Work with event directly - ArduinoJson type checking seems unreliable here
        JsonVariantConst eventVariant = doc["event"];
        
        // Check if we can access event_type to confirm it's structured correctly
        if (!eventVariant.containsKey("event_type")) {
            Serial.println("[WS] Error: Event does not contain event_type key");
            return;
        }
        
        Serial.println("[WS] Event contains event_type key - proceeding with processing");
        
        String eventType = eventVariant["event_type"] | "";
        Serial.println("[WS] Event type: " + eventType);
        
        if (eventType == "state_changed") {
            // Access nested data object
            if (!eventVariant.containsKey("data")) {
                Serial.println("[WS] Error: Missing data object");
                return;
            }
            
            JsonVariantConst dataVariant = eventVariant["data"];
            String entityId = dataVariant["entity_id"] | "";
            Serial.println("[WS] State changed for entity: " + entityId);
            
            if (entityId.length() == 0) {
                Serial.println("[WS] Error: Empty entity_id");
                return;
            }
            
            // Find the entity in our list
            for (int i = 0; i < ENTITY_COUNT; i++) {
                Serial.println("[WS] Checking entity " + String(i) + ": " + entities[i].entity_id + " vs " + entityId);
                if (entities[i].entity_id == entityId) {
                    Serial.println("[WS] MATCH! Processing entity " + String(i));
                    
                    // Check if new_state exists
                    if (!dataVariant.containsKey("new_state")) {
                        Serial.println("[WS] Error: Missing new_state object");
                        return;
                    }
                    
                    // Create a new document for the new_state data
                    DynamicJsonDocument newStateDoc(2048);
                    JsonVariantConst newStateVariant = dataVariant["new_state"];
                    
                    // Copy the new_state object to our document
                    newStateDoc.set(newStateVariant);
                    
                    updateEntityFromWebSocket(i, newStateDoc);
                    break;
                } else {
                    Serial.println("[WS] No match for entity " + String(i));
                }
            }
        }
    }
    
    void updateEntityFromWebSocket(int entityIndex, const DynamicJsonDocument& doc) {
        Serial.println("[WS] updateEntityFromWebSocket called for index: " + String(entityIndex));
        
        if (entityIndex < 0 || entityIndex >= ENTITY_COUNT) {
            Serial.println("[WS] Error: entityIndex out of bounds: " + String(entityIndex));
            return;
        }
        
        String state = doc["state"] | "";
        Serial.println("[WS] Entity " + entities[entityIndex].name + " (" + entities[entityIndex].entity_id + ") state: " + state);
        
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
        webSocket.sendTXT(message);
    }
    
    void processInitialStates(const DynamicJsonDocument& doc) {
        if (!doc["result"].is<JsonArray>()) {
            Serial.println("[WS] Error: result is not an array");
            return;
        }
        
        Serial.println("[WS] Processing initial states array");
        
        int entitiesFound = 0;
        
        // Check if result is an array and iterate
        if (doc["result"].is<JsonArray>()) {
            for (size_t stateIndex = 0; stateIndex < doc["result"].size(); stateIndex++) {
                String entityId = doc["result"][stateIndex]["entity_id"] | "";
                
                // Check if this entity is one we're monitoring
                for (int entityIndex = 0; entityIndex < ENTITY_COUNT; entityIndex++) {
                    if (entities[entityIndex].entity_id == entityId) {
                        // Found a matching entity - update its state
                        Serial.println("[WS] Found initial state for: " + entities[entityIndex].name);
                        
                        // Create a document for this entity's state data
                        DynamicJsonDocument entityDoc(1024);
                        String stateJson;
                        serializeJson(doc["result"][stateIndex], stateJson);
                        deserializeJson(entityDoc, stateJson);
                        
                        updateEntityFromWebSocket(entityIndex, entityDoc);
                        entitiesFound++;
                        break;
                    }
                }
            }
        }
        
        Serial.println("[WS] Initial states loaded for " + String(entitiesFound) + "/" + String(ENTITY_COUNT) + " entities");
        Serial.println("[WS] WebSocket ready for real-time updates");
    }
    
    void sendPing() {
        Serial.println("[WS] Sending ping");
        
        DynamicJsonDocument doc(256);
        doc["id"] = nextMessageId++;
        doc["type"] = "ping";
        
        String message;
        serializeJson(doc, message);
        webSocket.sendTXT(message);
    }
    
    // Helper function to extract entity type from entity_id
    String getEntityType(String entityId) {
        int dotIndex = entityId.indexOf('.');
        if (dotIndex > 0) {
            return entityId.substring(0, dotIndex);
        }
        return "unknown";
    }
    
    // ================ HYBRID MANAGEMENT METHODS ================
    
    void handleRestPolling(unsigned long now) {
        // Handle initial fetch first time
        if (!restInitialFetchDone) {
            fetchInitialStatesViaREST();
            return;
        }
        
        // Regular polling every 15 seconds as fallback
        if (now - lastRestPoll > REST_POLL_INTERVAL) {
            Serial.println("[REST] Polling entity states (fallback)");
            fetchEntityStatesViaREST();
            lastRestPoll = now;
        }
    }
    
    void fetchInitialStatesViaREST() {
        Serial.println("[REST] Fetching initial entity states (per-entity)");
        
        int entitiesLoaded = 0;
        
        // Fetch each entity individually
        for (int i = 0; i < ENTITY_COUNT; i++) {
            if (fetchSingleEntityViaREST(i)) {
                entitiesLoaded++;
            }
            delay(100); // Small delay between requests to avoid overwhelming HA
        }
        
        Serial.println("[REST] Initial states loaded for " + String(entitiesLoaded) + "/" + String(ENTITY_COUNT) + " entities");
        restInitialFetchDone = true;
        lastRestPoll = millis();
    }
    
    void fetchEntityStatesViaREST() {
        // Fetch individual entities via REST API
        for (int i = 0; i < ENTITY_COUNT; i++) {
            fetchSingleEntityViaREST(i);
            delay(50); // Small delay between requests
        }
    }
    
    bool fetchSingleEntityViaREST(int entityIndex) {
        if (entityIndex < 0 || entityIndex >= ENTITY_COUNT) return false;
        
        String baseUrl = String(HA_URL);
        // Remove trailing slash if present to avoid double slash
        if (baseUrl.endsWith("/")) {
            baseUrl = baseUrl.substring(0, baseUrl.length() - 1);
        }
        String url = baseUrl + "/api/states/" + entities[entityIndex].entity_id;
        Serial.println("[REST] Requesting URL: " + url);
        
        httpClient.begin(url);
        httpClient.addHeader("Authorization", "Bearer " + String(HA_TOKEN));
        httpClient.addHeader("Content-Type", "application/json");
        
        Serial.println("[REST] Fetching: " + entities[entityIndex].entity_id + " (" + entities[entityIndex].name + ")");
        int httpResponseCode = httpClient.GET();
        bool success = false;
        
        if (httpResponseCode == 200) {
            String response = httpClient.getString();
            Serial.println("[REST] Success response: " + response.substring(0, 200) + "...");
            processRestEntityResponse(entityIndex, response);
            success = true;
            Serial.println("[REST] Loaded state for: " + entities[entityIndex].name);
        } else if (httpResponseCode == 404) {
            Serial.println("[REST] Entity not found: " + entities[entityIndex].entity_id);
            String errorResponse = httpClient.getString();
            if (errorResponse.length() > 0) {
                Serial.println("[REST] 404 Response body: " + errorResponse);
            }
        } else {
            Serial.println("[REST] Failed to fetch " + entities[entityIndex].entity_id + ": HTTP " + String(httpResponseCode));
            String errorResponse = httpClient.getString();
            if (errorResponse.length() > 0) {
                Serial.println("[REST] Error response: " + errorResponse);
            }
        }
        
        httpClient.end();
        return success;
    }
    
    void processRestStatesResponse(const String& response) {
        DynamicJsonDocument doc(16384); // Larger buffer for all states
        DeserializationError error = deserializeJson(doc, response);
        
        if (error) {
            Serial.println("[REST] JSON parse error: " + String(error.c_str()));
            return;
        }
        
        int entitiesFound = 0;
        
        // Iterate through all states from Home Assistant
        for (JsonVariant stateVariant : doc.as<JsonArray>()) {
            String entityId = stateVariant["entity_id"] | "";
            
            // Check if this entity is one we're monitoring
            for (int entityIndex = 0; entityIndex < ENTITY_COUNT; entityIndex++) {
                if (entities[entityIndex].entity_id == entityId) {
                    // Found a matching entity - update its state
                    Serial.println("[REST] Found initial state for: " + entities[entityIndex].name);
                    
                    // Create a document for this entity's state data
                    DynamicJsonDocument entityDoc(1024);
                    String stateJson;
                    serializeJson(stateVariant, stateJson);
                    deserializeJson(entityDoc, stateJson);
                    
                    updateEntityFromREST(entityIndex, entityDoc);
                    entitiesFound++;
                    break;
                }
            }
        }
        
        Serial.println("[REST] Initial states loaded for " + String(entitiesFound) + "/" + String(ENTITY_COUNT) + " entities");
    }
    
    void processRestEntityResponse(int entityIndex, const String& response) {
        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, response);
        
        if (error) {
            Serial.println("[REST] JSON parse error for " + entities[entityIndex].entity_id + ": " + String(error.c_str()));
            return;
        }
        
        updateEntityFromREST(entityIndex, doc);
    }
    
    void updateEntityFromREST(int entityIndex, const DynamicJsonDocument& doc) {
        if (entityIndex < 0 || entityIndex >= ENTITY_COUNT) return;
        
        String state = doc["state"] | "";
        Serial.println("[REST] Entity " + entities[entityIndex].name + " state: " + state);
        
        // Update entity based on type - reuse WebSocket update logic
        updateEntityFromWebSocket(entityIndex, doc);
    }
    
    // ================ COMMAND QUEUE METHODS ================
    
    public:
    void queueCommand(String entityId, String action, String value = "") {
        if (pendingCommandCount >= MAX_PENDING_COMMANDS) {
            Serial.println("[WS] Command queue full, dropping command for " + entityId);
            return;
        }
        
        pendingCommands[pendingCommandCount].entityId = entityId;
        pendingCommands[pendingCommandCount].action = action;
        pendingCommands[pendingCommandCount].value = value;
        pendingCommands[pendingCommandCount].timestamp = millis();
        pendingCommandCount++;
        
        Serial.println("[WS] Queued command: " + action + " for " + entityId);
    }
    
    private:
    void processPendingCommands() {
        if (pendingCommandCount == 0 || currentState != WS_STATE_READY) {
            return;
        }
        
        // Process one command at a time
        PendingCommand& cmd = pendingCommands[0];
        
        // Check if command is too old (10 seconds)
        if (millis() - cmd.timestamp > 10000) {
            Serial.println("[WS] Command expired: " + cmd.action + " for " + cmd.entityId);
            removeFirstPendingCommand();
            return;
        }
        
        sendWebSocketCommand(cmd.entityId, cmd.action, cmd.value);
        removeFirstPendingCommand();
    }
    
    void removeFirstPendingCommand() {
        if (pendingCommandCount > 0) {
            // Shift all commands down
            for (int i = 1; i < pendingCommandCount; i++) {
                pendingCommands[i-1] = pendingCommands[i];
            }
            pendingCommandCount--;
        }
    }
    
    void sendWebSocketCommand(String entityId, String action, String value) {
        Serial.println("[WS] Sending command: " + action + " for " + entityId);
        
        DynamicJsonDocument doc(1024);
        doc["id"] = nextMessageId++;
        doc["type"] = "call_service";
        
        // Determine service based on entity type and action
        String entityType = getEntityType(entityId);
        String domain = entityType;
        String service = action;
        
        // Map common actions to proper services
        if (action == "toggle") {
            if (entityType == "light" || entityType == "switch" || entityType == "input_boolean") {
                service = "toggle";
            }
        } else if (action == "turn_on") {
            service = "turn_on";
        } else if (action == "turn_off") {
            service = "turn_off";
        } else if (action == "open_cover" || action == "close_cover" || action == "stop_cover") {
            domain = "cover";
            service = action;
        }
        
        doc["domain"] = domain;
        doc["service"] = service;
        doc["service_data"]["entity_id"] = entityId;
        
        // Add value if provided
        if (value.length() > 0) {
            if (action == "set_position") {
                doc["service_data"]["position"] = value.toInt();
            } else if (action == "set_temperature") {
                doc["service_data"]["temperature"] = value.toFloat();
            }
        }
        
        String message;
        serializeJson(doc, message);
        webSocket.sendTXT(message);
    }
};
