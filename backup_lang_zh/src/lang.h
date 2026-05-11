#ifndef LANG_H
#define LANG_H

#include <Arduino.h>

// Supported languages
enum Lang : uint8_t {
    LANG_EN = 0,
    LANG_DE = 1,
    LANG_COUNT
};

// String IDs for all translatable display texts
enum StringID : uint8_t {
    // Boot / Init
    STR_DISPLAY_INIT,
    STR_WIFI_INIT,
    STR_WEBSERVER_INIT,
    STR_API_INIT,
    STR_NFC_INIT,
    STR_SEARCHING_SCALE,
    STR_INIT_DONE,

    // Scale
    STR_TARE_SCALE,
    STR_SCALE_NOT_CALIBRATED,
    STR_SCALE_CAL,
    STR_EMPTY_SCALE,
    STR_PLACE_WEIGHT,
    STR_REMOVE_WEIGHT,
    STR_COMPLETED,
    STR_CALIBRATION_ERROR,
    STR_HX711_NOT_FOUND,

    // NFC / Spool
    STR_READING,
    STR_DECODING_DATA,
    STR_SPOOL_TAG,
    STR_WEIGHING,
    STR_WEIGHT_STABLE,
    STR_SENDING,
    STR_TAG_WRITTEN,
    STR_WRITING,
    STR_WRITE_TAG,
    STR_DONE,
    STR_PLACE_TAG_NOW,
    STR_DETECTING_TAG,
    STR_KNOWN_SPOOL,
    STR_QUICK_MODE,
    STR_LOCATION,
    STR_LOCATION_SET,
    STR_SCAN_SPOOL_FIRST,
    STR_WAIT_FMT,  // format string: "Wait... %ds" / "Warte... %ds"

    // Connection / API
    STR_NOT_REGISTERED,
    STR_API_CONN_LOST,
    STR_API_ERROR,
    STR_API_OFFLINE,
    STR_WEIGHT_SENT_REST,

    // Errors
    STR_FAILURE,
    STR_FAILURE_EXCL,
    STR_UNKNOWN_TAG,
    STR_UNKNOWN_TAG_TYPE,
    STR_NO_TAG_FOUND,
    STR_NFC_BUSY,
    STR_TAG_READ_ERROR,
    STR_TAG_TOO_SMALL,
    STR_NFC_RESET_FAILED,
    STR_TAG_LOST_RESET,
    STR_NFC_STILL_BROKEN,
    STR_TAG_DEFECT,
    STR_TEST_READ_ERROR,
    STR_TAG_REMOVED,
    STR_TAG_WRITE_PROT,
    STR_TEST_VERIFY_FAIL,
    STR_NDEF_INIT_FAILED,
    STR_NFC_UNSTABLE,
    STR_MEMORY_ERROR,
    STR_NO_RFID_BOARD,

    // WiFi
    STR_WIFI_CONFIG_MODE,
    STR_WIFI_NOT_CONNECTED,
    STR_WIFI_RECONNECTING,

    // OTA
    STR_UPDATE,
    STR_DOWNLOAD,

    // No-Scale mode
    STR_NOSCALE_MODE,
    STR_NOSCALE_PROMPT,

    STR_COUNT  // must be last
};

// Current language (default: English)
extern Lang currentLang;

// Get translated string by ID
const char* tr(StringID id);

// Load language setting from NVS
void loadLanguage();

// Save language setting to NVS
void saveLanguage(Lang lang);

// Get current language as string ("en" or "de")
const char* getLangCode();

#endif
