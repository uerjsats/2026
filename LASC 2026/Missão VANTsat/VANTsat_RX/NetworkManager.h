#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <WiFi.h>
#include <Arduino.h>

#define MAX_IMG_SIZE 100000

extern const char* ssid;
extern const char* password;
extern const char*serverIP;
extern const uint16_t port;

void setupNetwork();
void resetApenasRadio();
void monitorarConexao();
void receiveImage();

#endif
