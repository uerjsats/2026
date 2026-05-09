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