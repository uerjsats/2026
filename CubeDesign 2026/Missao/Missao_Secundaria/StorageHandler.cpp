/**************************************************************************************************
 * MÓDULO: StorageHandler
 * BASE: Reaproveitado do VANTsat_TX_V3 (captura, grava no SD e envia a foto via TCP).
 * OBJETIVO: Mesma lógica de captura/gravação/envio de foto de antes, mas restrita a
 *           disparar apenas quando a forma identificada for TRIANGULO.
 **************************************************************************************************/
#include "StorageHandler.h"
#include "VisionSystem.h"
#include "esp_camera.h"

#define MISSION_DIR "/missao"

const int MAX_CIRCULAR_INDEX = 20;
const char* LOG_FILE = "/missao/data.txt";
int circularIndex = 0;
uint32_t totalIndex = 1;
unsigned long missionStartTime = 0;

int lastSavedIndex = -1;
size_t lastSavedSize = 0;

// ---------------------------------------------------------
// Captura, identifica e salva a foto no SD (só quando é TRIANGULO)
// ---------------------------------------------------------
String captureAndSave() {
    camera_fb_t * old_fb = esp_camera_fb_get();
    if (old_fb) {
        esp_camera_fb_return(old_fb);
    }

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("ERR:CAMERA_FAIL");
        return "";
    }

    // Identificação de forma (mesmas funções do VisionSystem, sem alteração)
    findContour(fb->buf, fb->width, fb->height);
    String tipoFigura = "NENHUM";
    if (contourSize > 300) {
        simplifyContour(18.0);
        int centroX, centroY;
        calculateCentroid(&centroX, &centroY);
        tipoFigura = identifyShape(centroX, centroY, centroX - 160, centroY - 120);
    }

    if (tipoFigura != "TRIANGULO") {
        esp_camera_fb_return(fb);
        return tipoFigura;
    }

    size_t jpeg_len = 0;
    uint8_t * jpeg_buf = NULL;
    bool jpeg_converted = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_GRAYSCALE, 80, &jpeg_buf, &jpeg_len);

    // O JPEG já foi extraído para jpeg_buf: libera o frame buffer da câmera agora
    esp_camera_fb_return(fb);

    if (!jpeg_converted) {
        Serial.println("ERR:JPEG_COMPRESSION_FAIL");
        return tipoFigura;
    }

    if (!SD.exists(MISSION_DIR)) {
        SD.mkdir(MISSION_DIR);
    }

    String path = String(MISSION_DIR) + "/" + String(circularIndex) + ".jpg";
    File file = SD.open(path.c_str(), FILE_WRITE);
    size_t bytesSalvos = 0;

    if (!file) {
        Serial.println("ERR:SD_WRITE_FAIL");
    } else {
        size_t bytes_remaining = jpeg_len;
        uint8_t * write_ptr = jpeg_buf;
        const size_t CHUNK_SIZE = 4096;

        while (bytes_remaining > 0) {
            size_t to_write = (bytes_remaining > CHUNK_SIZE) ? CHUNK_SIZE : bytes_remaining;
            size_t written = file.write(write_ptr, to_write);

            bytesSalvos += written;
            write_ptr += written;
            bytes_remaining -= written;

            if (written != to_write) {
                Serial.println("ERR:SD_WRITE_TRUNCATED");
                break;
            }
        }
        file.close();

        double missionTimeSeconds = (millis() - missionStartTime) / 1000.0;
        File log = SD.open(LOG_FILE, FILE_APPEND);
        if (log) {
            log.printf("TIPO:%s, CID:%d, TID:%u, Size:%zu, TS:%.3f\n",
                        tipoFigura.c_str(), circularIndex, totalIndex, bytesSalvos, missionTimeSeconds);
            log.close();
        }
        Serial.printf("DONE:CAPTURED:%s (FORMA:%s) [JPEG SIZE: %zu]\n", path.c_str(), tipoFigura.c_str(), bytesSalvos);

        lastSavedIndex = circularIndex;
        lastSavedSize = bytesSalvos;

        circularIndex = (circularIndex + 1) % MAX_CIRCULAR_INDEX;
        totalIndex++;
    }

    if (jpeg_buf) {
        free(jpeg_buf);
    }
    return tipoFigura;
}

