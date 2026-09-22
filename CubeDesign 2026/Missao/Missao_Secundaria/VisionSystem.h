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
 * CONTEXTO: Motor de processamento de imagens focado na detecção geométrica.
 *           Realiza a classificação determinística de triângulos e quadrados 
 *           exclusivamente através do cálculo e da verificação de ângulos.
 **************************************************************************************************/
#ifndef VISION_SYSTEM_H
#define VISION_SYSTEM_H

#include "Arduino.h"
#include "esp_camera.h"

// Estrutura e variáveis originais
struct Point { int x, y; };
extern Point *contour;
extern Point vertices[40];
extern int contourSize;
extern int vertexCount;

// Protótipos das funções de visão
void initVisionBuffers();
String processVisionFrame(camera_fb_t * fb);
void findContour(uint8_t* buf, int w, int h);
void simplifyContour(float epsilon);
void calculateCentroid(int *cX, int *cY);
String identifyShape(int cX, int cY, int errX, int errY);
void drawLine(uint8_t* buf, int w, int h, int x0, int y0, int x1, int y1);

#endif