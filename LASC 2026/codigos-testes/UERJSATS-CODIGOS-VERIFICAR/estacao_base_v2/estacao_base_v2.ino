#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "HT_SSD1306Wire.h"

// ============================================
// --- CONFIGURAÇÕES LORA PARA HELTEC V3 ---
// ============================================
#define SS_PIN      8
#define RST_PIN     12
#define DIO1_PIN    14
#define BUSY_PIN    13

#define LORA_SCK    9
#define LORA_MISO   11
#define LORA_MOSI   10

#define RF_FREQUENCY           915.0
#define TX_OUTPUT_POWER        20
#define LORA_SPREADING_FACTOR  7
#define LORA_BANDWIDTH         125.0
#define LORA_CODINGRATE        5
#define LORA_PREAMBLE_LENGTH   8
#define LORA_SYNC_WORD         0x12

// Garantia para os pinos do OLED no Heltec V3
#ifndef SDA_OLED
#define SDA_OLED 17
#define SCL_OLED 18
#define RST_OLED 21
#define Vext     36
#endif

SPIClass spiLoRa(FSPI);
SPISettings spiSettings(2000000, MSBFIRST, SPI_MODE0);
SX1262 radio = new Module(SS_PIN, DIO1_PIN, RST_PIN, BUSY_PIN, spiLoRa, spiSettings);

#define MY_ADDRESS 42    // Estação Base
#define DEST_ADDRESS 43  // Satélite (OBC)
#define BUFFER_SIZE 300

struct __attribute__((packed)) TelemetryPacket {
  uint8_t src;
  uint8_t dst;
  uint16_t seconds;
  int16_t temp_dht_c_x100;
  uint16_t hum_dht_x100;
  uint16_t press_hpa_x10;
  int16_t alt_m_x10;
  uint8_t sats;
  int32_t lat_e6;
  int32_t lon_e6;
  int16_t ax_cms2_x100;
  int16_t ay_cms2_x100;
  int16_t az_cms2_x100;
  uint16_t volt_ina_x100;
  int16_t curr_ina_x10;
  uint16_t pow_ina_x10;
  uint8_t statusFlags;
};

#define FLAG_BME_OK  0x01
#define FLAG_MPU_OK  0x02
#define FLAG_GPS_OK  0x04
#define FLAG_DHT_OK  0x08
#define FLAG_SUP_OK  0x10
#define FLAG_CTL_OK  0x20

uint8_t rxBuffer[BUFFER_SIZE];
volatile bool packetReceived = false;

uint32_t pacoteRecebidoCount = 0;
int16_t lastRssi = 0;
float lastSnr = 0.0;

String inputString = "";
unsigned long lastDisplayUpdate = 0;
int displayPage = 0;  
String lastCommandSent = "";

SSD1306Wire myDisplay(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

void VextON(void) {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);
}

// CORREÇÃO: Interrupções no ESP32 precisam rodar na RAM (IRAM_ATTR)
void IRAM_ATTR setFlag(void) {
  packetReceived = true;
}

// ============================================
// --- FUNÇÕES DE DISPLAY ---
// ============================================
void displayTelemetry(TelemetryPacket* pkt) {
  myDisplay.clear();
  myDisplay.setFont(ArialMT_Plain_10);
  myDisplay.drawString(0, 0, "LASC 2026 v1.0");
  myDisplay.drawLine(0, 12, 128, 12);
  
  float temp = pkt->temp_dht_c_x100 / 100.0f;
  float alt = pkt->alt_m_x10 / 10.0f;
  float volt = pkt->volt_ina_x100 / 100.0f;
  
  myDisplay.drawString(0, 16, "T:" + String(pkt->seconds) + "s  Alt:" + String(alt, 1) + "m");
  myDisplay.drawString(0, 28, "Temp:" + String(temp, 1) + "C  V:" + String(volt, 1) + "V");
  
  char rssiStr[32];
  sprintf(rssiStr, "RSSI:%d dBm Pkts:%d", lastRssi, (int)pacoteRecebidoCount);
  myDisplay.drawString(0, 40, rssiStr);

  String comm = "SUP:";
  comm += (pkt->statusFlags & FLAG_SUP_OK) ? "OK" : "FALHA";
  comm += " CTL:";
  comm += (pkt->statusFlags & FLAG_CTL_OK) ? "OK" : "FALHA";
  myDisplay.drawString(0, 52, comm);

  myDisplay.display();
}

