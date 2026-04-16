#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <DHT.h>
#include <Adafruit_MPU6050.h>
#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "float16.h"

#define RF_FREQUENCY 915000000 // Hz
#define TX_OUTPUT_POWER 20

#define LORA_BANDWIDTH 0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODINGRATE 1
#define LORA_PREAMBLE_LENGTH 8
#define LORA_SYMBOL_TIMEOUT 0
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false

#define RX_TIMEOUT_VALUE 1000
#define BUFFER_SIZE 300

#define MY_ADDRESS 43 //Endereço do rádio do satélite
#define DEST_ADDRESS 42 //Endereço da estação base

// Endereços dos subsistemas
#define ADDR_SUPRIMENTO 1
#define ADDR_RASPBERRY 2
#define ADDR_CONTROLE 3

char txpacket[BUFFER_SIZE];
char rxpacket[BUFFER_SIZE];

double txNumber;
bool lora_idle = true;

static RadioEvents_t RadioEvents;
void OnTxDone(void);
void OnTxTimeout(void);
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssiValue, int8_t snr);  

TinyGPSPlus gps;
HardwareSerial gpsSerial(1);

// CORREÇÃO: Pinos corretos para comunicação com Controle de Atitude
#define CONTROLE_RX 12  // RX do Heltec -> TX do Controle (pino 12)
#define CONTROLE_TX 4   // TX do Heltec -> RX do Controle (pino 4)
HardwareSerial controleSerial(2);

// CORREÇÃO: Pinos para Raspberry
#define RASP_RX 6
#define RASP_TX 7
HardwareSerial raspSerial(0);

// CORREÇÃO: Serial para placa de suprimento (usa Serial1)
HardwareSerial suprimentoSerial(1);  // Usa Serial1 para suprimento

// Adicionar Serial de debug para monitorar o que está sendo enviado
#define DEBUG_SERIAL Serial

#define SEALEVELPRESSURE_HPA (950)
#define DHTPIN 48
#define DHTTYPE DHT22
#define TIMEZONE_OFFSET -3

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
const unsigned long dataRequestTimeout = 5000; // Timeout de 5 segundos para requisição de dados

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

void setup() {
    DEBUG_SERIAL.begin(115200);  // Debug em alta velocidade
    
    delay(1000);
    
    DEBUG_SERIAL.println("\n\n=== COMPUTADOR DE BORDO HELTEC ===");
    DEBUG_SERIAL.println("Inicializando...");
    
    // Configuração das Seriais
    gpsSerial.begin(9600, SERIAL_8N1, 45, 46);
    controleSerial.begin(9600, SERIAL_8N1, CONTROLE_RX, CONTROLE_TX);
    raspSerial.begin(9600, SERIAL_8N1, RASP_RX, RASP_TX);
    
    // CORREÇÃO: Configura Serial1 para a placa de suprimento
    // Se a placa de suprimento estiver conectada via USB, use Serial
    // Se estiver conectada nos pinos GPIO, configure adequadamente
    suprimentoSerial.begin(9600, SERIAL_8N1, 5, 18);  // Ajuste os pinos conforme necessário
    
    DEBUG_SERIAL.println("Seriais configuradas:");
    DEBUG_SERIAL.print("  GPS: RX=45, TX=46\n");
    DEBUG_SERIAL.print("  Controle: RX="); DEBUG_SERIAL.print(CONTROLE_RX); DEBUG_SERIAL.print(", TX="); DEBUG_SERIAL.println(CONTROLE_TX);
    DEBUG_SERIAL.print("  Raspberry: RX="); DEBUG_SERIAL.print(RASP_RX); DEBUG_SERIAL.print(", TX="); DEBUG_SERIAL.println(RASP_TX);
    DEBUG_SERIAL.println("  Suprimento: Serial1 (pinos 5,18)");

    if (!bme.begin(0x76)) {
        DEBUG_SERIAL.println("Erro ao inicializar o BME280!");
    }

    dht.begin();
    Wire.begin();

    if (!mpu.begin()) {
        DEBUG_SERIAL.println("Erro ao inicializar o MPU6050!");
    }

    startTime = millis();

    Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

    txNumber = 0;

    RadioEvents.TxDone = OnTxDone;
    RadioEvents.RxDone = OnRxDone;
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
    
    DEBUG_SERIAL.println("LoRa inicializado!");
    DEBUG_SERIAL.println("Sistema pronto!");
    
    // Teste de comunicação
    delay(2000);
    DEBUG_SERIAL.println("\n--- Teste de comunicação ---");
    
    DEBUG_SERIAL.println("Enviando comando de teste para Controle de Atitude...");
    enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "0");
    
    delay(500);
    
    DEBUG_SERIAL.println("Enviando comando de teste para Suprimento...");
    enviarComandoFormatado(suprimentoSerial, ADDR_SUPRIMENTO, "0");
}

void loop() {
    // Processa GPS
    while (gpsSerial.available()) {
        char c = gpsSerial.read();
        gps.encode(c);
    }

    Radio.IrqProcess();
    
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
    
    // Envio periódico dos sensores principais via LoRa
    if (millis() - lastTxTime >= txInterval && lora_idle) {
        enviarDadosSensores();
    }
}

