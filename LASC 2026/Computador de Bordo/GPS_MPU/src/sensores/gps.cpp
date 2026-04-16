#include "gps.h"
#include "../telemetria/telemetria.h"
#include <HardwareSerial.h>

TinyGPSPlus gps;
HardwareSerial GPSSerial(1);
#define GPS_RX 45
#define GPS_TX 46
//Inicializa Sensor
void gpsInit() 
{
    GPSSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
}

void gpsUpdate() 
{
    //Verifica se há dados disponíveis
    while (GPSSerial.available()) {
        char c = GPSSerial.read();
        gps.encode(c);
    }
    data.latitude = gps.location.lat();
    data.longitude = gps.location.lng();
    data.sats = gps.satellites.value();
}