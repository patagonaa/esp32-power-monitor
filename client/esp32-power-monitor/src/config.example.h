#pragma once

#define METER_NAME "esp32-01"
#define METER_COUNT 1

#define MQTT_SERVER "mqtt.example.com"
#define MQTT_USER "testuser"
#define MQTT_PASSWORD "secretsecretsecret"

#define WIFI_SSID "HomeWiFi"
#define WIFI_PASSWORD "secretsecretsecret"
#define OTA_PASSWORD "secretsecretsecret"

#define STATS_SEND_INTERVAL (1 * 60 * 1000)

struct meterConfig
{
    const int pulsePin;
    const int minPulseLength; // ms
    const float pulsesPerKilowattHour;
    const bool invert;
};

extern const struct meterConfig meterConfigs[METER_COUNT];