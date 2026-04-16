#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <DHT.h>
#include <Adafruit_MPU6050.h>
#include <SPI.h>
#include <LoRa.h>
#include "float16.h"

// ============================================
// --- CONFIGURAÇÕES LORA PARA HELTEC V3 ---
// ============================================
#define SS_PIN      8     // CS (Chip Select) - V3
#define RST_PIN     12    // Reset - V3
#define DIO0_PIN    14    // DIO0 - V3

#define RF_FREQUENCY        915E6   // 915 MHz
#define TX_OUTPUT_POWER     20      // dBm
#define LORA_SPREADING_FACTOR 7     // SF7
#define LORA_BANDWIDTH      125E3   // 125 kHz
#define LORA_CODINGRATE     5       // 4/5
#define LORA_PREAMBLE_LENGTH 8

#define BUFFER_SIZE 300

#define MY_ADDRESS 43        // Endereço do rádio do satélite (computador de bordo)
#define DEST_ADDRESS 42      // Endereço da estação base

// Endereços dos subsistemas
#define ADDR_SUPRIMENTO 1
#define ADDR_RASPBERRY 2
#define ADDR_CONTROLE 3

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

bool lora_idle = true;
bool txDone = false;

TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

// Pinos para comunicação com Controle de Atitude
#define CONTROLE_RX 4   // RX do Heltec -> TX do Controle
#define CONTROLE_TX 5   // TX do Heltec -> RX do Controle
HardwareSerial controleSerial(2);

// Pinos para Raspberry
#define RASP_RX 6
#define RASP_TX 7
HardwareSerial raspSerial(0);

// Serial para placa de suprimento
HardwareSerial suprimentoSerial(1);

// Debug Serial
#define DEBUG_SERIAL Serial

#define SEALEVELPRESSURE_HPA (950)
#define DHTPIN 48
#define DHTTYPE DHT22

Adafruit_BME280 bme;
DHT dht(DHTPIN, DHTTYPE);
Adafruit_MPU6050 mpu;

unsigned long startTime;
unsigned long lastTxTime = 0;
const unsigned long txInterval = 2000; // envia a cada 2s

// Variáveis de controle de solicitação
bool requestPowerSupplyData = false;
bool requestACData = false;
bool requestMData = false;
unsigned long lastDataRequest = 0;
const unsigned long dataRequestTimeout = 5000;

// Pacotes de dados
String powerSupplyData = " ";
String ACData = " ";
String MData = " ";

// Flags de atualização
bool powerSupplyUpdated = false;
bool MDataUpdated = false;
bool ACDataUpdated = false;

// Buffers para recepção serial
String bufferSuprimento = "";
String bufferControle = "";
String bufferRasp = "";

class EE24CXXX {
private:
    byte _device_address;

public:
    EE24CXXX(byte device_address) : _device_address(device_address) {}
    void write(unsigned int eeaddress, unsigned char *data, unsigned int data_len);
    void read(unsigned int eeaddress, unsigned char *data, unsigned int data_len);

    template <class T>
    int write(unsigned int eeaddress, const T &value);
    template <class T>
    int read(unsigned int eeaddress, T &value);
};

void EE24CXXX::write(unsigned int eeaddress, unsigned char *data, unsigned int data_len) {
    while (data_len > 0) {
        Wire.beginTransmission(_device_address);
        Wire.write((int)(eeaddress >> 8));
        Wire.write((int)(eeaddress & 0xFF));

        byte bytesToWrite = min(data_len, (unsigned int)16);
        for (byte i = 0; i < bytesToWrite; i++) {
            Wire.write(data[i]);
        }

        Wire.endTransmission();
        eeaddress += bytesToWrite;
        data += bytesToWrite;
        data_len -= bytesToWrite;

        delay(5);
    }
}

