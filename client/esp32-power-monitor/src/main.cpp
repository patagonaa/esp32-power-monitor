#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#include "config.h"
#include "clients.h"
#include "state.h"

#include "loopConnection.h"
#include "loopMetaData.h"

WiFiMulti wifiMulti;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

hw_timer_t *millisTimer = NULL;

#define PULSE_COUNT_BYTES 4

void meterPulseIsr();
void millisIsr();
bool writeWattHoursMQTT(int meterNum, float wattHours);
bool writePowerMQTT(int meterNum, float power);

volatile DRAM_ATTR time_ms_t isrCurrentTime = 0;
pulse_t totalPulseCounts[METER_COUNT] = {};

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
void setup()
{
  // Serial
  Serial.begin(115200);

  // Milliseconds Timer
  millisTimer = timerBegin(0, F_CPU / 1000000, true);
  timerAttachInterrupt(millisTimer, &millisIsr, true);
  timerAlarmWrite(millisTimer, 1000, true);
  timerAlarmEnable(millisTimer);

  for (int i = 0; i < METER_COUNT; i++)
  {
    isrMeterStates[i].lastActiveTime = 1000000;
    isrMeterStates[i].unhandledPulseCount = 0;
    isrMeterStates[i].lastPulseTime = 0;
  }

  // Pins
  for (int i = 0; i < METER_COUNT; i++)
  {
    pinMode(meterConfigs[i].pulsePin, INPUT_PULLUP);
  }
  delay(100);
  for (int i = 0; i < METER_COUNT; i++)
  {
    attachInterrupt(meterConfigs[i].pulsePin, meterPulseIsr, CHANGE);
  }
  delay(1000);

  // WiFi
  WiFi.persistent(false);

  if (strlen(WIFI_SSID) > 0)
  {
    wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  }
  wifiMulti.addAP("Freifunk", NULL);
  wifiMulti.addAP("karlsruhe.freifunk.net", NULL);

  if (wifiMulti.run() == WL_CONNECTED)
  {
    Serial.println();
    Serial.println("WiFi connected:");
    Serial.println(WiFi.SSID());
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  }

  ArduinoOTA.setHostname("powermeter-" METER_NAME);
  ArduinoOTA.setPassword((const char *)OTA_PASSWORD);
  ArduinoOTA.setPort(8266);
  ArduinoOTA.begin();

  // MQTT
  mqttClient.setServer(MQTT_SERVER, 1883);

  //Random Seed
  randomSeed(micros());

  //Start background tasks
  xTaskCreate(
      loopConnection,
      "Connection",
      10000, /* Stack size */
      NULL,  /* Parameter */
      1,     /* Priority */
      NULL); /* Task handle. */
  xTaskCreate(
      loopMetaData,
      "SendMetaData",
      10000, /* Stack size */
      NULL,  /* Parameter */
      1,     /* Priority */
      NULL); /* Task handle. */
}

time_ms_t lastHandledPulseTimes[METER_COUNT] = {};

void loop()
{
  struct meterState meterStates[METER_COUNT];

  portENTER_CRITICAL(&mux);
  memcpy(meterStates, (void *)isrMeterStates, sizeof(meterStates));
  for (int i = 0; i < METER_COUNT; i++)
  {
    isrMeterStates[i].unhandledPulseCount = 0;
  }
  portEXIT_CRITICAL(&mux);

  for (int i = 0; i < METER_COUNT; i++)
  {
    pulse_t lastUnhandledPulseTime = meterStates[i].lastPulseTime;
    pulse_t unhandledPulseCount = meterStates[i].unhandledPulseCount;

    if (unhandledPulseCount > 0)
    {
      totalPulseCounts[i] += unhandledPulseCount;
      pulse_t totalPulseCount = totalPulseCounts[i];

      if (!writeWattHoursMQTT(i, (totalPulseCount / meterConfigs[i].pulsesPerKilowattHour) * 1000.0f))
      {
        Serial.println("Error while publishing watthours to MQTT");
      }

      Serial.print("Meter ");
      Serial.print(i);
      Serial.print(" at ");
      Serial.print((unsigned long)lastHandledPulseTimes[i]);
      Serial.print(": ");
      Serial.println(totalPulseCount);

      if (lastHandledPulseTimes[i] != 0)
      {
        pulse_t pulseDiff = unhandledPulseCount;
        time_ms_t timeDiff = lastUnhandledPulseTime - lastHandledPulseTimes[i];
        if (timeDiff > 0)
        {
          float power = (float)pulseDiff / (timeDiff / 1000.0f / 60.0f / 60.0f) / meterConfigs[i].pulsesPerKilowattHour * 1000.0f;
          writePowerMQTT(i, power);
        }
      }

      lastHandledPulseTimes[i] = lastUnhandledPulseTime;
    }
  }
  ArduinoOTA.handle();
  delay(500);
}

bool writeWattHoursMQTT(int meterCount, float wattHours)
{
  if (wifiClient.connected() && mqttClient.connected())
  {
    char topicBuffer[50];
    sprintf(topicBuffer, "powermeter/%s/watthours_total/%d", METER_NAME, meterCount);

    char payloadBuffer[50];
    sprintf(payloadBuffer, "%.2f", wattHours);
    return mqttClient.publish(topicBuffer, payloadBuffer, true);
  }
  return false;
}

bool writePowerMQTT(int meterCount, float power)
{
  if (wifiClient.connected() && mqttClient.connected())
  {
    char topicBuffer[50];
    sprintf(topicBuffer, "powermeter/%s/watts/%d", METER_NAME, meterCount);

    char payloadBuffer[50];
    sprintf(payloadBuffer, "%.2f", power);
    return mqttClient.publish(topicBuffer, payloadBuffer, false);
  }
  return false;
}

void IRAM_ATTR meterPulseInactive(int meterNum, time_ms_t pulseTime)
{
  volatile struct meterState *meterState = &isrMeterStates[meterNum];
  const struct meterConfig *meterConfig = &meterConfigs[meterNum];
  if (pulseTime > (meterState->lastActiveTime + meterConfig->minPulseLength) && pulseTime > (meterState->lastPulseTime + meterConfig->minPulseLength))
  {
    meterState->unhandledPulseCount++;
    meterState->lastPulseTime = pulseTime;
  }
}

void IRAM_ATTR meterPulseActive(int meterNum, time_ms_t pulseTime)
{
  volatile struct meterState *meterState = &isrMeterStates[meterNum];
  meterState->lastActiveTime = pulseTime;
}

void IRAM_ATTR meterPulseIsr()
{
  portENTER_CRITICAL_ISR(&mux);
  time_ms_t pulseTime = isrCurrentTime;
  for (int i = 0; i < METER_COUNT; i++)
  {
    bool state = digitalRead(meterConfigs[i].pulsePin) ^ meterConfigs[i].invert;
    bool lastState = isrMeterStates[i].lastState;
    if (state != lastState)
    {
      isrMeterStates[i].lastState = state;
      state ? meterPulseActive(i, pulseTime) : meterPulseInactive(i, pulseTime);
    }
  }
  portEXIT_CRITICAL_ISR(&mux);
}

void IRAM_ATTR millisIsr()
{
  isrCurrentTime++;
}