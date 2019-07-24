#include <Arduino.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <PubSubClient.h>

#include "credentials.h"

#define METER_NAME "esp32-01"
const int meterPulsePin = 0;
const int minPulseLength = 10;                         // ms
const unsigned long aliveSendInterval = 5 * 60 * 1000; // 5 minutes

typedef uint32_t pulse_t;

WiFiMulti wifiMulti;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

hw_timer_t *millisTimer = NULL;

void meterPulseIsr();
void millisIsr();
void writePulseCountEEPROM(pulse_t currentPulseCount);
bool writePulseCountMQTT(pulse_t currentPulseCount);
bool writePowerMQTT(double power);

volatile DRAM_ATTR pulse_t isrPulseCount = 0;         // total pulse count
volatile DRAM_ATTR unsigned long isrLastHighTime = 0; // when was the pulse input high last
volatile DRAM_ATTR unsigned long isrLastLowTime = 0;  // when was the pulse input low last
volatile DRAM_ATTR unsigned long isrCurrentTime = 0;
volatile DRAM_ATTR pulse_t isrUnhandledPulseCount = 0; // number of unhandled pulses
volatile DRAM_ATTR unsigned long isrLastPulseTime = 0; // time of last pulse
unsigned long lastHandledPulseTime = 0;

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

pulse_t lastPulseCount;
unsigned long lastSendTime = 0;
void setup()
{
  // Serial
  Serial.begin(9600);

  // Milliseconds Timer
  millisTimer = timerBegin(0, F_CPU / 1000000, true);
  timerAttachInterrupt(millisTimer, &millisIsr, true);
  timerAlarmWrite(millisTimer, 1000, true);
  timerAlarmEnable(millisTimer);

  // Pulse Pin
  pinMode(meterPulsePin, INPUT_PULLUP);
  attachInterrupt(meterPulsePin, meterPulseIsr, CHANGE);
  delay(1000);

  // EEPROM
  EEPROM.begin(4);
  //writePulseCountEEPROM(181);
  isrPulseCount = (EEPROM.read(0) << 24) | (EEPROM.read(1) << 16) | (EEPROM.read(2) << 8) | EEPROM.read(3);
  lastPulseCount = isrPulseCount;
  Serial.println(isrPulseCount);

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
    writePulseCountEEPROM(isrPulseCount);
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
      delay(5000);
    }
  }
}

void loop()
{
  ensureConnected();

  portENTER_CRITICAL(&mux);
  pulse_t currentPulseCount = isrPulseCount;
  unsigned long currentTime = isrCurrentTime;
  pulse_t unhandledPulseCount = isrUnhandledPulseCount;
  unsigned long lastUnhandledPulseTime = isrLastPulseTime;
  isrUnhandledPulseCount = 0;
  portEXIT_CRITICAL(&mux);

  if (currentPulseCount != lastPulseCount)
  {
    writePulseCountEEPROM(currentPulseCount);
    if (!writePulseCountMQTT(currentPulseCount))
    {
      Serial.println("Error while publishing to MQTT");
    }

    Serial.println(currentPulseCount);
    lastPulseCount = currentPulseCount;
    lastSendTime = currentTime;
  }
  if (currentTime > (lastSendTime + aliveSendInterval))
  {
    writePulseCountMQTT(currentPulseCount);
    lastSendTime = currentTime;
  }

  if (unhandledPulseCount > 0)
  {
    if (lastHandledPulseTime != 0)
    {
      pulse_t pulseDiff = unhandledPulseCount;
      unsigned long timeDiff = lastUnhandledPulseTime - lastHandledPulseTime;
      if (timeDiff > 0)
      {
        double power = (double)pulseDiff / (timeDiff / 1000.0 / 60.0 / 60.0);
        writePowerMQTT(power);
      }
    }

    lastHandledPulseTime = lastUnhandledPulseTime;
  }

  delay(100);
  mqttClient.loop();
}

void writePulseCountEEPROM(pulse_t currentPulseCount)
{
  EEPROM.write(0, (uint8_t)(currentPulseCount >> 24 & 0xFF));
  EEPROM.write(1, (uint8_t)(currentPulseCount >> 16 & 0xFF));
  EEPROM.write(2, (uint8_t)(currentPulseCount >> 8 & 0xFF));
  EEPROM.write(3, (uint8_t)(currentPulseCount & 0xFF));
  EEPROM.commit();
}

bool writePulseCountMQTT(pulse_t currentPulseCount)
{
  if (wifiClient.connected() && mqttClient.connected())
  {
    char buffer[50];
    sprintf(buffer, "%" PRIu32, currentPulseCount);
    return mqttClient.publish("powermeter/" METER_NAME "/watthours_total", buffer, true);
  }
  return false;
}

bool writePowerMQTT(double power)
{
  if (wifiClient.connected() && mqttClient.connected())
  {
    char buffer[50];
    sprintf(buffer, "%.2f", power);
    return mqttClient.publish("powermeter/" METER_NAME "/watts", buffer, false);
  }
  return false;
}

void IRAM_ATTR meterPulseHigh(unsigned long pulseTime)
{
  isrLastHighTime = pulseTime;
}

void IRAM_ATTR meterPulseLow(unsigned long pulseTime)
{
  if (pulseTime > (isrLastLowTime + (minPulseLength * 2)) && pulseTime > (isrLastHighTime + minPulseLength))
  {
    portENTER_CRITICAL(&mux);
    isrPulseCount++;
    isrLastPulseTime = pulseTime;
    isrUnhandledPulseCount++;
    portEXIT_CRITICAL(&mux);
  }

  isrLastLowTime = pulseTime;
}

void IRAM_ATTR meterPulseIsr()
{
  portENTER_CRITICAL(&mux);
  unsigned long pulseTime = isrCurrentTime;
  portEXIT_CRITICAL(&mux);
  digitalRead(meterPulsePin) ? meterPulseHigh(pulseTime) : meterPulseLow(pulseTime);
}

void IRAM_ATTR millisIsr()
{
  portENTER_CRITICAL(&mux);
  isrCurrentTime++;
  portEXIT_CRITICAL(&mux);
}