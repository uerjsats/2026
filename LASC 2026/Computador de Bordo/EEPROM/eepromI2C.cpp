#include <Arduino.h>
#include "heltec.h"
#include <Wire.h>
#include <Preferences.h>
#include "SparkFun_External_EEPROM.h"

typedef struct {
  uint32_t contador;
  uint32_t timestamp;
  float lat;
  float lng;
} PacoteSat;

// =====================
// EEPROM I2C
// =====================
#define SDA_PIN 4
#define SCL_PIN 15

ExternalEEPROM minhaEEPROM;
uint32_t tamanhoEEPROM = 131072; // 131072 para 1Mbit, ou 32768 para 256Kbit
uint32_t head = 0;
uint32_t tail = 0;

Preferences memoriaFlash;

int calcularPendentes() {
  if (head >= tail) {
    return (head - tail) / sizeof(PacoteSat);
  } else {
    return ((tamanhoEEPROM - tail) + head) / sizeof(PacoteSat);
  }
}

void atualizarTela(String acao, PacoteSat item) {
  int pendentes = calcularPendentes();

  Heltec.display->clear();
  Heltec.display->setFont(ArialMT_Plain_10);

  Heltec.display->drawString(0, 0, "> " + acao);
  Heltec.display->drawString(0, 13, "Pkt: " + String(item.contador));
  Heltec.display->drawString(0, 26, "Pend: " + String(pendentes));

  Heltec.display->display();
}

void guardarNaEEPROM(PacoteSat item) {

  minhaEEPROM.put(head, item);

  head += sizeof(PacoteSat);
  if (head + sizeof(PacoteSat) > tamanhoEEPROM) {
    head = 0;
  }

  if (head == tail) {
    tail += sizeof(PacoteSat);
    if (tail + sizeof(PacoteSat) > tamanhoEEPROM) {
      tail = 0;
    }
    memoriaFlash.putUInt("tail", tail);
  }

  memoriaFlash.putUInt("head", head);
}

bool lerDaEEPROM(PacoteSat &item) {

  if (head == tail) {
    return false; 
  }

  minhaEEPROM.get(tail, item);

  tail += sizeof(PacoteSat);

  if (tail + sizeof(PacoteSat) > tamanhoEEPROM) {
    tail = 0;
  }

  memoriaFlash.putUInt("tail", tail);

  return true;
}

void setup() {
  Serial.begin(115200);
  delay(300);

  Heltec.begin(true, false, true, true, 915E6);

  Heltec.display->clear();
  Heltec.display->drawString(0, 0, "Iniciando...");
  Heltec.display->display();

  memoriaFlash.begin("pacoteSat", false);

  head = memoriaFlash.getUInt("head", 0);
  tail = memoriaFlash.getUInt("tail", 0);

  Wire.begin(SDA_PIN, SCL_PIN);

  if (!minhaEEPROM.begin()) {
    Serial.println("ERRO EEPROM!");
    while (1);
  }

  Serial.println("Sistema pronto!");
}

void loop() {

  static uint32_t contador = 0;

  PacoteSat item;
  item.contador = contador++;
  item.timestamp = millis();
  item.lat = -22.9068;   
  item.lng = -43.1729;

  guardarNaEEPROM(item);
  atualizarTela("Salvo", item);

  Serial.println("Salvou pacote: " + String(item.contador));

  delay(2000);

}