void EE24CXXX::read(unsigned int eeaddress, unsigned char *data, unsigned int data_len) {
    while (data_len > 0) {
        Wire.beginTransmission(_device_address);
        Wire.write((int)(eeaddress >> 8));
        Wire.write((int)(eeaddress & 0xFF));
        Wire.endTransmission();

        byte bytesToRead = min(data_len, (unsigned int)28);
        Wire.requestFrom(_device_address, bytesToRead);

        for (byte i = 0; i < bytesToRead && Wire.available(); i++) {
            data[i] = Wire.read();
        }

        data += bytesToRead;
        eeaddress += bytesToRead;
        data_len -= bytesToRead;
    }
}

template <class T> int EE24CXXX::write(unsigned int eeaddress, const T &value) {
    write(eeaddress, (unsigned char *)&value, sizeof(T));
    return sizeof(T);
}

template <class T> int EE24CXXX::read(unsigned int eeaddress, T &value) {
    read(eeaddress, (unsigned char *)&value, sizeof(T));
    return sizeof(T);
}

EE24CXXX eeprom(0x50);
int currentAddress = 0;

struct sensorsData {
    unsigned short seconds;
    float temperatureDHT;
    float humidityDHT;
    float pressure;
    int sats;
    float altitude;
    float latitude;
    float longitude;
    float accelX;
    float accelY;
    float accelZ;
};

// ============================================
// --- SETUP ---
// ============================================
void setup() {
    DEBUG_SERIAL.begin(115200);
    delay(1000);
    
    DEBUG_SERIAL.println("\n\n╔══════════════════════════════════════════════════════════╗");
    DEBUG_SERIAL.println("║        COMPUTADOR DE BORDO CUBEDESIGN - HELTEC V3        ║");
    DEBUG_SERIAL.println("╚══════════════════════════════════════════════════════════╝\n");
    
    // Configuração das Seriais
    gpsSerial.begin(9600, SERIAL_8N1, 45, 46);
    controleSerial.begin(9600, SERIAL_8N1, CONTROLE_RX, CONTROLE_TX);
    raspSerial.begin(9600, SERIAL_8N1, RASP_RX, RASP_TX);
    suprimentoSerial.begin(9600, SERIAL_8N1, 5, 18);
    
    DEBUG_SERIAL.println("Seriais configuradas:");
    DEBUG_SERIAL.print("  Controle: RX="); DEBUG_SERIAL.print(CONTROLE_RX); DEBUG_SERIAL.print(", TX="); DEBUG_SERIAL.println(CONTROLE_TX);
    DEBUG_SERIAL.print("  Raspberry: RX="); DEBUG_SERIAL.print(RASP_RX); DEBUG_SERIAL.print(", TX="); DEBUG_SERIAL.println(RASP_TX);
    DEBUG_SERIAL.println("  Suprimento: Serial1 (pinos 5,18)");

    // Inicializa sensores
    if (!bme.begin(0x76)) {
        DEBUG_SERIAL.println("Erro ao inicializar o BME280!");
    } else {
        DEBUG_SERIAL.println("BME280 OK");
    }

    dht.begin();
    Wire.begin();

    if (!mpu.begin()) {
        DEBUG_SERIAL.println("Erro ao inicializar o MPU6050!");
    } else {
        DEBUG_SERIAL.println("MPU6050 OK");
    }

    startTime = millis();

    // Inicializa LoRa para Heltec V3
    DEBUG_SERIAL.print("Inicializando LoRa V3... ");
    LoRa.setPins(SS_PIN, RST_PIN, DIO0_PIN);
    
    if (!LoRa.begin(RF_FREQUENCY)) {
        DEBUG_SERIAL.println("FALHA!");
        while (1) delay(1000);
    }
    DEBUG_SERIAL.println("OK!");
    
    // Configura parâmetros LoRa
    LoRa.setSpreadingFactor(LORA_SPREADING_FACTOR);
    LoRa.setSignalBandwidth(LORA_BANDWIDTH);
    LoRa.setCodingRate4(LORA_CODINGRATE);
    LoRa.setPreambleLength(LORA_PREAMBLE_LENGTH);
    LoRa.setTxPower(TX_OUTPUT_POWER);
    
    DEBUG_SERIAL.println("\n✅ Sistema pronto!");
    DEBUG_SERIAL.println("┌──────────────────────────────────────────────────────────┐");
    DEBUG_SERIAL.print("│ • Endereco Local:  "); DEBUG_SERIAL.print(MY_ADDRESS); DEBUG_SERIAL.println("                                     │");
    DEBUG_SERIAL.print("│ • Endereco Remoto: "); DEBUG_SERIAL.print(DEST_ADDRESS); DEBUG_SERIAL.println("                                     │");
    DEBUG_SERIAL.println("│ • Frequencia:      915 MHz                               │");
    DEBUG_SERIAL.println("│ • SF:              7                                     │");
    DEBUG_SERIAL.println("└──────────────────────────────────────────────────────────┘\n");
    
    // Teste de comunicação
    delay(2000);
    DEBUG_SERIAL.println("--- Teste de comunicação ---");
    DEBUG_SERIAL.println("Enviando comando de teste para Controle de Atitude...");
    enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "0");
    delay(500);
    DEBUG_SERIAL.println("Enviando comando de teste para Suprimento...");
    enviarComandoFormatado(suprimentoSerial, ADDR_SUPRIMENTO, "0");
    
    DEBUG_SERIAL.println("\n🔍 Aguardando telecomandos...\n");
}

