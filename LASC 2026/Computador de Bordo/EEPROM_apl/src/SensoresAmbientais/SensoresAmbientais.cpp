#include "SensoresAmbientais.h"
#include <math.h>

// ======================== CONSTRUTOR ========================
SensoresAmbientais::SensoresAmbientais(int dhtPin, int dhtType, byte bmeAddress)
    : _dht(dhtPin, dhtType), _bmeAddress(bmeAddress), _refPressure(NAN) {}


// ======================== INIT ========================
bool SensoresAmbientais::init(bool initWire) {

    if (initWire) {
        Wire.begin();
    }

    _dht.begin();

    bool bmeOk = _bme.begin(_bmeAddress);

    if (bmeOk) {
        float p = _bme.readPressure() / 100.0F;

        if (!isnan(p) && p > 0.0F) {
            _refPressure = p;
        }
    }

    return bmeOk;
}


// ======================== DHT ========================
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


// ======================== BME ========================
bool SensoresAmbientais::lerBME(float &pressao, float &altitude) {

    pressao     = _bme.readPressure() / 100.0F;
    if (isnan(pressao)) {
        pressao     = NAN;
        altitude    = NAN;
        return false;
    }

    altitude = isnan(_refPressure) ? NAN : _bme.readAltitude(_refPressure);

    return true;
}


// ======================== GET REF ========================
float SensoresAmbientais::getRefPressure() const {
    return _refPressure;
}