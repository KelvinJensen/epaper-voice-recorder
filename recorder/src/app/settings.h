#pragma once
#include <Arduino.h>

void classicSettingsBegin();
String classicWifiSsid();
String classicWifiPassword();
String classicOpenAiKey();
String classicLanguage();
bool classicSetLanguage(const String& language);
bool classicHasSetup();
bool classicSaveSettings(const String& ssid, const String& password,
                         const String& apiKey, const String& language);
