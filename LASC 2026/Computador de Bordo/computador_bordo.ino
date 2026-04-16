//computador_bordo.ino faz a integração da biblioteca SensoresAmbientais com o código principal

#include "SensoresAmbientais/SensoresAmbientais.h"

#define DHTPIN  48
#define DHTTYPE DHT22

// initWire =false — Wire.begin() já é chamado abaixo para o MPU6050
SensoresAmbientais sensores(DHTPIN, DHTTYPE);

void setup() {
    Serial.begin(9600);

    Wire.begin(); //centralizado aqui para todos os dispositivos I2C (BME + MPU6050)
    if (!sensores.init(false)) {
        Serial.println("[AVISO] BME280 nao inicializado. Pressao/altitude indisponiveis.");
    }

}

void loop() {   
    sensorsData dados; //leitura dos sensores ambientais

    float tempDHT, umidDHT;
    if (sensores.lerDHT(tempDHT, umidDHT)) {
        dados.temperatureDHT = tempDHT;
        dados.humidityDHT    = umidDHT;
    } else {
        dados.temperatureDHT = 0.0F;  
        dados.humidityDHT    = 0.0F;
        Serial.println("AVISO! Leitura DHT invalida.");
    }

    float pressao, altitude, tempBME;
    if (sensores.lerBME(pressao, altitude, tempBME)) {
        dados.pressure      = pressao;
        dados.altitude      = altitude;
        // dados.temperatureBME = tempBME;  //descomentar quando adicionar ao struct
    } else {
        dados.pressure = 0.0F;
        dados.altitude = 0.0F;
        Serial.println("[AVISO] Leitura BME invalida.");
    }

}
