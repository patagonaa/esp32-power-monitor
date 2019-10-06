#include <Arduino.h>
#include "clients.h"
#include "config.h"

//defined in arduino-esp32/cores/esp32/esp32-hal-misc.c
float temperatureRead();

bool publishMetaData()
{
    if (wifiClient.connected() && mqttClient.connected())
    {
        char buffer[50];
        bool success = true;
        sprintf(buffer, "%.2f", temperatureRead());
        success &= mqttClient.publish("powermeter/" METER_NAME "/temperature_c", buffer, false);
        sprintf(buffer, "%lu", millis());
        success &= mqttClient.publish("powermeter/" METER_NAME "/uptime_ms", buffer, false);
        return success;
    }
    return false;
}

bool loopMetaData(void *param)
{
    while (true)
    {
        publishMetaData();
        delay(STATS_SEND_INTERVAL);
    }
}