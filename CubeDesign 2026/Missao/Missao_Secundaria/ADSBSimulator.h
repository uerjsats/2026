/**************************************************************************************************
 * MÓDULO: ADSBSimulator
 * OBJETIVO: Manter uma base fixa de 5 registros ADS-B fictícios e sortear um
 *           deles sempre que um triângulo for identificado pelo VisionSystem.
 **************************************************************************************************/
#ifndef ADSB_SIMULATOR_H
#define ADSB_SIMULATOR_H

#include <Arduino.h>

struct ADSBRecord {
    const char* icao24;
    const char* callsign;
    float latitude;
    float longitude;
    int altitudeFt;
    int groundSpeedKt;
    int trackDeg;
    int verticalRateFpm;
    const char* squawk;
};

extern ADSBRecord adsbDatabase[5];

// Último registro ADS-B sorteado/enviado, exposto para o dashboard web
extern ADSBRecord lastADSBRecord;
extern bool adsbDataAvailable;

// Sorteia um dos 5 registros fictícios
ADSBRecord getRandomADSB();

// Serializa um registro em JSON para envio via WiFi, incluindo referência à foto salva no SD
String buildADSBPayload(const ADSBRecord &record, int photoIndex, size_t photoSize);

#endif
