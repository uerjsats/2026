#include <Arduino.h>
#include "../../libs/wifi/NetworkManager.h"
#include "../../libs/serialbridge/SerialBridge.h"

volatile bool imageReady = false;

void taskWiFi(void *pvParameters)
{
    setupNetwork();

    bool firstCycle = true;

    while (true)
    {
        if (firstCycle)
        {
            firstCycle = false;
            Serial.println("[WIFI] Aguardando reset da missao...");
            vTaskDelay(pdMS_TO_TICKS(7000));
        }

        Serial.println("[WIFI] Verificando recebimento...");

        bool received = false;

        if (WiFi.status() == WL_CONNECTED)
        {
            received = receiveImage();
        }

        if (received)
        {
            Serial.println("[WIFI] CHEGOU ALGO!");
            imageReady = true;
        }
        else
        {
            Serial.println("[WIFI] Nada recebido");
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}