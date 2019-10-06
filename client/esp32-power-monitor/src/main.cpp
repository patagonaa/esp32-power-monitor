#include <Arduino.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <PubSubClient.h>

#include "credentials.h"

typedef uint32_t pulse_t;
typedef uint64_t time_ms_t;

#define METER_NAME "esp32-01"
const int meterPulsePin = 0;
const int minPulseLength = 40;                     // ms
const time_ms_t statsSendInterval = 1 * 60 * 1000; // 1 minute
const float pulsesPerKilowattHour = 1000;

WiFiMulti wifiMulti;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

hw_timer_t *millisTimer = NULL;

void meterPulseIsr();
void millisIsr();
void writePulseCountEEPROM(pulse_t currentPulseCount);
bool writeWattHoursMQTT(float wattHours);
bool writePowerMQTT(float power);
bool writeStatsMQTT(float temp, time_ms_t time);

//defined in arduino-esp32/cores/esp32/esp32-hal-misc.c
float temperatureRead();

volatile DRAM_ATTR time_ms_t isrLastLowTime = 1000000; // when was the pulse input low last
volatile DRAM_ATTR time_ms_t isrCurrentTime = 0;
volatile DRAM_ATTR pulse_t isrUnhandledPulseCount = 0; // number of unhandled pulses
volatile DRAM_ATTR time_ms_t isrLastPulseTime = 0;     // time of last pulse
time_ms_t lastHandledPulseTime = 0;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
pulse_t totalPulseCount;
time_ms_t lastStatsSendTime = 0;
void setup()
{
  // Serial
  Serial.begin(115200);

  // Milliseconds Timer
  millisTimer = timerBegin(0, F_CPU / 1000000, true);
  timerAttachInterrupt(millisTimer, &millisIsr, true);
  timerAlarmWrite(millisTimer, 1000, true);
  timerAlarmEnable(millisTimer);

  // Pulse Pin
  pinMode(meterPulsePin, INPUT_PULLUP);
  delay(100);
  attachInterrupt(meterPulsePin, meterPulseIsr, CHANGE);
  delay(1000);

  // EEPROM
  EEPROM.begin(4);
  //writePulseCountEEPROM(181);
  totalPulseCount = (EEPROM.read(0) << 24) | (EEPROM.read(1) << 16) | (EEPROM.read(2) << 8) | EEPROM.read(3);
  Serial.println(totalPulseCount);

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

  // MQTT
  mqttClient.setServer(MQTT_SERVER, 1883);

  //Random Seed
  randomSeed(micros());
}

void ensureConnected()
{
  if (wifiMulti.run() != WL_CONNECTED)
  {
    Serial.println("Reconnecting to Wifi failed... rebooting in 10 seconds");
    delay(10000);
    writePulseCountEEPROM(totalPulseCount + isrUnhandledPulseCount);
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

void loop()
{
  ensureConnected();
  mqttClient.loop();
  portENTER_CRITICAL(&mux);
  time_ms_t currentTime = isrCurrentTime;
  pulse_t unhandledPulseCount = isrUnhandledPulseCount;
  time_ms_t lastUnhandledPulseTime = isrLastPulseTime;
  isrUnhandledPulseCount = 0;
  portEXIT_CRITICAL(&mux);

  if (currentTime > (lastStatsSendTime + statsSendInterval))
  {
    if (writeStatsMQTT(temperatureRead(), currentTime))
    {
      Serial.println("published stats to MQTT");
      lastStatsSendTime = currentTime;
    }
    else
    {
      Serial.println("Error while publishing stats to MQTT");
    }
  }

  if (unhandledPulseCount > 0)
  {
    totalPulseCount += unhandledPulseCount;

    writePulseCountEEPROM(totalPulseCount);
    if (!writeWattHoursMQTT((totalPulseCount / pulsesPerKilowattHour) * 1000.0f))
    {
      Serial.println("Error while publishing watthours to MQTT");
    }

    Serial.println(totalPulseCount);

    if (lastHandledPulseTime != 0)
    {
      pulse_t pulseDiff = unhandledPulseCount;
      time_ms_t timeDiff = lastUnhandledPulseTime - lastHandledPulseTime;
      if (timeDiff > 0)
      {
        float power = (float)pulseDiff / (timeDiff / 1000.0f / 60.0f / 60.0f) / pulsesPerKilowattHour * 1000.0f;
        writePowerMQTT(power);
      }
    }

    lastHandledPulseTime = lastUnhandledPulseTime;
  }

  mqttClient.loop();
  delay(500);
}

void writePulseCountEEPROM(pulse_t currentPulseCount)
{
  EEPROM.write(0, (uint8_t)(currentPulseCount >> 24 & 0xFF));
  EEPROM.write(1, (uint8_t)(currentPulseCount >> 16 & 0xFF));
  EEPROM.write(2, (uint8_t)(currentPulseCount >> 8 & 0xFF));
  EEPROM.write(3, (uint8_t)(currentPulseCount & 0xFF));
  EEPROM.commit();
}

bool writeWattHoursMQTT(float wattHours)
{
  if (wifiClient.connected() && mqttClient.connected())
  {
    char buffer[50];
    sprintf(buffer, "%.2f", wattHours);
    return mqttClient.publish("powermeter/" METER_NAME "/watthours_total", buffer, true);
  }
  return false;
}

bool writePowerMQTT(float power)
{
  if (wifiClient.connected() && mqttClient.connected())
  {
    char buffer[50];
    sprintf(buffer, "%.2f", power);
    return mqttClient.publish("powermeter/" METER_NAME "/watts", buffer, false);
  }
  return false;
}

bool writeStatsMQTT(float temp, time_ms_t time)
{
  if (wifiClient.connected() && mqttClient.connected())
  {
    char buffer[50];
    bool success = true;
    sprintf(buffer, "%.2f", temp);
    success &= mqttClient.publish("powermeter/" METER_NAME "/temperature_c", buffer, false);
    sprintf(buffer, "%" PRIu64, time);
    success &= mqttClient.publish("powermeter/" METER_NAME "/uptime_ms", buffer, false);
    return success;
  }
  return false;
}

void IRAM_ATTR meterPulseHigh(time_ms_t pulseTime)
{
  if (pulseTime > (isrLastLowTime + minPulseLength) && pulseTime > (isrLastPulseTime + minPulseLength))
  {
    isrUnhandledPulseCount++;
    isrLastPulseTime = pulseTime;
  }
}

void IRAM_ATTR meterPulseLow(time_ms_t pulseTime)
{
  isrLastLowTime = pulseTime;
}

void IRAM_ATTR meterPulseIsr()
{
  portENTER_CRITICAL_ISR(&mux);
  time_ms_t pulseTime = isrCurrentTime;
  digitalRead(meterPulsePin) ? meterPulseHigh(pulseTime) : meterPulseLow(pulseTime);
  portEXIT_CRITICAL_ISR(&mux);
}

void IRAM_ATTR millisIsr()
{
  isrCurrentTime++;
}