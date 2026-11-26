#include "nfc.h"
#include "scale.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "HX711.h"
#include "display.h"
#include "esp_task_wdt.h"
#include <Preferences.h>
#include "lang.h"

HX711 scale;

TaskHandle_t ScaleTask;

int16_t weight = 0;

// Weight stabilization variables
#define MOVING_AVERAGE_SIZE 8           // Reduced from 20 to 8 for faster response
#define LOW_PASS_ALPHA 0.3f            // Increased from 0.15 to 0.3 for faster tracking
#define DISPLAY_HYSTERESIS 0.6f        // Float-Hysterese gegen Flackern an .5-g-Grenzen
#define DISPLAY_HYSTERESIS_NFC_GATE 1.0f // mit "NFC nur bei stabilem Gewicht": etwas groesser, da Restrauschen nach dem Funkfeld-Aus
#define MEASUREMENT_INTERVAL_MS 30     // Reduced from 50ms to 30ms for faster updates
#define HX711_MAX_RAW 8388607L         // 0x7FFFFF (gesaettigt)
#define HX711_MIN_RAW (-8388608L)      // -0x800000 (gesaettigt)
#define SCALE_STALL_MS 2000            // ohne gueltigen Messwert -> Recovery
#define SCALE_RECOVERY_INTERVAL_MS 5000
#define SCALE_SETTLE_SAMPLES 4         // Messwerte nach Recovery verwerfen

float weightBuffer[MOVING_AVERAGE_SIZE];
uint8_t bufferIndex = 0;
bool bufferFilled = false;
float filteredWeight = 0.0f;
int16_t lastDisplayedWeight = 0;
unsigned long lastMeasurementTime = 0;

// Ringpuffer fuer Median-aus-3 (Ausreisserfilter, Gramm)
static float medianBuf[3];
static uint8_t medianCount = 0;
static uint8_t medianIndex = 0;

uint8_t weightCounterToApi = 0;
uint8_t scale_tare_counter = 0;
volatile bool scaleTareRequest = false;
volatile uint8_t pauseMainTask = 0;
volatile bool scaleCalibrated;
volatile bool scaleConnected;
volatile bool autoTare = true;
volatile bool scaleCalibrationActive = false;
volatile bool scaleError = false;
volatile bool scalePauseRequest = false;  // z. B. waehrend OTA: Scale-Loop misst und zeichnet nicht
volatile bool scaleCalibrationRequest = false;

/**
 * Non blocking wait function
 */
void friendlyWait(uint16_t ticks) {
    for (uint16_t i = 0; i < ticks; i++) {
    yield();
    vTaskDelay(pdMS_TO_TICKS(1));
    esp_task_wdt_reset();
  }
}

// ##### HX711 Lesefunktionen mit Timeout #####

/**
 * Einzelnen Rohwert lesen. false bei Timeout oder gesaettigtem Wert.
 */
static bool readRawSample(long &out, uint32_t timeoutMs) {
  if (!scale.wait_ready_timeout(timeoutMs, 1)) return false;
  long v = scale.read();
  if (v >= HX711_MAX_RAW || v <= HX711_MIN_RAW) return false;
  out = v;
  return true;
}

/**
 * Median aus bis zu `samples` (max. 15) gueltigen Rohwerten.
 * false, wenn weniger als die Haelfte gueltig war.
 */
static bool readRawMedian(long &out, uint8_t samples, uint32_t timeoutPerSampleMs) {
  if (samples > 15) samples = 15;
  if (samples == 0) return false;
  long vals[15];
  uint8_t n = 0;
  for (uint8_t i = 0; i < samples; i++) {
    long v;
    if (readRawSample(v, timeoutPerSampleMs)) vals[n++] = v;
    esp_task_wdt_reset();
  }
  if (n == 0 || n < (samples + 1) / 2) return false;

  // Insertion Sort
  for (uint8_t i = 1; i < n; i++) {
    long k = vals[i];
    int8_t j = i - 1;
    while (j >= 0 && vals[j] > k) {
      vals[j + 1] = vals[j];
      j--;
    }
    vals[j + 1] = k;
  }
  out = vals[n / 2];
  return true;
}

/**
 * Tare ueber Median; bei Fehlschlag bleibt der Offset unveraendert.
 */
