#include "DisplayHandler.h"
#include <Wire.h>
#include <Arduino.h>

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setupDisplay(){
    pinMode(Vext, OUTPUT);
    digitalWrite(Vext, LOW);
    delay(100);

    Wire.begin(SDA_OLED, SCL_OLED);
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        Serial.println("[ERROR] SSD1306 allocation failed");
    } else {
        display.clearDisplay();       
        display.setTextColor(SSD1306_WHITE);
        display.display();
        Serial.println("[OK] OLED Initialized");
    }
}

void updateDisplay(String status, String info){
    display.clearDisplay();
    display.setCursor(0,0);
    display.setTextSize(1);
    display.println("--- VANTsat RX ---");
    display.println("");
    display.println(status);
    display.println(info);
    display.display();
}
