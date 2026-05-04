#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "../../libs/telemetria/telemetria.h"

extern QueueHandle_t filaTelemetria;

void taskDebug(void *pvParameters)
{
    sensorsData dados;

    while (true)
    {
        //Espera dado da fila
        if (xQueueReceive(filaTelemetria, &dados, portMAX_DELAY))
        {
            Serial.println("----- TELEMETRIA -----");

            Serial.print("Lat: "); Serial.println(dados.latitude, 6);
            Serial.print("Lon: "); Serial.println(dados.longitude, 6);
            Serial.print("Sats: "); Serial.println(dados.sats);

            Serial.print("Temp: "); Serial.println(dados.temperatura);
            Serial.print("Umidade: "); Serial.println(dados.umidade);
            Serial.print("Pressao: "); Serial.println(dados.pressao);
            Serial.print("Altitude: "); Serial.println(dados.altitude);

            Serial.print("AccelX: "); Serial.println(dados.accelX);
            Serial.print("AccelY: "); Serial.println(dados.accelY);
            Serial.print("AccelZ: "); Serial.println(dados.accelZ);

            Serial.print("Tempo: "); Serial.println(dados.seconds);

            Serial.println("----------------------\n");
        }
    }
}