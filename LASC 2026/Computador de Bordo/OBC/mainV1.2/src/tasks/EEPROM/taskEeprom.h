#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

//Endereço atual da EEPROM
extern int currentAddress;

//Tamanho máximo da EEPROM
#ifndef EEPROM_MAX_SIZE
#define EEPROM_MAX_SIZE 4096
#endif

//Intervalo entre gravações
static const unsigned long saveInterval = 2000;

//Protótipo da task
void taskEEPROM(void *pvParameters);
