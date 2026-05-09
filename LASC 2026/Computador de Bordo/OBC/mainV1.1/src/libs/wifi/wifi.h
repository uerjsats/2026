#pragma once

#include <Arduino.h>
#include <WiFi.h>

#ifdef __cplusplus
extern "C" {
#endif

void wifiInit();
void wifiStep();
void wifiLoop();

bool wifiIsConnected();

bool wifiImageReady();
uint8_t* wifiGetBuffer();
uint32_t wifiGetImageSize();
uint32_t wifiGetImageID();

bool wifiHasLink();        
void wifiDisable();          
void wifiEnable();           

void wifiConsumeImage();

#ifdef __cplusplus
}
#endif