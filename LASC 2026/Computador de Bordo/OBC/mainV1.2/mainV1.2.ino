#include <Arduino.h>
#include <Wire.h>

//FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

//Tasks
#include "src/tasks/sensores/taskSensores.h"
#include "src/tasks/telemetria/taskTelemetria.h"
#include "src/tasks/EEPROM/taskEeprom.h"
#include "src/tasks/wifi/taskWifi.h"

//Struct usada para envio de dados da telemetria
sensorsData dados;
respost respostas;

//Fila global
QueueHandle_t filaTelemetria;
QueueHandle_t filaCompleta;

void setup() {
    Serial.begin(115200);

    Wire.begin(); //Inicializa I2C

    filaTelemetria = xQueueCreate(10, sizeof(sensorsData));
    filaCompleta = xQueueCreate(3, sizeof(respost));

    //Cria task de sensores
    xTaskCreatePinnedToCore(
        
        taskSensores,      //função
        "TaskSensores",    //nome
        4096,              //stack
        NULL,              //parâmetros
        3,                 //prioridade
        NULL,              //handle
        1                  //core
    );

    //Cria task de EEPROM
    xTaskCreatePinnedToCore(
        
        taskEEPROM,      //função
        "TaskEEPROM",    //nome
        4096,              //stack
        NULL,              //parâmetros
        3,                 //prioridade
        NULL,              //handle
        1                  //core
    );

    xTaskCreatePinnedToCore(
        taskTelemetria,
        "TaskTelemetria",
        4096,
        NULL,
        4,      //prioridade maior (importante)
        NULL,
        1       //Core 1
    );

    xTaskCreatePinnedToCore(
        taskWiFi,
        "TaskWiFi",
        8192,
        NULL,
        2,
        NULL,
        0
    );

}

void loop() {

}