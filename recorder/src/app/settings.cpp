#include "settings.h"
#include <Preferences.h>
#include "settings_pair.h"
#if __has_include("../../secrets.h")
#include "../../secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID  ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS  ""
#endif
#ifndef OPENAI_KEY
#define OPENAI_KEY ""
#endif

namespace {
Preferences prefs;
bool ready = false;
bool languageSupported(const String& language) {
  return language == "de" || language == "en" || language == "fr" ||
         language == "es" || language == "it" || language == "pl" ||
         language == "ru" || language == "cs" || language == "uk" ||
         language == "nl" || language == "pt";
}
String get(const char* key) {
  classicSettingsBegin();
  return ready ? prefs.getString(key, "") : String();
}
// Device setup wins; secrets.h is used as a whole set only when none is saved,
// so an open network saved on the device never inherits WIFI_PASS.
bool useBuildDefaults() { return get("wifi_ssid").length() == 0; }
}

void classicSettingsBegin() {
  if (!ready) ready = prefs.begin("recorder", false);
}

String classicWifiSsid() { return useBuildDefaults() ? String(WIFI_SSID) : get("wifi_ssid"); }
String classicWifiPassword() { return useBuildDefaults() ? String(WIFI_PASS) : get("wifi_pass"); }
String classicOpenAiKey() { return useBuildDefaults() ? String(OPENAI_KEY) : get("openai_key"); }
String classicLanguage() {
  const String language = get("ui_lang");
  return languageSupported(language) ? language : "en";
}
bool classicSetLanguage(const String& language) {
  classicSettingsBegin();
  if (!ready || !languageSupported(language)) return false;
  prefs.putString("ui_lang", language);
  return prefs.getString("ui_lang", "") == language;
}
bool classicHasSetup() {
  return classicWifiSsid().length() > 0 && classicOpenAiKey().length() >= 11;
}
bool classicSaveSettings(const String& ssid, const String& password,
                         const String& apiKey, const String& language) {
  classicSettingsBegin();
  if (!ready || !ssid.length() || ssid.length() > 32 || password.length() > 63 ||
      apiKey.length() < 11 || apiKey.length() > 200 ||
      !languageSupported(language)) return false;
  const String oldSsid = prefs.getString("wifi_ssid", "");
  const String oldPassword = prefs.getString("wifi_pass", "");
  const String oldKey = prefs.getString("openai_key", "");
  const String oldLanguage = prefs.getString("ui_lang", "");
  const auto read = [](bool secret) { return prefs.getString(secret ? "wifi_pass" : "wifi_ssid", ""); };
  const auto write = [](bool secret, const String& value) { prefs.putString(secret ? "wifi_pass" : "wifi_ssid", value); };
  if (!saveSettingsPair(ssid, password, read, write)) return false;
  prefs.putString("openai_key", apiKey);
  prefs.putString("ui_lang", language);
  const bool saved = prefs.getString("wifi_ssid", "") == ssid &&
         prefs.getString("wifi_pass", "") == password &&
         prefs.getString("openai_key", "") == apiKey &&
         prefs.getString("ui_lang", "") == language;
  if (!saved) {
    saveSettingsPair(oldSsid, oldPassword, read, write);
    prefs.putString("openai_key", oldKey);
    prefs.putString("ui_lang", oldLanguage);
  }
  return saved;
}
