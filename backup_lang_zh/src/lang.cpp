#include "lang.h"
#include "config.h"
#include <Preferences.h>

Lang currentLang = LANG_EN;

// =====================================================================
// English strings
// =====================================================================
static const char EN_DISPLAY_INIT[]      = "Display init";
static const char EN_WIFI_INIT[]         = "WiFi init";
static const char EN_WEBSERVER_INIT[]    = "Webserver init";
static const char EN_API_INIT[]          = "API init";
static const char EN_NFC_INIT[]          = "NFC init";
static const char EN_SEARCHING_SCALE[]   = "Searching scale";
static const char EN_INIT_DONE[]         = "Setup finished";

static const char EN_TARE_SCALE[]        = "TARE Scale";
static const char EN_SCALE_NOT_CAL[]     = "Scale not calibrated";
static const char EN_SCALE_CAL[]         = "Scale Cal.";
static const char EN_EMPTY_SCALE[]       = "Empty Scale";
static const char EN_PLACE_WEIGHT[]      = "Place the weight";
static const char EN_REMOVE_WEIGHT[]     = "Remove weight";
static const char EN_COMPLETED[]         = "Completed";
static const char EN_CAL_ERROR[]         = "Calibration error";
static const char EN_HX711_NOT_FOUND[]   = "HX711 not found";

static const char EN_READING[]           = "Reading";
static const char EN_DECODING_DATA[]     = "Decoding data";
static const char EN_SPOOL_TAG[]         = "Spool Tag";
static const char EN_WEIGHING[]          = "Weighing...";
static const char EN_WEIGHT_STABLE[]     = "Weight stable";
static const char EN_SENDING[]           = "Sending...";
static const char EN_TAG_WRITTEN[]       = "Tag written";
static const char EN_WRITING[]           = "Writing";
static const char EN_WRITE_TAG[]         = "Write Tag";
static const char EN_DONE[]              = "Done!";
static const char EN_PLACE_TAG_NOW[]     = "Place tag now";
static const char EN_DETECTING_TAG[]     = "Detecting tag";
static const char EN_KNOWN_SPOOL[]       = "Known Spool";
static const char EN_QUICK_MODE[]        = "Quick mode";
static const char EN_LOCATION[]          = "Location";
static const char EN_LOCATION_SET[]      = "Location set";
static const char EN_SCAN_SPOOL_FIRST[] = "Scan spool first";
static const char EN_WAIT_FMT[]          = "Wait... %ds";

static const char EN_NOT_REGISTERED[]    = "Not Registered";
static const char EN_API_CONN_LOST[]     = "API Connection Lost";
static const char EN_API_ERROR[]         = "API Error";
static const char EN_API_OFFLINE[]       = "API offline";
static const char EN_WEIGHT_SENT_REST[]  = "Weight sent, rest:";

static const char EN_FAILURE[]           = "Failure";
static const char EN_FAILURE_EXCL[]      = "Failure!";
static const char EN_UNKNOWN_TAG[]       = "Unknown tag";
static const char EN_UNKNOWN_TAG_TYPE[]  = "Unknown tag type";
static const char EN_NO_TAG_FOUND[]      = "No tag found";
static const char EN_NFC_BUSY[]          = "NFC busy!";
static const char EN_TAG_READ_ERROR[]    = "Tag read error";
static const char EN_TAG_TOO_SMALL[]     = "Tag too small for data";
static const char EN_NFC_RESET_FAIL[]    = "NFC Reset failed";
static const char EN_TAG_LOST_RESET[]    = "Tag lost after reset";
static const char EN_NFC_STILL_BROKEN[]  = "NFC still broken";
static const char EN_TAG_DEFECT[]        = "Tag/Interface defect";
static const char EN_TEST_READ_ERROR[]   = "Test page read error";
static const char EN_TAG_REMOVED[]       = "Tag removed";
static const char EN_TAG_WRITE_PROT[]    = "Tag write protected?";
static const char EN_TEST_VERIFY_FAIL[]  = "Test verify failed";
static const char EN_NDEF_INIT_FAIL[]    = "NDEF init failed";
static const char EN_NFC_UNSTABLE[]      = "NFC Interface unstable";
static const char EN_MEMORY_ERROR[]      = "Memory error";
static const char EN_NO_RFID_BOARD[]     = "No RFID Board found";

