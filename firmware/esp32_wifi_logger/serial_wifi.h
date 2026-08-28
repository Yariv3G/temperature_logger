#pragma once

#include <Arduino.h>

// Callbacks implemented by the main sketch.
void serialWifiPrintTemperatures();
void serialWifiTakeSample();

// Returns true when the user pressed 'c' or 'w' within the boot window.
bool serialWifiBootConfigRequested(unsigned long windowMs);

// Try stored credentials from NVS. Uses DHCP.
bool serialWifiConnectStored(unsigned long timeoutMs);

// Interactive serial wizard: scan, enter SSID/password, verify sensors, save.
bool serialWifiRunSetup();

// Print the landing-page URL on Serial.
void serialWifiPrintLandingPage();
