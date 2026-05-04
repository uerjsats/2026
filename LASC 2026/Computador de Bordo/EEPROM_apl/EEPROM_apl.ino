#include "src/EEPROM/eepromI2C.h"
#include "src/gps/gps.h"
#include "src/mpu/mpu.h"
#include "src/SensoresAmbientais/SensoresAmbientais.h"
#include <EEPROM.h>

struct sensorsData {
  float temperature;
  float humidity;
  float pressure;
  float altitude;
  float latitude;
  float longitude;
  int sats;
  float accelX;
  float accelY;
  float accelZ;
  unsigned long seconds;
};

// Instâncias globais
sensorsData dados;

SensoresAmbientais sensores(42, DHT22, 0X76);

unsigned long missionStart;
static unsigned long lastSend = 0;
bool showStatus = false;
int end = 0; 

extern int currentAddress; 

#ifndef EEPROM_MAX_SIZE
#define EEPROM_MAX_SIZE 4096 
#endif

void setup() {
  Wire.begin(); 

 for (int i = 0 ; i < EEPROM.length() ; i++) {
    EEPROM.write(i, 0xFF);
  }
  

  Serial.begin(115200);
  gpsInit(); 
  mpuInit(); 
  sensores.init(false); 
  
  missionStart = millis();
}

void loop() {

  float temp, hum, pressao, altitude, lat, lon, x, y, z;
  int sats;

  if(sensores.lerDHT(temp, hum)) {
    dados.temperature = temp;
    dados.humidity = hum;
    
  } else {
    dados.temperature = 0;
    dados.humidity = 0;
  }

  if(sensores.lerBME(pressao, altitude)) {
    dados.pressure = pressao;
    dados.altitude = altitude;
   
  } else {
    dados.pressure = 0;
    dados.altitude = 0;
  }

  gpsUpdate(lat, lon, sats);
  dados.latitude = lat;
  dados.longitude = lon;
  dados.sats = sats;
  
 
  mpuUpdate(x, y, z);
  dados.accelX = x;
  dados.accelY = y;
  dados.accelZ = z;



 if(millis() - lastSend >= 2000) { 
    
    unsigned long StartTime = millis(); 
    dados.seconds = (millis() - missionStart) / 1000; 

    if(currentAddress + sizeof(dados) < EEPROM_MAX_SIZE) { 
      eepromWriteBytes(currentAddress, (uint8_t*)&dados, sizeof(dados));
      
      Serial.print("Dados salvos no endereco: "); 
      Serial.println(currentAddress);
  

      eepromReadBytes(currentAddress, (uint8_t*)&dados, sizeof(dados)); 
      Serial.println("Dados recuperados da EEPROM ---");
      printEEPROM(); 

      
      currentAddress += sizeof(dados); 
    } else { 

      Serial.println("ALERTA: EEPROM LOTADA!"); 
      unsigned long CurrentTime = millis(); 
      if (end == 0) { 
        unsigned long ElapsedTime = millis() - missionStart; 
        Serial.println("Tempo ate lotar (ms): "); 
        Serial.println(ElapsedTime); 
        end++; 

      }
    }


    lastSend = millis(); 
  }

} 

void printEEPROM() {
  int totalBytes = currentAddress; 
  int numRegistros = totalBytes / sizeof(sensorsData); 

  //se funcionar tem que decrescer 44
  Serial.println("========== EEPROM DUMP ==========");
  Serial.print("Bytes gravados : "); Serial.println(totalBytes);
  Serial.print("Tamanho struct : "); Serial.println((int)sizeof(sensorsData));
  Serial.print("Registros      : "); Serial.println(numRegistros);
  Serial.print("Tamanho EEPROM : "); Serial.print(EEPROM_MAX_SIZE); Serial.println(" bytes"); // <---
  Serial.print("Espaco livre   : "); Serial.print(EEPROM_MAX_SIZE - totalBytes); Serial.println(" bytes");


  } 
 