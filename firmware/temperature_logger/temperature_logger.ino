#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include <OneWire.h>
#include <avr/pgmspace.h>

#include "config.h"
#include "web_assets.h"

OneWire oneWire(ONE_WIRE_PIN);
EthernetServer server(HTTP_PORT);

typedef uint8_t SensorAddress[8];
SensorAddress sensorAddresses[MAX_SENSORS];
float temperaturesC[MAX_SENSORS];
bool sensorValid[MAX_SENSORS];
uint8_t sensorCount = 0;

bool acquisitionRunning = false;
bool alarmActive = false;
bool sdReady = false;
float lowerLimitC = DEFAULT_LOWER_LIMIT_C;
float upperLimitC = DEFAULT_UPPER_LIMIT_C;
unsigned long sampleIntervalMs = DEFAULT_SAMPLE_INTERVAL_MS;
unsigned long lastSampleMs = 0;
unsigned long sampleTimeSeconds = 0;
unsigned long sampleSequence = 0;

void selectNoSpiDevice() {
  digitalWrite(ETHERNET_CHIP_SELECT_PIN, HIGH);
  digitalWrite(SD_CS_PIN, HIGH);
}

void discoverSensors() {
  sensorCount = 0;
  oneWire.reset_search();
  uint8_t address[8];
  while (sensorCount < MAX_SENSORS && oneWire.search(address)) {
    if (address[0] == 0x28 && OneWire::crc8(address, 7) == address[7]) {
      memcpy(sensorAddresses[sensorCount], address, sizeof(address));
      temperaturesC[sensorCount] = 0.0F;
      sensorValid[sensorCount] = false;
      sensorCount++;
    }
  }
}

void printAddress(Print &output, const SensorAddress address) {
  for (uint8_t i = 0; i < 8; ++i) {
    if (address[i] < 16) {
      output.print('0');
    }
    output.print(address[i], HEX);
  }
}

void ensureLogHeader() {
  if (!sdReady || SD.exists(SD_LOG_FILE)) {
    return;
  }
  File log = SD.open(SD_LOG_FILE, FILE_WRITE);
  if (log) {
    log.println(F("time_s,sensor,temperature_c"));
    log.close();
  }
}

void appendSampleToSd() {
  if (!sdReady) {
    return;
  }
  File log = SD.open(SD_LOG_FILE, FILE_WRITE);
  if (!log) {
    sdReady = false;
    return;
  }
  for (uint8_t i = 0; i < sensorCount; ++i) {
    log.print(sampleTimeSeconds);
    log.print(',');
    log.print(i);
    log.print(',');
    if (sensorValid[i]) {
      log.println(temperaturesC[i], 3);
    } else {
      log.println(F("FAULT"));
    }
  }
  log.close();
}

void updateAlarm() {
  bool outOfRange = sensorCount == 0;
  for (uint8_t i = 0; i < sensorCount; ++i) {
    if (!sensorValid[i] || temperaturesC[i] < lowerLimitC ||
        temperaturesC[i] > upperLimitC) {
      outOfRange = true;
    }
  }
  alarmActive = acquisitionRunning && outOfRange;
  digitalWrite(ALARM_OUTPUT_PIN, alarmActive ? HIGH : LOW);
}

void takeSample() {
  oneWire.reset();
  oneWire.skip();
  oneWire.write(0x44);
  delay(750);

  for (uint8_t i = 0; i < sensorCount; ++i) {
    uint8_t scratchpad[9];
    if (!oneWire.reset()) {
      sensorValid[i] = false;
      continue;
    }
    oneWire.select(sensorAddresses[i]);
    oneWire.write(0xBE);
    for (uint8_t byteIndex = 0; byteIndex < sizeof(scratchpad); ++byteIndex) {
      scratchpad[byteIndex] = oneWire.read();
    }
    const int16_t raw =
        static_cast<int16_t>((scratchpad[1] << 8) | scratchpad[0]);
    const float value = raw / 16.0F;
    sensorValid[i] = OneWire::crc8(scratchpad, 8) == scratchpad[8] &&
                     value >= -55.0F && value <= 125.0F;
    temperaturesC[i] = value;
  }
  sampleTimeSeconds = millis() / 1000UL;
  ++sampleSequence;
  updateAlarm();
  appendSampleToSd();
}

void sendHeaders(EthernetClient &client, const __FlashStringHelper *contentType,
                 const __FlashStringHelper *status = nullptr) {
  if (status == nullptr) {
    status = F("200 OK");
  }
  client.print(F("HTTP/1.1 "));
  client.println(status);
  client.print(F("Content-Type: "));
  client.println(contentType);
  client.println(F("Cache-Control: no-store"));
  client.println(F("Connection: close"));
  client.println();
}

