#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "../../libs/telemetria/telemetria.h"

extern LoraTelemetria lora;

void taskTelemetria(void *pvParameters);