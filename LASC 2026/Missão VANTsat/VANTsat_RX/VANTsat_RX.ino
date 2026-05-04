#include "DisplayHandler.h"
#include "NetworkManager.h"
#include "SerialBridge.h"


void setup(){
    Serial.begin(115200);
    setupDisplay();
    setupNetwork();
}


void loop(){
    if (receiveImage()) {
        // Esta função só executa se o receiveImage retornar true
        sendBufferToSerial(currentImgSize, currentTotalIndex, currentMissionTime);
    }
    yield();
    delay(3000);
}