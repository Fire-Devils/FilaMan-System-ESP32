#include "nfc.h"
#include <Arduino.h>
#include <Preferences.h>
#include <Adafruit_PN532.h>
#include <ArduinoJson.h>
#include "config.h"
#include "website.h"
#include "api.h"
#include "esp_task_wdt.h"
#include "scale.h"
#include "main.h"
#include "lang.h"

//Adafruit_PN532 nfc(PN532_SCK, PN532_MISO, PN532_MOSI, PN532_SS);
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// Adafruit_PN532::begin() toggles RSTPDN too briefly (~1ms/2ms) for some PN532
// modules to reliably leave powerdown, which then makes getFirmwareVersion()
// return 0 right after begin(). Pulse it explicitly with more margin first.
static void pulsePn532Reset() {
  pinMode(PN532_RESET, OUTPUT);
  digitalWrite(PN532_RESET, LOW);
  delay(50);
  digitalWrite(PN532_RESET, HIGH);
  delay(200);
}

TaskHandle_t RfidReaderTask;
// AsyncWebServerRequest* volatile activeNfcWriteRequest = nullptr; // Removed

JsonDocument rfidData;
String activeSpoolId = "";
String activeTagUuid = "";
String lastSpoolId = "";
String nfcJsonData = "";
volatile bool tagProcessed = false;
extern uint8_t weightSend;  // main.cpp
volatile bool isBambuTag = false;
volatile bool nfcReadingTaskSuspendRequest = false;
volatile bool nfcReadingTaskSuspendState = false;
volatile bool nfcStableOnly = false;        // Einstellung "NFC nur bei stabilem Gewicht" (Standard aus), siehe updateNfcGate in scale.cpp
volatile bool nfcRfFieldOff = false;         // true = PN532-Funkfeld per RFConfiguration abgeschaltet (Gate zu). Sonst bleibt es nach dem Pollen an und verschiebt die Waage um ~180 g
volatile unsigned long nfcRfFieldOffAt = 0;  // millis() des Abschaltens, fuer die Sperrzeit der Waage
volatile bool nfcRescanRequest = false;  // vom Gate gesetzt: Gewicht hat sich seit dem letzten Lesen geaendert (Spule weg/gewechselt), Tag-Zustand beim naechsten Fenster zuruecksetzen
volatile bool nfcGateClosed = false; // true = Waage misst: NFC-Polling pausiert (siehe updateNfcGate in scale.cpp)
volatile bool nfcWriteInProgress = false; // Prevent any tag operations during write
volatile bool scanRequestActive = false;  // Set by website when frontend requests a tag scan
unsigned long scanRequestStartedAtMs = 0;
const unsigned long SCAN_REQUEST_TIMEOUT_MS = 30000;

void sendScanRequestErrorToBackend(const char* errorMessage) {
    if (!scanRequestActive) {
        return;
    }

    JsonDocument doc;
    doc["scan_status"] = "error";
    doc["error_message"] = errorMessage;
    doc["source"] = "rfid_scan_request";

    String payload;
    serializeJson(doc, payload);
    sendTagDataAsync(payload);
    scanRequestActive = false;
    scanRequestStartedAtMs = 0;
}

void setScanRequest(bool active) {
    scanRequestActive = active;
    if (active) {
        scanRequestStartedAtMs = millis();
    } else {
        scanRequestStartedAtMs = 0;
    }
}

volatile nfcReaderStateType nfcReaderState = NFC_IDLE;
// 0 = nicht gelesen
// 1 = erfolgreich gelesen
// 2 = fehler beim Lesen
// 3 = schreiben
// 4 = fehler beim Schreiben
// 5 = erfolgreich geschrieben
// 6 = reading
// ***** PN532

// ##### Bambu Tag Helper Functions #####
// Simplified: only read UID, no decryption needed (UID is always visible)
bool detectBambuTag(const uint8_t* uid, uint8_t uidLength) {
    Serial.println("Detected Bambu Lab tag (Mifare Classic) - reading UID only");

    // Create UID string with separators (same format as NTAG)
    String uidString = "";
    for (uint8_t i = 0; i < uidLength; i++) {
        if (uid[i] < 0x10) uidString += "0";
        uidString += String(uid[i], HEX);
        if (i < uidLength - 1) {
            uidString += ":";
        }
    }
    uidString.toUpperCase();

    Serial.print("  UID: ");
    Serial.println(uidString);

    isBambuTag = true;
    activeTagUuid = uidString;
    activeSpoolId = ""; // Will be resolved by FilaMan API based on tag UID

    // Create minimal JSON for compatibility
    JsonDocument doc;
    doc["vendor"] = "Bambu Lab";
    doc["type"] = "Bambu";
    nfcJsonData = "";
    serializeJson(doc, nfcJsonData);

    // Set normal read success state - isBambuTag flag is set for later use
    nfcReaderState = NFC_READ_SUCCESS;

    return true;
}

// ##### Funktionen für RFID #####
uint16_t readTagSize()
{
  uint8_t buffer[4];
  memset(buffer, 0, 4);
  nfc.ntag2xx_ReadPage(3, buffer);
  return buffer[2]*8;
}

// Robust page reading with error recovery
bool robustPageRead(uint8_t page, uint8_t* buffer) {
    const int MAX_READ_ATTEMPTS = 3;

    for (int attempt = 0; attempt < MAX_READ_ATTEMPTS; attempt++) {
        esp_task_wdt_reset();
        yield();

        if (nfc.ntag2xx_ReadPage(page, buffer)) {
            return true;
        }

        Serial.printf("Page %d read failed, attempt %d/%d\n", page, attempt + 1, MAX_READ_ATTEMPTS);

        // Try to stabilize connection between attempts
        if (attempt < MAX_READ_ATTEMPTS - 1) {
            vTaskDelay(pdMS_TO_TICKS(25));

            // Re-verify tag presence with quick check
            uint8_t uid[7];
            uint8_t uidLength;
            if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 100)) {
                Serial.println("Tag lost during read operation");
                return false;
            }
        }
    }

    return false;
}