void displayCommandScreen() {
  myDisplay.clear();
  myDisplay.setFont(ArialMT_Plain_10);
  myDisplay.drawString(0, 0, "COMANDOS DISPONIVEIS");
  myDisplay.drawLine(0, 12, 128, 12);
  myDisplay.drawString(0, 16, "0-Parar/Reset  5-MotoresOFF");
  myDisplay.drawString(0, 28, "6-AbrirPainel  7-AbrirAnt");
  myDisplay.drawString(0, 40, "8-FecharAnt   10-Emergencia");
  myDisplay.drawString(0, 52, "11-TestePainel 13/14-Atuador");
  myDisplay.display();
}

// ============================================
// --- SETUP ---
// ============================================
void setup() {
  Serial.begin(115200);
  
  VextON();
  delay(100);

  Wire.begin(SDA_OLED, SCL_OLED);
  myDisplay.init();
  myDisplay.clear();
  myDisplay.setFont(ArialMT_Plain_10);
  myDisplay.drawString(0, 0, "LASC 2026");
  myDisplay.drawString(0, 16, "Estacao Base V3");
  myDisplay.drawString(0, 32, "Inicializando rádio...");
  myDisplay.display();

  spiLoRa.begin(LORA_SCK, LORA_MISO, LORA_MOSI, SS_PIN);

  Serial.print("[LORA] Inicializando modulo SX1262... ");
  
  // CORREÇÃO 1: TCXO configurado para 1.8V
  int state = radio.begin(
    RF_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
    LORA_CODINGRATE, LORA_SYNC_WORD, TX_OUTPUT_POWER, LORA_PREAMBLE_LENGTH, 1.8, false
  );

  if (state == RADIOLIB_ERR_NONE) {
    // CORREÇÃO 2: Habilitar o DIO2 como chave de antena
    radio.setDio2AsRfSwitch(true);
    Serial.println("OK!");
  } else {
    Serial.printf("FALHA (Cod: %d)\n", state);
    while (true);
  }

  radio.setDio1Action(setFlag);
  radio.startReceive();

  lastDisplayUpdate = millis();

  myDisplay.clear();
  myDisplay.drawString(0, 0, "CUBEDESIGN GS");
  myDisplay.drawString(0, 16, "Aguardando");
  myDisplay.drawString(0, 28, "telemetria...");
  myDisplay.drawString(0, 45, "Freq: 915 MHz");
  myDisplay.display();
  
  Serial.println("\n╔══════════════════════════════════════════════════════════╗");
  Serial.println("║            ESTACAO BASE LASC 2026 v1.0                   ║");
  Serial.println("║            Heltec WiFi LoRa 32 V3 (RadioLib)             ║");
  Serial.println("╚══════════════════════════════════════════════════════════╝\n");
  Serial.printf("📍 Endereco Local (GS): %d | Endereco Remoto (OBC): %d\n", MY_ADDRESS, DEST_ADDRESS);
  Serial.println("\n🔍 Digite o comando (ex: 6) e pressione ENTER para enviar.\n");
}

