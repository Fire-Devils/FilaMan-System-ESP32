#ifndef COMMONFS_H
#define COMMONFS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

bool removeJsonValue(const char* filename);
bool saveJsonValue(const char* filename, const JsonDocument& doc);
bool loadJsonValue(const char* filename, JsonDocument& doc);
String readFile(const char* filename);
void initializeFileSystem();

#endif
