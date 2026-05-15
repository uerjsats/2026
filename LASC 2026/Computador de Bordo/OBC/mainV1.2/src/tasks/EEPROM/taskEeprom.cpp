#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "../../libs/telemetria/telemetria.h"
#include "../../libs/EEPROM/eepromI2C.h"
#include "../telemetria/taskTelemetria.h"


static unsigned long lastSave = 0;

extern QueueHandle_t filaTelemetria;

static const unsigned long saveInterval = 2000;

void taskEEPROM(void *pvParameters)
{
    sensorsData dados;

    while (true)
    {
        if (xQueueReceive(
                filaTelemetria,
                &dados,
                portMAX_DELAY) == pdPASS)
        {
            bool tempoOK =
                millis() - lastSave >= saveInterval;

            bool altitudeOK =
                dados.altitude >= 5000;
            // se altitude estiver *10

            bool memoriaOK =
                currentAddress + sizeof(sensorsData) < EEPROM_MAX_SIZE;

            bool radioOK =
                lora.isIdle();

            if (
                tempoOK &&
                altitudeOK &&
                memoriaOK &&
                radioOK
            )
            {
                eepromWriteBytes(
                    currentAddress,
                    (uint8_t*)&dados,
                    sizeof(sensorsData)
                );

                currentAddress += sizeof(sensorsData);

                lastSave = millis();

                Serial.println("EEPROM SALVA");
            }
            else if (!memoriaOK)
            {
                Serial.println("EEPROM LOTADA");
            }
        }
    }
}