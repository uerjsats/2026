#include <Arduino.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <DHT.h>
#include <SPI.h>
#include <RadioLib.h>
#include "float16.h"

#define DEBUG_SERIAL Serial

// ============================================
// --- CONFIGURACOES LORA PARA HELTEC V3 ---
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

SPIClass spiLoRa(FSPI);
SPISettings spiSettings(2000000, MSBFIRST, SPI_MODE0);
SX1262 radio = new Module(SS_PIN, DIO1_PIN, RST_PIN, BUSY_PIN, spiLoRa, spiSettings);

volatile bool loraPacketReceived = false;

void IRAM_ATTR setFlagLoRa(void) {
  loraPacketReceived = true;
}

// ============================================
// --- GERENCIAMENTO DE HARDWARE SERIAIS ---
// ============================================
HardwareSerial gpsSerial(1);        // UART1 nativa para o GPS
HardwareSerial suprimentoSerial(2); // UART2 nativa para a Placa de Suprimento (Nano)
HardwareSerial controleSerial(0);   // UART0 nativa para a Placa de Controle (Nano)

#define SUPRIMENTO_RX_PIN 19
#define SUPRIMENTO_TX_PIN 20
#define CONTROLE_RX_PIN 34
#define CONTROLE_TX_PIN 33

#define ADDR_SUPRIMENTO  1
#define ADDR_CONTROLE    3

#define MY_ADDRESS       43
#define DEST_ADDRESS     42

// ============================================
// --- SENSORES LOCAIS (COMPUTADOR DE BORDO) --
// ============================================
#define DHTPIN 48
#define DHTTYPE DHT22
#define SEA_LEVEL_PRESSURE_HPA 950.0f

TinyGPSPlus gps;
DHT dht(DHTPIN, DHTTYPE);

uint8_t mpuAddress = 0;
uint8_t bmeAddress = 0;
bool mpuPresent = false;
bool bmePresent = false;
bool gpsPresent = false;
bool dhtOkGlobal = false;

// Coeficientes BME280/BMP280
uint16_t dig_T1;
int16_t dig_T2, dig_T3;
uint16_t dig_P1;
int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;
int32_t t_fine = 0;

unsigned long startTime = 0;
unsigned long lastTxTime = 0;
const unsigned long txInterval = 2000;

float inaVoltage = 0.0f, inaCurrent = 0.0f, inaPower = 0.0f;
String bufferSuprimento = "";
String bufferControle = "";

unsigned long lastSupRxMs = 0;
unsigned long lastCtlRxMs = 0;
bool supCommOk = false;
bool ctlCommOk = false;

// ============================================
// --- TELEMETRIA PACKET (PACKED STRUCT) ---
// ============================================
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

// ============================================
// --- DRIVER EEPROM EM BAIXO NIVEL ---
// ============================================
class EE24CXXX {
private:
  byte _device_address;
public:
  EE24CXXX(byte device_address) : _device_address(device_address) {}
  void write(unsigned int eeaddress, unsigned char *data, unsigned int data_len);
  void read(unsigned int eeaddress, unsigned char *data, unsigned int data_len);
  template <class T> int write(unsigned int eeaddress, const T &value) {
    write(eeaddress, (unsigned char *)&value, sizeof(T));
    return sizeof(T);
  }
  template <class T> int read(unsigned int eeaddress, T &value) {
    read(eeaddress, (unsigned char *)&value, sizeof(T));
    return sizeof(T);
  }
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
    uint8_t received = Wire.requestFrom((int)_device_address, (int)bytesToRead);
    for (byte i = 0; i < received; i++) {
      data[i] = Wire.read();
    }
    data += received;
    eeaddress += received;
    data_len -= received;
    if (received == 0) {
      break;
    }
  }
}

EE24CXXX eeprom(0x50);
int currentAddress = 0;

// ============================================
// --- FUNCOES AUXILIARES I2C ---
// ============================================
bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return (Wire.endTransmission() == 0);
}

bool i2cReadBytes(uint8_t addr, uint8_t reg, uint8_t *buffer, uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  uint8_t received = Wire.requestFrom((int)addr, (int)len);
  if (received != len) {
    return false;
  }
  for (uint8_t i = 0; i < len; i++) {
    buffer[i] = Wire.read();
  }
  return true;
}

bool i2cRead8(uint8_t addr, uint8_t reg, uint8_t &value) {
  return i2cReadBytes(addr, reg, &value, 1);
}

