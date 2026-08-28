#include "serial_wifi.h"

#include <Preferences.h>
#include <WiFi.h>

namespace {

constexpr char WIFI_NAMESPACE[] = "wifi";
constexpr char KEY_SSID[] = "ssid";
constexpr char KEY_PASS[] = "pass";

String readSerialLine(const __FlashStringHelper *prompt, bool hidden = false) {
  Serial.println(prompt);
  Serial.print(F("> "));
  String line;
  while (true) {
    while (!Serial.available()) {
      delay(10);
    }
    const int value = Serial.read();
    if (value == '\r') {
      continue;
    }
    if (value == '\n') {
      break;
    }
    if (hidden) {
      Serial.print('*');
    } else {
      Serial.write(static_cast<char>(value));
    }
    line += static_cast<char>(value);
  }
  Serial.println();
  line.trim();
  return line;
}

void waitForEnter(const __FlashStringHelper *prompt) {
  Serial.println(prompt);
  Serial.print(F("> "));
  while (true) {
    while (!Serial.available()) {
      delay(10);
    }
    const int value = Serial.read();
    if (value == '\n' || value == '\r') {
      break;
    }
  }
}

bool connectWithCredentials(const String &ssid, const String &password,
                            unsigned long timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.begin(ssid.c_str(), password.c_str());

  Serial.print(F("Connecting to "));
  Serial.print(ssid);
  const unsigned long deadline = millis() + timeoutMs;
  while (WiFi.status() != WL_CONNECTED &&
         static_cast<long>(deadline - millis()) > 0) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Connected. IP address: "));
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println(F("Connection failed."));
  return false;
}

bool saveCredentials(const String &ssid, const String &password) {
  Preferences prefs;
  if (!prefs.begin(WIFI_NAMESPACE, false)) {
    Serial.println(F("Failed to open Wi-Fi storage."));
    return false;
  }
  const bool ok =
      prefs.putString(KEY_SSID, ssid) > 0 && prefs.putString(KEY_PASS, password) >= 0;
  prefs.end();
  if (!ok) {
    Serial.println(F("Failed to save Wi-Fi credentials."));
  }
  return ok;
}

bool loadCredentials(String &ssid, String &password) {
  Preferences prefs;
  if (!prefs.begin(WIFI_NAMESPACE, true)) {
    return false;
  }
  ssid = prefs.getString(KEY_SSID, "");
  password = prefs.getString(KEY_PASS, "");
  prefs.end();
  return !ssid.isEmpty();
}

String resolveSsidChoice(const String &choice, int networkCount) {
  if (choice.isEmpty()) {
    return choice;
  }

  bool numeric = true;
  for (unsigned int index = 0; index < choice.length(); ++index) {
    if (!isDigit(choice[index])) {
      numeric = false;
      break;
    }
  }

  if (numeric) {
    const int selection = choice.toInt();
    if (selection >= 1 && selection <= networkCount) {
      return WiFi.SSID(selection - 1);
    }
  }

  return choice;
}

}  // namespace

bool serialWifiBootConfigRequested(unsigned long windowMs) {
  Serial.println();
  Serial.println(F("=== Temperature Logger Wi-Fi Setup ==="));
  Serial.print(F("Press 'c' within "));
  Serial.print(windowMs / 1000UL);
  Serial.println(
      F(" seconds to configure Wi-Fi, or wait to use saved settings."));
  const unsigned long deadline = millis() + windowMs;
  while (static_cast<long>(deadline - millis()) > 0) {
    if (Serial.available()) {
      const char key = static_cast<char>(Serial.read());
      if (key == 'c' || key == 'C' || key == 'w' || key == 'W') {
        while (Serial.available()) {
          Serial.read();
        }
        Serial.println(F("Starting serial Wi-Fi configuration..."));
        return true;
      }
    }
    delay(10);
  }
  return false;
}

bool serialWifiConnectStored(unsigned long timeoutMs) {
  String ssid;
  String password;
  if (!loadCredentials(ssid, password)) {
    Serial.println(F("No saved Wi-Fi credentials found."));
    return false;
  }

  Serial.print(F("Connecting with saved network: "));
  Serial.println(ssid);
  return connectWithCredentials(ssid, password, timeoutMs);
}

bool serialWifiRunSetup() {
  Serial.println();
  Serial.println(F("Scanning for Wi-Fi networks..."));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);

  const int networkCount = WiFi.scanNetworks();
  if (networkCount <= 0) {
    Serial.println(F("No networks found. Enter the SSID manually."));
  } else {
    Serial.println(F("Available networks:"));
    for (int index = 0; index < networkCount; ++index) {
      Serial.print(index + 1);
      Serial.print(F(": "));
      Serial.print(WiFi.SSID(index));
      Serial.print(F(" ("));
      Serial.print(WiFi.RSSI(index));
      Serial.println(F(" dBm)"));
    }
  }

  const String ssidChoice =
      readSerialLine(F("Enter network number or SSID name:"));
  const String ssid = resolveSsidChoice(ssidChoice, networkCount);
  if (ssid.isEmpty()) {
    Serial.println(F("SSID is required."));
    WiFi.scanDelete();
    return false;
  }

  const String password = readSerialLine(F("Enter Wi-Fi password (blank for open network):"),
                                         true);
  WiFi.scanDelete();

  if (!connectWithCredentials(ssid, password, 30000UL)) {
    return false;
  }

  Serial.println();
  Serial.println(F("Verifying DS18B20 temperature readings before web access..."));
  serialWifiTakeSample();
  serialWifiPrintTemperatures();

  waitForEnter(
      F("Verify the readings above, then press ENTER to enable the landing page."));
  if (!saveCredentials(ssid, password)) {
    return false;
  }

  serialWifiPrintLandingPage();
  return true;
}

void serialWifiPrintLandingPage() {
  Serial.println();
  Serial.print(F("Landing page: http://"));
  Serial.println(WiFi.localIP());
  Serial.println(F("Third-party queries: GET /api/temperature?metric=current|max|min|average&sensor=0"));
}
