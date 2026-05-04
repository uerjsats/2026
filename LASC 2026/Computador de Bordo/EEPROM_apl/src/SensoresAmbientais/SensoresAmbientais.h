#ifndef SENSORES_AMBIENTAIS_H
#define SENSORES_AMBIENTAIS_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <DHT.h>

class SensoresAmbientais {
public:
    // Construtor
    SensoresAmbientais(int dhtPin, int dhtType, byte bmeAddress);

    // Inicialização
    bool init(bool initWire = false);

    // Leituras
    bool lerDHT(float &temperatura, float &umidade);
    bool lerBME(float &pressao, float &altitude);

    // Debug / referência
    float getRefPressure() const;

private:
    DHT _dht;
    Adafruit_BME280 _bme;

    byte _bmeAddress;
    float _refPressure;
};

#endif