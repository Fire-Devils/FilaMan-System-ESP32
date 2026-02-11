#ifndef BAMBU_H
#define BAMBU_H

#include <Arduino.h>
#include <ArduinoJson.h>

// Supported Bambu Lab printer model identifiers
enum BambuPrinterModel : uint8_t {
    MODEL_UNKNOWN = 0,
    MODEL_X1C,
    MODEL_X1,
    MODEL_X1E,
    MODEL_P1P,
    MODEL_P1S,
    MODEL_P2S,
    MODEL_A1,
    MODEL_A1_MINI,
    MODEL_H2D,
    MODEL_H2D_PRO,
    MODEL_H2C,
    MODEL_H2S
};

struct TrayData {
    uint8_t id;
    String tray_info_idx;
    String tray_type;
    String tray_sub_brands;
    String tray_color;
    int nozzle_temp_min;
    int nozzle_temp_max;
    String setting_id;
    String cali_idx;
};

struct BambuCredentials {
    String ip;
    String serial;
    String accesscode;
    bool autosend_enable;
    int autosend_time;
    BambuPrinterModel printer_model;
};

#define MAX_AMS 17  // 16 normale AMS + 1 externe Spule
extern String amsJsonData;  // Für die vorbereiteten JSON-Daten

struct AMSData {
    uint8_t ams_id;
    TrayData trays[4]; // Annahme: Maximal 4 Trays pro AMS
};

extern bool bambu_connected;

extern int ams_count;
extern AMSData ams_data[MAX_AMS];
//extern bool autoSendToBambu;
extern uint16_t autoSetToBambuSpoolId;
extern bool bambuDisabled;
extern BambuCredentials bambuCredentials;

bool removeBambuCredentials();
bool loadBambuCredentials();
bool saveBambuCredentials(const String& bambu_ip, const String& bambu_serialnr, const String& bambu_accesscode, const bool autoSend, const String& autoSendTime, const String& printerModel);
const char* printerModelToString(BambuPrinterModel model);
BambuPrinterModel stringToPrinterModel(const String& modelStr);
bool isH2Series(BambuPrinterModel model);
bool isA1Series(BambuPrinterModel model);
bool setupMqtt();
void mqtt_loop(void * parameter);
bool setBambuSpool(String payload);
void bambu_restart();

extern TaskHandle_t BambuMqttTask;
#endif