static bool tareWithMedian() {
  long v;
  if (!readRawMedian(v, 15, 500)) {
    Serial.println("Tare failed: no valid HX711 readings");
    return false;
  }
  scale.set_offset(v);
  return true;
}

// ##### Weight stabilization functions #####

/**
 * Reset weight filter buffer - call after tare or calibration
 */
void resetWeightFilter() {
  bufferIndex = 0;
  bufferFilled = false;
  filteredWeight = 0.0f;
  lastDisplayedWeight = 0;
  medianCount = 0;
  medianIndex = 0;

  // Initialize buffer with zeros
  for (int i = 0; i < MOVING_AVERAGE_SIZE; i++) {
    weightBuffer[i] = 0.0f;
  }
  for (int i = 0; i < 3; i++) {
    medianBuf[i] = 0.0f;
  }
}

/**
 * Calculate moving average from weight buffer
 */
float calculateMovingAverage() {
  float sum = 0.0f;
  int count = bufferFilled ? MOVING_AVERAGE_SIZE : bufferIndex;

  for (int i = 0; i < count; i++) {
    sum += weightBuffer[i];
  }

  return (count > 0) ? sum / count : 0.0f;
}

/**
 * Apply low-pass filter to smooth weight readings
 * Uses exponential smoothing: y_new = alpha * x_new + (1-alpha) * y_old
 */
float applyLowPassFilter(float newValue) {
  filteredWeight = LOW_PASS_ALPHA * newValue + (1.0f - LOW_PASS_ALPHA) * filteredWeight;
  return filteredWeight;
}

/**
 * Process new weight reading with stabilization
 * Returns stabilized (rounded) weight value
 */
int16_t processWeightReading(float rawWeight) {
  // Median aus den letzten drei Rohgewichten (Ausreisserfilter)
  medianBuf[medianIndex] = rawWeight;
  medianIndex = (medianIndex + 1) % 3;
  if (medianCount < 3) medianCount++;

  float w = rawWeight;
  if (medianCount == 3) {
    float a = medianBuf[0], b = medianBuf[1], c = medianBuf[2];
    if ((a <= b && b <= c) || (c <= b && b <= a)) w = b;
    else if ((b <= a && a <= c) || (c <= a && a <= b)) w = a;
    else w = c;
  }

  // Add to moving average buffer
  weightBuffer[bufferIndex] = w;
  bufferIndex = (bufferIndex + 1) % MOVING_AVERAGE_SIZE;

  if (bufferIndex == 0) {
    bufferFilled = true;
  }

  // Calculate moving average
  float avgWeight = calculateMovingAverage();

  // Apply low-pass filter
  float smoothedWeight = applyLowPassFilter(avgWeight);

  // Gerundeter Wert, aber nur mit Hysterese aendern
  if (fabsf(smoothedWeight - (float)lastDisplayedWeight) >= (nfcStableOnly ? DISPLAY_HYSTERESIS_NFC_GATE : DISPLAY_HYSTERESIS)) {
    lastDisplayedWeight = (int16_t)lroundf(smoothedWeight);
  }

  return lastDisplayedWeight;
}

/**
 * Get current filtered weight for display purposes
 */
int16_t getFilteredDisplayWeight() {
  return lastDisplayedWeight;
}


// ##### NFC-Gate: NFC nur nach stabiler Waegung #####
// Messung zeigte: PN532-Polling erzeugt ~10 g Rauschen auf der Waage. Das Gate haelt den NFC-Task
// an, solange die Waage misst, und oeffnet ihn nur kurz, wenn das Gewicht stabil ist.
#define NFC_GATE_STABLE_MS        1500   // Anzeigewert so lange unveraendert -> stabil
#define NFC_GATE_WINDOW_MS        2000   // NFC-Fenster (SAMConfig + Pollen)
#define NFC_GATE_QUIET_IDLE_MS    8000   // Pause bis zum naechsten Fenster ohne Tag
#define NFC_GATE_QUIET_PRESENT_MS 15000  // Pause, wenn schon ein Tag gelesen wurde (Entfernen erkennen)
#define NFC_GATE_RELEASE_DELTA_G  5      // Gewichtsaenderung, die die Pause vorzeitig beendet
#define NFC_GATE_SETTLE_MS        400    // nach dem Funkfeld-Aus noch so lange Messwerte verwerfen (Messung: Ausschlag bis ~0,5 s danach)
static unsigned long gateLastChange = 0, gateOpenedAt = 0, gateQuietUntil = 0, gateClosedAt = 0;
static int16_t gateLastShown = 0, gateRefWeight = 0, gateOpenWeight = 0;

