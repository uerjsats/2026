#include "telemetria.h"

TelemetryData data;

void printTelemetria()
{
    Serial.print("Latitude: ");
    Serial.println(data.latitude, 6);

    Serial.print("Longitude: ");
    Serial.println(data.longitude, 6);

    Serial.print("Satélites: ");
    Serial.println(data.sats);

    Serial.print("Accel X: ");
    Serial.println(data.accelX);

    Serial.print("Accel Y: ");
    Serial.println(data.accelY);

    Serial.print("Accel Z: ");
    Serial.println(data.accelZ);

    Serial.println();
}