#include <Arduino.h>
#include "heltec.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define BAND 915E6

// 🔥 MAC DO DRONE
uint8_t macDrone[] = {0x68, 0x25, 0xDD, 0xC8, 0x0D, 0x1C};

String ultimaMsg = "";
bool aguardandoResposta = false;

String respostaLoRa = "";
bool enviarLoRa = false;

// ================= DISPLAY =================
void mostrarDisplay(String l1, String l2, String l3) {
  Heltec.display->clear();
  Heltec.display->drawString(0, 0, l1);
  Heltec.display->drawString(0, 15, l2);
  Heltec.display->drawString(0, 30, l3);
  Heltec.display->display();
}

// ================= RECEBE DO DRONE =================
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len) {

  char msg[250];
  if (len >= sizeof(msg)) len = sizeof(msg) - 1;

  memcpy(msg, incomingData, len);
  msg[len] = '\0';

  String resposta = String(msg);
  resposta.trim();

  Serial.println("📩 DRONE -> " + resposta);

  // 🔥 MOSTRA NO DISPLAY
  mostrarDisplay("DRONE RESP", resposta, "");

  // 🔥 APENAS SALVA (NÃO ENVIA AQUI)
  respostaLoRa = resposta;
  enviarLoRa = true;

  // 🔥 LIBERA FLUXO
  aguardandoResposta = false;
}

// ================= STATUS ENVIO =================
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("📤 ESP-NOW: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FALHA");
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(1500);

  Heltec.begin(true, false, true, true, BAND);

  // ===== LORA =====
  if (!LoRa.begin(BAND, true)) {
    Serial.println("❌ ERRO LORA");
    while (true);
  }

  LoRa.setSpreadingFactor(10);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setTxPower(14, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.enableCrc();
  LoRa.setSyncWord(0xF3);

  LoRa.receive(); // 🔥 já inicia em RX

  Serial.println("✅ LORA OK");

  // ===== WIFI =====
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.setSleep(false);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  Serial.print("MAC SATELITE: ");
  Serial.println(WiFi.macAddress());

  // ===== ESP-NOW =====
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW FAIL");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, macDrone, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❌ ERRO PEER");
    return;
  }

  Serial.println("🚀 SATELITE PRONTO");
  mostrarDisplay("SATELITE", "AGUARDANDO", "");
}

// ================= LOOP =================
void loop() {

  // ================= RECEBE DA BASE =================
  int packetSize = LoRa.parsePacket();

  if (packetSize && !aguardandoResposta) {

    String msg = "";

    while (LoRa.available()) {
      msg += (char)LoRa.read();
    }

    msg.trim();

    ultimaMsg = msg;

    Serial.println("📥 BASE -> " + msg);
    mostrarDisplay("LORA RX", msg, "sat");

    esp_err_t result = esp_now_send(
      macDrone,
      (uint8_t*)msg.c_str(),
      msg.length() + 1
    );

    if (result != ESP_OK) {
      Serial.println("❌ ERRO ENVIO ESP-NOW");
      aguardandoResposta = false;
      return;
    }

    aguardandoResposta = true;
  }

  // ================= ENVIO PARA BASE =================
  if (enviarLoRa) {

    enviarLoRa = false;

    LoRa.idle();
    delay(5);

    LoRa.beginPacket();
    LoRa.print(respostaLoRa);
    LoRa.endPacket(true);

    delay(5);
    LoRa.receive();

    Serial.println("📡 LORA -> " + respostaLoRa);
  }
}