// Bestimmt den NTAG-Typ ausschliesslich aus dem Capability Container (Seite 3).
// Keine Leseproben auf Seiten ausserhalb des Datenbereichs (die koennen je nach Tag/Reader
// fehlschlagen oder erfolgreich sein und sind daher kein verlaesslicher Indikator).
String detectNtagType()
{
  uint8_t ccBuffer[4];
  memset(ccBuffer, 0, 4);

  if (!nfc.ntag2xx_ReadPage(3, ccBuffer)) {
    Serial.println("Failed to read capability container");
    return "UNKNOWN";
  }

  Serial.print("Capability Container: ");
  for (int i = 0; i < 4; i++) {
    if (ccBuffer[i] < 0x10) Serial.print("0");
    Serial.print(ccBuffer[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  // CC[2] = Datenbereich in Bytes / 8
  uint16_t dataAreaSize = ccBuffer[2] * 8;
  Serial.print("Data area size from CC: ");
  Serial.println(dataAreaSize);

  String tagType;
  if (ccBuffer[2] == 0x12) {
    tagType = "NTAG213";
  } else if (ccBuffer[2] == 0x3E) {
    tagType = "NTAG215";
  } else if (ccBuffer[2] == 0x6D) {
    tagType = "NTAG216";
  } else {
    // Unbekannter CC-Wert: naechstkleineren Typ anhand der Datenbereichsgroesse waehlen
    if (dataAreaSize <= 144) {
      tagType = "NTAG213";
    } else if (dataAreaSize <= 504) {
      tagType = "NTAG215";
    } else {
      tagType = "NTAG216";
    }
    Serial.print("Unbekannter CC-Wert 0x");
    Serial.print(ccBuffer[2], HEX);
    Serial.print(" - Fallback anhand Datenbereich: ");
    Serial.println(tagType);
  }

  Serial.print("Detected: ");
  Serial.println(tagType);
  return tagType;
}

// Verfuegbare Nutzdaten in Bytes (feste Werte je Typ)
uint16_t getAvailableUserDataSize(const String& tagType)
{
  uint16_t userDataSize = 0;

  if (tagType == "NTAG213") {
    // NTAG213: User data from page 4-39 (36 pages * 4 bytes = 144 bytes)
    userDataSize = 144;
  } else if (tagType == "NTAG215") {
    // NTAG215: User data from page 4-129 (126 pages * 4 bytes = 504 bytes)
    userDataSize = 504;
  } else if (tagType == "NTAG216") {
    // NTAG216: User data from page 4-225 (222 pages * 4 bytes = 888 bytes)
    userDataSize = 888;
  } else {
    // Unknown tag type: konservativ wie NTAG213
    userDataSize = 144;
    Serial.println("Unknown NTAG type, using NTAG213 size (144 bytes) as fallback");
  }

  return userDataSize;
}

// Letzte beschreibbare User-Seite (feste Werte je Typ). Darueber liegen Config-/Lock-Seiten!
uint16_t getMaxUserDataPages(const String& tagType)
{
  uint16_t maxPages = 0;

  if (tagType == "NTAG213") {
    maxPages = 39; // Pages 4-39 are user data
  } else if (tagType == "NTAG215") {
    maxPages = 129; // Pages 4-129 are user data
  } else if (tagType == "NTAG216") {
    maxPages = 225; // Pages 4-225 are user data
  } else {
    // Conservative fallback
    maxPages = 39;
    Serial.println("Unknown tag type, using NTAG213 page limit as fallback");
  }

  Serial.print("Maximum writable page: ");
  Serial.println(maxPages);
  return maxPages;
}

// Liefert die lesbare Tag-Groesse (aus CC), begrenzt auf 888 Bytes und auf Vielfache von 4.
// Rueckgabe 0 = nicht lesbar / kein NTAG.
static uint16_t getReadableTagSize()
{
  uint16_t tagSize = readTagSize();
  if (tagSize > 888) tagSize = 888;
  tagSize &= ~((uint16_t)3);
  return tagSize;
}

uint8_t ntag2xx_WriteNDEF(const char *payload) {
  // Determine exact tag type and capabilities first (Typ nur einmal ermitteln)
  String tagType = detectNtagType();
  uint16_t tagSize = readTagSize();
  uint16_t availableUserData = getAvailableUserDataSize(tagType);
  uint16_t maxWritablePage = getMaxUserDataPages(tagType);

  Serial.println("=== NFC TAG ANALYSIS ===");
  Serial.print("Tag Type: ");Serial.println(tagType);
  Serial.print("Total Tag Size: ");Serial.println(tagSize);
  Serial.print("Available User Data: ");Serial.println(availableUserData);
  Serial.print("Max Writable Page: ");Serial.println(maxWritablePage);
  Serial.println("========================");

  uint8_t pageBuffer[4] = {0, 0, 0, 0};
  Serial.println("Beginne mit dem Schreiben der NDEF-Nachricht...");

  // Figure out how long the string is
  size_t payloadLenRaw = strlen(payload);
  if (payloadLenRaw > availableUserData) {
    // Passt sicher nicht auf den Tag (verhindert auch Ueberlauf von uint16_t)
    Serial.print("FEHLER: Payload zu gross: ");
    Serial.println((unsigned int)payloadLenRaw);
    oledDisplayText(tr(STR_TAG_TOO_SMALL));
    oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    oledClearPriority();
    return 0;
  }
  uint16_t payloadLen = (uint16_t)payloadLenRaw;
  Serial.print("Länge der Payload: ");
  Serial.println(payloadLen);

  Serial.print("Payload: ");Serial.println(payload);

  // MIME type for JSON
  const char mimeType[] = "application/json";
  uint8_t mimeTypeLen = strlen(mimeType);

  // Calculate NDEF record size
  // Short Record (payload <= 255): Header + Type Length + 1 Byte Payload Length = 3
  // Normal Record (payload > 255):  Header + Type Length + 4 Byte Payload Length = 6
  uint8_t ndefRecordHeaderSize = (payloadLen <= 255) ? 3 : 6;
  uint16_t ndefRecordSize = ndefRecordHeaderSize + mimeTypeLen + payloadLen;

  // Calculate TLV size - need to check if we need extended length format
  uint8_t tlvHeaderSize;
  uint16_t totalTlvSize;

  if (ndefRecordSize <= 254) {
    // Standard TLV format: Tag (1) + Length (1) + Value (ndefRecordSize)
    tlvHeaderSize = 2;
    totalTlvSize = tlvHeaderSize + ndefRecordSize + 1; // +1 for terminator TLV
  } else {
    // Extended TLV format: Tag (1) + 0xFF + Length (2) + Value (ndefRecordSize)
    tlvHeaderSize = 4;
    totalTlvSize = tlvHeaderSize + ndefRecordSize + 1; // +1 for terminator TLV
  }

  Serial.print("NDEF Record Size: ");
  Serial.println(ndefRecordSize);
  Serial.print("Total TLV Size: ");
  Serial.println(totalTlvSize);

  // Check if the message fits in the available user data space
  if (totalTlvSize > availableUserData) {
    Serial.println();
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println("FEHLER: Payload zu groß für diesen Tag-Typ!");
    Serial.print("Tag-Typ: ");Serial.println(tagType);
    Serial.print("Benötigt: ");Serial.print(totalTlvSize);Serial.println(" Bytes");
    Serial.print("Verfügbar: ");Serial.print(availableUserData);Serial.println(" Bytes");
    Serial.print("Überschuss: ");Serial.print(totalTlvSize - availableUserData);Serial.println(" Bytes");

    if (tagType == "NTAG213") {
      Serial.println("EMPFEHLUNG: Verwenden Sie einen NTAG215 (504 Bytes) oder NTAG216 (888 Bytes) Tag!");
      Serial.println("Oder kürzen Sie die Payload um mindestens " + String(totalTlvSize - availableUserData) + " Bytes.");
    }
    Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!");
    Serial.println();

    oledDisplayText(tr(STR_TAG_TOO_SMALL));
    oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    oledClearPriority();

    return 0;
  }

  Serial.println("✓ Payload passt in den Tag - Schreibvorgang wird fortgesetzt");

  // STEP 1: NFC Interface Reset and Reinitialization
  Serial.println();
  Serial.println("=== SCHRITT 1: NFC-INTERFACE RESET UND NEUINITIALISIERUNG ===");

  // First, check if the NFC interface is working at all
  Serial.println("Teste aktuellen NFC-Interface-Zustand...");

  // Try to read capability container (which worked during detection)
  uint8_t ccTest[4];
  bool ccReadable = nfc.ntag2xx_ReadPage(3, ccTest);
  Serial.print("Capability Container (Seite 3) lesbar: ");
  Serial.println(ccReadable ? "✓" : "❌");

  if (!ccReadable) {
    Serial.println("❌ NFC-Interface ist nicht funktionsfähig - führe Reset durch");

    // Perform NFC interface reset and reinitialization
    Serial.println("Führe NFC-Interface Reset durch...");

    // Step 1: Try to reinitialize the NFC interface completely
    Serial.println("1. Neuinitialisierung des PN532...");

    // Reinitialize the PN532
    pulsePn532Reset();
    nfc.begin();
    vTaskDelay(pdMS_TO_TICKS(500)); // Give it time to initialize

    // Check firmware version to ensure communication is working
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (versiondata) {
      Serial.print("PN532 Firmware Version: 0x");
      Serial.println(versiondata, HEX);
      Serial.println("✓ PN532 Kommunikation wiederhergestellt");
    } else {
      Serial.println("❌ PN532 Kommunikation fehlgeschlagen");
      oledDisplayText(tr(STR_NFC_RESET_FAILED));
      oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
      vTaskDelay(pdMS_TO_TICKS(3000));
      oledClearPriority();

      return 0;
    }

    // Step 2: Reconfigure SAM
    Serial.println("2. SAM-Konfiguration...");
    nfc.SAMConfig();
    vTaskDelay(pdMS_TO_TICKS(200));

    // Step 3: Re-detect the tag
    Serial.println("3. Tag-Wiedererkennung...");
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;
    bool tagRedetected = false;

    for (int attempts = 0; attempts < 5; attempts++) {
      Serial.print("Tag-Erkennungsversuch ");
      Serial.print(attempts + 1);
      Serial.print("/5... ");

      if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000)) {
        Serial.println("✓");
        tagRedetected = true;
        break;
      } else {
        Serial.println("❌");
        vTaskDelay(pdMS_TO_TICKS(300));
      }
    }

    if (!tagRedetected) {
      Serial.println("❌ Tag konnte nach Reset nicht wiedererkannt werden");
      oledDisplayText(tr(STR_TAG_LOST_RESET));
      oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
      vTaskDelay(pdMS_TO_TICKS(3000));
      oledClearPriority();

      return 0;
    }

    Serial.println("✓ Tag erfolgreich wiedererkannt");

    // Step 4: Test basic page reading
    Serial.println("4. Test der Grundfunktionalität...");
    vTaskDelay(pdMS_TO_TICKS(200)); // Give interface time to stabilize

    ccReadable = nfc.ntag2xx_ReadPage(3, ccTest);
    Serial.print("Capability Container nach Reset lesbar: ");
    Serial.println(ccReadable ? "✓" : "❌");

    if (!ccReadable) {
      Serial.println("❌ NFC-Interface funktioniert nach Reset immer noch nicht");
      oledDisplayText(tr(STR_NFC_STILL_BROKEN));
      oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
      vTaskDelay(pdMS_TO_TICKS(3000));
      oledClearPriority();

      return 0;
    }

    Serial.println("✓ NFC-Interface erfolgreich wiederhergestellt");
  } else {
    Serial.println("✓ NFC-Interface ist funktionsfähig");
  }

  // Display CC content for debugging
  if (ccReadable) {
    Serial.print("CC Inhalt: ");
    for (int i = 0; i < 4; i++) {
      if (ccTest[i] < 0x10) Serial.print("0");
      Serial.print(ccTest[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
  }

  Serial.println("=== SCHRITT 2: INTERFACE-FUNKTIONSTEST ===");

  // Test a few critical pages to ensure stable operation
  uint8_t testData[4];
  bool basicPagesReadable = true;

  for (uint8_t testPage = 0; testPage <= 6; testPage++) {
    bool readable = nfc.ntag2xx_ReadPage(testPage, testData);
    Serial.print("Seite ");
    Serial.print(testPage);
    Serial.print(": ");
    if (readable) {
      Serial.print("✓ - ");
      for (int i = 0; i < 4; i++) {
        if (testData[i] < 0x10) Serial.print("0");
        Serial.print(testData[i], HEX);
        Serial.print(" ");
      }
      Serial.println();
    } else {
      Serial.println("❌ - Nicht lesbar");
      if (testPage >= 3 && testPage <= 6) { // Critical pages for NDEF
        basicPagesReadable = false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // Small delay between reads
  }

  if (!basicPagesReadable) {
    Serial.println("❌ KRITISCHER FEHLER: Grundlegende NDEF-Seiten nicht lesbar!");
    Serial.println("Tag oder Interface ist defekt");
    oledDisplayText(tr(STR_TAG_DEFECT));
    oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    oledClearPriority();

    return 0;
  }

  Serial.println("✓ Alle kritischen Seiten sind lesbar");
  Serial.println("===================================================");

  Serial.println();
  Serial.println("=== SCHRITT 3: SCHREIBBEREITSCHAFTSTEST ===");

  // Reiner Lesetest (kein Testschreibzugriff, damit keine Tag-Daten veraendert werden)
  Serial.println("Teste Lesezugriff auf Nutzdaten des Tags...");

  uint8_t originalPage[4];

  if (!nfc.ntag2xx_ReadPage(10, originalPage)) {
    Serial.println("FEHLER: Kann Testseite nicht lesen");
    oledDisplayText(tr(STR_TEST_READ_ERROR));
    oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    oledClearPriority();

    return 0;
  }

  Serial.print("Inhalt Seite 10: ");
  for (int i = 0; i < 4; i++) {
    if (originalPage[i] < 0x10) Serial.print("0");
    Serial.print(originalPage[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  Serial.println("✓ Lesetest erfolgreich");
  Serial.println("======================================================");

  // HINWEIS: Die alte NDEF-Struktur wird NICHT vorab ueberschrieben. Die Hauptschreibschleife
  // schreibt zuerst die Seiten ab Seite 5 und zuletzt Seite 4 (TLV-Kopf). Seite 4 wird dabei
  // vollstaendig ueberschrieben, eine vorherige Initialisierung ist daher unnoetig.

  // STEP 5: Allow interface to stabilize before major write operation
  Serial.println();
  Serial.println("=== SCHRITT 5: NFC-INTERFACE STABILISIERUNG ===");
  Serial.println("Stabilisiere NFC-Interface vor Hauptschreibvorgang...");

  // Give the interface time to fully settle after NDEF initialization
  vTaskDelay(pdMS_TO_TICKS(200));

  // Test interface stability with a simple read
  uint8_t stabilityTest[4];
  bool interfaceStable = false;
  for (int attempts = 0; attempts < 3; attempts++) {
    if (nfc.ntag2xx_ReadPage(4, stabilityTest)) {
      Serial.print("Interface stability test ");
      Serial.print(attempts + 1);
      Serial.println("/3: ✓");
      interfaceStable = true;
      break;
    } else {
      Serial.print("Interface stability test ");
      Serial.print(attempts + 1);
      Serial.println("/3: ❌");
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }

  if (!interfaceStable) {
    Serial.println("FEHLER: NFC-Interface ist nicht stabil genug für Schreibvorgang");
    oledDisplayText(tr(STR_NFC_UNSTABLE));
    oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    oledClearPriority();

    return 0;
  }

  Serial.println("✓ NFC-Interface ist stabil - Schreibvorgang kann beginnen");
  Serial.println("=========================================================");

  // Allocate memory for the complete TLV structure
  uint8_t* tlvData = (uint8_t*) malloc(totalTlvSize);
  if (tlvData == NULL) {
    Serial.println("Fehler: Nicht genug Speicher für TLV-Daten vorhanden.");
    oledDisplayText(tr(STR_MEMORY_ERROR));
    oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
    vTaskDelay(pdMS_TO_TICKS(2000));
    oledClearPriority();
    return 0;
  }

  // Build TLV structure
  uint16_t offset = 0;

  // TLV Header
  tlvData[offset++] = 0x03; // NDEF Message TLV Tag

  if (ndefRecordSize <= 254) {
    // Standard length format
    tlvData[offset++] = (uint8_t)ndefRecordSize;
  } else {
    // Extended length format
    tlvData[offset++] = 0xFF;
    tlvData[offset++] = (uint8_t)(ndefRecordSize >> 8);  // High byte
    tlvData[offset++] = (uint8_t)(ndefRecordSize & 0xFF); // Low byte
  }

  // NDEF Record Header
  if (payloadLen <= 255) {
    tlvData[offset++] = 0xD2; // NDEF Record Header (TNF=0x2:MIME Media + SR + ME + MB)
    tlvData[offset++] = mimeTypeLen; // Type Length
    tlvData[offset++] = (uint8_t)payloadLen; // Payload Length (short record format)
  } else {
    tlvData[offset++] = 0xC2; // NDEF Record Header (TNF=0x2:MIME Media + ME + MB, kein SR)
    tlvData[offset++] = mimeTypeLen; // Type Length
    tlvData[offset++] = 0x00; // Payload Length (4 Byte, Big Endian)
    tlvData[offset++] = 0x00;
    tlvData[offset++] = (uint8_t)(payloadLen >> 8);
    tlvData[offset++] = (uint8_t)(payloadLen & 0xFF);
  }

  // MIME Type
  memcpy(&tlvData[offset], mimeType, mimeTypeLen);
  offset += mimeTypeLen;

  // JSON Payload
  memcpy(&tlvData[offset], payload, payloadLen);
  offset += payloadLen;

  // Terminator TLV
  tlvData[offset] = 0xFE;

  Serial.print("Gesamt-TLV-Länge: ");
  Serial.println(offset + 1);

  // Debug: Print first 64 bytes of TLV data
  Serial.println("TLV Daten (erste 64 Bytes):");
  for (int i = 0; i < min((int)(offset + 1), 64); i++) {
    if (tlvData[i] < 0x10) Serial.print("0");
    Serial.print(tlvData[i], HEX);
    Serial.print(" ");
    if ((i + 1) % 16 == 0) Serial.println();
  }
  Serial.println();

  // Write data to tag pages (starting from page 4)
  // Reihenfolge: zuerst alle Seiten ab Seite 5, ZULETZT Seite 4 (enthaelt den TLV-Kopf).
  // Bei Abbruch bleibt so entweder der alte Kopf mit teils neuen Daten (vom Parser als
  // ungueltig erkannt) oder der komplett neue Inhalt - nie "leer aber erfolgreich".
  uint16_t bytesWritten = 0;
  uint16_t totalBytes = offset + 1;
  uint16_t totalPages = (totalBytes + 3) / 4;
  uint16_t pageNumber = 4;

  Serial.println();
  Serial.println("=== SCHRITT 6: SCHREIBE NEUE NDEF-DATEN ===");
  Serial.print("Schreibe ");
  Serial.print(totalBytes);
  Serial.print(" Bytes in ");
  Serial.print(totalPages);
  Serial.println(" Seiten...");

  // Harte Grenze: nie auf eine Seite > letzte User-Seite schreiben
  if ((uint32_t)4 + totalPages - 1 > maxWritablePage) {
    Serial.print("FEHLER: Daten wuerden Seite ");
    Serial.print(maxWritablePage);
    Serial.println(" ueberschreiten - Abbruch ohne Schreiben");
    free(tlvData);
    return 0;
  }

  for (uint16_t step = 0; step < totalPages; step++) {
    // step 0..n-2 -> Seitenindex 1..n-1 (Seite 5..), letzter Schritt -> Seitenindex 0 (Seite 4)
    uint16_t pageIdx = (step + 1 < totalPages) ? (step + 1) : 0;
    pageNumber = 4 + pageIdx;
    uint16_t byteOffset = pageIdx * 4;

    // Additional safety check before writing each page
    if (pageNumber > maxWritablePage || byteOffset >= totalBytes) {
      Serial.print("STOP: Reached maximum writable page ");
      Serial.println(maxWritablePage);
      break;
    }

    // Clear page buffer
    memset(pageBuffer, 0, 4);

    // Calculate how many bytes to write to this page
    uint16_t remaining = totalBytes - byteOffset;
    uint16_t bytesToWrite = remaining < 4 ? remaining : 4;

    // Copy data to page buffer
    memcpy(pageBuffer, &tlvData[byteOffset], bytesToWrite);

    // Write page to tag with retry mechanism
    bool writeSuccess = false;
    for (int writeAttempt = 0; writeAttempt < 3; writeAttempt++) {
      if (nfc.ntag2xx_WritePage((uint8_t)pageNumber, pageBuffer)) {
        writeSuccess = true;
        break;
      } else {
        Serial.print("Schreibversuch ");
        Serial.print(writeAttempt + 1);
        Serial.print("/3 für Seite ");
        Serial.print(pageNumber);
        Serial.println(" fehlgeschlagen");

        if (writeAttempt < 2) {
          vTaskDelay(pdMS_TO_TICKS(50)); // Wait before retry
        }
      }
    }

    if (!writeSuccess) {
      Serial.print("FEHLER beim Schreiben der Seite ");
      Serial.println(pageNumber);
      Serial.print("Möglicherweise Page-Limit erreicht für ");
      Serial.println(tagType);
      Serial.print("Erwartetes Maximum: ");
      Serial.println(maxWritablePage);

      free(tlvData);
      return 0;
    }

    // IMMEDIATE verification after each write - this is critical!
    Serial.print("Verifiziere Seite ");
    Serial.print(pageNumber);
    Serial.print("... ");

    uint8_t verifyBuffer[4];
    vTaskDelay(pdMS_TO_TICKS(20)); // Increased delay before verification

    // Verification with retry mechanism
    bool verifySuccess = false;
    for (int verifyAttempt = 0; verifyAttempt < 3; verifyAttempt++) {
      if (nfc.ntag2xx_ReadPage((uint8_t)pageNumber, verifyBuffer)) {
        bool writeMatches = true;
        for (int i = 0; i < bytesToWrite; i++) {
          if (verifyBuffer[i] != pageBuffer[i]) {
            writeMatches = false;
            Serial.println();
            Serial.print("VERIFIKATIONSFEHLER bei Byte ");
            Serial.print(i);
            Serial.print(" - Erwartet: 0x");
            Serial.print(pageBuffer[i], HEX);
            Serial.print(", Gelesen: 0x");
            Serial.println(verifyBuffer[i], HEX);
            break;
          }
        }

        if (writeMatches) {
          verifySuccess = true;
          break;
        } else if (verifyAttempt < 2) {
          Serial.print("Verifikationsversuch ");
          Serial.print(verifyAttempt + 1);
          Serial.println("/3 fehlgeschlagen, wiederhole...");
          vTaskDelay(pdMS_TO_TICKS(30));
        }
      } else {
        Serial.print("Verifikations-Read-Versuch ");
        Serial.print(verifyAttempt + 1);
        Serial.println("/3 fehlgeschlagen");
        if (verifyAttempt < 2) {
          vTaskDelay(pdMS_TO_TICKS(30));
        }
      }
    }

    if (!verifySuccess) {
      Serial.println("❌ SCHREIBVORGANG/VERIFIKATION FEHLGESCHLAGEN!");
      free(tlvData);
      return 0;
    } else {
      Serial.println("✓");
    }

    Serial.print("Seite ");
    Serial.print(pageNumber);
    Serial.print(" ✓: ");
    for (int i = 0; i < 4; i++) {
      if (pageBuffer[i] < 0x10) Serial.print("0");
      Serial.print(pageBuffer[i], HEX);
      Serial.print(" ");
    }
    Serial.println();

    bytesWritten += bytesToWrite;

    yield();
    vTaskDelay(pdMS_TO_TICKS(10)); // Slightly increased delay between page writes
  }

  free(tlvData);

  if (bytesWritten < totalBytes) {
    Serial.println("WARNUNG: Nicht alle Daten konnten geschrieben werden!");
    Serial.print("Geschrieben: ");
    Serial.print(bytesWritten);
    Serial.print(" von ");
    Serial.print(totalBytes);
    Serial.println(" Bytes");
    Serial.print("Gestoppt bei Seite: ");
    Serial.println(pageNumber);
    return 0;
  }

  Serial.println();
  Serial.println("✓ NDEF-Nachricht erfolgreich geschrieben!");
  Serial.print("✓ Tag-Typ: ");Serial.println(tagType);
  Serial.print("✓ Insgesamt ");Serial.print(bytesWritten);Serial.println(" Bytes geschrieben");
  Serial.print("✓ Verwendete Seiten: 4-");Serial.println(4 + totalPages - 1);
  Serial.print("✓ Speicher-Auslastung: ");
  Serial.print((bytesWritten * 100) / availableUserData);
  Serial.println("%");
  Serial.println("✓ Bestehende Daten wurden überschrieben");

  // CRITICAL: Allow NFC interface to stabilize after write operation
  Serial.println();
  Serial.println("=== SCHRITT 7: NFC-INTERFACE STABILISIERUNG NACH SCHREIBVORGANG ===");
  Serial.println("Stabilisiere NFC-Interface nach Schreibvorgang...");

  // Give the tag and interface time to settle after write operation
  vTaskDelay(pdMS_TO_TICKS(300)); // Increased stabilization time

  // Test if the interface is still responsive
  uint8_t postWriteTest[4];
  bool interfaceResponsive = false;

  for (int stabilityAttempt = 0; stabilityAttempt < 5; stabilityAttempt++) {
    Serial.print("Post-write interface test ");
    Serial.print(stabilityAttempt + 1);
    Serial.print("/5... ");

    if (nfc.ntag2xx_ReadPage(3, postWriteTest)) { // Read capability container
      Serial.println("✓");
      interfaceResponsive = true;
      break;
    } else {
      Serial.println("❌");

      if (stabilityAttempt < 4) {
        Serial.println("Warte und versuche Interface zu stabilisieren...");
        vTaskDelay(pdMS_TO_TICKS(200));

        // Try to re-establish communication with a simple tag presence check
        uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
        uint8_t uidLength;
        bool tagStillPresent = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000);
        Serial.print("Tag presence check: ");
        Serial.println(tagStillPresent ? "✓" : "❌");

        if (!tagStillPresent) {
          Serial.println("Tag wurde während/nach Schreibvorgang entfernt!");
          break;
        }
      }
    }
  }

  if (!interfaceResponsive) {
    Serial.println("WARNUNG: NFC-Interface reagiert nach Schreibvorgang nicht mehr stabil");
    Serial.println("Schreibvorgang war erfolgreich, aber Interface benötigt möglicherweise Reset");
  } else {
    Serial.println("✓ NFC-Interface ist nach Schreibvorgang stabil");
  }

  Serial.println("==================================================================");

  return 1;
}

// bufferSize = Anzahl gueltiger Bytes in encodedMessage (Puffer beginnt bei Seite 4).
bool decodeNdefAndReturnJson(const byte* encodedMessage, size_t bufferSize, String uidString) {
  oledShowProgressBar(1, 4, tr(STR_READING), tr(STR_DECODING_DATA));
  oledSetPriority(DISPLAY_PRIORITY_ACTION, 1500);

  if (encodedMessage == nullptr || bufferSize < 4) {
    Serial.println("NDEF decode: buffer too small");
    return false;
  }

  // Debug: Print first 32 bytes (or less) of the raw data
  size_t dumpLen = bufferSize < 32 ? bufferSize : 32;
  Serial.println("Raw NDEF data (first 32 bytes):");
  for (size_t i = 0; i < dumpLen; i++) {
    if (encodedMessage[i] < 0x10) Serial.print("0");
    Serial.print(encodedMessage[i], HEX);
    Serial.print(" ");
    if ((i + 1) % 16 == 0) Serial.println();
  }
  Serial.println();

  // Look for the NDEF TLV structure starting from the beginning
  size_t tlvOffset = 0;
  bool foundNdefTlv = false;

  // Search for NDEF TLV (0x03) in the first few bytes
  size_t searchLen = bufferSize < 16 ? bufferSize : 16;
  for (size_t i = 0; i < searchLen; i++) {
    if (encodedMessage[i] == 0x03) {
      tlvOffset = i;
      foundNdefTlv = true;
      Serial.print("Found NDEF TLV at offset: ");
      Serial.println((unsigned int)tlvOffset);
      break;
    }
  }

  if (!foundNdefTlv) {
    Serial.println("No NDEF TLV found in tag data");
    return false;
  }

  // Get the NDEF message length from TLV
  uint32_t ndefMessageLength = 0;
  size_t ndefRecordOffset = 0;

  if (tlvOffset + 2 > bufferSize) {
    Serial.println("Invalid NDEF structure - TLV length beyond buffer");
    return false;
  }

  if (encodedMessage[tlvOffset + 1] == 0xFF) {
    // Extended length format: next 2 bytes contain the actual length
    if (tlvOffset + 4 > bufferSize) {
      Serial.println("Invalid NDEF structure - extended TLV length beyond buffer");
      return false;
    }
    ndefMessageLength = ((uint32_t)encodedMessage[tlvOffset + 2] << 8) | encodedMessage[tlvOffset + 3];
    ndefRecordOffset = tlvOffset + 4; // Skip TLV tag + 0xFF + 2 length bytes
    Serial.print("NDEF Message Length (extended): ");
  } else {
    // Standard length format: single byte contains the length
    ndefMessageLength = encodedMessage[tlvOffset + 1];
    ndefRecordOffset = tlvOffset + 2; // Skip TLV tag + 1 length byte
    Serial.print("NDEF Message Length (standard): ");
  }
  Serial.println(ndefMessageLength);

  // Need at least record header + type length + 1 byte payload length
  if (ndefRecordOffset + 3 > bufferSize) {
    Serial.println("Invalid NDEF structure - record header beyond buffer");
    return false;
  }

  // Get pointer to NDEF record; recordAvail = bytes available from the record start
  const byte* ndefRecord = &encodedMessage[ndefRecordOffset];
  size_t recordAvail = bufferSize - ndefRecordOffset;

  // Parse NDEF record header
  byte recordHeader = ndefRecord[0];
  byte typeLength = ndefRecord[1];

  Serial.print("NDEF Record Header: 0x");
  Serial.println(recordHeader, HEX);
  Serial.print("Type Length: ");
  Serial.println(typeLength);

  // Determine payload length (can be 1 or 4 bytes depending on SR flag)
  uint32_t payloadLength = 0;
  size_t payloadLengthBytes = 1;
  size_t payloadLengthOffset = 2;

  // Check if Short Record (SR) flag is set (bit 4)
  if (recordHeader & 0x10) { // SR flag
    payloadLength = ndefRecord[2];
    payloadLengthBytes = 1;
    payloadLengthOffset = 2;
  } else {
    // Long record format (4 bytes for payload length)
    if (recordAvail < 6) {
      Serial.println("Invalid NDEF structure - long payload length beyond buffer");
      return false;
    }
    payloadLength = ((uint32_t)ndefRecord[2] << 24) | ((uint32_t)ndefRecord[3] << 16) |
                    ((uint32_t)ndefRecord[4] << 8) | (uint32_t)ndefRecord[5];
    payloadLengthBytes = 4;
    payloadLengthOffset = 2;
  }

  Serial.print("Payload Length: ");
  Serial.println(payloadLength);
  Serial.print("Payload Length Bytes: ");
  Serial.println((unsigned int)payloadLengthBytes);

  // Check for ID field (if IL flag is set)
  size_t idLength = 0;
  if (recordHeader & 0x08) { // IL flag
    if (payloadLengthOffset + payloadLengthBytes >= recordAvail) {
      Serial.println("Invalid NDEF structure - ID length beyond buffer");
      return false;
    }
    idLength = ndefRecord[payloadLengthOffset + payloadLengthBytes];
    Serial.print("ID Length: ");
    Serial.println((unsigned int)idLength);
  }

  // Calculate offset to payload
  // Layout: Header, TypeLen, PayloadLen(1|4), [IDLen], Type, [ID], Payload
  size_t idLenFieldSize = (recordHeader & 0x08) ? 1 : 0;
  size_t payloadOffset = 1 + 1 + payloadLengthBytes + idLenFieldSize + typeLength + idLength;

  Serial.print("Calculated payload offset: ");
  Serial.println((unsigned int)payloadOffset);

  // Verify we have enough data (64 bit to avoid overflow with huge declared lengths)
  uint64_t payloadEnd = (uint64_t)payloadOffset + payloadLength;
  if (payloadEnd > ndefMessageLength) {
    Serial.println("Invalid NDEF structure - payload extends beyond message");
    Serial.print("Payload offset + length: ");
    Serial.print((unsigned long)payloadEnd);
    Serial.print(", NDEF message length: ");
    Serial.println(ndefMessageLength);
    return false;
  }
  if (payloadEnd > recordAvail) {
    Serial.println("Invalid NDEF structure - payload extends beyond read buffer");
    Serial.print("Payload end: ");
    Serial.print((unsigned long)payloadEnd);
    Serial.print(", available: ");
    Serial.println((unsigned int)recordAvail);
    return false;
  }

  // Print the record type for debugging (typeLength is within buffer: payloadOffset <= payloadEnd <= recordAvail)
  Serial.print("Record Type: ");
  for (size_t i = 0; i < typeLength; i++) {
    Serial.print((char)ndefRecord[1 + 1 + payloadLengthBytes + idLenFieldSize + i]);
  }
  Serial.println();

  // JSON lokal aufbauen (nfcJsonData wird von anderen Tasks gelesen) und am Ende einmal zuweisen
  String jsonLocal = "";

  // Extract JSON payload with validation
  uint32_t actualJsonLength = 0;
  int braceCount = 0;
  for (uint32_t i = 0; i < payloadLength; i++) {
    byte currentByte = ndefRecord[payloadOffset + i];

    // Stop at null terminator or if we find the end of JSON
    if (currentByte == 0x00) {
      Serial.print("Found null terminator at position: ");
      Serial.println(i);
      break;
    }

    // Only add printable characters and common JSON characters
    if (currentByte >= 32 && currentByte <= 126) {
      jsonLocal += (char)currentByte;
      actualJsonLength++;
    } else {
      Serial.print("Skipping non-printable byte at position ");
      Serial.print(i);
      Serial.print(": 0x");
      Serial.println(currentByte, HEX);
    }

    if (currentByte == '{') braceCount++;
    else if (currentByte == '}') braceCount--;

    // Check if we've reached the end of a JSON object
    if (currentByte == '}' && braceCount == 0) {
      Serial.print("Found complete JSON object at position: ");
      Serial.println(i);
      actualJsonLength = i + 1;
      break;
    }
  }

  Serial.print("Actual JSON length extracted: ");
  Serial.println(actualJsonLength);
  Serial.print("Total nfcJsonData length: ");
  Serial.println(jsonLocal.length());
  Serial.println("=== DECODED JSON DATA START ===");
  Serial.println(jsonLocal);
  Serial.println("=== DECODED JSON DATA END ===");

  // Check if JSON was truncated
  if (jsonLocal.length() < payloadLength && !jsonLocal.endsWith("}")) {
    Serial.println("WARNING: JSON payload appears to be truncated!");
    Serial.print("Expected payload length: ");
    Serial.println(payloadLength);
    Serial.print("Actual extracted length: ");
    Serial.println(jsonLocal.length());
  }

  // Trim any trailing whitespace or invalid characters
  jsonLocal.trim();

  // Einmalige Zuweisung an die globale Variable
  nfcJsonData = jsonLocal;

  // JSON-Dokument verarbeiten
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, nfcJsonData);
  if (error)
  {
    nfcJsonData = "";
    Serial.println("Fehler beim Verarbeiten des JSON-Dokuments");
    return false;
  }
  else
  {
    if(filamanConnected){
      Serial.println("JSON-Dokument erfolgreich verarbeitet");
      String idKey = doc["spool_id"].is<String>() ? "spool_id" : "sm_id";
      if (doc[idKey].is<String>() && doc[idKey] != "" && doc[idKey] != "0")
      {
        if (scanRequestActive) {
          scanRequestActive = false;
          sendTagDataAsync(nfcJsonData);
          tagProcessed = true;
        } else {
          oledShowProgressBar(2, 4, tr(STR_SPOOL_TAG), tr(STR_WEIGHING));
          oledSetPriority(DISPLAY_PRIORITY_ACTION, 2000);
          activeSpoolId = doc[idKey].as<String>();
          lastSpoolId = activeSpoolId;
        }
      }

      else if(doc["location_id"].is<int>())
      {
        Serial.println("Location Tag found!");
        int locId = doc["location_id"].as<int>();

        // Check if a spool was scanned before
        if (lastSpoolId.length() == 0 || lastSpoolId == "0") {
          Serial.println("No spool scanned before location tag - showing warning");
          oledShowProgressBar(1, 1, tr(STR_LOCATION), tr(STR_SCAN_SPOOL_FIRST));
          oledSetPriority(DISPLAY_PRIORITY_WARNING, 3000);
        } else {
          int sId = lastSpoolId.toInt();
          sendLocationAsync(sId, "", locId, "");

          // Display feedback - location was set
          oledShowProgressBar(1, 1, tr(STR_LOCATION), tr(STR_LOCATION_SET));
          oledSetPriority(DISPLAY_PRIORITY_ACTION, 3000);

          // Clear lastSpoolId to prevent accidental re-assignment
          lastSpoolId = "";
          Serial.println("Location set - lastSpoolId cleared to prevent re-assignment");
        }

        // Mark as processed so main.cpp doesn't try to send weight
        tagProcessed = true;
        activeSpoolId = "";
      }
      else if (doc["protocol"].is<String>() &&
               (doc["protocol"].as<String>() == "openspool" ||
                doc["protocol"].as<String>() == "filaman"))
      {
        // Tag mit erweitertem Protokoll (OpenSpool/FilaMan) aber ohne spool_id/sm_id.
        // FilaMan sucht die Spule anhand der tag_uuid (rfid_uid).
        String protocolStr = doc["protocol"].as<String>();
        String type  = doc["type"]  | "?";
        String brand = doc["brand"] | "";
        String displayText = brand.length() > 0 ? (brand + " " + type) : type;
        Serial.println("Extended-data tag: protocol=" + protocolStr + " type=" + type + " brand=" + brand);
        activeSpoolId = ""; // Keine spool_id/sm_id → Gewicht wird mit tag_uuid gesendet
        if (scanRequestActive) {
            scanRequestActive = false;
            sendTagDataAsync(nfcJsonData);
            tagProcessed = true;
        } else {
            oledShowProgressBar(2, 4, tr(STR_SPOOL_TAG), displayText.c_str());
            oledSetPriority(DISPLAY_PRIORITY_ACTION, 2000);
        }
      }
      else
      {
        Serial.println("Unbekannter Tag-Inhalt.");
        activeSpoolId = "";
        oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_UNKNOWN_TAG));
        oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
      }
    } else {
      oledShowProgressBar(4, 4, tr(STR_FAILURE_EXCL), tr(STR_API_OFFLINE));
      oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
    }
  }

  doc.clear();

  return true;
}

void writeJsonToTag(void *parameter) {
  NfcWriteParameterType* params = (NfcWriteParameterType*)parameter;

  // Gib die erstellte NDEF-Message aus
  Serial.println("Erstelle NDEF-Message...");
  Serial.println(params->payload);

  nfcReaderState = NFC_WRITING;
  nfcWriteInProgress = true; // Block high-level tag operations during write

  // Do NOT suspend the reading task - we need NFC interface for verification
  // Just use nfcWriteInProgress to prevent scanning
  Serial.println("NFC Write Task starting - High-level operations blocked, low-level NFC available");

  // Vor dem ersten PN532-Zugriff warten, bis der Reader-Task pausiert ist (max. 2 s)
  {
    uint16_t waitedMs = 0;
    while (!nfcReadingTaskSuspendState && waitedMs < 2000) {
      vTaskDelay(pdMS_TO_TICKS(10));
      waitedMs += 10;
    }
    if (!nfcReadingTaskSuspendState) {
      Serial.println("WARNUNG: Reader-Task nach 2s noch nicht pausiert - schreibe trotzdem");
    }
  }

  // aktualisieren der Website wenn sich der Status ändert
  sendNfcData();
  vTaskDelay(pdMS_TO_TICKS(100));

  // Show initial waiting message with countdown
  char _waitBuf[20];
  snprintf(_waitBuf, sizeof(_waitBuf), tr(STR_WAIT_FMT), 30);
  oledShowProgressBar(0, 30, tr(STR_WRITE_TAG), _waitBuf);
  oledSetPriority(DISPLAY_PRIORITY_ACTION, 1500);

  // Wait up to 30 seconds for tag
  uint8_t success = 0;
  String uidString = "";
  uint8_t uidLength = 0;
  unsigned long startTime = millis();
  uint8_t lastSecondsShown = 30;

  while (millis() - startTime < 30000) {
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
    // yield before potentially waiting for 400ms
    yield();
    esp_task_wdt_reset();

    // Update countdown display every second
    unsigned long elapsed = millis() - startTime;
    uint8_t remainingSeconds = (30000 - elapsed) / 1000;
    if (remainingSeconds != lastSecondsShown) {
      lastSecondsShown = remainingSeconds;
      char countdownText[20];
      snprintf(countdownText, sizeof(countdownText), tr(STR_WAIT_FMT), remainingSeconds);
      oledShowProgressBar(elapsed / 1000, 30, tr(STR_WRITE_TAG), countdownText);
      oledSetPriority(DISPLAY_PRIORITY_ACTION, 1500);
    }

    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 400);

    if (success) {
      for (uint8_t i = 0; i < uidLength; i++) {
        if (uid[i] < 0x10) uidString += "0";
        uidString += String(uid[i], HEX);
        if (i < uidLength - 1) {
            uidString += ":"; // Trennzeichen hinzufügen
        }
      }
      uidString.toUpperCase();
      break;
    }

    yield();
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  if (success)
  {
    // Check if this is a Bambu tag (Mifare Classic has 4 byte UID, NTAG has 7 byte)
    // If so, skip writing and just send success with weight to API
    if (uidLength != 7) {
        Serial.println("Bambu Lab tag detected during write - skipping write, sending result with weight");
        oledShowProgressBar(1, 1, tr(STR_WRITE_TAG), tr(STR_DONE));
        oledSetPriority(DISPLAY_PRIORITY_ACTION, 2000);

        // Send success to API with tag_uuid and current weight
        sendRfidResultAsync(uidString, params->spoolId, params->locationId, true, "", weight);

        vTaskDelay(pdMS_TO_TICKS(500));

        // Clean up and exit
        nfcWriteInProgress = false;
        nfcReaderState = NFC_IDLE;
        free(params->payload);
        delete params;
        vTaskDelete(NULL);
    }

    oledShowProgressBar(1, 3, tr(STR_WRITE_TAG), tr(STR_WRITING));
    oledSetPriority(DISPLAY_PRIORITY_ACTION, 2000);

    // Schreibe die NDEF-Message auf den Tag
    success = ntag2xx_WriteNDEF(params->payload);
    if (success)
    {
        Serial.println("NDEF-Message erfolgreich auf den Tag geschrieben");
        //oledDisplayText("NFC-Tag written");
        //vTaskDelay(pdMS_TO_TICKS(1000));
        nfcReaderState = NFC_WRITE_SUCCESS;
        // aktualisieren der Website wenn sich der Status ändert
        sendNfcData();
        if(params->tagType){
          oledShowProgressBar(1, 1, tr(STR_WRITE_TAG), tr(STR_DONE));
        }else{
          oledShowProgressBar(1, 1, tr(STR_WRITE_TAG), tr(STR_DONE));
        }
        oledSetPriority(DISPLAY_PRIORITY_ACTION, 3000);

        // CRITICAL: Properly stabilize NFC interface after write operation
        Serial.println();
        Serial.println("=== POST-WRITE NFC STABILIZATION ===");

        // Wait for tag operations to complete
        vTaskDelay(pdMS_TO_TICKS(500));

        // Test tag presence and remove detection
        uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
        uint8_t uidLength;
        int tagRemovalChecks = 0;

        Serial.println("Warte bis Tag entfernt wird...");

        // Monitor tag presence
        while (tagRemovalChecks < 10) {
          yield();
          esp_task_wdt_reset();

          bool tagPresent = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 500);

          if (!tagPresent) {
            Serial.println("✓ Tag wurde entfernt - NFC bereit für nächsten Scan");
            break;
          }

          tagRemovalChecks++;
          Serial.print("Tag noch vorhanden (");
          Serial.print(tagRemovalChecks);
          Serial.println("/10)");

          vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (tagRemovalChecks >= 10) {
          Serial.println("WARNUNG: Tag wurde nicht entfernt - fahre trotzdem fort");
        }

        // Additional interface stabilization before resuming normal operations
        Serial.println("Stabilisiere NFC-Interface für normale Operationen...");
        vTaskDelay(pdMS_TO_TICKS(200));

        // Test if interface is ready for normal scanning
        uint8_t interfaceTestBuffer[4];
        bool interfaceReady = false;

        for (int testAttempt = 0; testAttempt < 3; testAttempt++) {
          // Try a simple interface operation (without requiring tag presence)
          Serial.print("Interface readiness test ");
          Serial.print(testAttempt + 1);
          Serial.print("/3... ");

          // Use a safe read operation that doesn't depend on tag presence
          // This tests if the PN532 chip itself is responsive
          uint32_t versiondata = nfc.getFirmwareVersion();
          if (versiondata != 0) {
            Serial.println("✓");
            interfaceReady = true;
            break;
          } else {
            Serial.println("❌");
            vTaskDelay(pdMS_TO_TICKS(100));
          }
        }

        if (!interfaceReady) {
          Serial.println("WARNUNG: NFC-Interface reagiert nicht - könnte normale Scans beeinträchtigen");
        } else {
          Serial.println("✓ NFC-Interface ist bereit für normale Scans");
        }

        Serial.println("=========================================");

        // Send success response to API with tag_uuid and current weight
        Serial.println("Sending result to API via fire-and-forget...");
        sendRfidResultAsync(uidString, params->spoolId, params->locationId, true, "", weight);

        // vTaskResume(RfidReaderTask); // Don't resume as it was not suspended
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    else
    {
        Serial.println("Fehler beim Schreiben der NDEF-Message auf den Tag");
        oledShowIcon("failed");
        oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
        vTaskDelay(pdMS_TO_TICKS(2000));
        oledClearPriority();
        nfcReaderState = NFC_WRITE_ERROR;

        // Send error response to API
        Serial.println("Sending failure result to API via fire-and-forget...");
        sendRfidResultAsync("", params->spoolId, params->locationId, false, "Write failed");
    }
  }
  else
  {
    Serial.println("Fehler: Kein Tag zu schreiben gefunden.");
    oledShowProgressBar(1, 1, tr(STR_FAILURE_EXCL), tr(STR_NO_TAG_FOUND));
    oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
    vTaskDelay(pdMS_TO_TICKS(2000));
    oledClearPriority();
    nfcReaderState = NFC_IDLE;

    // Send timeout response to API
    Serial.println("Sending timeout result to API via fire-and-forget...");
    sendRfidResultAsync("", params->spoolId, params->locationId, false, "Timeout - no tag found");
  }

  // Reset write protection and cleanup
  oledClearPriority();
  nfcWriteInProgress = false; // Re-enable high-level tag operations

  // Make sure we are in a safe state
  if (nfcReaderState == NFC_WRITING) {
      nfcReaderState = NFC_IDLE;
  }

  free(params->payload);
  delete params;

  vTaskDelete(NULL);
}

// Ensures spool_id is always the first key in JSON for fast-path detection
String optimizeJsonForFastPath(const char* payload) {
    JsonDocument inputDoc;
    DeserializationError error = deserializeJson(inputDoc, payload);

    if (error) {
        Serial.print("JSON optimization failed: ");
        Serial.println(error.c_str());
        return String(payload); // Return original if parsing fails
    }

    // Create optimized JSON with spool_id first
    JsonDocument optimizedDoc;

    // Always add spool_id first (even if it's "0" for brand filaments)
    // Accept legacy sm_id as input too, but always emit spool_id
    if (inputDoc["spool_id"].is<int>() && inputDoc["spool_id"].as<int>() > 0) {
        optimizedDoc["spool_id"] = String(inputDoc["spool_id"].as<int>());
        Serial.print("Optimizing JSON: spool_id found = ");
        Serial.println(inputDoc["spool_id"].as<int>());
    } else if (inputDoc["spool_id"].is<String>() && inputDoc["spool_id"].as<String>() != "0" && inputDoc["spool_id"].as<String>() != "") {
        optimizedDoc["spool_id"] = inputDoc["spool_id"].as<String>();
        Serial.print("Optimizing JSON: spool_id found = ");
        Serial.println(inputDoc["spool_id"].as<String>());
    } else if (inputDoc["sm_id"].is<String>() && inputDoc["sm_id"].as<String>() != "0" && inputDoc["sm_id"].as<String>() != "") {
        optimizedDoc["spool_id"] = inputDoc["sm_id"].as<String>();
        Serial.print("Optimizing JSON: Converted legacy sm_id to spool_id = ");
        Serial.println(inputDoc["sm_id"].as<String>());
    } else {
        optimizedDoc["spool_id"] = "0"; // Default for brand filaments
        Serial.println("Optimizing JSON: No spool_id or sm_id found, setting spool_id to '0'");
    }

    for (JsonPair kv : inputDoc.as<JsonObject>()) {
        String key = kv.key().c_str();
        if (key != "sm_id" && key != "spool_id") {
            optimizedDoc[key] = kv.value();
        }
    }

    String optimizedJson;
    serializeJson(optimizedDoc, optimizedJson);

    Serial.println("JSON optimized for fast-path detection:");
    Serial.print("Original:  ");
    Serial.println(payload);
    Serial.print("Optimized: ");
    Serial.println(optimizedJson);

    inputDoc.clear();
    optimizedDoc.clear();

    return optimizedJson;
}

void startWriteJsonToTag(const bool isSpoolTag, const char* payload, int spoolId, int locationId) {
  Serial.printf("startWriteJsonToTag called for spoolId=%d locationId=%d\n", spoolId, locationId);

  // Prevent immediate re-entry before task starts
  if (nfcWriteInProgress) {
    Serial.println("startWriteJsonToTag: NFC Busy (nfcWriteInProgress=true)");
    return;
  }

  // Optimize JSON to ensure spool_id is first key for fast-path detection
  String optimizedPayload = optimizeJsonForFastPath(payload);

  NfcWriteParameterType* parameters = new NfcWriteParameterType();
  parameters->tagType = isSpoolTag;
  parameters->payload = strdup(optimizedPayload.c_str()); // Use optimized payload
  parameters->spoolId = spoolId;
  parameters->locationId = locationId;

  // Task nicht mehrfach starten
  if (nfcReaderState == NFC_IDLE || nfcReaderState == NFC_READ_ERROR || nfcReaderState == NFC_READ_SUCCESS) {
    nfcWriteInProgress = true; // Lock immediately to prevent race conditions
    Serial.println("startWriteJsonToTag: Starting task, lock acquired.");

    oledShowProgressBar(0, 1, tr(STR_WRITE_TAG), tr(STR_PLACE_TAG_NOW));
    oledSetPriority(DISPLAY_PRIORITY_ACTION, 2000);
    // Erstelle die Task
    BaseType_t result = xTaskCreatePinnedToCore(
        writeJsonToTag,        // Task-Funktion
        "WriteJsonToTagTask",       // Task-Name
        8192,                        // Stackgröße in Bytes
        (void*)parameters,         // Parameter
        rfidWriteTaskPrio,           // Priorität
        NULL,                        // Task-Handle (nicht benötigt)
        1                            // Core 1 (Arduino Core)
    );

    if (result != pdPASS) {
        Serial.println("Failed to create write task!");
        nfcWriteInProgress = false; // Release lock if task creation failed
        free(parameters->payload);
        delete parameters;
    }
  }else{
    Serial.printf("startWriteJsonToTag: State mismatch (State: %d)\n", nfcReaderState);
    oledShowProgressBar(0, 1, tr(STR_FAILURE), tr(STR_NFC_BUSY));
    oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
    free(parameters->payload);
    delete parameters;
  }
}

// Safe tag detection with manual retry logic and short timeouts
bool safeTagDetection(uint8_t* uid, uint8_t* uidLength) {
    const int MAX_ATTEMPTS = 3;
    const int SHORT_TIMEOUT = 100; // Very short timeout to prevent hanging

    for (int attempt = 0; attempt < MAX_ATTEMPTS; attempt++) {
        // Watchdog reset on each attempt
        esp_task_wdt_reset();
        yield();

        // Use short timeout to avoid blocking
        bool success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLength, SHORT_TIMEOUT);

        if (success) {
            Serial.printf("✓ Tag detected on attempt %d with %dms timeout\n", attempt + 1, SHORT_TIMEOUT);
            return true;
        }

        // Short pause between attempts
        vTaskDelay(pdMS_TO_TICKS(25));

        // Refresh RF field after failed attempt (but not on last attempt)
        if (attempt < MAX_ATTEMPTS - 1) {
            nfc.SAMConfig();
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    return false;
}

void scanRfidTask(void * parameter) {
  Serial.println("RFID Task gestartet");
  for(;;) {
    // Regular watchdog reset
    esp_task_wdt_reset();
    yield();

    if (scanRequestActive && scanRequestStartedAtMs > 0 && (millis() - scanRequestStartedAtMs) > SCAN_REQUEST_TIMEOUT_MS) {
      Serial.println("RFID scan-request timeout reached without tag");
      sendScanRequestErrorToBackend("Timeout - no tag found");
    }

    // Gate wieder offen: PN532 neu konfigurieren, danach schaltet das Pollen das Funkfeld wieder ein
    if (nfcRfFieldOff && !nfcGateClosed) {
      nfc.SAMConfig();   // bekannte Folge aus safeTagDetection; InListPassiveTarget schaltet das Feld selbst wieder an
      nfcRfFieldOff = false;
      Serial.println("NFC: Gate offen, SAMConfig");
      // Ohne Dauerpolling faellt beim direkten Spulenwechsel keine Abfrage ohne Tag an: Zustand wie nach "Tag removed" zuruecksetzen, damit der neue Tag gelesen wird
      if (nfcRescanRequest) {
        nfcRescanRequest = false;
        if (nfcReaderState != NFC_IDLE && nfcReaderState != NFC_READING && nfcReaderState != NFC_WRITING && !nfcWriteInProgress) {
          Serial.printf("NFC: Gewichtsaenderung, Tag-Zustand zurueckgesetzt (Previous state: %d)\n", nfcReaderState);
          nfcReaderState = NFC_IDLE;
          nfcJsonData = "";
          activeSpoolId = "";
          activeTagUuid = "";
          tagProcessed = false;
          weightSend = 0;  // Sendemerker mit zuruecksetzen, sonst wird der neu gelesene Tag nie gesendet (Display bleibt auf "Wiegen")
          isBambuTag = false;
          pauseMainTask = 0;
          lastRemainingShown = 0;  // Rest der vorherigen Spule nicht fuer den neuen Tag wiederverwenden
          oledClearPriority();
        }
      }
    }

    // Skip scanning during write operations, but keep NFC interface active
    if (nfcReaderState != NFC_WRITING && !nfcWriteInProgress && !nfcReadingTaskSuspendRequest && !booting && !nfcGateClosed)
    {
      nfcReadingTaskSuspendState = false;
      yield();

      uint8_t success;
      uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
      uint8_t uidLength;

      // Use safe tag detection instead of blocking readPassiveTargetID
      success = safeTagDetection(uid, &uidLength);

      foundNfcTag(nullptr, success);

      // As long as there is still a tag on the reader, do not try to read it again
      if (success && nfcReaderState == NFC_IDLE)
      {
        // Set the current tag as not processed
        tagProcessed = false;

        // Wake display when a tag is detected
        oledResetActivityTimer();

        // Display some basic information about the card
        Serial.println("Found an ISO14443A card");

        nfcReaderState = NFC_READING;
        pauseMainTask = 1;
        lastRemainingShown = 0;  // Rest der vorherigen Spule nicht fuer den neuen Tag wiederverwenden

        oledShowProgressBar(0, 4, tr(STR_READING), tr(STR_DETECTING_TAG));
        oledSetPriority(DISPLAY_PRIORITY_ACTION, 1500);

        // Stabilization time for reliable tag communication
        Serial.println("Tag detected, stabilizing...");
        vTaskDelay(pdMS_TO_TICKS(500)); // Increased from 200ms for reliable reads

        // Wurde waehrend der Stabilisierung ein Schreibvorgang/Suspend angefordert? Dann Durchlauf abbrechen
        if (nfcWriteInProgress || nfcReadingTaskSuspendRequest) {
          Serial.println("NFC: Schreib-/Suspend-Anforderung waehrend Lesevorgang - Durchlauf abgebrochen");
          nfcReaderState = NFC_IDLE;
          pauseMainTask = 0;
          lastRemainingShown = 0;  // Rest der vorherigen Spule nicht fuer den neuen Tag wiederverwenden
          oledClearPriority();
          continue;
        }

        // create Tag UID string
        String uidString = "";
        for (uint8_t i = 0; i < uidLength; i++) {
          //TBD: Rework to remove all the string operations
          if (uid[i] < 0x10) uidString += "0";
          uidString += String(uid[i], HEX);
          if (i < uidLength - 1) {
              uidString += ":"; // Optional: Trennzeichen hinzufügen
          }
        }

        if (uidLength == 7)
        {
          activeTagUuid = uidString;
          uint16_t tagSize = getReadableTagSize();
          if(tagSize > 0)
          {
            // Create a buffer depending on the size of the tag
            uint8_t* data = (uint8_t*)malloc(tagSize);
            if (data == NULL) {
              Serial.println("Fehler: Nicht genug Speicher fuer Tag-Daten");
              oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_TAG_READ_ERROR));
              oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
              nfcReaderState = NFC_READ_ERROR;
            }
            else
            {
            memset(data, 0, tagSize);

            // We probably have an NTAG2xx card (though it could be Ultralight as well)
            Serial.println("Seems to be an NTAG2xx tag (7 byte UID)");
            Serial.print("Tag size: ");
            Serial.print(tagSize);
            Serial.println(" bytes");

            uint16_t numPages = tagSize / 4;

            for (uint16_t i = 4; i < 4+numPages; i++) {

              if ((uint32_t)(i - 4) * 4 + 4 > tagSize) break; // Pufferschutz

              if (!robustPageRead((uint8_t)i, data+(i-4) * 4))
              {
                Serial.printf("Failed to read page %d after retries, stopping\n", i);
                break; // Stop if reading fails after retries
              }

              // Check for NDEF message end
              if (data[(i - 4) * 4] == 0xFE)
              {
                Serial.println("Found NDEF message end marker");
                break; // End of NDEF message
              }

              yield();
              esp_task_wdt_reset();
              // Reduced delay for faster reading
              vTaskDelay(pdMS_TO_TICKS(2)); // Reduced from 5ms to 2ms
            }

            Serial.println("Tag reading completed, starting NDEF decode...");

            if (!decodeNdefAndReturnJson(data, tagSize, uidString))
            {
              oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_UNKNOWN_TAG));
              oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
              nfcReaderState = NFC_READ_ERROR;
            }
            else
            {
              nfcReaderState = NFC_READ_SUCCESS;
            }

            free(data);
            }
          }
          else
          {
            // NTAG reading failed, try reading as Bambu Lab tag
            Serial.println("NTAG read failed, trying Bambu Lab tag...");
            if (!detectBambuTag(uid, uidLength)) {
                oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_TAG_READ_ERROR));
                oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
                nfcReaderState = NFC_READ_ERROR;
                activeSpoolId = "";
                Serial.println("Tag read failed - activeSpoolId reset to prevent autoSet");
            }
          }
        }
        else
        {
          // UID length != 7 — could be Mifare Classic (BambuLab) or non-standard NTAG.
          // Try NTAG-style NDEF read first to check for openspool/filaman protocol.
          Serial.println("UID length != 7, checking for openspool/filaman protocol first...");
          activeTagUuid = uidString;

          bool handledAsNdef = false;
          uint16_t tagSize = getReadableTagSize();
          uint8_t* data = (tagSize > 0) ? (uint8_t*)malloc(tagSize) : NULL;
          if (data != NULL)
          {
            memset(data, 0, tagSize);
            uint16_t numPages = tagSize / 4;
            for (uint16_t i = 4; i < 4 + numPages; i++) {
              if ((uint32_t)(i - 4) * 4 + 4 > tagSize) break; // Pufferschutz
              if (!robustPageRead((uint8_t)i, data + (i - 4) * 4)) {
                Serial.printf("Page %d read failed, stopping\n", i);
                break;
              }
              if (data[(i - 4) * 4] == 0xFE) {
                Serial.println("Found NDEF end marker");
                break;
              }
              yield();
              esp_task_wdt_reset();
              vTaskDelay(pdMS_TO_TICKS(2));
            }
            if (decodeNdefAndReturnJson(data, tagSize, uidString)) {
              handledAsNdef = true;
              nfcReaderState = NFC_READ_SUCCESS;
            }
            free(data);
          }

          if (!handledAsNdef)
          {
            Serial.println("No NDEF/openspool protocol found, trying Bambu Lab tag...");
            if (!detectBambuTag(uid, uidLength)) {
              oledShowProgressBar(1, 1, tr(STR_FAILURE), tr(STR_UNKNOWN_TAG_TYPE));
              oledSetPriority(DISPLAY_PRIORITY_WARNING, 2000);
              Serial.println("This doesn't seem to be an NTAG2xx tag (UUID length != 7 bytes)!");
              activeSpoolId = "";
              Serial.println("Unknown tag type - activeSpoolId reset to prevent autoSet");
            }
          }
        }
      }

      if (!success && nfcReaderState != NFC_IDLE && !nfcReadingTaskSuspendRequest)
      {
        Serial.printf("NFC: Tag removed (Previous state: %d)\n", nfcReaderState);
        nfcReaderState = NFC_IDLE;
        nfcJsonData = "";
        activeSpoolId = "";
        activeTagUuid = "";
        tagProcessed = false;
        pauseMainTask = 0;
        lastRemainingShown = 0;  // Rest der vorherigen Spule nicht fuer den neuen Tag wiederverwenden
        oledClearPriority();
        oledShowWeight(weight);
      }

      // Reset state after successful read when tag is removed
      else if (!success && nfcReaderState == NFC_READ_SUCCESS)
      {
        nfcReaderState = NFC_IDLE;
        tagProcessed = false;
        isBambuTag = false;
        Serial.println("Tag nach erfolgreichem Lesen entfernt - bereit für nächsten Tag");
      }

      // Add a pause after successful reading to prevent immediate re-reading
      if (nfcReaderState == NFC_READ_SUCCESS) {
        // After tag is processed, slow down scanning to give API time
        Serial.println("Tag processed - slowing scan to 2 seconds");
        vTaskDelay(pdMS_TO_TICKS(2000));
      } else {
        // Faster scanning when no tag or idle state
        vTaskDelay(pdMS_TO_TICKS(500));
      }

      // aktualisieren der Website wenn sich der Status ändert
      sendNfcData();
    }
    else
    {
      nfcReadingTaskSuspendState = true;

      // Different behavior for write protection vs. full suspension
      if (nfcGateClosed && !nfcWriteInProgress && !nfcReadingTaskSuspendRequest && nfcReaderState != NFC_WRITING && !booting) {
        // Gate zu: Waage misst, PN532 bleibt still (kein Funkfeld, kein I2C)
        if (!nfcRfFieldOff) {
          bool offOk = nfc.disableRFField();  // RFConfiguration: RF-Feld aus (Messung: Feld bleibt sonst nach dem Pollen an)
          nfcRfFieldOffAt = millis();
          nfcRfFieldOff = true;
          Serial.println(offOk ? "NFC: RF-Feld aus (Gate zu)" : "NFC: RF-Feld aus FEHLGESCHLAGEN");
        }
        nfcReadingTaskSuspendState = false; // Schreibvorgaenge muessen warten, bis das Gate offen und der PN532 neu konfiguriert ist
        vTaskDelay(pdMS_TO_TICKS(50));
      } else if (nfcWriteInProgress) {
        // During write: Just pause scanning, don't disable NFC interface
        // Serial.println("NFC Scanning paused during write operation");
        vTaskDelay(pdMS_TO_TICKS(100)); // Shorter delay during write
      } else {
        // Full suspension requested
        Serial.println("NFC Reading disabled");
        vTaskDelay(pdMS_TO_TICKS(1000));
      }
    }
    yield();
  }
}

void loadNfcStableOnly() {
  Preferences preferences;
  preferences.begin(NVS_NAMESPACE_SETTINGS, true);
  nfcStableOnly = preferences.getBool(NVS_KEY_NFC_STABLE_ONLY, false);
  preferences.end();
  Serial.printf("NFC nur bei stabilem Gewicht: %s\n", nfcStableOnly ? "an" : "aus");
}

void saveNfcStableOnly(bool enabled) {
  nfcStableOnly = enabled;
  Preferences preferences;
  preferences.begin(NVS_NAMESPACE_SETTINGS, false);
  preferences.putBool(NVS_KEY_NFC_STABLE_ONLY, enabled);
  preferences.end();
}

void startNfc() {
  loadNfcStableOnly();
  oledShowProgressBar(4, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_NFC_INIT));
  pulsePn532Reset();
  nfc.begin();                                           // Beginne Kommunikation mit RFID Leser

  delay(1000);
  unsigned long versiondata = nfc.getFirmwareVersion();  // Lese Versionsnummer der Firmware aus
  if (! versiondata) {                                   // Wenn keine Antwort kommt
    Serial.println("Kann kein RFID Board finden !");            // Sende Text "Kann kein..." an seriellen Monitor
    oledDisplayText(tr(STR_NO_RFID_BOARD));
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
  else {
    Serial.print("Chip PN5 gefunden"); Serial.println((versiondata >> 24) & 0xFF, HEX); // Sende Text und Versionsinfos an seriellen
    Serial.print("Firmware ver. "); Serial.print((versiondata >> 16) & 0xFF, DEC);      // Monitor, wenn Antwort vom Board kommt
    Serial.print('.'); Serial.println((versiondata >> 8) & 0xFF, DEC);                  //

    nfc.SAMConfig();
    // Set the max number of retry attempts to read from a card
    // This prevents us from waiting forever for a card, which is
    // the default behaviour of the PN532.
    //nfc.setPassiveActivationRetries(0x7F);
    //nfc.setPassiveActivationRetries(0xFF);

    BaseType_t result = xTaskCreatePinnedToCore(
      scanRfidTask, /* Function to implement the task */
      "RfidReader", /* Name of the task */
      6144,  /* Stack size in bytes */
      NULL,  /* Task input parameter */
      rfidTaskPrio,  /* Priority of the task */
      &RfidReaderTask,  /* Task handle. */
      rfidTaskCore); /* Core where the task should run */

      if (result != pdPASS) {
        Serial.println("Fehler beim Erstellen des RFID Tasks");
    } else {
        Serial.println("RFID Task erfolgreich erstellt");
    }
  }
}
