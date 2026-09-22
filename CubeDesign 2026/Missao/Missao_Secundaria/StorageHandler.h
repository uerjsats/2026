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

extern const int MAX_CIRCULAR_INDEX;
extern const char* LOG_FILE;
extern int circularIndex;
extern uint32_t totalIndex;
extern unsigned long missionStartTime;

// Índice/tamanho da última foto salva com sucesso (usados no broadcast ADS-B)
extern int lastSavedIndex;
extern size_t lastSavedSize;

// Pasta desta sessão de missão (ex: /missao/missao_003), onde ficam as fotos
// já enviadas via TCP + o log de ADS-B. Sobrevive a reinícios (fora do que
// resetMission() limpa).
extern String currentMissionDir;

// Captura um frame, roda a identificação de forma e, se for TRIANGULO, salva o JPEG no SD
String captureAndSave();

// Envia a foto de um índice específico via TCP (protocolo START:.../END_FRAME) — igual ao original
// Ao concluir com sucesso, move a foto para a pasta da missão atual (currentMissionDir).
void sendImageToClient(WiFiClient &client, int index);

// Limpa as fotos/log da missão anterior (buffer circular de /missao)
void resetMission();

// Cria/recupera a pasta numerada desta sessão de missão (contador persistente no SD,
// já que sem internet não há como usar data/hora real)
void initMissionFolder();

// Grava (append) um registro ADS-B enviado no adsb.txt da pasta da missão atual
void logADSBRecord(const ADSBRecord &record, int photoIndex);

#endif
