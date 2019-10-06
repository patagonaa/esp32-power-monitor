#include <Arduino.h>
#include <WiFi.h>
#include "clients.h"
#include "config.h"


void ensureConnected()
{
  if (wifiMulti.run() != WL_CONNECTED)
  {
    Serial.println("Reconnecting to Wifi failed... rebooting in 10 seconds");
    delay(10000);
    ESP.restart();
  }
  else if (!mqttClient.connected())
  {
    Serial.println("Connecting to Mqtt...");
    if (mqttClient.connect("powermeter-" METER_NAME, MQTT_USER, MQTT_PASSWORD, "powermeter/" METER_NAME "/dead", MQTTQOS0, false, "pwease weconnect me UwU"))
    {
      Serial.println("connected");
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(mqttClient.state());
      delay(1000);
    }
  }
}

void loopConnection(void *param)
{
  while (true)
  {
    ensureConnected();
    mqttClient.loop();
    delay(1000);
  }
}