#include <SPI.h>
#include <LoRa.h>

// ============================================
// --- CONFIGURAÇÕES LORA ---
// ============================================
#define MY_ADDRESS 42
#define DEST_ADDRESS 43

#define SS_PIN      18    // CS (Chip Select)
#define RST_PIN     14    // Reset
#define DIO0_PIN    26    // DIO0

#define RF_FREQUENCY        915E6   // 915 MHz
#define TX_OUTPUT_POWER     14      // dBm
#define LORA_SPREADING_FACTOR 7     // SF7
#define LORA_BANDWIDTH      125E3   // 125 kHz
#define LORA_CODINGRATE     5       // 4/5
#define LORA_PREAMBLE_LENGTH 8      // <-- ADICIONADO

#define BUFFER_SIZE         300  

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

int16_t rssi, rxSize;
uint32_t pacoteRecebidoCount = 0;
uint32_t lastPacketTime = 0;
String inputString = "";

// ---------- Variáveis para parsing dos dados ----------
struct TelemetryData {
  unsigned long seconds;
  float tempDHT;
  float humDHT;
  float altitude;
  float pressure;
  int satellites;
  double latitude;
  double longitude;
  float accelX;
  float accelY;
  float accelZ;
  
  String supplyData;
  String controlData;
  String missionData;
  
  bool hasSupplyData;
  bool hasControlData;
  bool hasMissionData;
};

TelemetryData telemetry;

// ---------- Função para parsear os dados recebidos ----------
void parseTelemetryData(String data) {
  telemetry.hasSupplyData = false;
  telemetry.hasControlData = false;
  telemetry.hasMissionData = false;
  
  int pos = 0;
  int nextPos;
  
  // seconds
  nextPos = data.indexOf(':', pos);
  if (nextPos != -1) {
    telemetry.seconds = data.substring(pos, nextPos).toInt();
    pos = nextPos + 1;
  }
  
  // tempDHT
  nextPos = data.indexOf(':', pos);
  if (nextPos != -1) {
    telemetry.tempDHT = data.substring(pos, nextPos).toFloat();
    pos = nextPos + 1;
  }
  
  // humDHT
  nextPos = data.indexOf(':', pos);
  if (nextPos != -1) {
    telemetry.humDHT = data.substring(pos, nextPos).toFloat();
    pos = nextPos + 1;
  }
  
  // altitude
  nextPos = data.indexOf(':', pos);
  if (nextPos != -1) {
    telemetry.altitude = data.substring(pos, nextPos).toFloat();
    pos = nextPos + 1;
  }
  
  // pressure
  nextPos = data.indexOf(':', pos);
  if (nextPos != -1) {
    telemetry.pressure = data.substring(pos, nextPos).toFloat();
    pos = nextPos + 1;
  }
  
  // satellites
  nextPos = data.indexOf(':', pos);
  if (nextPos != -1) {
    telemetry.satellites = data.substring(pos, nextPos).toInt();
    pos = nextPos + 1;
  }
  
  // latitude
  nextPos = data.indexOf(':', pos);
  if (nextPos != -1) {
    telemetry.latitude = data.substring(pos, nextPos).toFloat();
    pos = nextPos + 1;
  }
  
  // longitude
  nextPos = data.indexOf(':', pos);
  if (nextPos != -1) {
    telemetry.longitude = data.substring(pos, nextPos).toFloat();
    pos = nextPos + 1;
  }
  
  // accelX
  nextPos = data.indexOf(':', pos);
  if (nextPos != -1) {
    telemetry.accelX = data.substring(pos, nextPos).toFloat();
    pos = nextPos + 1;
  }
  
  // accelY
  nextPos = data.indexOf(':', pos);
  if (nextPos != -1) {
    telemetry.accelY = data.substring(pos, nextPos).toFloat();
    pos = nextPos + 1;
  }
  
  // accelZ
  nextPos = data.indexOf(':', pos);
  if (nextPos != -1) {
    telemetry.accelZ = data.substring(pos, nextPos).toFloat();
    pos = nextPos + 1;
    
    if (pos < data.length()) {
      String remaining = data.substring(pos);
      int remColons = 0;
      for (unsigned int i = 0; i < remaining.length(); i++) {
        if (remaining.charAt(i) == ':') remColons++;
      }
      
      if (remColons >= 2) {
        int p1 = remaining.indexOf(':');
        int p2 = remaining.indexOf(':', p1 + 1);
        
        if (p1 > 0) {
          telemetry.supplyData = remaining.substring(0, p1);
          telemetry.hasSupplyData = (telemetry.supplyData.length() > 1 && telemetry.supplyData != " ");
        }
        
        if (p2 > p1) {
          telemetry.controlData = remaining.substring(p1 + 1, p2);
          telemetry.hasControlData = (telemetry.controlData.length() > 1 && telemetry.controlData != " ");
          
          telemetry.missionData = remaining.substring(p2 + 1);
          telemetry.hasMissionData = (telemetry.missionData.length() > 1 && telemetry.missionData != " ");
        }
      }
    }
  }
}

