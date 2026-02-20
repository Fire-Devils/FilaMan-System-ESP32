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
    NFC_WRITE_ERROR
} nfcReaderStateType;

struct NfcWriteParameterType {
  bool tagType;
  char* payload;
  int spoolId;
  int locationId;
};

void startNfc();
void scanRfidTask(void * parameter);
void startWriteJsonToTag(const bool isSpoolTag, const char* payload, int spoolId = 0, int locationId = 0);
bool quickSpoolIdCheck(String uidString);
bool readCompleteJsonForFastPath(); // Read complete JSON data for fast-path web interface display

extern TaskHandle_t RfidReaderTask;
extern String nfcJsonData;
extern String activeSpoolId;
extern String activeTagUuid;
extern String lastSpoolId;
extern volatile nfcReaderStateType nfcReaderState;
extern volatile bool nfcWriteInProgress;
extern bool tagProcessed;



#endif