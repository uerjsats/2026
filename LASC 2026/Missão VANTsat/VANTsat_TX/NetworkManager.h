#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <WiFi.h>

// Definições de Rede originais
extern const char* ssid;
extern const char* password;
extern const int port;

// Protótipos das funções de rede
void setupWiFi();
void handleClient();

#endif