// ============================================
// --- LOOP ---
// ============================================
void loop() {
    // Processa GPS
    while (gpsSerial.available()) {
        char c = gpsSerial.read();
        gps.encode(c);
    }
    
    // Processa mensagens recebidas das seriais
    processarMensagensSuprimento();
    processarMensagensControle();
    processarMensagensRasp();
    
    // Verifica timeout das requisições de dados
    if (millis() - lastDataRequest > dataRequestTimeout) {
        if (requestPowerSupplyData || requestACData || requestMData) {
            DEBUG_SERIAL.println("Timeout de requisição de dados!");
        }
        requestPowerSupplyData = false;
        requestACData = false;
        requestMData = false;
    }
    
    // Verifica recepção LoRa
    int packetSize = LoRa.parsePacket();
    if (packetSize > 0) {
        processarPacoteLoRa(packetSize);
    }
    
    // Envio periódico dos sensores principais via LoRa
    if (millis() - lastTxTime >= txInterval) {
        enviarDadosSensores();
        lastTxTime = millis();
    }
}

// ============================================
// --- PROCESSAMENTO DE PACOTES LORA ---
// ============================================
void processarPacoteLoRa(int packetSize) {
    if (packetSize < 2) return;
    
    // Lê o pacote
    int i = 0;
    while (LoRa.available() && i < BUFFER_SIZE - 1) {
        rxpacket[i++] = (char)LoRa.read();
    }
    rxpacket[i] = '\0';
    
    uint8_t sender = rxpacket[0];
    uint8_t receiver = rxpacket[1];
    
    // Verifica se é para mim e do remetente correto
    if (receiver != MY_ADDRESS || sender != DEST_ADDRESS) {
        DEBUG_SERIAL.print("Pacote ignorado: From 0x");
        DEBUG_SERIAL.print(sender, HEX);
        DEBUG_SERIAL.print(" To 0x");
        DEBUG_SERIAL.println(receiver, HEX);
        return;
    }
    
    String comando = String(&rxpacket[2]);
    int rssi = LoRa.packetRssi();
    
    DEBUG_SERIAL.println("\n╔══════════════════════════════════════════════════════════╗");
    DEBUG_SERIAL.println("║              TELECOMANDO RECEBIDO                        ║");
    DEBUG_SERIAL.println("╠══════════════════════════════════════════════════════════╣");
    DEBUG_SERIAL.print("║ Comando: "); DEBUG_SERIAL.print(comando);
    for (int j = comando.length() + 10; j < 58; j++) DEBUG_SERIAL.print(" ");
    DEBUG_SERIAL.println("║");
    DEBUG_SERIAL.print("║ RSSI: "); DEBUG_SERIAL.print(rssi); DEBUG_SERIAL.print(" dBm");
    for (int j = String(rssi).length() + 11; j < 58; j++) DEBUG_SERIAL.print(" ");
    DEBUG_SERIAL.println("║");
    DEBUG_SERIAL.println("╚══════════════════════════════════════════════════════════╝\n");
    
    // Processa o comando
    executarTelecomando(comando);
}

