/*
 * @Description: Home Assistant WebSocket Configuration
 * @Author: WARP
 * @Date: 2024-10-08
 * @License: GPL 3.0
 */
#pragma once

// WebSocket Configuration
#define WS_RECONNECT_INTERVAL 5000     // Reconnect interval in ms
#define WS_HEARTBEAT_INTERVAL 30000    // Heartbeat interval in ms
#define WS_RESPONSE_TIMEOUT 10000      // Response timeout in ms
#define WS_MAX_MESSAGE_SIZE 16384      // Maximum WebSocket message size
#define WS_CONNECTION_TIMEOUT 10000    // Connection timeout in ms

// Home Assistant WebSocket API Configuration
#define HA_WS_TYPE_AUTH_REQUIRED "auth_required"
#define HA_WS_TYPE_AUTH_OK "auth_ok"
#define HA_WS_TYPE_AUTH_INVALID "auth_invalid"
#define HA_WS_TYPE_RESULT "result"
#define HA_WS_TYPE_EVENT "event"
#define HA_WS_TYPE_PONG "pong"

// Message IDs - Now using auto-incrementing IDs instead of fixed constants
// All message IDs are managed by nextMessageId++ in the WebSocket manager

// WebSocket States
enum WebSocketState {
    WS_STATE_DISCONNECTED = 0,
    WS_STATE_CONNECTING,
    WS_STATE_CONNECTED,
    WS_STATE_AUTHENTICATING,
    WS_STATE_AUTHENTICATED,
    WS_STATE_SUBSCRIBING,
    WS_STATE_READY,
    WS_STATE_ERROR
};

// WebSocket connection status strings
const char* getWebSocketStateString(WebSocketState state) {
    switch (state) {
        case WS_STATE_DISCONNECTED: return "Disconnected";
        case WS_STATE_CONNECTING: return "Connecting";
        case WS_STATE_CONNECTED: return "Connected";
        case WS_STATE_AUTHENTICATING: return "Authenticating";
        case WS_STATE_AUTHENTICATED: return "Authenticated";
        case WS_STATE_SUBSCRIBING: return "Subscribing";
        case WS_STATE_READY: return "Ready";
        case WS_STATE_ERROR: return "Error";
        default: return "Unknown";
    }
}
