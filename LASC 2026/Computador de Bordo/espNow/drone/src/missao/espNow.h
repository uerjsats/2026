#ifndef ESPNOW_H
#define ESPNOW_H

#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>

// Definições Universais
extern esp_now_peer_info_t peerInfo;
extern uint8_t broadcastAddress[6]; 

void nowInit();
void sendNow(String macStr, String dono ,String dados1, String dados2);
void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len);
void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status);

#endif
