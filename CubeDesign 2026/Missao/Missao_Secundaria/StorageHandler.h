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
#include "ADSBSimulator.h"

extern int missionPhotoIndex;
extern uint32_t totalIndex;
extern unsigned long missionStartTime;

// Índice/tamanho da última foto salva com sucesso (usados no broadcast ADS-B)
extern int lastSavedIndex;
extern size_t lastSavedSize;

// Pasta desta sessão de missão (ex: /missao/missao_003). É onde as fotos e o
// log de telemetria são salvos diretamente — sobrevive a reinícios, já que
// cada sessão ganha sua própria pasta.
extern String currentMissionDir;

// Captura um frame, roda a identificação de forma e, se for TRIANGULO, salva o
// JPEG direto na pasta da missão atual (currentMissionDir)
String captureAndSave();

// Envia a foto de um índice específico via TCP (protocolo START:.../END_FRAME) — igual ao original
void sendImageToClient(WiFiClient &client, int index);

// Cria a pasta base /missao (se não existir ainda)
void resetMission();

// Cria/recupera a pasta numerada desta sessão de missão (contador persistente no SD,
// já que sem internet não há como usar data/hora real)
void initMissionFolder();

// Grava (append) um registro ADS-B enviado no adsb.txt da pasta da missão atual
void logADSBRecord(const ADSBRecord &record, int photoIndex);

#endif
