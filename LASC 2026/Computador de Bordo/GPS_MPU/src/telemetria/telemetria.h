#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <Arduino.h>

struct TelemetryData {
    uint32_t timestamp;

    float sats;
    float latitude;
    float longitude;

    float accelX;
    float accelY;
    float accelZ;
};

void printTelemetria();

extern TelemetryData data;

#endif