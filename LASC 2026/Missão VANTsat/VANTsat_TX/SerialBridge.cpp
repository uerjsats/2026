#include "SerialBridge.h"
#include "VisionSystem.h"   
#include "StorageHandler.h"
#include "esp_camera.h"

void handleSerialCommands() {
    if (Serial.available() > 0) {
        char rawCmd = Serial.read();
        if (rawCmd == '\n' || rawCmd == '\r') return;
        char cmd = toupper(rawCmd); 
        if (cmd == 'C') {
            captureAndSave();
        } 
        else if (cmd == 'R') {
            resetMission();
        }
        else if (cmd >= '0' && cmd <= '9') {
            sendImageDataSerial(cmd - '0');
        }
    }
}