// ============================================
// --- SCANNER INTELIGENTE BYPASS CHIP-ID ---
// ============================================
void scanI2CAndAssignSensors() {
  DEBUG_SERIAL.println("[I2C] Iniciando varredura forcada...");
  mpuAddress = 0;
  bmeAddress = 0;
  mpuPresent = false;
  bmePresent = false;

  for (uint8_t address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      if (address == 0x68 || address == 0x69) {
        mpuAddress = address;
        mpuPresent = true;
      }
      if (address == 0x76 || address == 0x77) {
        bmeAddress = address;
        bmePresent = true;
      }
    }
  }
}

// ============================================
// --- DRIVERS DE REGISTRADORES (NO BRACO) ---
// ============================================
bool initMPU6050() {
  if (!mpuPresent) {
    return false;
  }
  return i2cWrite8(mpuAddress, 0x6B, 0x00);
}

bool readRawMPU(int16_t &ax, int16_t &ay, int16_t &az) {
  if (!mpuPresent) {
    return false;
  }
  uint8_t buf[6];
  if (!i2cReadBytes(mpuAddress, 0x3B, buf, 6)) {
    return false;
  }
  ax = (int16_t)((buf[0] << 8) | buf[1]);
  ay = (int16_t)((buf[2] << 8) | buf[3]);
  az = (int16_t)((buf[4] << 8) | buf[5]);
  return true;
}

bool initBME280() {
  if (!bmePresent) {
    return false;
  }
  uint8_t calib[24];
  if (!i2cReadBytes(bmeAddress, 0x88, calib, 24)) {
    return false;
  }

  dig_T1 = (uint16_t)(calib[1] << 8 | calib[0]);
  dig_T2 = (int16_t)(calib[3] << 8 | calib[2]);
  dig_T3 = (int16_t)(calib[5] << 8 | calib[4]);
  dig_P1 = (uint16_t)(calib[7] << 8 | calib[6]);
  dig_P2 = (int16_t)(calib[9] << 8 | calib[8]);
  dig_P3 = (int16_t)(calib[11] << 8 | calib[10]);
  dig_P4 = (int16_t)(calib[13] << 8 | calib[12]);
  dig_P5 = (int16_t)(calib[15] << 8 | calib[14]);
  dig_P6 = (int16_t)(calib[17] << 8 | calib[16]);
  dig_P7 = (int16_t)(calib[19] << 8 | calib[18]);
  dig_P8 = (int16_t)(calib[21] << 8 | calib[20]);
  dig_P9 = (int16_t)(calib[23] << 8 | calib[22]);

  i2cWrite8(bmeAddress, 0xF4, 0x27);
  i2cWrite8(bmeAddress, 0xF5, 0xA0);
  return true;
}

bool readRawBME(int32_t &adc_P, int32_t &adc_T) {
  if (!bmePresent) {
    return false;
  }
  uint8_t buf[6];
  if (!i2cReadBytes(bmeAddress, 0xF7, buf, 6)) {
    return false;
  }
  adc_P = ((uint32_t)buf[0] << 12) | ((uint32_t)buf[1] << 4) | (buf[2] >> 4);
  adc_T = ((uint32_t)buf[3] << 12) | ((uint32_t)buf[4] << 4) | (buf[5] >> 4);
  return true;
}

float compensateTemperature(int32_t adc_T) {
  int32_t var1, var2, T;
  var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
  var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
  t_fine = var1 + var2;
  T = (t_fine * 5 + 128) >> 8;
  return (float)T / 100.0f;
}