// true, solange NFC pollt (oder gerade aufgehoert hat): Messwerte nicht in den Filter geben, nicht tarieren
static bool nfcNoisePhase() {
  if (!nfcStableOnly) return false;  // Einstellung aus: Verhalten wie bisher
  // erst wenn das Funkfeld tatsaechlich aus ist (Abschalten passiert im NFC-Task, bis ~2 s nach dem Schliessen), dann Einschwingzeit
  return !nfcGateClosed || !nfcRfFieldOff || (millis() - nfcRfFieldOffAt) < NFC_GATE_SETTLE_MS;
}

static void updateNfcGate() {
  if (!nfcStableOnly) {
    // Einstellung aus (Standard): Gate dauerhaft offen. Wurde sie gerade ausgeschaltet, oeffnet der NFC-Task das Funkfeld wieder.
    nfcGateClosed = false;
    nfcRescanRequest = false;
    return;
  }
  unsigned long now = millis();
  if (lastDisplayedWeight != gateLastShown) {
    gateLastShown = lastDisplayedWeight;
    gateLastChange = now;
  }
  if (abs(lastDisplayedWeight - gateRefWeight) >= NFC_GATE_RELEASE_DELTA_G) {
    gateQuietUntil = 0;  // Spule aufgelegt/entfernt: bald wieder NFC erlauben
    if (nfcReaderState != NFC_IDLE) nfcRescanRequest = true;  // Tag war schon verarbeitet: beim naechsten Fenster neu lesen
  }

  // Nur aktive Vorgaenge halten das Gate offen. Ergebnis-Zustaende (READ_SUCCESS, WRITE_SUCCESS, *_ERROR, BAMBU_*)
  // bleiben bestehen, solange der Tag auf dem Leser liegt (Messung nach Tag-Schreiben: Gate blieb dauerhaft offen).
  bool busy = nfcWriteInProgress || scanRequestActive ||
              nfcReaderState == NFC_READING || nfcReaderState == NFC_WRITING;
  if (busy) {
    if (nfcGateClosed) {
      nfcGateClosed = false;
      gateOpenedAt = now;
      gateOpenWeight = lastDisplayedWeight;
    }
    return;
  }

  if (nfcGateClosed) {
    if (now - gateLastChange >= NFC_GATE_STABLE_MS && now >= gateQuietUntil) {
      nfcGateClosed = false;
      gateOpenedAt = now;
      gateOpenWeight = lastDisplayedWeight;
    }
  } else if (now - gateOpenedAt >= NFC_GATE_WINDOW_MS) {
    nfcGateClosed = true;
    gateClosedAt = now;
    // Gewicht war im ganzen Fenster stabil: Der Neuscan-Merker stammt nur von der veralteten Referenz (leere Waage), der Tag wurde in diesem Fenster gelesen
    if (abs(lastDisplayedWeight - gateOpenWeight) < NFC_GATE_RELEASE_DELTA_G) nfcRescanRequest = false;
    gateRefWeight = lastDisplayedWeight;
    gateQuietUntil = now + (nfcReaderState != NFC_IDLE ? NFC_GATE_QUIET_PRESENT_MS : NFC_GATE_QUIET_IDLE_MS);
  }
}

// Nur lesen, fuer GET /api/debug-state: kopiert die Gate-Werte (keine Zustandsaenderung)
void scaleGetGateDebug(int16_t &refWeight, int16_t &openWeight, uint32_t &stableForMs, uint32_t &quietRemainingMs) {
  unsigned long now = millis();
  refWeight = gateRefWeight;
  openWeight = gateOpenWeight;
  stableForMs = now - gateLastChange;
  quietRemainingMs = (gateQuietUntil > now) ? (gateQuietUntil - now) : 0;
}

// ##### Funktionen für Waage #####
uint8_t setAutoTare(bool autoTareValue) {
  Serial.print("Set AutoTare to ");
  Serial.println(autoTareValue);
  autoTare = autoTareValue;

  // Speichern mit NVS
  Preferences preferences;
  preferences.begin(NVS_NAMESPACE_SCALE, false); // false = readwrite
  preferences.putBool(NVS_KEY_AUTOTARE, autoTare);
  preferences.end();

  return 1;
}