static const char EN_WIFI_CONFIG[]       = "WiFi Config Mode";
static const char EN_WIFI_NOT_CONN[]     = "WiFi not connected Check Portal";
static const char EN_WIFI_RECONN[]       = "WiFi reconnecting";

static const char EN_UPDATE[]            = "Update";
static const char EN_DOWNLOAD[]          = "Download";

static const char EN_NOSCALE_MODE[]      = "Activate NFC-only mode";
static const char EN_NOSCALE_PROMPT[]    = "ready...";

// =====================================================================
// German strings
// =====================================================================
static const char DE_DISPLAY_INIT[]      = "Display init";
static const char DE_WIFI_INIT[]         = "WiFi init";
static const char DE_WEBSERVER_INIT[]    = "Webserver init";
static const char DE_API_INIT[]          = "API init";
static const char DE_NFC_INIT[]          = "NFC init";
static const char DE_SEARCHING_SCALE[]   = "Suche Waage";
static const char DE_INIT_DONE[]         = "Setup abgeschlossen";

static const char DE_TARE_SCALE[]        = "Waage tarieren";
static const char DE_SCALE_NOT_CAL[]     = "Waage nicht kalibriert";
static const char DE_SCALE_CAL[]         = "Kalibrieren";
static const char DE_EMPTY_SCALE[]       = "Waage leeren";
static const char DE_PLACE_WEIGHT[]      = "Gewicht auflegen";
static const char DE_REMOVE_WEIGHT[]     = "Gewicht entfernen";
static const char DE_COMPLETED[]         = "Abgeschlossen";
static const char DE_CAL_ERROR[]         = "Kalibrierungsfehler";
static const char DE_HX711_NOT_FOUND[]   = "HX711 nicht gefunden";

static const char DE_READING[]           = "Lesen";
static const char DE_DECODING_DATA[]     = "Daten dekodieren";
static const char DE_SPOOL_TAG[]         = "Spulen-Tag";
static const char DE_WEIGHING[]          = "Wiegen...";
static const char DE_WEIGHT_STABLE[]     = "Gewicht stabil";
static const char DE_SENDING[]           = "Senden...";
static const char DE_TAG_WRITTEN[]       = "Tag beschrieben";
static const char DE_WRITING[]           = "Schreiben";
static const char DE_WRITE_TAG[]         = "Tag schreiben";
static const char DE_DONE[]              = "Fertig!";
static const char DE_PLACE_TAG_NOW[]     = "Tag jetzt auflegen";
static const char DE_DETECTING_TAG[]     = "Tag erkennen";
static const char DE_KNOWN_SPOOL[]       = "Bekannte Spule";
static const char DE_QUICK_MODE[]        = "Schnellmodus";
static const char DE_LOCATION[]          = "Standort";
static const char DE_LOCATION_SET[]      = "Standort gesetzt";
static const char DE_SCAN_SPOOL_FIRST[] = "Zuerst Spule scannen";
static const char DE_WAIT_FMT[]          = "Warte... %ds";

static const char DE_NOT_REGISTERED[]    = "Nicht registriert";
static const char DE_API_CONN_LOST[]     = "API-Verbindung weg";
static const char DE_API_ERROR[]         = "API-Fehler";
static const char DE_API_OFFLINE[]       = "API offline";
static const char DE_WEIGHT_SENT_REST[]  = "Gesendet, Rest:";