float compensatePressure(int32_t adc_P) {
  int64_t var1, var2, p;
  var1 = ((int64_t)t_fine) - 128000;
  var2 = var1 * var1 * (int64_t)dig_P6;
  var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
  var2 = var2 + (((int64_t)dig_P4) << 35);
  var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
  var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;
  if (var1 == 0) {
    return 0.0f;
  }
  p = 1048576 - adc_P;
  p = (((p << 31) - var2) * 3125) / var1;
  var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (((int64_t)dig_P8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
  return ((float)p / 256.0f) / 100.0f;
}

// ============================================
// --- COMUNICACAO SERIAL ENTRE PLACAS -------
// ============================================
void solicitarDadosEscravo(uint8_t destino, String comando) {
  String frame = String(destino) + ":" + comando;
  if (destino == ADDR_SUPRIMENTO) {
    suprimentoSerial.println(frame);
  } else if (destino == ADDR_CONTROLE) {
    controleSerial.println(frame);
  }
}

void processarRespostasSlaves() {
  while (suprimentoSerial.available()) {
    char c = suprimentoSerial.read();
    if (c == '\n' || c == '\r') {
      if (bufferSuprimento.length() > 0) {
        bufferSuprimento.trim();
        int sepIdx = bufferSuprimento.indexOf(':');
        if (sepIdx != -1) {
          int srcAddr = bufferSuprimento.substring(0, sepIdx).toInt();
          String payload = bufferSuprimento.substring(sepIdx + 1);

          if (srcAddr == ADDR_SUPRIMENTO) {
            int idx1 = payload.indexOf(':');
            int idx2 = payload.lastIndexOf(':');
            if (idx1 != -1 && idx2 != -1) {
              inaVoltage = payload.substring(0, idx1).toFloat();
              inaCurrent = payload.substring(idx1 + 1, idx2).toFloat();
              inaPower = payload.substring(idx2 + 1).toFloat();
              lastSupRxMs = millis();
              supCommOk = true;
            }
          }
        }
        bufferSuprimento = "";
      }
    } else {
      bufferSuprimento += c;
    }
  }

  while (controleSerial.available()) {
    char c = controleSerial.read();
    if (c == '\n' || c == '\r') {
      if (bufferControle.length() > 0) {
        bufferControle.trim();
        int sepIdx = bufferControle.indexOf(':');
        if (sepIdx != -1) {
          int srcAddr = bufferControle.substring(0, sepIdx).toInt();
          String payload = bufferControle.substring(sepIdx + 1);
          if (srcAddr == ADDR_CONTROLE) {
            lastCtlRxMs = millis();
            ctlCommOk = true;
            DEBUG_SERIAL.println("[RX CONTROLE] " + payload);
          }
        } else {
          DEBUG_SERIAL.println("[RX CONTROLE] " + bufferControle);
        }
        bufferControle = "";
      }
    } else {
      bufferControle += c;
    }
  }
}

// ============================================
// --- SETUP COMPLETO ---
// ============================================
void setup() {
  DEBUG_SERIAL.begin(115200);
  delay(1000);

  gpsSerial.begin(9600, SERIAL_8N1, 45, 46);
  suprimentoSerial.begin(9600, SERIAL_8N1, SUPRIMENTO_RX_PIN, SUPRIMENTO_TX_PIN);
  controleSerial.begin(9600, SERIAL_8N1, CONTROLE_RX_PIN, CONTROLE_TX_PIN);

  dht.begin();
  Wire.begin(41, 42, 100000);
  delay(200);

  scanI2CAndAssignSensors();
  initBME280();
  initMPU6050();

  startTime = millis();
  spiLoRa.begin(LORA_SCK, LORA_MISO, LORA_MOSI, SS_PIN);

  int state = radio.begin(
    RF_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
    LORA_CODINGRATE, LORA_SYNC_WORD, TX_OUTPUT_POWER, LORA_PREAMBLE_LENGTH, 1.8, false
  );

  if (state == RADIOLIB_ERR_NONE) {
    radio.setDio2AsRfSwitch(true);
    DEBUG_SERIAL.println("[LORA] Modulo Pronto!");
  } else {
    DEBUG_SERIAL.print("[LORA] Erro fatal: ");
    DEBUG_SERIAL.println(state);
  }

  radio.setDio1Action(setFlagLoRa);
  radio.startReceive();
}

// ============================================
// --- LOOP PRINCIPAL ---
// ============================================
void loop() {
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }
  gpsPresent = gps.location.isValid();

  processarRespostasSlaves();

  if (loraPacketReceived) {
    loraPacketReceived = false;
    uint8_t rxBuf[300];
    int state = radio.readData(rxBuf, 300);

    if (state == RADIOLIB_ERR_NONE) {
      size_t len = radio.getPacketLength();

      if (len > 2 && rxBuf[0] == DEST_ADDRESS && rxBuf[1] == MY_ADDRESS) {
        String cmd = "";
        for (size_t i = 2; i < len; i++) {
          cmd += (char)rxBuf[i];
        }
        DEBUG_SERIAL.println("\n[LORA] Comando recebido da Base: " + cmd);

        if (cmd == "0" || cmd == "5" || cmd == "6" || cmd == "7" || cmd == "8" || cmd == "10" || cmd == "11" || cmd == "13" || cmd == "14") {
          String msgMecanismo = String(ADDR_CONTROLE) + ":" + cmd;
          controleSerial.println(msgMecanismo);
          DEBUG_SERIAL.println("-> Encaminhado para Placa de Controle!");
        }
      }
    }
    radio.startReceive();
  }

  static unsigned long lastSlaveReq = 0;
  if (millis() - lastSlaveReq >= 1000) {
    solicitarDadosEscravo(ADDR_SUPRIMENTO, "REQ_INA");
    solicitarDadosEscravo(ADDR_CONTROLE, "REQ_STATUS");
    lastSlaveReq = millis();
  }

  unsigned long nowMs = millis();
  if (nowMs - lastSupRxMs > 3500) {
    supCommOk = false;
  }
  if (nowMs - lastCtlRxMs > 3500) {
    ctlCommOk = false;
  }

  if (millis() - lastTxTime >= txInterval) {
    empacotarEEnviarSensores();
    lastTxTime = millis();
  }
}

// ============================================
// --- COMPACTACAO E TRANSMISSAO DE DADOS -----
// ============================================
void empacotarEEnviarSensores() {
  uint16_t seconds = (uint16_t)((millis() - startTime) / 1000UL);

  float tempDHT = dht.readTemperature();
  float humDHT = dht.readHumidity();
  if (isnan(tempDHT) || isnan(humDHT)) {
    tempDHT = 0.0f;
    humDHT = 0.0f;
  } else {
    dhtOkGlobal = true;
  }

  int32_t rawBME_P = 0, rawBME_T = 0;
  bool bmeOkNow = readRawBME(rawBME_P, rawBME_T);
  float pressure = 0.0f;
  if (bmeOkNow) {
    compensateTemperature(rawBME_T);
    pressure = compensatePressure(rawBME_P);
  }

  static float refPressure = 0.0f;
  if (refPressure == 0.0f && pressure > 0.0f) {
    refPressure = pressure;
  }

  float altitude = 0.0f;
  if (pressure > 0.0f && refPressure > 0.0f) {
    altitude = 44330.0f * (1.0f - pow(pressure / refPressure, 0.1903f));
  }

  float latVal = gps.location.isValid() ? gps.location.lat() : 0.0f;
  float lonVal = gps.location.isValid() ? gps.location.lng() : 0.0f;
  uint8_t satsVal = gps.satellites.isValid() ? gps.satellites.value() : 0;

  int16_t raw_ax = 0, raw_ay = 0, raw_az = 0;
  bool mpuOkNow = readRawMPU(raw_ax, raw_ay, raw_az);
  float accelX = mpuOkNow ? ((float)raw_ax / 16384.0f) * 9.80665f : 0.0f;
  float accelY = mpuOkNow ? ((float)raw_ay / 16384.0f) * 9.80665f : 0.0f;
  float accelZ = mpuOkNow ? ((float)raw_az / 16384.0f) * 9.80665f : 0.0f;

  if ((currentAddress <= 1480) && (altitude >= 500.0f)) {
    eeprom.write(currentAddress, float16((float)seconds));
    currentAddress += 2;
    eeprom.write(currentAddress, float16(tempDHT));
    currentAddress += 2;
  }

  TelemetryPacket pkt;
  pkt.src = MY_ADDRESS;
  pkt.dst = DEST_ADDRESS;
  pkt.seconds = seconds;
  pkt.temp_dht_c_x100 = (int16_t)(tempDHT * 100.0f);
  pkt.hum_dht_x100 = (uint16_t)(humDHT * 100.0f);
  pkt.press_hpa_x10 = (uint16_t)(pressure * 10.0f);
  pkt.alt_m_x10 = (int16_t)(altitude * 10.0f);
  pkt.sats = satsVal;
  pkt.lat_e6 = (int32_t)(latVal * 1000000.0);
  pkt.lon_e6 = (int32_t)(lonVal * 1000000.0);
  pkt.ax_cms2_x100 = (int16_t)(accelX * 100.0f);
  pkt.ay_cms2_x100 = (int16_t)(accelY * 100.0f);
  pkt.az_cms2_x100 = (int16_t)(accelZ * 100.0f);

  pkt.volt_ina_x100 = (uint16_t)(inaVoltage * 100.0f);
  pkt.curr_ina_x10 = (int16_t)(inaCurrent * 10.0f);
  pkt.pow_ina_x10 = (uint16_t)(inaPower * 10.0f);

  pkt.statusFlags = 0;
  if (bmeOkNow) {
    pkt.statusFlags |= FLAG_BME_OK;
  }
  if (mpuOkNow) {
    pkt.statusFlags |= FLAG_MPU_OK;
  }
  if (gpsPresent) {
    pkt.statusFlags |= FLAG_GPS_OK;
  }
  if (dhtOkGlobal) {
    pkt.statusFlags |= FLAG_DHT_OK;
  }
  if (supCommOk) {
    pkt.statusFlags |= FLAG_SUP_OK;
  }
  if (ctlCommOk) {
    pkt.statusFlags |= FLAG_CTL_OK;
  }

  radio.standby();
  radio.transmit((uint8_t *)&pkt, sizeof(pkt));
  radio.startReceive();
}
