#ifndef SCALE_H
#define SCALE_H

#include <Arduino.h>
#include "HX711.h"

uint8_t setAutoTare(bool autoTareValue);
void start_scale(bool touchSensorConnected);
uint8_t calibrate_scale();
uint8_t tareScale();

// Weight stabilization functions
void resetWeightFilter();
float calculateMovingAverage();
float applyLowPassFilter(float newValue);
int16_t processWeightReading(float rawWeight);
int16_t getFilteredDisplayWeight();

extern HX711 scale;
extern int16_t weight;
extern uint8_t weightCounterToApi;
extern uint8_t scale_tare_counter;
extern volatile bool scaleTareRequest;
extern volatile uint8_t pauseMainTask;
extern volatile bool scaleCalibrated;
extern volatile bool scaleConnected;
extern volatile bool autoTare;
extern volatile bool scaleCalibrationActive;
extern volatile bool scaleCalibrationRequest;
extern volatile bool scalePauseRequest; // Scale-Loop kooperativ anhalten (OTA)
extern volatile bool scaleError;   // HX711 liefert keine gueltigen Messwerte

extern TaskHandle_t ScaleTask;

#endif
