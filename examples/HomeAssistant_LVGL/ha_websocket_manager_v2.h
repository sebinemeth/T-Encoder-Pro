/*
 * @Description: Home Assistant Hybrid Manager v2 (WebSocket + REST fallback)
 * @Author: WARP
 * @Date: 2024-10-08
 * @License: GPL 3.0
 *
 * - WebSocket: authenticated, subscribed only to the configured entities
 *   (subscribe_trigger), and used to send service calls.
 * - REST: one state sync whenever the WebSocket becomes ready (covers events
 *   missed while disconnected), per-entity refresh after a failed service call,
 *   and fallback polling while the WebSocket is not ready.
 */
#pragma once

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "ha_config.h"
#include "ha_websocket_config.h"
#include "entities.h"

// Set to 1 to log every WebSocket message
#ifndef HA_WS_VERBOSE
#define HA_WS_VERBOSE 0
#endif

class HAWebSocketManagerV2 {
private:
    WebSocketsClient webSocket;
    WebSocketState currentState = WS_STATE_DISCONNECTED;
    unsigned long lastHeartbeat = 0;
    unsigned long lastReconnectAttempt = 0;
    unsigned long connectionStartTime = 0;
    int nextMessageId = 10;
    bool useTriggerSubscription = true; // falls back to subscribe_events if rejected
    String lastError;

    // REST
    HTTPClient httpClient;
    unsigned long lastRestPoll = 0;
    static const unsigned long REST_POLL_INTERVAL = 15000; // only while WS is not ready
    bool syncRequested = false;                            // full REST sync pending
    bool refreshEntity[ENTITY_COUNT] = {};                 // per-entity REST refresh pending

    // Service calls awaiting a result, so a failure can be reconciled
    struct InflightCall {
        int id;
        int entityIndex;
        unsigned long sentAt;
    };
    static const int MAX_INFLIGHT = 8;
    static const unsigned long INFLIGHT_TTL = 10000;
    InflightCall inflight[MAX_INFLIGHT] = {};

public:
    ~HAWebSocketManagerV2() {
        disconnect();
    }

    bool begin() {
        Serial.println("[WS] Initializing WebSocket manager v2");
        currentState = WS_STATE_DISCONNECTED;
        webSocket.onEvent([this](WStype_t type, uint8_t* payload, size_t length) {
            this->webSocketEvent(type, payload, length);
        });
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

            case WS_STATE_READY:
                if (now - lastHeartbeat > WS_HEARTBEAT_INTERVAL) {
                    sendPing();
                    lastHeartbeat = now;
                }
                expireInflight(now);
                break;

            default:
                if (now - connectionStartTime > WS_RESPONSE_TIMEOUT) {
                    Serial.println("[WS] Response timeout in state: " + String(getWebSocketStateString(currentState)));
                    disconnect();
                }
                break;
        }

        webSocket.loop();
        handleRest(now);
    }

    bool isReady() const { return currentState == WS_STATE_READY; }
    bool isConnected() const { return currentState >= WS_STATE_CONNECTED; }
    WebSocketState getState() const { return currentState; }
    String getStateString() const { return String(getWebSocketStateString(currentState)); }
    String getLastError() const { return lastError; }

    void disconnect() {
        Serial.println("[WS] Disconnecting");
        webSocket.disconnect();
        currentState = WS_STATE_DISCONNECTED;
        lastReconnectAttempt = millis();
    }

    // Call a Home Assistant service over the WebSocket. Returns false if the
    // WebSocket is not ready (caller should fall back to REST).
    bool callService(const char* domain, const char* service, int entityIndex) {
        return sendServiceCall<int>(domain, service, entityIndex, nullptr, 0);
    }
    bool callService(const char* domain, const char* service, int entityIndex, const char* key, int value) {
        return sendServiceCall<int>(domain, service, entityIndex, key, value);
    }
    bool callService(const char* domain, const char* service, int entityIndex, const char* key, float value) {
        return sendServiceCall<float>(domain, service, entityIndex, key, value);
    }

