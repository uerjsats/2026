#include "LoRaWan_APP.h"
#include "HT_SSD1306Wire.h"

// ---------- Configurações ----------
#define MY_ADDRESS 42
#define DEST_ADDRESS 43

#define RF_FREQUENCY                915000000 // Hz
#define TX_OUTPUT_POWER             14        // dBm
#define LORA_BANDWIDTH              0         // 125 kHz
#define LORA_SPREADING_FACTOR       7         // [SF7..SF12]
#define LORA_CODINGRATE             1         // 4/5
#define LORA_PREAMBLE_LENGTH        8
#define LORA_SYMBOL_TIMEOUT         0
#define LORA_FIX_LENGTH_PAYLOAD_ON  false
#define LORA_IQ_INVERSION_ON        false

#define RX_TIMEOUT_VALUE            1000
#define BUFFER_SIZE                 300  

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

static RadioEvents_t RadioEvents;

int16_t txNumber;
int16_t rssi, rxSize;
uint32_t pacoteRecebidoCount = 0;
bool lora_idle = true;
String inputString = "";
unsigned long lastDisplayUpdate = 0;
int displayPage = 0;  // 0 = dados, 1 = comandos enviados
String lastCommandSent = "";
int comandosEnviados = 0;

// ---------- OLED ----------
SSD1306Wire myDisplay(0x3c, 500000, SDA_OLED, SCL_OLED, GEOMETRY_128_64, RST_OLED);

// ---------- Controle da alimentação do OLED ----------
void VextON(void) {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW); // Liga o Vext
}

void VextOFF(void) {
  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, HIGH); // Desliga o Vext
}

// ---------- Função para exibir telemetria resumida no OLED ----------
void displayTelemetry(String rawData) {
  myDisplay.clear();
  myDisplay.setFont(ArialMT_Plain_10);
  
  // Título
  myDisplay.drawString(0, 0, "CUBEDESIGN GS v1.0");
  myDisplay.drawLine(0, 12, 128, 12);
  
  // Tenta extrair alguns dados para mostrar
  int firstColon = rawData.indexOf(':');
  int secondColon = rawData.indexOf(':', firstColon + 1);
  int thirdColon = rawData.indexOf(':', secondColon + 1);
  int fourthColon = rawData.indexOf(':', thirdColon + 1);
  
  if (fourthColon > 0) {
    String tempo = rawData.substring(0, firstColon);
    String temp = rawData.substring(firstColon + 1, secondColon);
    String altitude = rawData.substring(thirdColon + 1, fourthColon);
    
    myDisplay.drawString(0, 16, "T:" + tempo + "s Alt:" + altitude + "m");
    myDisplay.drawString(0, 28, "Temp:" + temp + "C");
  } else {
    // Se não conseguir parsear, mostra mensagem truncada
    String shortMsg = rawData.substring(0, min(20, (int)rawData.length()));
    myDisplay.drawString(0, 16, "Msg: " + shortMsg);
  }
  
  // RSSI e Pacotes
  char rssiStr[32];
  sprintf(rssiStr, "RSSI:%d dBm Pkts:%d", rssi, (int)pacoteRecebidoCount);
  myDisplay.drawString(0, 40, rssiStr);
  
  // Último comando enviado (se houver)
  if (lastCommandSent.length() > 0) {
    String cmdShort = lastCommandSent.substring(0, min(12, (int)lastCommandSent.length()));
    myDisplay.drawString(0, 52, "Cmd:" + cmdShort);
  }
  
  myDisplay.display();
}

// ---------- Função para exibir tela de comandos ----------
void displayCommandScreen() {
  myDisplay.clear();
  myDisplay.setFont(ArialMT_Plain_10);
  
  myDisplay.drawString(0, 0, "COMANDOS DISPONIVEIS");
  myDisplay.drawLine(0, 12, 128, 12);
  
  myDisplay.drawString(0, 16, "1-Estabilizar  2-Angulos");
  myDisplay.drawString(0, 28, "3-Luz  4-Azimuth  5-Parar");
  myDisplay.drawString(0, 40, "6-AbrirPainel 7-AbrirAnt");
  myDisplay.drawString(0, 52, "8-FecharAnt DATA_S/C/M");
  
  myDisplay.display();
}

// ---------- SETUP ----------
void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  // Liga o display
  VextON();
  delay(100);

  Wire.begin(SDA_OLED, SCL_OLED);
  myDisplay.init();
  myDisplay.clear();
  myDisplay.display();
  
  myDisplay.setFont(ArialMT_Plain_10);
  myDisplay.drawString(0, 0, "CUBEDESIGN");
  myDisplay.drawString(0, 16, "Estacao Base V3");
  myDisplay.drawString(0, 32, "Inicializando...");
  myDisplay.display();

  txNumber = 0;
  rssi = 0;
  lastDisplayUpdate = millis();

  RadioEvents.RxDone = OnRxDone;
  RadioEvents.TxDone = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(RF_FREQUENCY);
  Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
                    LORA_SPREADING_FACTOR, LORA_CODINGRATE,
                    LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                    true, 0, 0, LORA_IQ_INVERSION_ON, 3000);
  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                    LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH,
                    LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                    0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

  Radio.Rx(0);
  
  delay(1500);
  
  myDisplay.clear();
  myDisplay.drawString(0, 0, "CUBEDESIGN GS");
  myDisplay.drawString(0, 16, "Aguardando");
  myDisplay.drawString(0, 28, "telemetria...");
  myDisplay.drawString(0, 45, "Freq: 915 MHz");
  myDisplay.display();
  
  Serial.println("\n╔══════════════════════════════════════════════════════════╗");
  Serial.println("║           ESTACAO BASE CUBEDESIGN v1.0                   ║");
  Serial.println("║              Heltec WiFi LoRa 32 V3                      ║");
  Serial.println("╚══════════════════════════════════════════════════════════╝\n");
  
  Serial.println("✅ Sistema inicializado!");
  Serial.printf("📍 Endereco Local: %d | Endereco Remoto: %d\n", MY_ADDRESS, DEST_ADDRESS);
  Serial.println("📡 Frequencia: 915 MHz | SF: 7\n");
  
  Serial.println("💡 COMANDOS DISPONIVEIS PARA ENVIO:");
  Serial.println("   Controle: 1-14");
  Serial.println("   Dados: DATA_S, DATA_C, DATA_M, DATA_ALL");
  Serial.println("\n🔍 Digite o comando e pressione ENTER para enviar ao satelite\n");
}

