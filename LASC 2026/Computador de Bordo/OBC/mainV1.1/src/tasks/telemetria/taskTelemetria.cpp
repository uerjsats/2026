#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "../../libs/telemetria/telemetria.h"
#include "../../libs/wifi/wifi.h"

unsigned long lastSensorSend = 0;

const int SENSOR_INTERVAL = 1000;

extern QueueHandle_t filaTelemetria;

//Instância do LoRa
LoraTelemetria lora(0x01, 0x02);

//Callback de recebimento
void onReceive(uint8_t* data, uint16_t size)
{
    Serial.print("[LoRa RX] Recebido: ");
    for (int i = 0; i < size; i++)
    {
        Serial.print(data[i], HEX);
        Serial.print(" ");
    }
    Serial.println();
}

void taskTelemetria(void *pvParameters)
{
    lora.init();
    lora.onPacketReceived(onReceive);

    sensorsData dados;

    while (true)
    {
        lora.process();

        if (lora.isIdle())
        {
            unsigned long now = millis();
            if (now - lastSensorSend >= SENSOR_INTERVAL)
            {
                if (xQueueReceive(filaTelemetria, &dados, 0))
                {
                    lora.sendPacket((uint8_t*)&dados, sizeof(sensorsData), TYPE_SENSOR);
                    lastSensorSend = now;
                }
            }
            else 
            {
                if (imgTx.ativo)
                {
                    lora.sendImageChunk();
                }
                else if (wifiImageReady())
                {
                    lora.sendImageRaw(wifiGetBuffer(), wifiGetImageSize());
                    wifiConsumeImage();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}