// ============================================
// --- LOOP ---
// ============================================
void loop() {
  // 1. Processamento de comandos digitados no Serial Monitor
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n' || inChar == '\r') {
      if (inputString.length() > 0) {
        inputString.trim();
        Serial.printf("\n📤 Preparando comando: '%s'\n", inputString.c_str());
        
        radio.standby();
        
        uint8_t txBuf[BUFFER_SIZE];
        txBuf[0] = MY_ADDRESS;   // 42
        txBuf[1] = DEST_ADDRESS; // 43
        memcpy(&txBuf[2], inputString.c_str(), inputString.length());
        
        int state = radio.transmit(txBuf, inputString.length() + 2);
        
        if (state == RADIOLIB_ERR_NONE) {
          lastCommandSent = inputString;
          Serial.println("✅ Comando enviado via LoRa com sucesso!");
          
          myDisplay.clear();
          myDisplay.drawString(0, 0, "COMANDO ENVIADO");
          myDisplay.drawLine(0, 12, 128, 12);
          myDisplay.drawString(0, 20, inputString);
          myDisplay.drawString(0, 40, "Aguardando satelite...");
          myDisplay.display();
        } else {
          Serial.printf("❌ Erro ao enviar comando (Cod: %d)\n", state);
        }
        
        inputString = "";
        radio.startReceive(); // Volta a escutar pacotes do satélite
      }
    } else {
      inputString += inChar;
    }
  }

  // 2. Processamento de pacotes recebidos via interrupção
  if (packetReceived) {
    packetReceived = false;
    
    int state = radio.readData(rxBuffer, BUFFER_SIZE);
    
    if (state == RADIOLIB_ERR_NONE) {
      size_t len = radio.getPacketLength();
      
      if (len == sizeof(TelemetryPacket)) {
        TelemetryPacket* pkt = (TelemetryPacket*)rxBuffer;
        
        if (pkt->src == DEST_ADDRESS && pkt->dst == MY_ADDRESS) {
          pacoteRecebidoCount++;
          lastRssi = radio.getRSSI();
          lastSnr = radio.getSNR();
          lastDisplayUpdate = millis();
          
          float temp_c = pkt->temp_dht_c_x100 / 100.0f;
          float hum_pct = pkt->hum_dht_x100 / 100.0f;
          float press_hpa = pkt->press_hpa_x10 / 10.0f;
          float alt_m = pkt->alt_m_x10 / 10.0f;
          float lat = pkt->lat_e6 / 1000000.0;
          float lon = pkt->lon_e6 / 1000000.0;
          float ax = pkt->ax_cms2_x100 / 100.0f;
          float ay = pkt->ay_cms2_x100 / 100.0f;
          float az = pkt->az_cms2_x100 / 100.0f;
          float volt = pkt->volt_ina_x100 / 100.0f;
          float curr = pkt->curr_ina_x10 / 10.0f;
          float pwr = pkt->pow_ina_x10 / 10.0f;

          Serial.println("\n╔══════════════════════════════════════════════════════════╗");
          Serial.println("║               TELEMETRIA RECEBIDA                        ║");
          Serial.println("╠══════════════════════════════════════════════════════════╣");
          Serial.printf("║ RSSI: %d dBm | SNR: %.1f dB | Pacote: #%d\n", lastRssi, lastSnr, pacoteRecebidoCount);
          Serial.println("╠══════════════════════════════════════════════════════════╣");
          Serial.printf("║ Tempo (s): %d\n", pkt->seconds);
          Serial.printf("║ Ambiente : %.2f °C | Umidade: %.2f %%\n", temp_c, hum_pct);
          Serial.printf("║ Atmosfera: %.1f hPa | Altitude: %.1f m\n", press_hpa, alt_m);
          Serial.printf("║ Energia  : %.2f V | %.1f mA | %.1f mW\n", volt, curr, pwr);
          Serial.printf("║ Inercial : X=%.2f  Y=%.2f  Z=%.2f m/s2\n", ax, ay, az);
          Serial.printf("║ GPS      : Sats=%d | Lat=%.6f | Lon=%.6f\n", pkt->sats, lat, lon);
          Serial.printf("║ Flags    : 0x%02X\n", pkt->statusFlags);
          Serial.printf("║ Sensores : BME=%s MPU=%s GPS=%s DHT=%s\n",
                        (pkt->statusFlags & FLAG_BME_OK) ? "OK" : "FALHA",
                        (pkt->statusFlags & FLAG_MPU_OK) ? "OK" : "FALHA",
                        (pkt->statusFlags & FLAG_GPS_OK) ? "OK" : "FALHA",
                        (pkt->statusFlags & FLAG_DHT_OK) ? "OK" : "FALHA");
          Serial.printf("║ Escravas : SUP=%s CTL=%s\n",
                        (pkt->statusFlags & FLAG_SUP_OK) ? "OK" : "FALHA",
                        (pkt->statusFlags & FLAG_CTL_OK) ? "OK" : "FALHA");
          Serial.println("╚══════════════════════════════════════════════════════════╝\n");

          displayTelemetry(pkt);
        }
      }
    }
    radio.startReceive();
  }

  // 3. Gerenciamento de Telas do OLED (a cada 5 segundos)
  if (millis() - lastDisplayUpdate > 5000) {
    lastDisplayUpdate = millis();
    displayPage = (displayPage + 1) % 2;
    
    if (displayPage == 0) {
      myDisplay.clear();
      myDisplay.drawString(0, 0, "CUBEDESIGN GS");
      myDisplay.drawString(0, 20, "Aguardando");
      myDisplay.drawString(0, 32, "telemetria...");
      char pktsStr[20];
      sprintf(pktsStr, "Pkts: %d", (int)pacoteRecebidoCount);
      myDisplay.drawString(0, 48, pktsStr);
      myDisplay.display();
    } else {
      displayCommandScreen();
    }
  }
}