void sendWebPage(EthernetClient &client) {
  if (sdReady) {
    File page = SD.open("index.htm", FILE_READ);
    if (page) {
      sendHeaders(client, F("text/html; charset=utf-8"));
      uint8_t fileBuffer[48];
      while (page.available()) {
        const int count = page.read(fileBuffer, sizeof(fileBuffer));
        if (count > 0) {
          client.write(fileBuffer, static_cast<size_t>(count));
        }
      }
      page.close();
      return;
    }
  }
  sendHeaders(client, F("text/html; charset=utf-8"));
  char buffer[48];
  uint16_t offset = 0;
  while (true) {
    uint8_t length = 0;
    while (length < sizeof(buffer)) {
      const char value = pgm_read_byte_near(WEB_PAGE + offset++);
      if (value == '\0') {
        break;
      }
      buffer[length++] = value;
    }
    if (length > 0) {
      client.write(reinterpret_cast<const uint8_t *>(buffer), length);
    }
    if (length < sizeof(buffer)) {
      break;
    }
  }
}

void sendStatus(EthernetClient &client) {
  sendHeaders(client, F("application/json"));
  client.print(F("{\"running\":"));
  client.print(acquisitionRunning ? F("true") : F("false"));
  client.print(F(",\"alarm\":"));
  client.print(alarmActive ? F("true") : F("false"));
  client.print(F(",\"sd\":"));
  client.print(sdReady ? F("true") : F("false"));
  client.print(F(",\"interval\":"));
  client.print(sampleIntervalMs / 1000UL);
  client.print(F(",\"lower\":"));
  client.print(lowerLimitC, 2);
  client.print(F(",\"upper\":"));
  client.print(upperLimitC, 2);
  client.print(F(",\"time\":"));
  client.print(sampleTimeSeconds);
  client.print(F(",\"seq\":"));
  client.print(sampleSequence);
  client.print(F(",\"sensorCount\":"));
  client.print(sensorCount);
  client.print(F(",\"sensors\":["));
  for (uint8_t i = 0; i < sensorCount; ++i) {
    if (i > 0) {
      client.print(',');
    }
    client.print(F("{\"address\":\""));
    printAddress(client, sensorAddresses[i]);
    client.print(F("\",\"valid\":"));
    client.print(sensorValid[i] ? F("true") : F("false"));
    client.print(F(",\"c\":"));
    client.print(sensorValid[i] ? temperaturesC[i] : 0.0F, 3);
    client.print('}');
  }
  client.println(F("]}"));
}

int formInt(const String &body, const char *key, int fallback) {
  const String prefix = String(key) + '=';
  int start = body.indexOf(prefix);
  if (start < 0) {
    return fallback;
  }
  start += prefix.length();
  int finish = body.indexOf('&', start);
  if (finish < 0) {
    finish = body.length();
  }
  return body.substring(start, finish).toInt();
}

float formFloat(const String &body, const char *key, float fallback) {
  const String prefix = String(key) + '=';
  int start = body.indexOf(prefix);
  if (start < 0) {
    return fallback;
  }
  start += prefix.length();
  int finish = body.indexOf('&', start);
  if (finish < 0) {
    finish = body.length();
  }
  return body.substring(start, finish).toFloat();
}

void sendResult(EthernetClient &client, bool ok, const __FlashStringHelper *message) {
  sendHeaders(client, F("application/json"),
              ok ? F("200 OK") : F("400 Bad Request"));
  client.print(F("{\"ok\":"));
  client.print(ok ? F("true") : F("false"));
  client.print(F(",\"message\":\""));
  client.print(message);
  client.println(F("\"}"));
}

void configureAcquisition(EthernetClient &client, const String &body) {
  const int intervalSeconds = formInt(body, "interval", sampleIntervalMs / 1000UL);
  const float lower = formFloat(body, "lower", lowerLimitC);
  const float upper = formFloat(body, "upper", upperLimitC);
  if (intervalSeconds < 1 || intervalSeconds > 3600 || lower >= upper ||
      lower < -55.0F || upper > 125.0F) {
    sendResult(client, false, F("Invalid interval or limits"));
    return;
  }
  sampleIntervalMs = static_cast<unsigned long>(intervalSeconds) * 1000UL;
  lowerLimitC = lower;
  upperLimitC = upper;
  updateAlarm();
  sendResult(client, true, F("Configuration applied"));
}

void controlAcquisition(EthernetClient &client, const String &body) {
  if (body.indexOf(F("action=start")) >= 0) {
    acquisitionRunning = true;
    lastSampleMs = millis() - sampleIntervalMs;
    updateAlarm();
    sendResult(client, true, F("Acquisition started"));
  } else if (body.indexOf(F("action=stop")) >= 0) {
    acquisitionRunning = false;
    updateAlarm();
    sendResult(client, true, F("Acquisition stopped"));
  } else {
    sendResult(client, false, F("Action must be start or stop"));
  }
}

