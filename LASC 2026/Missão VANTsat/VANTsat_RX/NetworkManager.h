#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <WiFi.h>
#include <Arduino.h>

#define MAX_IMG_SIZE 30000

extern const char* ssid;
extern const char* password;
extern const char*serverIP;
extern const uint16_t port;

// --- Declarações Externas ---
extern uint32_t currentTotalIndex;
extern float currentMissionTime;
extern size_t currentImgSize;
extern bool firstConnectionEver;

extern size_t currentImgSize;
extern uint32_t currentTotalIndex;
extern float currentMissionTime;

void setupNetwork();
void resetRadio();
void monitorConnection();
bool receiveImage();

#endif
