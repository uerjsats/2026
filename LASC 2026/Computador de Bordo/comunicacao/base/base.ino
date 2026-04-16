#include <Arduino.h>
#include "heltec.h"

#define BAND 915E6

unsigned long ultimoEnvio = 0;
unsigned long timeout = 2000;

bool aguardandoResposta = false;
int tentativas = 0;
const int maxTentativas = 3;

String msgAtual = "1";

void mostrarDisplay(String t, String l1, String l2) {
  Heltec.display->clear();
  Heltec.display->drawString(0, 0, t);
  Heltec.display->drawString(0, 15, l1);
  Heltec.display->drawString(0, 30, l2);
  Heltec.display->display();
}

void enviarLoRa() {
  Serial.println("📤 TX -> " + msgAtual);

  LoRa.idle();
  delay(5);

  LoRa.beginPacket();
  LoRa.print(msgAtual);
  LoRa.endPacket(true);

  LoRa.receive();

  ultimoEnvio = millis();
  aguardandoResposta = true;
  tentativas++;
}

void setup() {
  Serial.begin(115200);
  delay(1500);

  Heltec.begin(true, true, true, true, BAND);

  LoRa.begin(BAND, true);
  LoRa.setSpreadingFactor(10);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.enableCrc();
  LoRa.setTxPower(14, PA_OUTPUT_PA_BOOST_PIN);
  LoRa.setSyncWord(0xF3);

  LoRa.receive();

  Serial.println("🚀 BASE PROFISSIONAL PRONTA");
}

void loop() {

  // 🔥 PRIMEIRO ENVIO
  if (!aguardandoResposta) {
    tentativas = 0;
    enviarLoRa();
  }

  // 🔥 TIMEOUT + RETRY
  if (aguardandoResposta && millis() - ultimoEnvio > timeout) {

    if (tentativas < maxTentativas) {
      Serial.println("🔁 RETRY...");
      enviarLoRa();
    } else {
      Serial.println("❌ SEM RESPOSTA");
      aguardandoResposta = false;
    }
  }

  // 🔥 RECEPÇÃO
  int packetSize = LoRa.parsePacket();

  if (packetSize) {

    String msg = "";

    while (LoRa.available()) {
      msg += (char)LoRa.read();
    }

    msg.trim();

    Serial.println("📩 RX <- " + msg);
    mostrarDisplay("RX", msg, "");

    aguardandoResposta = false;
  }
}