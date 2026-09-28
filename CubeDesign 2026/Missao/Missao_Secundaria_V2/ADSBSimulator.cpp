/**************************************************************************************************
 * MÓDULO: ADSBSimulator
 * OBJETIVO: Manter uma base fixa de 5 registros ADS-B fictícios e sortear um
 *           deles sempre que um triângulo for identificado pelo VisionSystem.
 **************************************************************************************************/
#include "ADSBSimulator.h"
#include "esp_system.h"

// Base fixa de 5 aeronaves fictícias
ADSBRecord adsbDatabase[5] = {
    {"A1B2C3", "TAM3251", -22.9068f, -43.1729f, 35000, 450, 90,    0, "2200"},
    {"D4E5F6", "GLO1054", -22.8100f, -43.2400f, 28000, 400, 270, -500, "3421"},
    {"11AACC", "AZU4402", -22.9500f, -43.1800f, 41000, 470,  45,    0, "7000"},
    {"7788FF", "TAP0803", -22.8800f, -43.3000f, 38000, 460, 180,  600, "5511"},
    {"33CC99", "UAL1890", -23.0000f, -43.1000f, 32000, 430, 315, -300, "1200"}
};

ADSBRecord lastADSBRecord = adsbDatabase[0];
bool adsbDataAvailable = false;

ADSBRecord getRandomADSB() {
    // esp_random() é o gerador de hardware do ESP32, dispensa randomSeed()
    int index = esp_random() % 5;
    return adsbDatabase[index];
}

String buildADSBPayload(const ADSBRecord &record, int photoIndex, size_t photoSize) {
    String json = "{";
    json += "\"icao24\":\"" + String(record.icao24) + "\",";
    json += "\"callsign\":\"" + String(record.callsign) + "\",";
    json += "\"lat\":" + String(record.latitude, 4) + ",";
    json += "\"lon\":" + String(record.longitude, 4) + ",";
    json += "\"alt_ft\":" + String(record.altitudeFt) + ",";
    json += "\"gs_kt\":" + String(record.groundSpeedKt) + ",";
    json += "\"track_deg\":" + String(record.trackDeg) + ",";
    json += "\"vrate_fpm\":" + String(record.verticalRateFpm) + ",";
    json += "\"squawk\":\"" + String(record.squawk) + "\",";
    json += "\"photo_index\":" + String(photoIndex) + ",";
    json += "\"photo_size\":" + String((unsigned long)photoSize);
    json += "}";
    return json;
}
