#ifndef NFC_H
#define NFC_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>

typedef enum{
    NFC_IDLE,
    NFC_READING,
    NFC_READ_SUCCESS,
    NFC_READ_ERROR,
    NFC_WRITING,
    NFC_WRITE_SUCCESS,
    NFC_WRITE_ERROR,
    NFC_BAMBU_DETECTED,
    NFC_BAMBU_ERROR
} nfcReaderStateType;

struct NfcWriteParameterType {
  bool tagType;
  char* payload;
  int spoolId;
  int locationId;
};

void startNfc();
void loadNfcStableOnly();
void saveNfcStableOnly(bool enabled);
void scanRfidTask(void * parameter);
void startWriteJsonToTag(const bool isSpoolTag, const char* payload, int spoolId = 0, int locationId = 0);
void setScanRequest(bool active);

extern TaskHandle_t RfidReaderTask;
extern String nfcJsonData;
extern String activeSpoolId;
extern String activeTagUuid;
extern String lastSpoolId;
extern volatile nfcReaderStateType nfcReaderState;
extern volatile bool nfcWriteInProgress;
extern volatile bool scanRequestActive;
extern volatile bool tagProcessed;
extern volatile bool isBambuTag;
extern volatile bool nfcReadingTaskSuspendRequest; // Reader-Task per Flag anhalten (statt vTaskSuspend)
extern volatile bool nfcStableOnly;                // Einstellung "NFC nur bei stabilem Gewicht" (Standard: false)
extern volatile bool nfcRfFieldOff;                 // true = PN532-Funkfeld aus (Gate zu)
extern volatile unsigned long nfcRfFieldOffAt;
extern volatile bool nfcRescanRequest;              // Gewichtsaenderung nach gelesenem Tag: Tag-Zustand zuruecksetzen
extern volatile bool nfcGateClosed;                 // true = NFC-Polling pausiert, Waage misst
extern volatile bool nfcReadingTaskSuspendState;   // true = Reader-Task pausiert

#endif
