#include <Arduino.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <PubSubClient.h>

#include "credentials.h"

#define METER_NAME "esp32-01"
const int meterPulsePin = 0;
const int minPulseLength = 10; // ms
const unsigned long aliveSendInterval = 5 * 60 * 1000; // 5 minutes

const char *mqtt_server = MQTT_SERVER;
const char *mqtt_user = MQTT_USER;
const char *mqtt_password = MQTT_PASSWORD;

const char *wifi_ssid = WIFI_SSID;
const char *wifi_password = WIFI_PASSWORD;

typedef uint32_t pulse_t;

WiFiMulti wifiMulti;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

hw_timer_t *millisTimer = NULL;

void meterPulseIsr();
void millisIsr();
void writePulseCountEEPROM(pulse_t currentPulseCount);
bool writePulseCountMQTT(pulse_t currentPulseCount);

volatile DRAM_ATTR pulse_t pulseCount = 0;
volatile DRAM_ATTR unsigned long lastHighTime = 0;
volatile DRAM_ATTR unsigned long lastLowTime = 0;
volatile DRAM_ATTR unsigned long currentTime = 0;

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
  pulseCount = (EEPROM.read(0) << 24) | (EEPROM.read(1) << 16) | (EEPROM.read(2) << 8) | EEPROM.read(3);
  lastPulseCount = pulseCount;
  Serial.println(pulseCount);

  // WiFi
  WiFi.persistent(false);

  if (strlen(wifi_ssid) > 0)
  {
    wifiMulti.addAP(wifi_ssid, wifi_password);
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
  mqttClient.setServer(mqtt_server, 1883);

  //Random Seed
  randomSeed(micros());
}

void loop()
{
  unsigned long currentMillis = millis();
  if (wifiMulti.run() != WL_CONNECTED)
  {
    Serial.println("Reconnecting to Wifi failed... rebooting in 10 seconds");
    delay(10000);
    writePulseCountEEPROM(pulseCount);
    ESP.restart();
  }
  else if (!mqttClient.connected())
  {
    String clientId = "ESPClient-";
    clientId += String(random(0xffff), HEX);
    Serial.println("Connecting to Mqtt...");
    if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_password))
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
  uint32_t currentPulseCount = pulseCount;
  if (currentPulseCount != lastPulseCount || currentMillis > (lastSendTime + aliveSendInterval))
  {
    writePulseCountEEPROM(currentPulseCount);
    if (!writePulseCountMQTT(currentPulseCount))
    {
      Serial.println("Error while publishing to MQTT");
    }

    Serial.println(currentPulseCount);
    lastPulseCount = currentPulseCount;
    lastSendTime = currentMillis;
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
    size_t count = sprintf(buffer, "%" PRIu32, currentPulseCount);

    return mqttClient.publish("powermeter/" METER_NAME "/watthours_total", buffer, count);
  }
  return false;
}

void IRAM_ATTR meterPulseHigh(unsigned long pulseTime)
{
  lastHighTime = pulseTime;
}

void IRAM_ATTR meterPulseLow(unsigned long pulseTime)
{
  if (pulseTime > (lastLowTime + (minPulseLength * 2) && pulseTime > (lastHighTime + minPulseLength)))
  {
    pulseCount++;
  }

  lastLowTime = pulseTime;
}

void IRAM_ATTR meterPulseIsr()
{
  portENTER_CRITICAL(&mux);
  unsigned long pulseTime = currentTime;
  portEXIT_CRITICAL(&mux);
  digitalRead(meterPulsePin) ? meterPulseHigh(pulseTime) : meterPulseLow(pulseTime);
}

void IRAM_ATTR millisIsr()
{
  portENTER_CRITICAL(&mux);
  currentTime++;
  portEXIT_CRITICAL(&mux);
}