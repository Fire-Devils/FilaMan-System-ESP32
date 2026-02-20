#ifndef API_H
#define API_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "website.h"
#include "display.h"
#include <ArduinoJson.h>

typedef enum {
    API_IDLE,
    API_TRANSMITTING
} filamanApiStateType;

typedef enum {
    API_REQUEST_REGISTER,
    API_REQUEST_HEARTBEAT,
    API_REQUEST_WEIGHT,
    API_REQUEST_LOCATE,
    API_REQUEST_RFID_RESULT
} FilamanApiRequestType;

extern volatile filamanApiStateType filamanApiState;
extern bool filamanConnected;

// FilaMan API functions
bool initFilaman();
bool registerDevice(const String& deviceCode);
void sendHeartbeatAsync();
void sendWeightAsync(int spoolId, String tagUuid, float weight);
void sendLocationAsync(int spoolId, String spoolTagUuid, int locationId, String locationTagUuid);
void sendRfidResultAsync(String tagUuid, int spoolId, int locationId, bool success, String errorMessage);

// Internal blocking functions (used by async task)
bool sendHeartbeat();
bool sendWeight(int spoolId, String tagUuid, float weight);
bool sendLocation(int spoolId, String spoolTagUuid, int locationId, String locationTagUuid);
bool sendRfidResult(String tagUuid, int spoolId, int locationId, bool success, String errorMessage);

// Helper functions
void saveFilamanConfig();
void loadFilamanConfig();
bool checkFilamanRegistration();

#endif