// ============================================
// --- EXECUÇÃO DE TELECOMANDOS ---
// ============================================
void executarTelecomando(String cmd) {
    // Comandos de controle
    if (cmd == "1") {
        DEBUG_SERIAL.println("-> Estabilizar satélite");
        enviarOrdemslave(1);
    }
    else if (cmd == "2") {
        DEBUG_SERIAL.println("-> Definir ângulos");
        enviarOrdemslave(2);
    }
    else if (cmd == "3") {
        DEBUG_SERIAL.println("-> Orientar para luz");
        enviarOrdemslave(3);
    }
    else if (cmd == "4") {
        DEBUG_SERIAL.println("-> Buscar azimuth");
        enviarOrdemslave(4);
    }
    else if (cmd == "5") {
        DEBUG_SERIAL.println("-> Parar controle");
        enviarOrdemslave(5);
    }
    else if (cmd == "6") {
        DEBUG_SERIAL.println("-> Abrir painéis");
        enviarOrdemslave(6);
    }
    else if (cmd == "7") {
        DEBUG_SERIAL.println("-> Abrir antena");
        enviarOrdemslave(7);
    }
    else if (cmd == "8") {
        DEBUG_SERIAL.println("-> Fechar antena");
        enviarOrdemslave(8);
    }
    else if (cmd == "9") {
        DEBUG_SERIAL.println("-> Ajustar PID");
        enviarOrdemslave(9);
    }
    else if (cmd == "10") {
        DEBUG_SERIAL.println("-> Emergência");
        enviarOrdemslave(10);
    }
    else if (cmd == "11") {
        DEBUG_SERIAL.println("-> Testar painéis");
        enviarOrdemslave(11);
    }
    else if (cmd == "12") {
        DEBUG_SERIAL.println("-> Iniciar missão");
        enviarOrdemslave(12);
    }
    else if (cmd == "13") {
        DEBUG_SERIAL.println("-> Testar atuador");
        enviarOrdemslave(13);
    }
    else if (cmd == "14") {
        DEBUG_SERIAL.println("-> Status mecanismos");
        enviarOrdemslave(14);
    }
    // Comandos de solicitação de dados
    else if (cmd == "DATA_S") {
        DEBUG_SERIAL.println("-> Solicitar dados de Suprimento");
        solicitarDados('S');
    }
    else if (cmd == "DATA_C") {
        DEBUG_SERIAL.println("-> Solicitar dados do Controle");
        solicitarDados('C');
    }
    else if (cmd == "DATA_M") {
        DEBUG_SERIAL.println("-> Solicitar dados da Missão");
        solicitarDados('M');
    }
    else if (cmd == "DATA_ALL") {
        DEBUG_SERIAL.println("-> Solicitar TODOS os dados");
        solicitarDados('A');
    }
    else {
        DEBUG_SERIAL.print("Comando desconhecido: ");
        DEBUG_SERIAL.println(cmd);
    }
}

// ============================================
// --- PROCESSAMENTO DE MENSAGENS SERIAIS ---
// ============================================
void processarMensagensSuprimento() {
    while (suprimentoSerial.available()) {
        char c = suprimentoSerial.read();
        
        if (c == '\n' || c == '\r') {
            if (bufferSuprimento.length() > 0) {
                bufferSuprimento.trim();
                if (bufferSuprimento.length() > 0) {
                    int idx = bufferSuprimento.indexOf(':');
                    if (idx != -1) {
                        String enderecoStr = bufferSuprimento.substring(0, idx);
                        int endereco = enderecoStr.toInt();
                        String dados = bufferSuprimento.substring(idx + 1);
                        
                        if (endereco == ADDR_SUPRIMENTO) {
                            powerSupplyData = dados;
                            powerSupplyUpdated = true;
                            DEBUG_SERIAL.print("✓ Suprimento: ");
                            DEBUG_SERIAL.println(dados);
                        }
                    }
                }
                bufferSuprimento = "";
            }
        } else {
            bufferSuprimento += c;
        }
    }
}