void sendLatestCsv(EthernetClient &client, const String &target) {
  int sensorIndex = 0;
  const int query = target.indexOf(F("sensor="));
  if (query >= 0) {
    sensorIndex = target.substring(query + 7).toInt();
  }
  if (sensorIndex < 0 || sensorIndex >= sensorCount || !sensorValid[sensorIndex]) {
    sendHeaders(client, F("text/plain"), F("404 Not Found"));
    client.println(F("Sensor unavailable"));
    return;
  }
  sendHeaders(client, F("text/csv"));
  client.println(F("time,temperature"));
  client.print(sampleTimeSeconds);
  client.print(',');
  client.println(temperaturesC[sensorIndex], 3);
}

void sendSdCsv(EthernetClient &client) {
  if (!sdReady) {
    sendHeaders(client, F("text/plain"), F("503 Service Unavailable"));
    client.println(F("SD card unavailable"));
    return;
  }
  File log = SD.open(SD_LOG_FILE, FILE_READ);
  if (!log) {
    sendHeaders(client, F("text/plain"), F("404 Not Found"));
    client.println(F("No log file"));
    return;
  }
  sendHeaders(client, F("text/csv"));
  uint8_t buffer[48];
  while (log.available()) {
    const int count = log.read(buffer, sizeof(buffer));
    if (count > 0) {
      client.write(buffer, static_cast<size_t>(count));
    }
  }
  log.close();
}

void sendNotFound(EthernetClient &client) {
  sendHeaders(client, F("text/plain"), F("404 Not Found"));
  client.println(F("Not found"));
}

void handleHttpClient(EthernetClient client) {
  client.setTimeout(400);
  String requestLine = client.readStringUntil('\n');
  requestLine.trim();
  if (requestLine.length() == 0) {
    client.stop();
    return;
  }

  int contentLength = 0;
  while (client.connected()) {
    String header = client.readStringUntil('\n');
    header.trim();
    if (header.length() == 0) {
      break;
    }
    if (header.startsWith(F("Content-Length:"))) {
      contentLength = header.substring(15).toInt();
    }
  }

  String body;
  const unsigned long deadline = millis() + 500UL;
  while (body.length() < static_cast<unsigned int>(contentLength) &&
         static_cast<long>(deadline - millis()) > 0) {
    while (client.available() && body.length() < static_cast<unsigned int>(contentLength)) {
      body += static_cast<char>(client.read());
    }
  }

  const int firstSpace = requestLine.indexOf(' ');
  const int secondSpace = requestLine.indexOf(' ', firstSpace + 1);
  const String method = requestLine.substring(0, firstSpace);
  const String target = requestLine.substring(firstSpace + 1, secondSpace);

  if (method == F("GET") && target == F("/")) {
    sendWebPage(client);
  } else if (method == F("GET") && target == F("/api/status")) {
    sendStatus(client);
  } else if (method == F("GET") && target == F("/api/history")) {
    sendStatus(client);
  } else if (method == F("GET") && target.startsWith(F("/api/latest"))) {
    sendLatestCsv(client, target);
  } else if (method == F("GET") && target == F("/api/csv")) {
    sendSdCsv(client);
  } else if (method == F("POST") && target == F("/api/config")) {
    configureAcquisition(client, body);
  } else if (method == F("POST") && target == F("/api/control")) {
    controlAcquisition(client, body);
  } else {
    sendNotFound(client);
  }
  delay(1);
  client.stop();
}

void setup() {
  pinMode(ALARM_OUTPUT_PIN, OUTPUT);
  digitalWrite(ALARM_OUTPUT_PIN, LOW);
  pinMode(ETHERNET_CHIP_SELECT_PIN, OUTPUT);
  pinMode(SD_CS_PIN, OUTPUT);
  selectNoSpiDevice();

  Serial.begin(115200);
  discoverSensors();

  digitalWrite(ETHERNET_CHIP_SELECT_PIN, HIGH);
  sdReady = SD.begin(SD_CS_PIN);
  ensureLogHeader();
  digitalWrite(SD_CS_PIN, HIGH);

  Ethernet.begin(MAC_ADDRESS, DEVICE_IP, DNS_SERVER, GATEWAY, SUBNET);
  server.begin();
  Serial.println(Ethernet.localIP());
}

void loop() {
  if (acquisitionRunning &&
      static_cast<unsigned long>(millis() - lastSampleMs) >= sampleIntervalMs) {
    lastSampleMs = millis();
    takeSample();
  }

  EthernetClient client = server.available();
  if (client) {
    handleHttpClient(client);
  }
}
