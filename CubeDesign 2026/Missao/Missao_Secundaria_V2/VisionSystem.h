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
 *            e classificação autônoma de silhuetas de aeronaves (asa delta/UCAV).
 * CONTEXTO:  Branch dedicada só à detecção de avião — o caminho original de
 *            TRIANGULO/QUADRADO do VANTsat_TX_V3 foi removido daqui (continua
 *            intacto na main). Ver VisionSystem.cpp para o mapeamento exato do
 *            que foi herdado do VANTsat_TX_V3 x o que é novo desta branch.
 **************************************************************************************************/
#ifndef VISION_SYSTEM_H
#define VISION_SYSTEM_H

#include "Arduino.h"
#include "esp_camera.h"

// Estrutura e buffer de contorno — herdados do VANTsat_TX_V3, reaproveitados
// pelo detector de aviões (acTrace/acPolygon escrevem/leem em contour[]).
struct Point { int x, y; };
extern Point *contour;
extern int contourSize;

void initVisionBuffers();

// Herdada do VANTsat_TX_V3 sem alteração (reaproveitada por acRdp/acPolygon).
float perpendicularDistance(Point p, Point s, Point e);

// Silhueta de aeronave (asa delta). Retorna "AVIAO" ou "NENHUM".
String identifyAircraft(uint8_t* buf, int w, int h);

#endif