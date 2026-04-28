#include "src/gps/gps.h"
#include "src/mpu/mpu.h"
#include "src/SensoresAmbientais/SensoresAmbientais.h"
#include "src/telemetria/telemetria.h"

//Struct usada para envio de dados da telemetria
sensorsData dados;

SensoresAmbientais sensores(48, DHT22, 0X76);

LoraTelemetria lora(43, 42);

unsigned long missionStart;

unsigned long displayTime = 0;
bool showStatus = false;

void setup() {
  Serial.begin(115200);
  Wire.begin(); //Inicializa I2C
  Serial.println("I2C Foiiiii");
  gpsInit(); //Inicializa GPS
  Serial.println("GPS Foiiiii");
  mpuInit(); //Inicializa MPU
  Serial.println("MPU Foiiiii");
  sensores.init(false); //Inicializa BME e DHT -
  Serial.println("BME e DHt Foiiiii");
  lora.init(); //Inicializa Rádio
  lora.setTxInterval(2000); //Controle de Tempo
  Serial.println("LoRa Foiiiii");
  //Contagem do tempo de missão
  missionStart = millis();

  Serial.println("Computador de Bordo Foiiiii");
}

void loop() {

  float temp, hum; //dados DHT
  float pressao, altitude; //dados BME
  //dados do GPS
  int sats;
  float latitude, longitude;
  float x, y, z; //Três eixos do MPU

  lora.process();

  //Leitura DHT
  if(sensores.lerDHT(temp, hum))
  {
    dados.temperature = temp * 100;
    dados.humidity = hum * 100;
  }
  else
  {
    dados.temperature = 0; //Caso não consiga ler retorna 0, para não perder toda a missão
    dados.humidity    = 0;
  }

  //Leitura BME
  if(sensores.lerBME(pressao, altitude))
  {
    dados.pressure = pressao;
    dados.altitude = altitude * 10;
  }
  else
  {
    dados.pressure = 0;
    dados.altitude = 0;
  }

  //Leitura GPS
  gpsUpdate(latitude, longitude, sats);
  dados.latitude = latitude;
  dados.longitude = longitude;
  dados.sats = sats;

  //Leitura MPU
  mpuUpdate(x, y, z);
  dados.accelX = x * 100;
  dados.accelY = y * 100;
  dados.accelZ = z * 100;

  //Salvar dados da payload
  dados.seconds = millis() / 1000;
  dados.seconds = (millis() - missionStart) / 1000;
  //Envio de Dados
  lora.sendPacket((uint8_t*)&dados, sizeof(dados));
}