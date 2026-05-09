#include "gps.h"
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

//Atualiza variáveis para o Main usar
void gpsUpdate(float &latitude, float &longitude, int &sats) 
{
    //Verifica se há dados disponíveis
    while (GPSSerial.available()) {
        char c = GPSSerial.read();
        gps.encode(c);
    }
    latitude = gps.location.lat();
    longitude = gps.location.lng();
    sats = gps.satellites.value();
}