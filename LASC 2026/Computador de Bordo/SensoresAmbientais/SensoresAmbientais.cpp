#include "SensoresAmbientais.h"
#include <Wire.h>
#include <math.h>

// _refPressure começa como NAN — só é definida após leitura real do BME no init()
// evita usar 1013.25 hPa (nível do mar) como fallback incorreto

SensoresAmbientais::SensoresAmbientais(int dhtPin, int dhtType, byte bmeAddress)
    : _dht(dhtPin, dhtType), _bmeAddress(bmeAddress), _refPressure(NAN) {}

//initWire = false por padrão: o main já faz Wire.begin() para o MPU6050
//só passa initWire = true se este for o único dispositivo I2C no projeto

bool SensoresAmbientais::init(bool initWire) {
    if (initWire) {
        Wire.begin();
    }

    _dht.begin();

    bool bmeOk = _bme.begin(_bmeAddress);

    if (bmeOk) {
        float p = _bme.readPressure() / 100.0F;
        // Calibra a referência só se o valor for plausível
        if (!isnan(p) && p > 0.0F) {
            _refPressure = p;
        }
    }
    //não trava o sistema se BME falhar e retorna false para o main decidir
    return bmeOk;
}

bool SensoresAmbientais::lerDHT(float &temperatura, float &umidade) {
    temperatura = _dht.readTemperature();
    umidade     = _dht.readHumidity();

    if (isnan(temperatura) || isnan(umidade)) {
        temperatura = NAN;
        umidade     = NAN;
        return false;
    }
    return true;
}

// lerBME() lê pressão, altitude e temperatura do BME280.
// Altitude só é calculada se _refPressure foi calibrada no init()
//se não foi (BME falhou no init), altitude fica NAN

bool SensoresAmbientais::lerBME(float &pressao, float &altitude, float &temperatura) {
    pressao     = _bme.readPressure() / 100.0F;
    temperatura = _bme.readTemperature();

    if (isnan(pressao) || isnan(temperatura)) {
        pressao     = NAN;
        altitude    = NAN;
        temperatura = NAN;
        return false;
    }

    //Só calcula altitude se tiver referência válida
    altitude = isnan(_refPressure) ? NAN : _bme.readAltitude(_refPressure);

    return true;
}

// getRefPressure() é útil para debug e para o pacote LoRa incluir a referência de pressão

float SensoresAmbientais::getRefPressure() const {
    return _refPressure;
}