// CORREÇÃO: Processamento de mensagens da placa de suprimento
void processarMensagensSuprimento() {
    while (suprimentoSerial.available()) {
        char c = suprimentoSerial.read();
        
        if (c == '\n' || c == '\r') {
            if (bufferSuprimento.length() > 0) {
                bufferSuprimento.trim();
                if (bufferSuprimento.length() > 0) {
                    DEBUG_SERIAL.print("RX Suprimento: ");
                    DEBUG_SERIAL.println(bufferSuprimento);
                    
                    int idx = bufferSuprimento.indexOf(':');
                    if (idx != -1) {
                        String enderecoStr = bufferSuprimento.substring(0, idx);
                        int endereco = enderecoStr.toInt();
                        String dados = bufferSuprimento.substring(idx + 1);
                        
                        if (endereco == ADDR_SUPRIMENTO) {
                            powerSupplyData = dados;
                            powerSupplyUpdated = true;
                            DEBUG_SERIAL.println("✓ Dados de suprimento atualizados!");
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
                    DEBUG_SERIAL.print("RX Controle: ");
                    DEBUG_SERIAL.println(bufferControle);
                    
                    int idx = bufferControle.indexOf(':');
                    if (idx != -1) {
                        String enderecoStr = bufferControle.substring(0, idx);
                        int endereco = enderecoStr.toInt();
                        String dados = bufferControle.substring(idx + 1);
                        
                        if (endereco == ADDR_CONTROLE) {
                            ACData = dados;
                            ACDataUpdated = true;
                            DEBUG_SERIAL.println("✓ Dados do controle atualizados!");
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
                    DEBUG_SERIAL.print("RX Raspberry: ");
                    DEBUG_SERIAL.println(bufferRasp);
                    
                    int idx = bufferRasp.indexOf(':');
                    if (idx != -1) {
                        String enderecoStr = bufferRasp.substring(0, idx);
                        int endereco = enderecoStr.toInt();
                        String dados = bufferRasp.substring(idx + 1);
                        
                        if (endereco == ADDR_RASPBERRY) {
                            MData = dados;
                            MDataUpdated = true;
                            DEBUG_SERIAL.println("✓ Dados da missão atualizados!");
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
        
        // Reseta as flags de atualização
        if (requestPowerSupplyData) powerSupplyUpdated = false;
        if (requestACData) ACDataUpdated = false;
        if (requestMData) MDataUpdated = false;
        
        DEBUG_SERIAL.println("Enviando dados adicionais via LoRa");
    } else {
        strcpy(&txpacket[2], basePacket);
        DEBUG_SERIAL.println("Enviando dados base via LoRa");
    }

    // Envia dados para estação base
    if (txpacket[1] == DEST_ADDRESS) {
        Radio.Send((uint8_t *)txpacket, strlen((char *)txpacket));
        lora_idle = false;
        lastTxTime = millis();
    }
}

// CORREÇÃO: Função para enviar comando formatado (endereço:comando)
void enviarComandoFormatado(HardwareSerial &serialPort, int endereco, String comando) {
    String msg = String(endereco) + ":" + comando;
    
    DEBUG_SERIAL.print("TX [");
    DEBUG_SERIAL.print(endereco);
    DEBUG_SERIAL.print("]: '");
    DEBUG_SERIAL.print(msg);
    DEBUG_SERIAL.println("'");
    
    serialPort.println(msg);
    serialPort.flush();
    delay(10);
}

// CORREÇÃO: Solicitação de dados com formato correto
void solicitarDados(char tipo) {
    lastDataRequest = millis();
    
    switch(tipo) {
        case 'S': // Suprimento
            requestPowerSupplyData = true;
            DEBUG_SERIAL.println("Solicitando dados de Suprimento...");
            enviarComandoFormatado(suprimentoSerial, ADDR_SUPRIMENTO, "REQ:1");
            break;
            
        case 'C': // Controle de Atitude
            requestACData = true;
            DEBUG_SERIAL.println("Solicitando dados do Controle de Atitude...");
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "REQ:3");
            break;
            
        case 'M': // Missão/Raspberry
            requestMData = true;
            DEBUG_SERIAL.println("Solicitando dados da Missão...");
            enviarComandoFormatado(raspSerial, ADDR_RASPBERRY, "REQ:2");
            break;
            
        case 'A': // Todos
            requestPowerSupplyData = true;
            requestACData = true;
            requestMData = true;
            DEBUG_SERIAL.println("Solicitando dados de TODOS os subsistemas...");
            enviarComandoFormatado(suprimentoSerial, ADDR_SUPRIMENTO, "REQ:1");
            delay(50);
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "REQ:3");
            delay(50);
            enviarComandoFormatado(raspSerial, ADDR_RASPBERRY, "REQ:2");
            break;
    }
}

void enviarOrdemslave(int Buscador) {
    DEBUG_SERIAL.print("Processando comando: ");
    DEBUG_SERIAL.println(Buscador);
    
    switch(Buscador){
        case 0:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "0");
            break;
        case 1:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "1");
            break;
        case 2:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "2:23.00 45.00");
            break;
        case 3:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "3");
            break;
        case 4:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "4");
            break;
        case 5: 
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "5");
            break;
        case 6: 
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "6");
            break;
        case 7:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "7");
            break;
        case 8:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "8");
            break;
        case 9:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "9");
            break;
        case 10:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "10");
            break;
        case 11:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "11");
            break;
        case 12:
            enviarComandoFormatado(raspSerial, ADDR_RASPBERRY, "12");
            break;
        case 13:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "13");
            break;
        case 14:
            enviarComandoFormatado(controleSerial, ADDR_CONTROLE, "14");
            break;
        default:
            DEBUG_SERIAL.print("Ordem inválida: ");
            DEBUG_SERIAL.println(Buscador);
            break;  
    }
}

