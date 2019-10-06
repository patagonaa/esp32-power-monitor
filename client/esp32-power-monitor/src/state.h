#include <Arduino.h>
#include <stdint.h>
#include "config.h"

typedef uint64_t time_ms_t;
typedef uint32_t pulse_t;

struct meterState
{
    pulse_t totalPulseCount;
    pulse_t unhandledPulseCount;
    time_ms_t lastActiveTime; // when did the pin last go high (if not inverted)
    time_ms_t lastPulseTime; // when was the last pulse counted
    bool lastState; // was the pin last active/inactive (high/low if not inverted)
};

extern volatile DRAM_ATTR struct meterState isrMeterStates[METER_COUNT];