// ---------------------------------------------------------
// Limpa fotos/log da missão anterior
// ---------------------------------------------------------
void resetMission() {
    if (!SD.exists(MISSION_DIR)) {
        SD.mkdir(MISSION_DIR);
        Serial.println("DONE:MISSION_DIR_CREATED");
    }

    File dir = SD.open(MISSION_DIR);
    if (!dir || !dir.isDirectory()) {
        Serial.println("ERR:MISSION_DIR_NOT_FOUND_OR_INVALID");
        return;
    }
    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String fileName = file.name();
            int slashIndex = fileName.lastIndexOf('/');
            if (slashIndex != -1) {
                fileName = fileName.substring(slashIndex + 1);
            }
            String filePath = String(MISSION_DIR) + "/" + fileName;
            file.close();
            if (SD.remove(filePath)) {
                Serial.printf("DONE:FILE_REMOVED:%s\n", filePath.c_str());
            } else {
                Serial.printf("ERR:FILE_REMOVE_FAIL:%s\n", filePath.c_str());
            }
        } else {
            file.close();
        }
        file = dir.openNextFile();
    }
    dir.close();

    File log = SD.open(LOG_FILE, FILE_WRITE);
    if (log) {
        log.close();
    }
    Serial.println("DONE:MISSION_RESET_COMPLETE");
}

// ---------------------------------------------------------
// Envia a foto de um índice específico pro computador de bordo via TCP
// ---------------------------------------------------------
void sendImageToClient(WiFiClient &client, int index) {
    String path = "/missao/" + String(index) + ".jpg";
    File file = SD.open(path.c_str(), FILE_READ);

    if (!file) {
        client.println("ERR:NOT_FOUND");
        return;
    }

    size_t size = file.size();
    uint32_t retTID = 0;
    float retTS = 0.0;
    char retTipo[32] = "N/A";

    File log = SD.open(LOG_FILE, FILE_READ);
    if (log) {
        String searchTag = "CID:" + String(index) + ",";
        while (log.available()) {
            String line = log.readStringUntil('\n');
            if (line.indexOf(searchTag) != -1) {
                int tipoStart = line.indexOf("TIPO:") + 5;
                int tipoEnd = line.indexOf(",", tipoStart);
                if (tipoStart >= 5 && tipoEnd != -1) {
                    String tipo = line.substring(tipoStart, tipoEnd);
                    strncpy(retTipo, tipo.c_str(), sizeof(retTipo) - 1);
                    retTipo[sizeof(retTipo) - 1] = '\0';
                }
                int tidStart = line.indexOf("TID:") + 4;
                int tidEnd = line.indexOf(",", tidStart);
                if (tidStart >= 4 && tidEnd != -1) {
                    retTID = line.substring(tidStart, tidEnd).toInt();
                }
                int tsStart = line.indexOf("TS:") + 3;
                if (tsStart >= 3) {
                    retTS = line.substring(tsStart).toFloat();
                }
            }
        }
        log.close();
    }

    client.printf("START:%s:%d:%zu:%u:%.3f\n", retTipo, index, size, retTID, retTS);
    client.flush();

    uint8_t buffer[2048];
    bool transferComplete = true;

    while (file.available()) {
        if (!client.connected()) {
            Serial.println("[ERRO] Socket TCP fechado de forma assincrona pelo cliente.");
            transferComplete = false;
            break;
        }

        size_t len = file.read(buffer, sizeof(buffer));
        size_t written = 0;
        unsigned long blockStart = millis();

        while (written < len) {
            size_t w = client.write(buffer + written, len - written);
            if (w > 0) {
                written += w;
                blockStart = millis();
            } else {
                if (millis() - blockStart > 3000) {
                    Serial.println("[ERRO] Timeout de bloqueio na escrita do TCP.");
                    transferComplete = false;
                    break;
                }
            }
            yield();
        }

        if (!transferComplete) break;
    }

    file.close();

    if (transferComplete && client.connected()) {
        client.print("\nEND_FRAME\n");
        client.flush();
        Serial.printf("[OK] Foto %d enviada (%zu bytes) | Tipo: %s\n", index, size, retTipo);
    } else {
        Serial.printf("[FALHA] Transmissao da foto %d abortada pelo stack TCP.\n", index);
    }
}
