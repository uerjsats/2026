#include <Arduino.h>
#include "heltec.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

// 🔥 MAC DO SATÉLITE 
uint8_t macSatelite[] = {0x68, 0x25, 0xDD, 0xC8, 0x0F, 0xA4};

// ================= DISPLAY =================
void mostrarDisplay(String l1, String l2, String l3) {
  Heltec.display->clear();
  Heltec.display->drawString(0, 0, l1);
  Heltec.display->drawString(0, 15, l2);
  Heltec.display->drawString(0, 30, l3);
  Heltec.display->display();
}

// ================= RECEBE =================
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {

  Serial.println("🔥 RECEBEU ALGO!");

  char msg[250];
  if (len >= sizeof(msg)) len = sizeof(msg) - 1;

  memcpy(msg, incomingData, len);
  msg[len] = '\0';

  String mensagem = String(msg);
  mensagem.trim();

  Serial.println("📩 MSG: " + mensagem);


  mostrarDisplay("DRONE RX", "MSG:", mensagem);

  // 🔥 RESPONDE
  delay(30);

  String resposta;

  if (mensagem == "1") {
    resposta = "HELLO";
  } else {
    resposta = "OK";
  }

  esp_err_t result = esp_now_send(
    macSatelite,
    (uint8_t*)resposta.c_str(),
    resposta.length() + 1
  );

  if (result != ESP_OK) {
    Serial.println("❌ ERRO AO ENVIAR RESPOSTA");
  }
}

// ================= STATUS ENVIO =================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("📤 ENVIO: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FALHA");
}

// ================= SETUP =================
void setup() {

  Serial.begin(115200);
  delay(1500);

  Heltec.begin(true, false, true, true, 915E6);

  mostrarDisplay("DRONE", "INICIANDO", "");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);

  // 🔥 TEM QUE SER IGUAL AO SATÉLITE
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  Serial.print("MAC DRONE: ");
  Serial.println(WiFi.macAddress());

  Serial.print("Canal WiFi DRONE: ");
  Serial.println(WiFi.channel());

  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW ERRO");
    mostrarDisplay("ERRO", "ESP-NOW", "");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  esp_now_register_send_cb(OnDataSent);

  // ADICIONA SATÉLITE
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, macSatelite, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❌ ERRO PEER");
    mostrarDisplay("ERRO", "PEER", "");
    return;
  }

  Serial.println("🚀 DRONE PRONTO");
  mostrarDisplay("DRONE", "AGUARDANDO", "");
}

// ================= LOOP =================
void loop() {
  // tudo via callback
}