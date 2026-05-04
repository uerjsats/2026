#include <Arduino.h>
#include <Wire.h>

//FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "src/libs/telemetria/telemetria.h"

//Tasks
#include "src/tasks/sensores/taskSensores.h"
#include "src/tasks/debug/debug.h"
#include "src/tasks/telemetria/taskTelemetria.h"
#include "src/tasks/wifi/taskWifi.h"

//Struct usada para envio de dados da telemetria
sensorsData dados;

//Fila global
QueueHandle_t filaTelemetria;

void setup() {
    Serial.begin(115200);
    Wire.begin(); //Inicializa I2C

    filaTelemetria = xQueueCreate(10, sizeof(sensorsData));

    //Verifica se a Fila funciona
    if (filaTelemetria == NULL)
    {
        Serial.println("Erro ao criar fila!");
        while (true);
    }
    

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
    /*/Cria task de debug
    xTaskCreatePinnedToCore(
        taskDebug,      //função
        "TaskDebug",    //nome
        4096,              //stack
        NULL,              //parâmetros
        2,                 //prioridade
        NULL,              //handle
        1                  //core
    );*/

    xTaskCreatePinnedToCore(
        taskTelemetria,
        "TaskTelemetria",
        4096,
        NULL,
        2,      // prioridade maior (importante)
        NULL,
        1       // Core 0
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