uint8_t tareScale() {
  Serial.println("Tare scale");
  tareWithMedian();
  resetWeightFilter();

  return 1;
}

void scale_loop(void * parameter) {
  Serial.println("++++++++++++++++++++++++++++++");
  Serial.println("Scale Loop started");
  Serial.println("++++++++++++++++++++++++++++++");

  // Initialize weight filter
  resetWeightFilter();
  lastMeasurementTime = millis();

  unsigned long lastValidSampleTime = millis();
  unsigned long lastRecoveryTime = 0;
  uint8_t settleDiscard = 0;

  for(;;) {
    // Kooperative Pause (statt vTaskSuspend: der Task koennte den Display-Mutex halten)
    if (scalePauseRequest) {
      lastValidSampleTime = millis();
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    unsigned long currentTime = millis();

    // Only measure at defined intervals to reduce noise
    if (currentTime - lastMeasurementTime >= MEASUREMENT_INTERVAL_MS) {
      if (scale.is_ready())
      {
        // Waage manuell Taren
        if ((scaleTareRequest == true || (autoTare && scale_tare_counter >= 20)) && !nfcNoisePhase())
        {
          Serial.println("Re-Tare scale");
          oledDisplayText(tr(STR_TARE_SCALE));
          vTaskDelay(pdMS_TO_TICKS(1000));
          tareWithMedian();
          resetWeightFilter(); // Reset filter after manual tare
          vTaskDelay(pdMS_TO_TICKS(1000));
          oledShowWeight(0);
          scaleTareRequest = false;
          scale_tare_counter = 0;
          weight = 0; // Reset global weight variable after tare
          lastValidSampleTime = millis();
        }

        // Check for calibration request
        if (scaleCalibrationRequest) {
            scaleCalibrationRequest = false;
            calibrate_scale();
            lastValidSampleTime = millis();
        }

        // Rohwert lesen (mit Timeout, gesaettigte Werte werden verworfen)
        long raw;
        if (readRawSample(raw, 100)) {
          lastValidSampleTime = millis();
          scaleError = false;

          if (settleDiscard > 0) {
            // Einschwingzeit nach Recovery
            settleDiscard--;
          } else if (nfcNoisePhase()) {
            // NFC-Fenster: Messwert verwerfen, Filter und Anzeige bleiben stehen
          } else {
            float rawWeight = (float)(raw - scale.get_offset()) / scale.get_scale();

            // Process weight with stabilization
            int16_t stabilizedWeight = processWeightReading(rawWeight);

            if (stabilizedWeight != weight) {
              weight = stabilizedWeight;
              oledResetActivityTimer(); // Wake display on weight change
            }

            // Prüfen ob die Waage korrekt genullt ist (gefilterter Wert, damit
            // einzelne Rauschausreißer keinen Re-Tare auslösen).
            // Totzone |w| < 2 (wie Anzeige in main.cpp) wird ignoriert.
            int16_t filteredForTare = getFilteredDisplayWeight();
            if (autoTare && ((filteredForTare >= 2 && filteredForTare < 7) || filteredForTare <= -2))
            {
              scale_tare_counter++;
            }
            else
            {
              scale_tare_counter = 0;
            }
          }
        }

        lastMeasurementTime = currentTime;
      }
    }

    // Laufzeitueberwachung: lange kein gueltiger Messwert -> HX711 neu starten
    unsigned long now = millis();
    if (now - lastValidSampleTime > SCALE_STALL_MS) {
      scaleError = true;
      if (now - lastRecoveryTime >= SCALE_RECOVERY_INTERVAL_MS) {
        lastRecoveryTime = now;
        Serial.println("HX711: no valid reading, power cycle");
        scale.power_down();
        vTaskDelay(pdMS_TO_TICKS(10));
        scale.power_up();
        settleDiscard = SCALE_SETTLE_SAMPLES;
      }
    }

    updateNfcGate();

    vTaskDelay(pdMS_TO_TICKS(10)); // Shorter delay for more responsive loop
  }
}

long readHX711Raw() {
  long value = 0;

  for (int i = 0; i < 24; i++) {
    digitalWrite(LOADCELL_SCK_PIN, HIGH);
    delayMicroseconds(1);

    value = (value << 1) | digitalRead(LOADCELL_DOUT_PIN);

    digitalWrite(LOADCELL_SCK_PIN, LOW);
    delayMicroseconds(1);
  }

  // Gain = 128 (1 extra pulse)
  digitalWrite(LOADCELL_SCK_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(LOADCELL_SCK_PIN, LOW);

  return value;
}

bool deepSearchScale() {
  // without pull-up resistor (10k) on LOADCELL_DOUT_PIN the hx711 lib functions
  // are not reliable in detecting a missing chip
  // these tests work even without pull-up resistor
  const uint32_t timeout = 1000; // 1 second timeout for waiting for the chip to respond
  const int samples = 10;

  pinMode(LOADCELL_DOUT_PIN, INPUT);
  pinMode(LOADCELL_SCK_PIN, OUTPUT);
  digitalWrite(LOADCELL_SCK_PIN, LOW);

  //delay(500);
  friendlyWait(500);

  // 1. HX711 vorhanden?
  uint32_t start = millis();
  while (digitalRead(LOADCELL_DOUT_PIN) == HIGH) {
    if (millis() - start > timeout) {
      Serial.println("HX711 not found (DOUT stays HIGH).");
      return false;
    }
  }

  Serial.println("HX711 seems to answer...");

  // 2. Mehrere Werte lesen
  long values[samples];

  for (int i = 0; i < samples; i++) {
    // warten bis bereit
    uint32_t t = millis();
    while (digitalRead(LOADCELL_DOUT_PIN) == HIGH) {
      if (millis() - t > timeout) {
        Serial.println("HX711 not found (Timeout during read).");
        return false;
      }
    }

    values[i] = readHX711Raw();
    delay(50);
  }

  // 3. Analyse
  int invalidCount = 0;

  for (int i = 0; i < samples; i++) {
    if (values[i] == 0 || values[i] == 0xFFFFFF) {
      invalidCount++;
    }
  }

  // 4. Entscheidungen

  // Kein Sensor
  if (invalidCount > samples / 2) {
    Serial.println("HX711 not ready (Loadcell missing).");
    return false;
  }

  Serial.println("HX711 found.");
  return true;
}

bool scaleDetected(){
  // Step 1: use hx711 library functions (need pull-up resistor to recognize missing hx711)
  if ( !(scale.wait_ready_timeout(1000))) {
    Serial.println("HX711 not found (pull-up resistor installed).");
    return false;
  }

  //Step 2: when pull-up resistor is missing, then deep search...
  return deepSearchScale();

}

void start_scale(bool touchSensorConnected) {
  Serial.println("Prüfe Calibration Value");
  float calibrationValue;

  // NVS lesen
  Preferences preferences;
  preferences.begin(NVS_NAMESPACE_SCALE, true); // true = readonly
  if(preferences.isKey(NVS_KEY_CALIBRATION)){
    calibrationValue = preferences.getFloat(NVS_KEY_CALIBRATION);
    if (calibrationValue == 0.0f || !isfinite(calibrationValue)) {
      // ungueltiger Wert -> wie nicht kalibriert behandeln
      calibrationValue = SCALE_DEFAULT_CALIBRATION_VALUE;
      scaleCalibrated = false;
    } else {
      scaleCalibrated = true;
    }
  }else{
    calibrationValue = SCALE_DEFAULT_CALIBRATION_VALUE;
    scaleCalibrated = false;
  }

  // auto Tare
  // Wenn Touch Sensor verbunden, dann autoTare auf false setzen
  // Danach prüfen was in NVS gespeichert ist
  autoTare = (touchSensorConnected) ? false : true;
  autoTare = preferences.getBool(NVS_KEY_AUTOTARE, autoTare);

  preferences.end();

  Serial.print("Read Scale Calibration Value ");
  Serial.println(calibrationValue);

  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  oledShowProgressBar(5, NUM_SETUP_STEPS, DISPLAY_BOOT_TEXT, tr(STR_SEARCHING_SCALE));
  friendlyWait(3000);

  scaleConnected = scaleDetected();
  if ( scaleConnected ) {
    scale.set_scale(calibrationValue);
    //vTaskDelay(pdMS_TO_TICKS(5000));

    // Initialize weight stabilization filter
    resetWeightFilter();

    // Display Gewicht
    oledShowWeight(0);

    Serial.println("starte Scale Task");
    BaseType_t result = xTaskCreatePinnedToCore(
      scale_loop, /* Function to implement the task */
      "ScaleLoop", /* Name of the task */
      4096,  /* Stack size in words */
      NULL,  /* Task input parameter */
      scaleTaskPrio,  /* Priority of the task */
      &ScaleTask,  /* Task handle. */
      scaleTaskCore); /* Core where the task should run */

    if (result != pdPASS) {
        Serial.println("Fehler beim Erstellen des ScaleLoop-Tasks");
    } else {
        Serial.println("ScaleLoop-Task erfolgreich erstellt");
    }
  }
  else {
    // No HX711 - switching to NFC-only mode
    Serial.println("Kann kein HX711 Board finden !");            // Sende Text "Kann kein..." an seriellen Monitor
    oledDisplayText(tr(STR_HX711_NOT_FOUND));
    vTaskDelay(pdMS_TO_TICKS(1500));
    oledDisplayText(tr(STR_NOSCALE_MODE));
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}

uint8_t calibrate_scale() {
  uint8_t returnState = 0;

  scaleCalibrationActive = true;

  // NFC-Task kooperativ pausieren (kein vTaskSuspend: er koennte den I2C-Lock halten)
  nfcReadingTaskSuspendRequest = true;
  for (uint16_t i = 0; i < 300 && !nfcReadingTaskSuspendState; i++) {
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_task_wdt_reset();
  }

  pauseMainTask = 1;

  long rawZero = 0;
  long rawLoad = 0;
  bool ok = scale.wait_ready_timeout(1000);

  if (ok)
  {
    oledShowProgressBar(0, 3, tr(STR_SCALE_CAL), tr(STR_EMPTY_SCALE));
    friendlyWait(5000);

    ok = readRawMedian(rawZero, 15, 500);
    if (ok) {
      Serial.println("Zero reading done...");
      Serial.print("Place a known weight on the scale...");

      oledShowProgressBar(1, 3, tr(STR_SCALE_CAL), tr(STR_PLACE_WEIGHT));
      friendlyWait(5000);

      ok = readRawMedian(rawLoad, 15, 500);
    }
  }

  if (ok)
  {
    long diff = rawLoad - rawZero;
    float newCalibrationValue = (float)diff / (float)SCALE_LEVEL_WEIGHT;
    Serial.print("Result (raw diff): ");
    Serial.println(diff);

    // Negative Faktoren sind erlaubt (vertauschte Zellen-Adern)
    if (labs(diff) >= 5000 && isfinite(newCalibrationValue) && fabsf(newCalibrationValue) >= 1.0f)
    {
      Serial.print("New calibration value has been set to: ");
      Serial.println(newCalibrationValue);

      // Speichern mit NVS
      Preferences preferences;
      preferences.begin(NVS_NAMESPACE_SCALE, false); // false = readwrite
      preferences.putFloat(NVS_KEY_CALIBRATION, newCalibrationValue);
      preferences.end();

      // Verifizieren
      preferences.begin(NVS_NAMESPACE_SCALE, true);
      float verifyValue = preferences.getFloat(NVS_KEY_CALIBRATION, 0);
      preferences.end();

      Serial.print("Verified stored value: ");
      Serial.println(verifyValue);

      oledShowProgressBar(2, 3, tr(STR_SCALE_CAL), tr(STR_REMOVE_WEIGHT));

      scale.set_scale(newCalibrationValue);
      scale.set_offset(rawZero);
      resetWeightFilter(); // Reset filter after calibration
      friendlyWait(2000);

      oledShowProgressBar(3, 3, tr(STR_SCALE_CAL), tr(STR_COMPLETED));

      // Gewicht ist jetzt entfernt: Scale-Loop nullt neu
      scaleTareRequest = true;

      friendlyWait(2000);

      scaleCalibrated = true;
      returnState = 1;
    }
    else
    {
      Serial.println("Calibration value is invalid. Please recalibrate.");

      oledShowProgressBar(3, 3, tr(STR_FAILURE), tr(STR_CALIBRATION_ERROR));
      friendlyWait(5000);
      returnState = 0;
    }
  }
  else
  {
    Serial.println("HX711 not found.");

    oledDisplayText(tr(STR_HX711_NOT_FOUND));
    friendlyWait(5000);
    returnState = 0;
  }

  nfcReadingTaskSuspendRequest = false;
  pauseMainTask = 0;
  scaleCalibrationActive = false;

  return returnState;
}
