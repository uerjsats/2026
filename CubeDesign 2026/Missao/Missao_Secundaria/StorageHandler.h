/**************************************************************************************************
 * MÓDULO: StorageHandler
 * BASE: Reaproveitado do VANTsat_TX_V3 (captura, grava no SD e envia a foto via TCP).
 * OBJETIVO: Mesma lógica de captura/gravação/envio de foto de antes, mas restrita a
 *           disparar apenas quando a forma identificada for TRIANGULO.
 **************************************************************************************************/
#ifndef STORAGE_HANDLER_H
#define STORAGE_HANDLER_H

#include "Arduino.h"
#include "FS.h"
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>

extern const int MAX_CIRCULAR_INDEX;
extern const char* LOG_FILE;
extern int circularIndex;
extern uint32_t totalIndex;
extern unsigned long missionStartTime;

// Índice/tamanho da última foto salva com sucesso (usados no broadcast ADS-B)
extern int lastSavedIndex;
extern size_t lastSavedSize;

// Captura um frame, roda a identificação de forma e, se for TRIANGULO, salva o JPEG no SD
String captureAndSave();

// Envia a foto de um índice específico via TCP (protocolo START:.../END_FRAME) — igual ao original
void sendImageToClient(WiFiClient &client, int index);

// Limpa as fotos/log da missão anterior
void resetMission();

#endif
