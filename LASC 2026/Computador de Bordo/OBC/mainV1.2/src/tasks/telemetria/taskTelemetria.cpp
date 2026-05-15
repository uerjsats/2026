#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "taskTelemetria.h"
#include "../../libs/telemetria/telemetria.h"
#include "../../libs/wifi/NetworkManager.h"
#include "../../libs/slaves/slaves.h"

unsigned long lastSensorSend = 0;

bool sDataUpdated = false;
bool cDataUpdated = false;

const int SENSOR_INTERVAL = 1500;

extern QueueHandle_t filaTelemetria;
extern QueueHandle_t filaCompleta;

//Instância do LoRa
LoraTelemetria lora(0x01, 0x02);

const int RX_WINDOW = 500;

//Callback de recebimento
void onReceive(uint8_t* data, uint16_t size) {
  enviarOrdem((int)data);
}

void taskTelemetria(void *pvParameters)
{

    char cData[64] = "";
    char sData[64] = "";
    
    lora.init();
    serialInit();
    lora.onPacketReceived(onReceive);

    sensorsData dados;
    respost resposta;
    unsigned long lastSensorSend = 0;
    const int SENSOR_INTERVAL = 1500;

    unsigned long lastRxTime = 0;
    const int RX_WINDOW = 500;

    while (true)
    {
        lora.process();
        unsigned long now = millis();
        
        receberResposta(cData, cDataUpdated, sData, sDataUpdated);

        if (!lora.isIdle()) {
            lastRxTime = now;
        }

        if (now - lastRxTime < RX_WINDOW) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        if (lora.isImageSending()) {
            lora.sendImageChunk();
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (now - lastSensorSend >= SENSOR_INTERVAL)
        {
            if (lora.isIdle() && xQueueReceive(filaTelemetria, &dados, 0))
            {
                if(sDataUpdated || cDataUpdated)
                {
                    resposta.sensor = dados;
                    strncpy(
                        resposta.suprimento,
                        sData,
                        sizeof(resposta.suprimento)
                    );

                    strncpy(
                        resposta.controle,
                        cData,
                        sizeof(resposta.controle)
                    );
                    lora.sendPacket((uint8_t*)&resposta, sizeof(resposta), TYPE_SENSOR);
                    cDataUpdated = false;
                    sDataUpdated = false;
                    lastSensorSend = now;
                }
                else
                {   
                    lora.sendPacket((uint8_t*)&dados, sizeof(sensorsData), TYPE_SENSOR);
                    lastSensorSend = now;
                }
                
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}