#ifndef DISPLAY_HANDLER_H
#define DISPLAY_HANDLER_H

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

//Definições de hardware heltec
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET 21
#define SCREEN_ADDRESS 0x3C
#define SDA_OLED 17
#define SCL_OLED 18
#define Vext 36

extern Adafruit_SSD1306 display;

void setupDisplay();
void updateDisplay(String status, String info =  "");

#endif