static const char DE_FAILURE[]           = "Fehler";
static const char DE_FAILURE_EXCL[]      = "Fehler!";
static const char DE_UNKNOWN_TAG[]       = "Unbekannter Tag";
static const char DE_UNKNOWN_TAG_TYPE[]  = "Unbekannter Tagtyp";
static const char DE_NO_TAG_FOUND[]      = "Kein Tag gefunden";
static const char DE_NFC_BUSY[]          = "NFC belegt!";
static const char DE_TAG_READ_ERROR[]    = "Tag-Lesefehler";
static const char DE_TAG_TOO_SMALL[]     = "Tag zu klein";
static const char DE_NFC_RESET_FAIL[]    = "NFC-Reset fehlgeschl.";
static const char DE_TAG_LOST_RESET[]    = "Tag nach Reset weg";
static const char DE_NFC_STILL_BROKEN[]  = "NFC immer noch defekt";
static const char DE_TAG_DEFECT[]        = "Tag/Interface defekt";
static const char DE_TEST_READ_ERROR[]   = "Testseite Lesefehler";
static const char DE_TAG_REMOVED[]       = "Tag entfernt";
static const char DE_TAG_WRITE_PROT[]    = "Tag schreibgeschuetzt";
static const char DE_TEST_VERIFY_FAIL[]  = "Test fehlgeschlagen";
static const char DE_NDEF_INIT_FAIL[]    = "NDEF-Init Fehler";
static const char DE_NFC_UNSTABLE[]      = "NFC instabil";
static const char DE_MEMORY_ERROR[]      = "Speicherfehler";
static const char DE_NO_RFID_BOARD[]     = "Kein RFID-Board";

static const char DE_WIFI_CONFIG[]       = "WiFi Konfig-Modus";
static const char DE_WIFI_NOT_CONN[]     = "WiFi nicht verbunden Portal pruefen";
static const char DE_WIFI_RECONN[]       = "WiFi Neuverbindung";

static const char DE_UPDATE[]            = "Update";
static const char DE_DOWNLOAD[]          = "Download";

static const char DE_NOSCALE_MODE[]      = "Aktiviere NFC-only Modus";
static const char DE_NOSCALE_PROMPT[]    = "bereit...";

