#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <OneWire.h>

#include "config.h"
#include "secrets.h"
#include "web_assets.h"

OneWire oneWire(ONE_WIRE_PIN);
WebServer server(HTTP_PORT);

typedef uint8_t SensorAddress[8];
SensorAddress sensorAddresses[MAX_SENSORS];
float temperaturesC[MAX_SENSORS];
bool sensorValid[MAX_SENSORS];
uint8_t sensorCount = 0;

uint32_t historyTimes[MAX_HISTORY_SAMPLES];
float historyTemperatures[MAX_HISTORY_SAMPLES][MAX_SENSORS];
uint16_t historyCount = 0;
uint16_t historyHead = 0;

bool acquisitionRunning = false;
bool alarmActive = false;
bool accessPointMode = false;
float lowerLimitC = DEFAULT_LOWER_LIMIT_C;
float upperLimitC = DEFAULT_UPPER_LIMIT_C;
unsigned long sampleIntervalMs = DEFAULT_SAMPLE_INTERVAL_MS;
unsigned long lastSampleMs = 0;
unsigned long sampleTimeSeconds = 0;
unsigned long sampleSequence = 0;

String addressText(const SensorAddress address) {
  String result;
  result.reserve(16);
  for (uint8_t index = 0; index < 8; ++index) {
    if (address[index] < 16) {
      result += '0';
    }
    result += String(address[index], HEX);
  }
  result.toUpperCase();
  return result;
}

void discoverSensors() {
  sensorCount = 0;
  oneWire.reset_search();
  uint8_t address[8];
  while (sensorCount < MAX_SENSORS && oneWire.search(address)) {
    if (address[0] == 0x28 && OneWire::crc8(address, 7) == address[7]) {
      memcpy(sensorAddresses[sensorCount], address, sizeof(address));
      temperaturesC[sensorCount] = NAN;
      sensorValid[sensorCount] = false;
      ++sensorCount;
    }
  }
}

void updateAlarm() {
  bool fault = sensorCount == 0;
  for (uint8_t sensor = 0; sensor < sensorCount; ++sensor) {
    if (!sensorValid[sensor] || temperaturesC[sensor] < lowerLimitC ||
        temperaturesC[sensor] > upperLimitC) {
      fault = true;
    }
  }
  alarmActive = acquisitionRunning && fault;
  digitalWrite(ALARM_OUTPUT_PIN, alarmActive ? HIGH : LOW);
}

void recordHistory() {
  historyTimes[historyHead] = sampleTimeSeconds;
  for (uint8_t sensor = 0; sensor < MAX_SENSORS; ++sensor) {
    historyTemperatures[historyHead][sensor] =
        sensor < sensorCount && sensorValid[sensor] ? temperaturesC[sensor] : NAN;
  }
  historyHead = (historyHead + 1) % MAX_HISTORY_SAMPLES;
  if (historyCount < MAX_HISTORY_SAMPLES) {
    ++historyCount;
  }
}

void takeSample() {
  oneWire.reset();
  oneWire.skip();
  oneWire.write(0x44);
  delay(750);

  for (uint8_t sensor = 0; sensor < sensorCount; ++sensor) {
    uint8_t scratchpad[9];
    if (!oneWire.reset()) {
      sensorValid[sensor] = false;
      continue;
    }
    oneWire.select(sensorAddresses[sensor]);
    oneWire.write(0xBE);
    for (uint8_t byteIndex = 0; byteIndex < sizeof(scratchpad); ++byteIndex) {
      scratchpad[byteIndex] = oneWire.read();
    }
    const int16_t raw =
        static_cast<int16_t>((scratchpad[1] << 8) | scratchpad[0]);
    const float value = raw / 16.0F;
    sensorValid[sensor] = OneWire::crc8(scratchpad, 8) == scratchpad[8] &&
                          value >= -55.0F && value <= 125.0F;
    temperaturesC[sensor] = value;
  }

  sampleTimeSeconds = millis() / 1000UL;
  ++sampleSequence;
  updateAlarm();
  recordHistory();
}