// ---------- LOOP ----------
void loop() {
  // Envio de comandos via Serial
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n' || inChar == '\r') {
      if (inputString.length() > 0) {
        inputString.trim();
        if (inputString.length() > 0 && inputString.length() < BUFFER_SIZE - 2) {
          Serial.printf("\n📤 Enviando comando: '%s'\n", inputString.c_str());
          
          memset(txpacket, 0, BUFFER_SIZE);
          txpacket[0] = MY_ADDRESS;
          txpacket[1] = DEST_ADDRESS;
          inputString.toCharArray(&txpacket[2], BUFFER_SIZE - 2);

          if (txpacket[1] == DEST_ADDRESS) {
            int msgLen = inputString.length();
            Radio.Send((uint8_t *)txpacket, msgLen + 2);
            
            lastCommandSent = inputString;
            comandosEnviados++;
            
            Serial.println("✅ Comando enviado com sucesso!");
            
            // Atualiza display com confirmação
            myDisplay.clear();
            myDisplay.drawString(0, 0, "COMANDO ENVIADO");
            myDisplay.drawLine(0, 12, 128, 12);
            myDisplay.drawString(0, 20, inputString);
            myDisplay.drawString(0, 40, "Aguardando resposta...");
            myDisplay.display();
          }
          lora_idle = false;
        }
        inputString = "";
      }
    } else {
      inputString += inChar;
    }
  }

  // Alterna entre telas do display a cada 5 segundos quando ocioso
  if (millis() - lastDisplayUpdate > 5000 && lora_idle) {
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

  if (lora_idle) {
    lora_idle = false;
    Radio.Rx(0);
  }

  Radio.IrqProcess();
}

// ---------- CALLBACKS ----------
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssiValue, int8_t snr) {
  rssi = rssiValue;
  rxSize = size;
  lastDisplayUpdate = millis();

  if (size < 2) {
    lora_idle = true;
    Radio.Rx(0);
    return;
  }

  uint8_t sender = payload[0];
  uint8_t receiver = payload[1];

  if (receiver != MY_ADDRESS || sender != DEST_ADDRESS) {
    Serial.printf("⚠️ Pacote ignorado: Origem 0x%02X, Destino 0x%02X\n", sender, receiver);
    lora_idle = true;
    Radio.Rx(0);
    return;
  }

  memcpy(rxpacket, &payload[2], size - 2);
  rxpacket[size - 2] = '\0';
  Radio.Sleep();

  String rawMessage = String((char*)rxpacket);
  pacoteRecebidoCount++;

  // Exibe no Serial
  Serial.println("\n╔══════════════════════════════════════════════════════════╗");
  Serial.println("║              TELEMETRIA RECEBIDA                         ║");
  Serial.println("╠══════════════════════════════════════════════════════════╣");
  Serial.printf("║ RSSI: %d dBm | Tamanho: %d bytes | Pacote: #%d\n", rssi, rxSize, pacoteRecebidoCount);
  Serial.println("╠══════════════════════════════════════════════════════════╣");
  
  // Mostra os dados de forma legível
  int colonCount = 0;
  for (unsigned int i = 0; i < rawMessage.length(); i++) {
    if (rawMessage.charAt(i) == ':') colonCount++;
  }
  
  if (colonCount > 5) {
    // Tenta parsear e mostrar de forma organizada
    int pos = 0;
    int nextPos;
    String fields[11];
    for (int i = 0; i < 11; i++) {
      nextPos = rawMessage.indexOf(':', pos);
      if (nextPos != -1) {
        fields[i] = rawMessage.substring(pos, nextPos);
        pos = nextPos + 1;
      }
    }
    
    Serial.printf("║ Tempo: %s s\n", fields[0].c_str());
    Serial.printf("║ Temp: %s C | Umid: %s %%\n", fields[1].c_str(), fields[2].c_str());
    Serial.printf("║ Altitude: %s m | Pressao: %s hPa\n", fields[3].c_str(), fields[4].c_str());
    Serial.printf("║ Sats: %s | Lat: %s | Lng: %s\n", fields[5].c_str(), fields[6].c_str(), fields[7].c_str());
  } else {
    Serial.printf("║ Dados brutos: %s\n", rawMessage.c_str());
  }
  Serial.println("╚══════════════════════════════════════════════════════════╝\n");

  // Atualiza display
  displayTelemetry(rawMessage);

  // Pisca LED interno para indicar recebimento
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);
  delay(30);
  digitalWrite(LED, LOW);

  lora_idle = true;
  Radio.Rx(0);
}

void OnTxDone() {
  Serial.println("✅ Transmissao concluida!\n");
  lora_idle = true;
  Radio.Rx(0);
}

void OnTxTimeout() {
  Serial.println("❌ Timeout de transmissao!\n");
  Radio.Sleep();
  lora_idle = true;
  Radio.Rx(0);
}