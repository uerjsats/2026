//Bibliotecas do RTOS
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

//Bibliotecas Modularizadas
#include "../../libs/telemetria/telemetria.h"
#include "../../libs/gps/gps.h"
#include "../../libs/mpu/mpu.h"
#include "../../libs/SensoresAmbientais/SensoresAmbientais.h"

//Fila criada no main.cpp
extern QueueHandle_t filaTelemetria;

SensoresAmbientais sensores(48, DHT22, 0X76);

// Funções do seu módulo de sensores
void initSensores()
{
    gpsInit();
    sensores.init(false);
    mpuInit();
}

void lerSensores(float &latitude, float &longitude, int &sats, float &temperatura, float &umidade, float &pressao, float &altitude, float &accelX, float &accelY, float &accelZ)
{
    gpsUpdate(latitude, longitude, sats);
    sensores.lerDHT(temperatura, umidade);
    sensores.lerBME(pressao, altitude);
    mpuUpdate(accelX, accelY, accelZ);
}

void taskSensores(void *pvParameters)
{
    //Inicializa sensores
    initSensores();
    sensorsData dados;

    //Variáveis locais
    //GPS
    float latitude = 0, longitude = 0;
    int sats = 0;

    //DHT
    float temperatura = 0, umidade = 0;

    //BME
    float pressao = 0, altitude = 0;

    //MPU
    float accelX = 0, accelY = 0, accelZ = 0;

    while (true)
    {
        //Limpa struct
        memset(&dados, 0, sizeof(sensorsData));

        //Lê todos os sensores
        lerSensores(
            latitude,
            longitude,
            sats,
            temperatura,
            umidade,
            pressao,
            altitude,
            accelX,
            accelY,
            accelZ
        );

        //Preenche struct
        dados.latitude = latitude * 10000000.0f;
        dados.longitude = longitude * 10000000.0f;
        dados.sats = sats;

        dados.temperatura = temperatura * 100.0f;
        dados.umidade = umidade * 100.0f;
        dados.pressao = pressao;
        dados.altitude = altitude * 10.0f;

        dados.accelX = accelX * 100.0f;
        dados.accelY = accelY * 100.0f;
        dados.accelZ = accelZ * 100.0f;

        //Timestamp
        dados.seconds = millis() / 1000;

        //Envia para fila (não bloqueante)
        xQueueSend(filaTelemetria, &dados, 0);

        //Frequência da leitura
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}