void processarMensagensControle() {
    while (controleSerial.available()) {
        char c = controleSerial.read();
        
        if (c == '\n' || c == '\r') {
            if (bufferControle.length() > 0) {
                bufferControle.trim();
                if (bufferControle.length() > 0) {
                    int idx = bufferControle.indexOf(':');
                    if (idx != -1) {
                        String enderecoStr = bufferControle.substring(0, idx);
                        int endereco = enderecoStr.toInt();
                        String dados = bufferControle.substring(idx + 1);
                        
                        if (endereco == ADDR_CONTROLE) {
                            ACData = dados;
                            ACDataUpdated = true;
                            DEBUG_SERIAL.print("✓ Controle: ");
                            DEBUG_SERIAL.println(dados);
                        }
                    }
                }
                bufferControle = "";
            }
        } else {
            bufferControle += c;
        }
    }
}

void processarMensagensRasp() {
    while (raspSerial.available()) {
        char c = raspSerial.read();
        
        if (c == '\n' || c == '\r') {
            if (bufferRasp.length() > 0) {
                bufferRasp.trim();
                if (bufferRasp.length() > 0) {
                    int idx = bufferRasp.indexOf(':');
                    if (idx != -1) {
                        String enderecoStr = bufferRasp.substring(0, idx);
                        int endereco = enderecoStr.toInt();
                        String dados = bufferRasp.substring(idx + 1);
                        
                        if (endereco == ADDR_RASPBERRY) {
                            MData = dados;
                            MDataUpdated = true;
                            DEBUG_SERIAL.print("✓ Missão: ");
                            DEBUG_SERIAL.println(dados);
                        }
                    }
                }
                bufferRasp = "";
            }
        } else {
            bufferRasp += c;
        }
    }
}

// ============================================
// --- ENVIO DE DADOS VIA LORA ---
// ============================================
void enviarDadosSensores() {
    sensorsData dados;
    unsigned short elapsedTime = (millis() - startTime) / 1000;
    dados.seconds = elapsedTime;
    
    // Dados DHT
    dados.temperatureDHT = dht.readTemperature();
    if (isnan(dados.temperatureDHT)) dados.temperatureDHT = 0.0;

    dados.humidityDHT = dht.readHumidity();
    if (isnan(dados.humidityDHT)) dados.humidityDHT = 0.0;
    
    // Dados do BME
    dados.pressure = bme.readPressure() / 100.0F;
    static float refPressure = 0;
    if (refPressure == 0) {
        refPressure = dados.pressure;  
    }

    dados.altitude = bme.readAltitude(refPressure);
    
    // Dados GPS
    dados.latitude = gps.location.lat();
    dados.longitude = gps.location.lng();
    dados.sats = gps.satellites.value();

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    
    // Dados GY
    dados.accelX = a.acceleration.x;
    dados.accelY = a.acceleration.y;
    dados.accelZ = a.acceleration.z;
    
    // Escrita na EEPROM
    if ((currentAddress <= 1480) && (dados.altitude >= 500.00)) {
        eeprom.write(currentAddress, float16(dados.seconds)); currentAddress += 2;
        eeprom.write(currentAddress, float16(dados.temperatureDHT)); currentAddress += 2;
        eeprom.write(currentAddress, float16(dados.humidityDHT)); currentAddress += 2;
        eeprom.write(currentAddress, float16(dados.pressure)); currentAddress += 2;
        eeprom.write(currentAddress, float16(dados.altitude)); currentAddress += 2;
        eeprom.write(currentAddress, float16(dados.latitude)); currentAddress += 2;
        eeprom.write(currentAddress, float16(dados.longitude)); currentAddress += 2;
        eeprom.write(currentAddress, float16(dados.accelX)); currentAddress += 2;
        eeprom.write(currentAddress, float16(dados.accelY)); currentAddress += 2;
        eeprom.write(currentAddress, float16(dados.accelZ)); currentAddress += 2;
    }

    memset(txpacket, 0, BUFFER_SIZE);
    txpacket[0] = MY_ADDRESS;
    txpacket[1] = DEST_ADDRESS;

    // Monta o pacote base
    char basePacket[BUFFER_SIZE - 2];
    snprintf(basePacket, BUFFER_SIZE - 2,
            "%u:%.2f:%.2f:%.2f:%.2f:%d:%.6f:%.6f:%.2f:%.2f:%.2f",
            dados.seconds, dados.temperatureDHT, dados.humidityDHT, dados.altitude,
            dados.pressure, dados.sats, dados.latitude, dados.longitude,
            dados.accelX, dados.accelY, dados.accelZ);

    // Adiciona dados adicionais apenas se solicitados e atualizados
    if ((requestPowerSupplyData && powerSupplyUpdated) || 
        (requestACData && ACDataUpdated) || 
        (requestMData && MDataUpdated)) {
        
        snprintf(&txpacket[2], BUFFER_SIZE - 2, "%s:%s:%s:%s",
                basePacket,
                (requestPowerSupplyData && powerSupplyUpdated) ? powerSupplyData.c_str() : "",
                (requestACData && ACDataUpdated) ? ACData.c_str() : "",
                (requestMData && MDataUpdated) ? MData.c_str() : "");
        
        if (requestPowerSupplyData) powerSupplyUpdated = false;
        if (requestACData) ACDataUpdated = false;
        if (requestMData) MDataUpdated = false;
        
        DEBUG_SERIAL.println("📤 Enviando telemetria + dados adicionais...");
    } else {
        strcpy(&txpacket[2], basePacket);
        DEBUG_SERIAL.println("📤 Enviando telemetria base...");
    }

    // Envia via LoRa
    LoRa.beginPacket();
    LoRa.write((uint8_t*)txpacket, strlen(txpacket));
    LoRa.endPacket();
    
    DEBUG_SERIAL.println("✅ Pacote enviado!\n");
}

