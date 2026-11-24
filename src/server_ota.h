#ifndef SERVER_OTA_H
#define SERVER_OTA_H

#include <Arduino.h>

// Projektkennung dieses Builds (per build_flags je Env gesetzt, siehe platformio.ini).
#ifndef FW_PROJECT
  #define FW_PROJECT "filaman-scale-esp32"
#endif

// Marker im Firmware-Image: der FilaMan-Server liest Projekt und Version daraus.
extern "C" const char FILAMAN_FW_INFO[];

// Ganz frueh in setup() (nach Serial.begin, vor WLAN/NFC/Scale): Boot-Zaehler und
// Software-Rollback nach einem fehlgeschlagenen Server-Update.
void serverOtaEarlyInit();

// Startet den Hintergrund-Task (periodische Pruefung, Rollback-Frist, Berichte).
void serverOtaStart();

// Wird aufgerufen, sobald der Server mit 2xx geantwortet hat (bestaetigt eine neue Firmware).
void serverOtaConfirm();

// Update-Hinweis aus der Heartbeat-Antwort (firmware_update): loest bald eine sofortige
// Pruefung aus. Thread-sicher, darf aus dem FilaManApi-Task aufgerufen werden.
void serverOtaUpdateHint(const char* version);

// true, solange ein Server-Update heruntergeladen/geschrieben wird.
bool serverOtaInProgress();

// true waehrend JEDES Firmware-Updates (Server oder manueller Upload):
// Hauptschleife, API-Queue, Heartbeat und Display-Updates ruhen dann.
bool firmwareUpdateActive();

// Manueller Upload hat eine Firmware installiert: pending/rolled_back verwerfen.
void serverOtaResetState();

// Implementiert in ota.cpp: true waehrend eines manuellen Uploads ueber die Weboberflaeche.
bool otaManualUploadActive();

#endif
