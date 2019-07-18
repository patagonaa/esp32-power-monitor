#include <Arduino.h>
#include <EEPROM.h> 

const int meterPulsePin = 0;
const int minPulseLength = 10; // ms

void meterPulse();
void writePulseCount(uint32_t currentPulseCount);

volatile DRAM_ATTR uint32_t pulseCount = 0;
volatile DRAM_ATTR unsigned long lastHighTime = 0;
volatile DRAM_ATTR unsigned long lastLowTime = 0;
volatile DRAM_ATTR unsigned long currentTime = 0;

uint32_t lastPulseCount;
void setup() {
  EEPROM.begin(4);
  //writePulseCount(104);
  Serial.begin(9600);

  pinMode(meterPulsePin, INPUT_PULLUP);
  attachInterrupt(meterPulsePin, meterPulse, CHANGE);
  delay(100);
  pulseCount = (EEPROM.read(0) << 24) | (EEPROM.read(1) << 16) | (EEPROM.read(2) << 8) | EEPROM.read(3);
  lastPulseCount = pulseCount;
  Serial.println(pulseCount);
}


void loop() {
  currentTime = millis();
  uint32_t currentPulseCount = pulseCount;
  if(currentPulseCount != lastPulseCount){
    writePulseCount(pulseCount);

    Serial.println(currentPulseCount);
    lastPulseCount = currentPulseCount;
  }
  delay(100);
}

void writePulseCount(uint32_t currentPulseCount){
  EEPROM.write(0, (uint8_t)(currentPulseCount >> 24 & 0xFF));
  EEPROM.write(1, (uint8_t)(currentPulseCount >> 16 & 0xFF));
  EEPROM.write(2, (uint8_t)(currentPulseCount >> 8 & 0xFF));
  EEPROM.write(3, (uint8_t)(currentPulseCount & 0xFF));
  EEPROM.commit();
}

void IRAM_ATTR meterPulseHigh(){
  unsigned long pulseTime = currentTime;
  lastHighTime = pulseTime;
}

void IRAM_ATTR meterPulseLow(){
  unsigned long pulseTime = currentTime;
  if(pulseTime > (lastLowTime + (minPulseLength * 2) && pulseTime > (lastHighTime + minPulseLength))){
    pulseCount++;
  }

  lastLowTime = pulseTime;
}

void IRAM_ATTR meterPulse(){
  digitalRead(meterPulsePin) ? meterPulseHigh() : meterPulseLow();
}