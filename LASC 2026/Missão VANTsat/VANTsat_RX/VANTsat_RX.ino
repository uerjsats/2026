#include "DisplayHandler.h"
#include "NetworkManager.h"

void setup(){
    Serial.begin(115200);
    setupDisplay();
    setupNetwork();
}

void loop(){
    receiveImage();
    delay(1000);
}