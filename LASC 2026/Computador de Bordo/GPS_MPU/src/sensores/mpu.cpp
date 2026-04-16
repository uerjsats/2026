#include "mpu.h"
#include "../telemetria/telemetria.h"
#include <Wire.h>
#include <Adafruit_MPU6050.h>

Adafruit_MPU6050 mpu;

//Configurações da Média Móvel
const int numLeituras = 10; // Aumente para suavizar mais, diminua para ter menos atraso
float leiturasX[numLeituras], leiturasY[numLeituras], leiturasZ[numLeituras];
int indice = 0;
float somaX = 0, somaY = 0, somaZ = 0;

void mpuInit() {
  Wire.begin();
  if (!mpu.begin()) {
    Serial.println("Erro ao inicializar o MPU6050!");
  }
  
  //Inicializa os arrays com zero
  for (int i = 0; i < numLeituras; i++) {
    leiturasX[i] = 0;
    leiturasY[i] = 0;
    leiturasZ[i] = 0;
  }
}

void mpuUpdate() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  //Subtrai a leitura mais antiga da soma total
  somaX -= leiturasX[indice];
  somaY -= leiturasY[indice];
  somaZ -= leiturasZ[indice];
  //Armazena a nova leitura do sensor
  leiturasX[indice] = a.acceleration.x;
  leiturasY[indice] = a.acceleration.y;
  leiturasZ[indice] = a.acceleration.z;

  //Adiciona a nova leitura à soma
  somaX += leiturasX[indice];
  somaY += leiturasY[indice];
  somaZ += leiturasZ[indice];

  //Avança para a próxima posição do array
  indice++;
  if (indice >= numLeituras) {
    indice = 0; //Volta ao início (buffer circular)
  }

  //Calcula a média e salva na estrutura de telemetria
  data.accelX = somaX / numLeituras;
  data.accelY = somaY / numLeituras;
  data.accelZ = somaZ / numLeituras;
}
