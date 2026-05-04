#ifndef SERIAL_BRIDGE_H
#define SERIAL_BRIDGE_H

#include <Arduino.h>

// Referências ao buffer e metadados que foram preenchidos no receiveImage()
extern uint8_t fb_buf[]; 
extern int nextIndexToRequest;

void sendBufferToSerial(size_t imgSize, uint32_t totalIndex, float missionTime);

#endif