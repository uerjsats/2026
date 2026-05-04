#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

//Task principal
void taskSensores(void *pvParameters);

void initSensores();
void lerSensores(
    float &latitude,
    float &longitude,
    int &sats,
    float &temperatura,
    float &umidade,
    float &pressao,
    float &altitude,
    float &accelX,
    float &accelY,
    float &accelZ
);