void OnTxDone() {
    lora_idle = true;
    Radio.Rx(0);
}

void OnTxTimeout() {
    Radio.Sleep();
    lora_idle = true;
    Radio.Rx(0);
}

// Recebe Telecomando
void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssiValue, int8_t snr) {
    DEBUG_SERIAL.println("\n=== TELECOMANDO RECEBIDO ===");
    
    if (size < 2) return;

    uint8_t sender = payload[0];
    uint8_t receiver = payload[1];

    if (receiver != MY_ADDRESS || sender != DEST_ADDRESS) {
        lora_idle = true;
        Radio.Rx(0);
        return;
    }

    memset(rxpacket, 0, BUFFER_SIZE);
    memcpy(rxpacket, &payload[2], size - 2);
    rxpacket[size - 2] = '\0';
    Radio.Sleep();
    
    DEBUG_SERIAL.print("Comando: '");
    DEBUG_SERIAL.print(rxpacket);
    DEBUG_SERIAL.println("'");

    // Comandos de controle
    if (strcmp(rxpacket, "1") == 0) {
        DEBUG_SERIAL.println("-> Estabilizar satélite");
        enviarOrdemslave(1);
    }
    else if(strcmp(rxpacket, "2") == 0) {
        DEBUG_SERIAL.println("-> Definir ângulos");
        enviarOrdemslave(2);
    }
    else if(strcmp(rxpacket, "3") == 0) {
        DEBUG_SERIAL.println("-> Orientar para luz");
        enviarOrdemslave(3);
    }
    else if(strcmp(rxpacket, "4") == 0) {
        DEBUG_SERIAL.println("-> Buscar azimuth");
        enviarOrdemslave(4);
    }
    else if(strcmp(rxpacket, "5") == 0) {
        DEBUG_SERIAL.println("-> Parar controle");
        enviarOrdemslave(5);
    }
    else if(strcmp(rxpacket, "6") == 0) {
        DEBUG_SERIAL.println("-> Abrir painéis");
        enviarOrdemslave(6);
    }
    else if(strcmp(rxpacket, "7") == 0) {
        DEBUG_SERIAL.println("-> Abrir antena");
        enviarOrdemslave(7);
    }
    else if(strcmp(rxpacket, "8") == 0) {
        DEBUG_SERIAL.println("-> Fechar antena");
        enviarOrdemslave(8);
    }
    else if(strcmp(rxpacket, "9") == 0) {
        DEBUG_SERIAL.println("-> Ajustar PID");
        enviarOrdemslave(9);
    }
    else if(strcmp(rxpacket, "10") == 0) {
        DEBUG_SERIAL.println("-> Emergência");
        enviarOrdemslave(10);
    }
    else if(strcmp(rxpacket, "11") == 0) {
        DEBUG_SERIAL.println("-> Testar painéis");
        enviarOrdemslave(11);
    }
    else if(strcmp(rxpacket, "12") == 0) {
        DEBUG_SERIAL.println("-> Iniciar missão");
        enviarOrdemslave(12);
    }
    else if(strcmp(rxpacket, "13") == 0) {
        DEBUG_SERIAL.println("-> Testar atuador");
        enviarOrdemslave(13);
    }
    else if(strcmp(rxpacket, "14") == 0) {
        DEBUG_SERIAL.println("-> Status mecanismos");
        enviarOrdemslave(14);
    }
    // Comandos de solicitação de dados
    else if(strcmp(rxpacket, "DATA_S") == 0) {
        DEBUG_SERIAL.println("-> Solicitar dados de Suprimento");
        solicitarDados('S');
    }
    else if(strcmp(rxpacket, "DATA_C") == 0) {
        DEBUG_SERIAL.println("-> Solicitar dados do Controle");
        solicitarDados('C');
    }
    else if(strcmp(rxpacket, "DATA_M") == 0) {
        DEBUG_SERIAL.println("-> Solicitar dados da Missão");
        solicitarDados('M');
    }
    else if(strcmp(rxpacket, "DATA_ALL") == 0) {
        DEBUG_SERIAL.println("-> Solicitar TODOS os dados");
        solicitarDados('A');
    }

    lora_idle = true;
    DEBUG_SERIAL.println("===============================\n");
}