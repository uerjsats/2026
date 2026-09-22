/**************************************************************************************************     
 __     ___    _   _ _____          _             _   _ _____ ____     _           _       
 \ \   / / \  | \ | |_   _|__  __ _| |_          | | | | ____|  _ \   | |___  __ _| |_ ___ 
  \ \ / / _ \ |  \| | | |/ __|/ _` | __|  _____  | | | |  _| | |_) |  | / __|/ _` | __/ __|
   \ V / ___ \| |\  | | |\__ \ (_| | |_  |_____| | |_| | |___|  _ < |_| \__ \ (_| | |_\__ \
    \_/_/   \_\_| \_| |_||___/\__,_|\__|          \___/|_____|_| \_\___/|___/\__,_|\__|___/
                                                                                               
 **************************************************************************************************
 * PROJETO: VANTsat - Equipe UERJsats: Missão Atlas LASC 2026
 * DESENVOLVEDORES: Carlos Leal e Vitor Forny
 * GITHUB: https://github.com/uerjsats https://github.com/Caduleal https://github.com/vitorforny04
 * OBJETIVO:  Sistema de reconhecimento computacional de imagens focado na detecção 
 *          e classificação autônoma de formas geométricas (triângulos e quadrados).
 * CONTEXTO: Gerenciador da camada de conectividade e telemetria. Responsável por 
 *           estabelecer o enlace de comunicação (transmissão de dados de 
 *           reconhecimento para a estação base). Atua em integração 
 *           direta com o `StorageHandler` para garantir a persistência local das 
 *           imagens e parâmetros da missão.
 **************************************************************************************************/

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <WiFi.h>
#include "ADSBSimulator.h"

// Definições de Rede originais
extern const char* ssid;
extern const char* password;
extern const int adsbPort;
extern const int port;

// Protótipos das funções de rede
void setupWiFi();
void sendADSB(const ADSBRecord &record, int photoIndex, size_t photoSize);
void handleClient();

#endif
