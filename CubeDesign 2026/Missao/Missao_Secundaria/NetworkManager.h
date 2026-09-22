/**************************************************************************************************
 * MÓDULO: NetworkManager
 * OBJETIVO: Reaproveita a camada de conectividade WiFi original (Access Point) e o
 *           servidor TCP de envio de foto (igual ao VANTsat_TX_V3), e adiciona um
 *           broadcast UDP com o registro ADS-B fictício sempre que um triângulo é
 *           identificado. O broadcast já informa o índice da foto salva no SD para
 *           o computador de bordo puxá-la via TCP (GET:<index>).
 **************************************************************************************************/
#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <WiFi.h>
#include "ADSBSimulator.h"

extern const char* ssid;
extern const char* password;
extern const int adsbPort;
extern const int imagePort;

void setupWiFi();
void sendADSB(const ADSBRecord &record, int photoIndex, size_t photoSize);

// Atende pedidos "GET:<index>" de foto vindos do computador de bordo — chamar no loop()
void handleClient();

#endif
