#include <Arduino.h>
#include "state.h"

volatile DRAM_ATTR struct meterState isrMeterStates[METER_COUNT] = {};