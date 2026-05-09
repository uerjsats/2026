#include <Arduino.h>
#include "../../libs/wifi/wifi.h"
#include "../../libs/telemetria/telemetria.h"

// Instância do LoRa
extern LoraTelemetria lora;

void taskWiFi(void *pvParameters)
{
    wifiInit();

    while (true)
    {
        //Executa máquina de estados
        wifiStep();

        //Log quando imagem chega
        if (wifiImageReady())
        {
            Serial.println("[WiFi] Imagem recebida e pronta");
        }

        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}