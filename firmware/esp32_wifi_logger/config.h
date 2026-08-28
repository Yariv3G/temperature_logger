#pragma once

#include <Arduino.h>
#include <WiFi.h>

// Safe default pins for a classic ESP32 DevKit / ESP-WROOM-32.
constexpr uint8_t ONE_WIRE_PIN = 27;
constexpr uint8_t ALARM_OUTPUT_PIN = 26;
constexpr uint8_t MAX_SENSORS = 16;
constexpr uint16_t MAX_HISTORY_SAMPLES = 300;

constexpr unsigned long DEFAULT_SAMPLE_INTERVAL_MS = 2000UL;
constexpr float DEFAULT_LOWER_LIMIT_C = 10.0F;
constexpr float DEFAULT_UPPER_LIMIT_C = 40.0F;
constexpr uint16_t HTTP_PORT = 80;

// Keep the same logger address as the Uno version. Never power both loggers
// on the same network with this address.
static IPAddress DEVICE_IP(10, 100, 102, 247);
static IPAddress GATEWAY(10, 100, 102, 1);
static IPAddress SUBNET(255, 255, 255, 0);
static IPAddress DNS_SERVER(10, 100, 102, 1);
constexpr char MDNS_NAME[] = "temperature-logger";