String statusJson() {
  String json;
  json.reserve(320 + sensorCount * 100);
  json += F("{\"running\":");
  json += acquisitionRunning ? F("true") : F("false");
  json += F(",\"alarm\":");
  json += alarmActive ? F("true") : F("false");
  json += F(",\"wifi\":");
  json += WiFi.status() == WL_CONNECTED ? F("true") : F("false");
  json += F(",\"apMode\":");
  json += accessPointMode ? F("true") : F("false");
  json += F(",\"ip\":\"");
  json += accessPointMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  json += F("\",\"interval\":");
  json += String(sampleIntervalMs / 1000UL);
  json += F(",\"lower\":");
  json += String(lowerLimitC, 2);
  json += F(",\"upper\":");
  json += String(upperLimitC, 2);
  json += F(",\"time\":");
  json += String(sampleTimeSeconds);
  json += F(",\"seq\":");
  json += String(sampleSequence);
  json += F(",\"sensorCount\":");
  json += String(sensorCount);
  json += F(",\"sensors\":[");
  for (uint8_t sensor = 0; sensor < sensorCount; ++sensor) {
    if (sensor) {
      json += ',';
    }
    json += F("{\"address\":\"");
    json += addressText(sensorAddresses[sensor]);
    json += F("\",\"valid\":");
    json += sensorValid[sensor] ? F("true") : F("false");
    json += F(",\"c\":");
    json += sensorValid[sensor] ? String(temperaturesC[sensor], 3) : String('0');
    json += '}';
  }
  json += F("]}");
  return json;
}

void sendJsonResult(bool ok, const String &message, int status = 200) {
  String json = F("{\"ok\":");
  json += ok ? F("true") : F("false");
  json += F(",\"message\":\"");
  json += message;
  json += F("\"}");
  server.send(status, "application/json", json);
}

void handleConfig() {
  if (!server.hasArg("interval") || !server.hasArg("lower") ||
      !server.hasArg("upper")) {
    sendJsonResult(false, F("Missing configuration fields"), 400);
    return;
  }
  const int intervalSeconds = server.arg("interval").toInt();
  const float lower = server.arg("lower").toFloat();
  const float upper = server.arg("upper").toFloat();
  if (intervalSeconds < 1 || intervalSeconds > 3600 || lower < -55.0F ||
      upper > 125.0F || lower >= upper) {
    sendJsonResult(false, F("Invalid interval or limits"), 400);
    return;
  }
  sampleIntervalMs = static_cast<unsigned long>(intervalSeconds) * 1000UL;
  lowerLimitC = lower;
  upperLimitC = upper;
  updateAlarm();
  sendJsonResult(true, F("Configuration applied"));
}

void handleControl() {
  const String action = server.arg("action");
  if (action == F("start")) {
    acquisitionRunning = true;
    lastSampleMs = millis() - sampleIntervalMs;
    updateAlarm();
    sendJsonResult(true, F("Acquisition started"));
  } else if (action == F("stop")) {
    acquisitionRunning = false;
    updateAlarm();
    sendJsonResult(true, F("Acquisition stopped"));
  } else {
    sendJsonResult(false, F("Action must be start or stop"), 400);
  }
}

int requestedSensor() {
  return server.hasArg("sensor") ? server.arg("sensor").toInt() : 0;
}

void handleLatestCsv() {
  const int sensor = requestedSensor();
  if (sensor < 0 || sensor >= sensorCount || !sensorValid[sensor]) {
    server.send(404, "text/plain", "Sensor unavailable");
    return;
  }
  String csv = F("time,temperature\n");
  csv += String(sampleTimeSeconds);
  csv += ',';
  csv += String(temperaturesC[sensor], 3);
  csv += '\n';
  server.send(200, "text/csv", csv);
}

