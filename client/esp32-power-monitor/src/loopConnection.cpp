#include <Arduino.h>
#include <WiFi.h>
#include "clients.h"
#include "config.h"

const int maxRetries = 5;
int connectRetryCount = 0;

void ensureConnected()
{
  if (wifiMulti.run() != WL_CONNECTED)
  {
    Serial.println("Reconnecting to Wifi failed...");
    delay(10000);
    connectRetryCount++;
  }
  else if (!mqttClient.connected())
  {
    Serial.println("Connecting to Mqtt...");
    if (mqttClient.connect("powermeter-" METER_NAME, MQTT_USER, MQTT_PASSWORD, "powermeter/" METER_NAME "/dead", MQTTQOS0, false, "pwease weconnect me UwU"))
    {
      Serial.println("connected");
      if (mqttClient.publish("powermeter/" METER_NAME "/up", "connected!", false))
      {
        Serial.println("sent up message successfully!");
        connectRetryCount = 0;
      }
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.println(mqttClient.state());
      delay(1000);
      connectRetryCount++;
    }
  }

  if (connectRetryCount >= maxRetries)
  {
    Serial.println("Rebooting in 5 seconds");
    delay(5000);
    ESP.restart();
  }
  else if (connectRetryCount == 0)
  {
  }
  else
  {
    Serial.print("Try ");
    Serial.print(connectRetryCount);
    Serial.print(" / ");
    Serial.println(maxRetries);
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