// ---------- Função para exibir telemetria ----------
void printTelemetryToSerial() {
  Serial.println("\n╔══════════════════════════════════════════════════════════╗");
  Serial.println("║              TELEMETRIA CUBEDESIGN - PACOTE              ║");
  Serial.println("╠══════════════════════════════════════════════════════════╣");
  
  char buffer[80];
  sprintf(buffer, "║ Tempo: %6lu s  │  Altitude: %7.1f m                ║", 
          telemetry.seconds, telemetry.altitude);
  Serial.println(buffer);
  
  sprintf(buffer, "║ Temp:  %6.1f C  │  Umidade:  %6.1f%%                ║", 
          telemetry.tempDHT, telemetry.humDHT);
  Serial.println(buffer);
  
  sprintf(buffer, "║ Press: %6.1f hPa│                                   ║", 
          telemetry.pressure);
  Serial.println(buffer);
  
  Serial.println("╠══════════════════════════════════════════════════════════╣");
  
  if (telemetry.satellites > 0) {
    sprintf(buffer, "║ GPS: %d satelites                                     ║", telemetry.satellites);
    Serial.println(buffer);
    sprintf(buffer, "║ Lat: %12.6f                                  ║", telemetry.latitude);
    Serial.println(buffer);
    sprintf(buffer, "║ Lng: %12.6f                                  ║", telemetry.longitude);
    Serial.println(buffer);
  } else {
    Serial.println("║ GPS: Aguardando fixacao                               ║");
  }
  
  Serial.println("╠══════════════════════════════════════════════════════════╣");
  sprintf(buffer, "║ Acel X: %7.2f │ Acel Y: %7.2f │ Acel Z: %7.2f ║", 
          telemetry.accelX, telemetry.accelY, telemetry.accelZ);
  Serial.println(buffer);
  
  if (telemetry.hasSupplyData || telemetry.hasControlData || telemetry.hasMissionData) {
    Serial.println("╠══════════════════════════════════════════════════════════╣");
    
    if (telemetry.hasSupplyData) {
      Serial.print("║ Suprimento: ");
      Serial.print(telemetry.supplyData);
      for (int i = telemetry.supplyData.length(); i < 42; i++) Serial.print(" ");
      Serial.println("║");
    }
    if (telemetry.hasControlData) {
      Serial.print("║ Controle:   ");
      Serial.print(telemetry.controlData);
      for (int i = telemetry.controlData.length(); i < 42; i++) Serial.print(" ");
      Serial.println("║");
    }
    if (telemetry.hasMissionData) {
      Serial.print("║ Missao:     ");
      Serial.print(telemetry.missionData);
      for (int i = telemetry.missionData.length(); i < 42; i++) Serial.print(" ");
      Serial.println("║");
    }
  }
  
  Serial.println("╠══════════════════════════════════════════════════════════╣");
  sprintf(buffer, "║ RSSI: %4d dBm │ Tamanho: %3d bytes │ Pacote: #%-6lu║", 
          rssi, rxSize, pacoteRecebidoCount);
  Serial.println(buffer);
  Serial.println("╚══════════════════════════════════════════════════════════╝\n");
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n╔══════════════════════════════════════════════════════════╗");
  Serial.println("║           ESTACAO BASE CUBEDESIGN v1.0                   ║");
  Serial.println("║              Heltec WiFi LoRa 32 V2                      ║");
  Serial.println("║              Usando LoRa.h                               ║");
  Serial.println("╚══════════════════════════════════════════════════════════╝\n");
  
  // Configura os pinos do LoRa
  LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);
  
  // Inicializa o rádio LoRa
  Serial.print("Inicializando LoRa... ");
  if (!LoRa.begin(RF_FREQUENCY)) {
    Serial.println("Falha!");
    while (1) delay(1000);
  }
  Serial.println("OK!");
  
  // Configura parâmetros LoRa
  LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
  LoRa.setSignalBandwidth(LORA_BANDWIDTH);
  LoRa.setCodingRate4(LORA_CODINGRATE);
  LoRa.setPreambleLength(LORA_PREAMBLE_LENGTH);
  LoRa.setTxPower(TX_OUTPUT_POWER);
  
  Serial.println("\n✅ Sistema inicializado com sucesso!");
  Serial.println("┌──────────────────────────────────────────────────────────┐");
  Serial.print("│ • Endereco Local:  "); Serial.print(MY_ADDRESS); Serial.println("                                     │");
  Serial.print("│ • Endereco Remoto: "); Serial.print(DEST_ADDRESS); Serial.println("                                     │");
  Serial.println("│ • Frequencia:      915 MHz                               │");
  Serial.print("│ • SF:              "); Serial.print(LORA_SPREADING_FACTOR); Serial.println("                                     │");
  Serial.println("└──────────────────────────────────────────────────────────┘\n");
  
  Serial.println("🔍 Aguardando telemetria...\n");
  Serial.println("💡 Digite o comando e pressione ENTER para enviar");
  Serial.println("   Comandos: 1-14, DATA_S, DATA_C, DATA_M, DATA_ALL\n");
}