void handleHistoryJson() {
  const int sensor = requestedSensor();
  if (sensor < 0 || sensor >= sensorCount) {
    server.send(404, "text/plain", "Sensor unavailable");
    return;
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  server.sendContent(F("{\"sensor\":"));
  server.sendContent(String(sensor));
  server.sendContent(F(",\"points\":["));
  for (uint16_t offset = 0; offset < historyCount; ++offset) {
    const uint16_t index =
        (historyHead + MAX_HISTORY_SAMPLES - historyCount + offset) %
        MAX_HISTORY_SAMPLES;
    if (offset) {
      server.sendContent(",");
    }
    String point = "[";
    point += String(historyTimes[index]);
    point += ',';
    if (isnan(historyTemperatures[index][sensor])) {
      point += F("null]");
    } else {
      point += String(historyTemperatures[index][sensor], 3);
      point += ']';
    }
    server.sendContent(point);
  }
  server.sendContent(F("]}"));
}

void handleCsvDownload() {
  const int sensor = requestedSensor();
  if (sensor < 0 || sensor >= sensorCount) {
    server.send(404, "text/plain", "Sensor unavailable");
    return;
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.sendHeader("Content-Disposition", "attachment; filename=\"temperature.csv\"");
  server.send(200, "text/csv", "");
  server.sendContent(F("time,temperature\n"));
  for (uint16_t offset = 0; offset < historyCount; ++offset) {
    const uint16_t index =
        (historyHead + MAX_HISTORY_SAMPLES - historyCount + offset) %
        MAX_HISTORY_SAMPLES;
    if (isnan(historyTemperatures[index][sensor])) {
      continue;
    }
    String row = String(historyTimes[index]) + ',' +
                 String(historyTemperatures[index][sensor], 3) + '\n';
    server.sendContent(row);
  }
}

void configureRoutes() {
  server.on("/", HTTP_GET,
            []() { server.send_P(200, "text/html; charset=utf-8", WEB_PAGE); });
  server.on("/api/status", HTTP_GET,
            []() { server.send(200, "application/json", statusJson()); });
  server.on("/api/history", HTTP_GET, handleHistoryJson);
  server.on("/api/latest", HTTP_GET, handleLatestCsv);
  server.on("/api/csv", HTTP_GET, handleCsvDownload);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/control", HTTP_POST, handleControl);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.config(DEVICE_IP, GATEWAY, SUBNET, DNS_SERVER);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print(F("Connecting to Wi-Fi"));
  const unsigned long deadline = millis() + 20000UL;
  while (WiFi.status() != WL_CONNECTED &&
         static_cast<long>(deadline - millis()) > 0) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("Landing page: http://"));
    Serial.println(WiFi.localIP());
    if (MDNS.begin(MDNS_NAME)) {
      MDNS.addService("http", "tcp", HTTP_PORT);
      Serial.println(F("Also: http://temperature-logger.local/"));
    }
    return;
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  accessPointMode = WiFi.softAP("Temperature-Logger-Setup");
  Serial.println(F("Wi-Fi connection failed; setup AP enabled"));
  Serial.print(F("Join Temperature-Logger-Setup and open http://"));
  Serial.println(WiFi.softAPIP());
}

void setup() {
  Serial.begin(115200);
  pinMode(ALARM_OUTPUT_PIN, OUTPUT);
  digitalWrite(ALARM_OUTPUT_PIN, LOW);
  discoverSensors();
  connectWifi();
  configureRoutes();
  server.begin();
  Serial.print(F("Detected DS18B20 sensors: "));
  Serial.println(sensorCount);
}

void loop() {
  server.handleClient();
  if (acquisitionRunning &&
      static_cast<unsigned long>(millis() - lastSampleMs) >= sampleIntervalMs) {
    lastSampleMs = millis();
    takeSample();
  }
}
