#include <esp_now.h>
#include "espNow.h"
#include <WiFi.h>

esp_now_peer_info_t peerInfo;
uint8_t broadcastAddress[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
extern bool houveRecebimento;
extern String ultimaMensagemBruta;

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
    char buffer[len + 1];
    memcpy(buffer, incomingData, len);
    buffer[len] = '\0';
    
    ultimaMensagemBruta = String(buffer); 
    houveRecebimento = true;              
}


void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) 
{
    Serial.print("\r\n[ESP-NOW] Status: ");
    if (status == ESP_NOW_SEND_SUCCESS) {
        Serial.println("Sucesso");
    } else {
        Serial.println("Falha");
    }
}


void nowInit()
{
    WiFi.mode(WIFI_STA);
    if (esp_now_init() != ESP_OK) 
    {
        Serial.println("Erro ao inicializar ESP-NOW");
        return;
    }

    //Registra os callbacks
    esp_now_register_send_cb(OnDataSent);
    esp_now_register_recv_cb(OnDataRecv);
    
    //Peer
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, broadcastAddress, 6);
    peerInfo.channel = 0;  
    peerInfo.encrypt = false;
    esp_now_add_peer(&peerInfo);
}

void sendNow(String macStr, String dono ,String dados1, String dados2) 
{
    uint8_t macDestino[6];
    int v[6];
    
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
        for (int i = 0; i < 6; i++) macDestino[i] = (uint8_t)v[i];
    } else {
        Serial.println("Erro: MAC inválido");
        return;
    }

    if (!esp_now_is_peer_exist(macDestino)) {
        esp_now_peer_info_t tempPeer;
        memset(&tempPeer, 0, sizeof(tempPeer));
        memcpy(tempPeer.peer_addr, macDestino, 6);
        tempPeer.channel = 0; 
        tempPeer.encrypt = false;
        esp_now_add_peer(&tempPeer);
    }

    String payload = dono + ":" + dados1 + "," + dados2;
    esp_now_send(macDestino, (uint8_t *) payload.c_str(), payload.length());
}