private:
    // ================ CONNECTION ================

    bool connect() {
        if (currentState != WS_STATE_DISCONNECTED) return false;

        currentState = WS_STATE_CONNECTING;
        connectionStartTime = millis();

        // Parse HA_URL (plain http only) into host and port
        String url = String(HA_URL);
        if (url.startsWith("http://")) url = url.substring(7);
        if (url.endsWith("/")) url = url.substring(0, url.length() - 1);

        String host = url;
        int port = 8123;
        int portIndex = url.indexOf(':');
        if (portIndex > 0) {
            host = url.substring(0, portIndex);
            port = url.substring(portIndex + 1).toInt();
        }

        Serial.println("[WS] Connecting to " + host + ":" + String(port));
        webSocket.begin(host, port, "/api/websocket");
        return true;
    }

    void webSocketEvent(WStype_t type, uint8_t* payload, size_t length) {
        switch (type) {
            case WStype_DISCONNECTED:
                Serial.println("[WS] Disconnected");
                currentState = WS_STATE_DISCONNECTED;
                lastReconnectAttempt = millis();
                clearInflight();
                break;

            case WStype_CONNECTED:
                Serial.println("[WS] Connected");
                currentState = WS_STATE_AUTHENTICATING;
                connectionStartTime = millis();
                break;

            case WStype_TEXT:
#if HA_WS_VERBOSE
                Serial.printf("[WS] Received text: %s\n", payload);
#endif
                processTextMessage((const char*)payload, length);
                break;

            case WStype_ERROR:
                Serial.printf("[WS] Error: %s\n", payload);
                lastError = String((char*)payload);
                break;

            default:
                break;
        }
    }

    void processTextMessage(const char* payload, size_t length) {
        if (length > WS_MAX_MESSAGE_SIZE) {
            Serial.println("[WS] Message too large, ignored: " + String(length));
            return;
        }

        DynamicJsonDocument doc(length * 2 + 512);
        DeserializationError error = deserializeJson(doc, payload, length);
        if (error) {
            Serial.println("[WS] JSON parse error: " + String(error.c_str()) + " (length " + String(length) + ")");
            return;
        }

        const char* type = doc["type"] | "";

        if (strcmp(type, HA_WS_TYPE_AUTH_REQUIRED) == 0) {
            sendAuthentication();
        } else if (strcmp(type, HA_WS_TYPE_AUTH_OK) == 0) {
            Serial.println("[WS] Authentication successful");
            currentState = WS_STATE_AUTHENTICATED;
            subscribe();
        } else if (strcmp(type, HA_WS_TYPE_AUTH_INVALID) == 0) {
            Serial.println("[WS] Authentication failed");
            lastError = "Authentication failed - check token";
            currentState = WS_STATE_ERROR;
        } else if (strcmp(type, HA_WS_TYPE_RESULT) == 0) {
            handleResultMessage(doc);
        } else if (strcmp(type, HA_WS_TYPE_EVENT) == 0) {
            handleEventMessage(doc);
        }
        // pong and anything else is ignored
    }

    void sendAuthentication() {
        Serial.printf("[WS] Sending auth (token length %u)\n", (unsigned)strlen(HA_TOKEN));

        DynamicJsonDocument doc(512);
        doc["type"] = "auth";
        doc["access_token"] = HA_TOKEN;

        String message;
        serializeJson(doc, message); // never log this message: it contains the token
        webSocket.sendTXT(message);
    }

    void sendPing() {
        DynamicJsonDocument doc(64);
        doc["id"] = nextMessageId++;
        doc["type"] = "ping";
        String message;
        serializeJson(doc, message);
        webSocket.sendTXT(message);
    }

    // ================ SUBSCRIPTION ================

    // Preferred: server-side filtered subscribe_trigger on our entities only.
    // Fallback: subscribe_events state_changed (every event in HA, filtered here).
    void subscribe() {
        currentState = WS_STATE_SUBSCRIBING;
        connectionStartTime = millis();

        DynamicJsonDocument doc(1024);
        doc["id"] = nextMessageId++;
        if (useTriggerSubscription) {
            Serial.println("[WS] Subscribing to " + String(ENTITY_COUNT) + " entities (subscribe_trigger)");
            doc["type"] = "subscribe_trigger";
            JsonObject trigger = doc.createNestedObject("trigger");
            trigger["platform"] = "state";
            JsonArray ids = trigger.createNestedArray("entity_id");
            for (int i = 0; i < ENTITY_COUNT; i++) ids.add(entities[i].entity_id);
        } else {
            Serial.println("[WS] Subscribing to all state_changed events (fallback)");
            doc["type"] = "subscribe_events";
            doc["event_type"] = "state_changed";
        }

        String message;
        serializeJson(doc, message);
        webSocket.sendTXT(message);
    }

    void handleResultMessage(const JsonDocument& doc) {
        int id = doc["id"] | 0;
        bool success = doc["success"] | false;

        // Result of a service call?
        for (int i = 0; i < MAX_INFLIGHT; i++) {
            if (id != 0 && inflight[i].id == id) {
                if (!success) {
                    const char* msg = doc["error"]["message"] | "Service call failed";
                    Serial.println("[WS] Service call failed: " + String(msg));
                    lastError = msg;
                    refreshEntity[inflight[i].entityIndex] = true; // reconcile UI with HA
                }
                inflight[i].id = 0;
                return;
            }
        }

        if (currentState == WS_STATE_SUBSCRIBING) {
            if (success) {
                Serial.println("[WS] Subscribed, ready");
                currentState = WS_STATE_READY;
                lastHeartbeat = millis();
                syncRequested = true; // pick up changes missed while disconnected
            } else if (useTriggerSubscription) {
                Serial.println("[WS] subscribe_trigger rejected, falling back to state_changed events");
                useTriggerSubscription = false;
                subscribe();
            } else {
                lastError = doc["error"]["message"] | "Subscription failed";
                currentState = WS_STATE_ERROR;
            }
        } else if (!success) {
            lastError = doc["error"]["message"] | "Command failed";
            Serial.println("[WS] Command failed: " + lastError);
        }
    }

    void handleEventMessage(const JsonDocument& doc) {
        JsonVariantConst event = doc["event"];

        // subscribe_trigger: event.variables.trigger.to_state
        JsonVariantConst toState = event["variables"]["trigger"]["to_state"];
        if (!toState.isNull()) {
            applyState(toState);
            return;
        }

        // subscribe_events fallback: event.data.new_state
        if (strcmp(event["event_type"] | "", "state_changed") == 0) {
            JsonVariantConst newState = event["data"]["new_state"];
            if (!newState.isNull()) applyState(newState);
        }
    }

    // Find the entity a state object belongs to and apply it
    void applyState(JsonVariantConst stateObj) {
        const char* entityId = stateObj["entity_id"] | "";
        for (int i = 0; i < ENTITY_COUNT; i++) {
            if (entities[i].entity_id == entityId) {
                updateEntityFromState(i, stateObj);
                return;
            }
        }
    }

    // ================ STATE -> ENTITY ================

    void updateEntityFromState(int entityIndex, JsonVariantConst stateObj) {
        if (entityIndex < 0 || entityIndex >= ENTITY_COUNT) return;

        HAEntity& e = entities[entityIndex];
        String state = stateObj["state"] | "";
        JsonVariantConst attrs = stateObj["attributes"];
#if HA_WS_VERBOSE
        Serial.println("[HA] " + e.entity_id + " -> " + state);
#endif

        switch (e.type) {
            case ENTITY_TOGGLE: {
                bool newState = (state == "on");
                if (e.state != newState) {
                    e.state = newState;
                    e.lastState = !newState; // force UI update
                }
                break;
            }
            case ENTITY_COVER: {
                e.coverState = state;
                if (attrs["current_position"].is<int>()) {
                    int newPosition = attrs["current_position"];
                    if (e.position != newPosition) {
                        e.position = newPosition;
                        e.lastPosition = -1;
                    }
                }
                break;
            }
            case ENTITY_THERMOSTAT: {
                if (attrs["current_temperature"].is<float>()) {
                    e.currentTemp = attrs["current_temperature"];
                }
                if (attrs["min_temp"].is<float>()) e.minTemp = attrs["min_temp"];
                if (attrs["max_temp"].is<float>()) e.maxTemp = attrs["max_temp"];
                if (attrs["temperature"].is<float>()) {
                    e.targetTemp = attrs["temperature"];
                }
                // Target or range may have changed: make sure label and arc are re-synced
                e.lastTargetTemp = -1;
                e.hvacMode = state; // climate state is the hvac mode
                e.hvacAction = attrs["hvac_action"] | "idle";
                break;
            }
            case ENTITY_SENSOR: {
                e.sensorValue = state;
                e.sensorUnit = attrs["unit_of_measurement"] | "";
                break;
            }
            case ENTITY_FAN: {
                int newSpeed = 0;
                if (state == "on" && attrs["percentage"].is<int>()) {
                    newSpeed = attrs["percentage"];
                }
                if (e.fanSpeed != newSpeed) {
                    e.fanSpeed = newSpeed;
                    e.lastFanSpeed = -1;
                }
                break;
            }
        }

        ensureEntityIcon(e);
    }

    // ================ REST ================

    void handleRest(unsigned long now) {
        if (WiFi.status() != WL_CONNECTED) return;

        if (currentState == WS_STATE_READY) {
            if (syncRequested) {
                syncRequested = false;
                fetchAllViaREST();
                return;
            }
            for (int i = 0; i < ENTITY_COUNT; i++) {
                if (refreshEntity[i]) {
                    refreshEntity[i] = false;
                    fetchEntityViaREST(i);
                    return; // one request per loop iteration
                }
            }
            return;
        }

        // WebSocket not ready: poll as a fallback
        if (lastRestPoll == 0 || now - lastRestPoll > REST_POLL_INTERVAL) {
            lastRestPoll = now ? now : 1;
            fetchAllViaREST();
        }
    }

    void fetchAllViaREST() {
        int loaded = 0;
        for (int i = 0; i < ENTITY_COUNT; i++) {
            if (fetchEntityViaREST(i)) loaded++;
        }
        Serial.println("[REST] Synced " + String(loaded) + "/" + String(ENTITY_COUNT) + " entities");
    }

    bool fetchEntityViaREST(int entityIndex) {
        String baseUrl = String(HA_URL);
        if (baseUrl.endsWith("/")) baseUrl = baseUrl.substring(0, baseUrl.length() - 1);

        httpClient.begin(baseUrl + "/api/states/" + entities[entityIndex].entity_id);
        httpClient.addHeader("Authorization", "Bearer " + String(HA_TOKEN));
        httpClient.setTimeout(2000);
        httpClient.setConnectTimeout(1000);

        int code = httpClient.GET();
        bool ok = false;
        if (code == 200) {
            DynamicJsonDocument doc(2048);
            DeserializationError err = deserializeJson(doc, httpClient.getString());
            if (!err) {
                updateEntityFromState(entityIndex, doc.as<JsonVariantConst>());
                ok = true;
            } else {
                Serial.println("[REST] JSON parse error for " + entities[entityIndex].entity_id + ": " + String(err.c_str()));
            }
        } else {
            Serial.println("[REST] " + entities[entityIndex].entity_id + ": HTTP " + String(code));
        }
        httpClient.end();
        return ok;
    }

    // ================ SERVICE CALLS ================

    template <typename T>
    bool sendServiceCall(const char* domain, const char* service, int entityIndex, const char* key, T value) {
        if (currentState != WS_STATE_READY) return false;

        DynamicJsonDocument doc(512);
        int id = nextMessageId++;
        doc["id"] = id;
        doc["type"] = "call_service";
        doc["domain"] = domain;
        doc["service"] = service;
        JsonObject data = doc.createNestedObject("service_data");
        data["entity_id"] = entities[entityIndex].entity_id;
        if (key) data[key] = value;

        String message;
        serializeJson(doc, message);
        if (!webSocket.sendTXT(message)) return false;

        trackInflight(id, entityIndex);
        return true;
    }

    void trackInflight(int id, int entityIndex) {
        int slot = 0;
        for (int i = 0; i < MAX_INFLIGHT; i++) {
            if (inflight[i].id == 0) { slot = i; break; }
            if (inflight[i].sentAt < inflight[slot].sentAt) slot = i; // else reuse the oldest
        }
        inflight[slot] = {id, entityIndex, millis()};
    }

    void expireInflight(unsigned long now) {
        for (int i = 0; i < MAX_INFLIGHT; i++) {
            if (inflight[i].id != 0 && now - inflight[i].sentAt > INFLIGHT_TTL) inflight[i].id = 0;
        }
    }

    void clearInflight() {
        for (int i = 0; i < MAX_INFLIGHT; i++) inflight[i].id = 0;
    }
};