// =====================================================================
// String table: [StringID][Lang]
// =====================================================================
static const char* const stringTable[STR_COUNT][LANG_COUNT] = {
    // Boot / Init
    { EN_DISPLAY_INIT,     DE_DISPLAY_INIT },
    { EN_WIFI_INIT,        DE_WIFI_INIT },
    { EN_WEBSERVER_INIT,   DE_WEBSERVER_INIT },
    { EN_API_INIT,         DE_API_INIT },
    { EN_NFC_INIT,         DE_NFC_INIT },
    { EN_SEARCHING_SCALE,  DE_SEARCHING_SCALE },
    { EN_INIT_DONE,        DE_INIT_DONE },

    // Scale
    { EN_TARE_SCALE,       DE_TARE_SCALE },
    { EN_SCALE_NOT_CAL,    DE_SCALE_NOT_CAL },
    { EN_SCALE_CAL,        DE_SCALE_CAL },
    { EN_EMPTY_SCALE,      DE_EMPTY_SCALE },
    { EN_PLACE_WEIGHT,     DE_PLACE_WEIGHT },
    { EN_REMOVE_WEIGHT,    DE_REMOVE_WEIGHT },
    { EN_COMPLETED,        DE_COMPLETED },
    { EN_CAL_ERROR,        DE_CAL_ERROR },
    { EN_HX711_NOT_FOUND,  DE_HX711_NOT_FOUND },

    // NFC / Spool
    { EN_READING,          DE_READING },
    { EN_DECODING_DATA,    DE_DECODING_DATA },
    { EN_SPOOL_TAG,        DE_SPOOL_TAG },
    { EN_WEIGHING,         DE_WEIGHING },
    { EN_WEIGHT_STABLE,    DE_WEIGHT_STABLE },
    { EN_SENDING,          DE_SENDING },
    { EN_TAG_WRITTEN,      DE_TAG_WRITTEN },
    { EN_WRITING,          DE_WRITING },
    { EN_WRITE_TAG,        DE_WRITE_TAG },
    { EN_DONE,             DE_DONE },
    { EN_PLACE_TAG_NOW,    DE_PLACE_TAG_NOW },
    { EN_DETECTING_TAG,    DE_DETECTING_TAG },
    { EN_KNOWN_SPOOL,      DE_KNOWN_SPOOL },
    { EN_QUICK_MODE,       DE_QUICK_MODE },
    { EN_LOCATION,         DE_LOCATION },
    { EN_LOCATION_SET,     DE_LOCATION_SET },
    { EN_SCAN_SPOOL_FIRST, DE_SCAN_SPOOL_FIRST },
    { EN_WAIT_FMT,         DE_WAIT_FMT },

    // Connection / API
    { EN_NOT_REGISTERED,   DE_NOT_REGISTERED },
    { EN_API_CONN_LOST,    DE_API_CONN_LOST },
    { EN_API_ERROR,        DE_API_ERROR },
    { EN_API_OFFLINE,      DE_API_OFFLINE },
    { EN_WEIGHT_SENT_REST, DE_WEIGHT_SENT_REST },

    // Errors
    { EN_FAILURE,          DE_FAILURE },
    { EN_FAILURE_EXCL,     DE_FAILURE_EXCL },
    { EN_UNKNOWN_TAG,      DE_UNKNOWN_TAG },
    { EN_UNKNOWN_TAG_TYPE, DE_UNKNOWN_TAG_TYPE },
    { EN_NO_TAG_FOUND,     DE_NO_TAG_FOUND },
    { EN_NFC_BUSY,         DE_NFC_BUSY },
    { EN_TAG_READ_ERROR,   DE_TAG_READ_ERROR },
    { EN_TAG_TOO_SMALL,    DE_TAG_TOO_SMALL },
    { EN_NFC_RESET_FAIL,   DE_NFC_RESET_FAIL },
    { EN_TAG_LOST_RESET,   DE_TAG_LOST_RESET },
    { EN_NFC_STILL_BROKEN, DE_NFC_STILL_BROKEN },
    { EN_TAG_DEFECT,       DE_TAG_DEFECT },
    { EN_TEST_READ_ERROR,  DE_TEST_READ_ERROR },
    { EN_TAG_REMOVED,      DE_TAG_REMOVED },
    { EN_TAG_WRITE_PROT,   DE_TAG_WRITE_PROT },
    { EN_TEST_VERIFY_FAIL, DE_TEST_VERIFY_FAIL },
    { EN_NDEF_INIT_FAIL,   DE_NDEF_INIT_FAIL },
    { EN_NFC_UNSTABLE,     DE_NFC_UNSTABLE },
    { EN_MEMORY_ERROR,     DE_MEMORY_ERROR },
    { EN_NO_RFID_BOARD,    DE_NO_RFID_BOARD },

    // WiFi
    { EN_WIFI_CONFIG,      DE_WIFI_CONFIG },
    { EN_WIFI_NOT_CONN,    DE_WIFI_NOT_CONN },
    { EN_WIFI_RECONN,      DE_WIFI_RECONN },

    // OTA
    { EN_UPDATE,           DE_UPDATE },
    { EN_DOWNLOAD,         DE_DOWNLOAD },

    // No-Scale mode
    { EN_NOSCALE_MODE,     DE_NOSCALE_MODE },
    { EN_NOSCALE_PROMPT,   DE_NOSCALE_PROMPT },
};

const char* tr(StringID id) {
    if (id >= STR_COUNT) return "???";
    return stringTable[id][currentLang];
}

void loadLanguage() {
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_SETTINGS, true);
    currentLang = (Lang)preferences.getUChar(NVS_KEY_LANGUAGE, LANG_EN);
    if (currentLang >= LANG_COUNT) currentLang = LANG_EN;
    preferences.end();
    Serial.printf("Language loaded: %s\n", getLangCode());
}

void saveLanguage(Lang lang) {
    if (lang >= LANG_COUNT) return;
    currentLang = lang;
    Preferences preferences;
    preferences.begin(NVS_NAMESPACE_SETTINGS, false);
    preferences.putUChar(NVS_KEY_LANGUAGE, (uint8_t)lang);
    preferences.end();
    Serial.printf("Language saved: %s\n", getLangCode());
}

const char* getLangCode() {
    return (currentLang == LANG_DE) ? "de" : "en";
}
