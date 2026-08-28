#pragma once

#include <Arduino.h>
#include <Ethernet.h>

// W5100 Ethernet shield: D10 is Ethernet CS and D4 is SD CS.
constexpr uint8_t ONE_WIRE_PIN = 2;
constexpr uint8_t SD_CS_PIN = 4;
constexpr uint8_t ALARM_OUTPUT_PIN = 6;
constexpr uint8_t ETHERNET_CHIP_SELECT_PIN = 10;

constexpr uint8_t MAX_SENSORS = 8;
constexpr unsigned long DEFAULT_SAMPLE_INTERVAL_MS = 2000UL;
constexpr unsigned long MIN_SAMPLE_INTERVAL_MS = 1000UL;
constexpr unsigned long MAX_SAMPLE_INTERVAL_MS = 3600000UL;
constexpr float DEFAULT_LOWER_LIMIT_C = 10.0F;
constexpr float DEFAULT_UPPER_LIMIT_C = 40.0F;

// Use a locally administered MAC address. Change it if another device uses it.
static byte MAC_ADDRESS[] = {0x02, 0x54, 0x4C, 0x4F, 0x47, 0x01};
static IPAddress DEVICE_IP(10, 100, 102, 247);
static IPAddress DNS_SERVER(10, 100, 102, 1);
static IPAddress GATEWAY(10, 100, 102, 1);
static IPAddress SUBNET(255, 255, 255, 0);

constexpr uint16_t HTTP_PORT = 80;
constexpr char SD_LOG_FILE[] = "log.csv";