// ============================================
// --- FUNÇÕES AUXILIARES ---
// ============================================
void enviarComandoFormatado(HardwareSerial &serialPort, int endereco, String comando) {
    String msg = String(endereco) + ":" + comando;
    
    DEBUG_SERIAL.print("TX ["); DEBUG_SERIAL.print(endereco); DEBUG_SERIAL.print("]: ");
    DEBUG_SERIAL.println(msg);
    
    serialPort.println(msg);
    serialPort.flush();
    delay(10);
}

void solicitarDados(char tipo) {
    lastDataRequest = millis();
    
    switch(tipo) {
        case 'S':
            requestPowerSupplyData = true;
            enviarComandoFormatado(suprimentoSerial, ADDR_SUPRIMENTO, "REQ:1");
            break;
        case 'C':
            requestACData = true;
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "REQ:3");
            break;
        case 'M':
            requestMData = true;
            enviarComandoFormatado(raspSerial, ADDR_RASPBERRY, "REQ:2");
            break;
        case 'A':
            requestPowerSupplyData = true;
            requestACData = true;
            requestMData = true;
            enviarComandoFormatado(suprimentoSerial, ADDR_SUPRIMENTO, "REQ:1");
            delay(50);
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "REQ:3");
            delay(50);
            enviarComandoFormatado(raspSerial, ADDR_RASPBERRY, "REQ:2");
            break;
    }
}

void enviarOrdemslave(int Buscador) {
    switch(Buscador){
        case 0:  enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "0"); break;
        case 1:  enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "1"); break;
        case 2:  enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "2:23.00 45.00"); break;
        case 3:  enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "3"); break;
        case 4:  enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "4"); break;
        case 5:  enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "5"); break;
        case 6:  enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "6"); break;
        case 7:  enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "7"); break;
        case 8:  enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "8"); break;
        case 9:  enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "9"); break;
        case 10: enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "10"); break;
        case 11: enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "11"); break;
        case 12: enviarComandoFormatado(raspSerial, ADDR_RASPBERRY, "12"); break;
        case 13: enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "13"); break;
        case 14: enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "14"); break;
        default: DEBUG_SERIAL.println("Ordem inválida!"); break;
    }
}