// ---------- LOOP ----------
void loop() {
  // Envio de comandos via Serial
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n') {
      inputString.trim();
      if (inputString.length() > 0 && inputString.length() < BUFFER_SIZE - 2) {
        Serial.print("\n📤 Enviando comando: ");
        Serial.println(inputString);
        
        memset(txpacket, 0, BUFFER_SIZE);
        txpacket[0] = MY_ADDRESS;
        txpacket[1] = DEST_ADDRESS;
        inputString.toCharArray(&txpacket[2], BUFFER_SIZE - 2);
        
        // Envia o pacote
        LoRa.beginPacket();
        LoRa.write((uint8_t*)txpacket, inputString.length() + 2);
        LoRa.endPacket();
        
        Serial.println("✅ Comando enviado com sucesso!\n");
      }
      inputString = "";
    } else {
      inputString += inChar;
    }
  }
  
  // Verifica recepção
  int packetSize = LoRa.parsePacket();
  if (packetSize > 0) {
    rxSize = packetSize;
    rssi = LoRa.packetRssi();
    lastPacketTime = millis();
    
    // Lê o pacote
    int i = 0;
    while (LoRa.available() && i < BUFFER_SIZE - 1) {
      rxpacket[i++] = (char)LoRa.read();
    }
    rxpacket[i] = '\0';
    
    if (rxSize >= 2) {
      uint8_t sender = rxpacket[0];
      uint8_t receiver = rxpacket[1];
      
      if (receiver == MY_ADDRESS && sender == DEST_ADDRESS) {
        String rawMessage = String(&rxpacket[2]);
        pacoteRecebidoCount++;
        
        parseTelemetryData(rawMessage);
        printTelemetryToSerial();
        
        // Pisca LED
        pinMode(LED_BUILTIN, OUTPUT);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(20);
        digitalWrite(LED_BUILTIN, LOW);
      }
    }
  }
}