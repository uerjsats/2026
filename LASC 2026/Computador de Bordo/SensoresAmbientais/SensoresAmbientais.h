#ifndef SENSORESAMBIENTAIS_H
#define SENSORESAMBIENTAIS_H

#include <DHT.h>
#include <Adafruit_BME280.h>

class SensoresAmbientais {
public:
    //Construtor: pino e tipo do DHT, endereço I2C do BME (segui o padrão)
    SensoresAmbientais(int dhtPin, int dhtType, byte bmeAddress = 0x76);

    // initWire = false pois o main já chama Wire.begin() para o MPU6050
    bool init(bool initWire = false);

    
    //lê temperatura e umidade do DHT22 e retorna false se for inválida— valores ficam em NAN 
    bool lerDHT(float &temperatura, float &umidade);
    
    //lê pressão, altitude e temperatura do BME280 e retorna false se der errado
    bool lerBME(float &pressao, float &altitude, float &temperatura);

    //Retorna a pressão de referência calibrada
    float getRefPressure() const;

private:
    DHT _dht;
    Adafruit_BME280 _bme;
    byte _bmeAddress;
    float _refPressure;
};

#endif
