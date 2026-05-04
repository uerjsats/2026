#include "SerialBridge.h"
#include "NetworkManager.h"

void sendBufferToSerial(size_t imgSize, uint32_t totalIndex, float missionTime) {
    // 1. Envia o Header START para sincronização do Python
    // Formato compatível: START:index:size:totalIndex:missionTime
    Serial.printf("START:%d:%zu:%u:%.3f\n", 
                  currentTotalIndex, imgSize, totalIndex, missionTime);
    Serial.flush();

    // 2. Transmissão Binária do Buffer (DRAM -> UART)
    // Enviamos o buffer bruto que o receiveImage() acabou de preencher
    size_t bytesSent = 0;
    const size_t chunkSize = 1024; // Blocos de 1KB para não engasgar a UART

    while (bytesSent < imgSize) {
        size_t toWrite = min(chunkSize, imgSize - bytesSent);
        Serial.write(fb_buf + bytesSent, toWrite);
        bytesSent += toWrite;
    }

    // 3. Finalizador para garantir que o script Python saiba que o bloco acabou
    Serial.print("\nEND_FRAME\n");
    Serial.flush();
}