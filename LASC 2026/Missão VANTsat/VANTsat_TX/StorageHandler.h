#ifndef STORAGE_HANDLER_H
#define STORAGE_HANDLER_H

#include "Arduino.h"
#include "FS.h"
#include "SD_MMC.h"
#include <WiFi.h>

// Variáveis globais de controle da missão
extern const int MAX_CIRCULAR_INDEX;
extern const char* LOG_FILE;
extern int circularIndex;
extern uint32_t totalIndex;
extern unsigned long missionStartTime;

// Protótipos das funções de armazenamento
void captureAndSave();
void sendImageDataSerial(int index);
void sendImageToClient(WiFiClient &client, int index);
void resetMission();

#endif