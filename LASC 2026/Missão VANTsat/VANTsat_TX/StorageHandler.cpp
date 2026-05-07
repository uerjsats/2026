#include "SerialBridge.h"
#include "StorageHandler.h"
#include "VisionSystem.h"
#include "esp_camera.h"

// Inicialização das variáveis de estado
const int MAX_CIRCULAR_INDEX = 10;
const char* LOG_FILE = "/data.txt";
int circularIndex = 0;
uint32_t totalIndex = 1;
unsigned long missionStartTime = 0;

void captureAndSave() {

    camera_fb_t * old_fb = esp_camera_fb_get();
    if (old_fb) {
        esp_camera_fb_return(old_fb);
    }

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("ERR:CAMERA_FAIL");
        return;
    }

    String tipoFigura = processVisionFrame(fb);
    String path = "/" + String(circularIndex) + ".jpg";
    
    // Variáveis para receber o buffer JPEG compactado
    size_t jpeg_len = 0;
    uint8_t * jpeg_buf = NULL;

    bool jpeg_converted = fmt2jpg(fb->buf, fb->len, fb->width, fb->height, PIXFORMAT_GRAYSCALE, 80, &jpeg_buf, &jpeg_len);

    if (!jpeg_converted) {
        Serial.println("ERR:JPEG_COMPRESSION_FAIL");
        esp_camera_fb_return(fb);
        return;
    }

    File file = SD_MMC.open(path.c_str(), FILE_WRITE);
    size_t bytesSalvos = 0; 

    if (!file) {
        Serial.println("ERR:SD_WRITE_FAIL");
    } else {
        // Gravação do buffer JPEG compactado no SD_MMC
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
        
        // Gravação do LOG 
        double missionTimeSeconds = (millis() - missionStartTime) / 1000.0;
        File log = SD_MMC.open(LOG_FILE, FILE_APPEND);
        if (log) {
            log.printf("TIPO:%s, CID:%d, TID:%u, Size:%zu, TS:%.3f\n", 
                        tipoFigura.c_str(), circularIndex, totalIndex, bytesSalvos, missionTimeSeconds);
            log.close();
        }
        
        Serial.printf("DONE:CAPTURED:%s (FORMA:%s) [JPEG SIZE: %zu]\n", path.c_str(), tipoFigura.c_str(), bytesSalvos);
        circularIndex = (circularIndex + 1) % MAX_CIRCULAR_INDEX;
        totalIndex++;
    }

    // Libera a memória alocada dinamicamente pelo conversor JPEG
    if (jpeg_buf) {
        free(jpeg_buf);
    }
    // Libera o DMA do framebuffer da câmera
    esp_camera_fb_return(fb);
}

void resetMission() {
    for (int i = 0; i < MAX_CIRCULAR_INDEX; i++) {
        String path = "/" + String(i) + ".jpg";
        if (SD_MMC.exists(path.c_str())) SD_MMC.remove(path.c_str());
    }
    SD_MMC.remove(LOG_FILE);
    
    circularIndex = 0;
    totalIndex = 1;
    missionStartTime = millis();
    Serial.println("DONE:MISSION_RESET");
}

void sendImageToClient(WiFiClient &client, int index) {
    String path = "/" + String(index) + ".jpg";
    File file = SD_MMC.open(path.c_str(), FILE_READ);
    
    if (!file) {
        client.println("ERR:NOT_FOUND");
        return;
    }

    size_t size = file.size();
    uint32_t retTID = 0;
    float retTS = 0.0;
    char retTipo[32] = "N/A";
    bool found = false;

    // Recupera os metadados mais recentes do LOG_FILE
    File log = SD_MMC.open(LOG_FILE, FILE_READ);
    if (log) {
        String searchTag = "CID:" + String(index) + ",";
        while (log.available()) {
            String line = log.readStringUntil('\n');
            
            // O formato no SD é: TIPO:%s, CID:%d, TID:%u, Size:%zu, TS:%.3f
            if (line.indexOf(searchTag) != -1) {
                // %31[^,] lê a string até encontrar uma vírgula, protegendo contra buffer overflow
                sscanf(line.c_str(), "TIPO:%31[^,], CID:%*d, TID:%u, Size:%*u, TS:%f", retTipo, &retTID, &retTS);
                found = true;
            }
        }
        log.close();
    }

    // Envia o Header estendido via TCP
    client.printf("START:%s:%d:%zu:%u:%.3f\n", retTipo, index, size, retTID, retTS);
    client.flush();

    uint8_t buffer[2048]; 
    
    while (file.available()) {
        if (!client.connected()) {
            Serial.println("[ERRO] Conexão TCP interrompida durante o envio binário.");
            break;
        }

        size_t len = file.read(buffer, sizeof(buffer));
        client.write(buffer, len);
        delay(1); 
    }
    
    file.close();
    client.flush(); 
    client.print("\nEND_FRAME\n");
    client.flush();
    
    Serial.printf("[OK] Foto %d enviada (%zu bytes) | Tipo: %s\n", index, size, retTipo);
}

void sendImageDataSerial(int index) {
    String path = "/" + String(index) + ".jpg";
    File file = SD_MMC.open(path.c_str(), FILE_READ);
    
    if (!file) {
        Serial.println("ERR:NOT_FOUND");
        return;
    }

    size_t size = file.size();
    
    // Variável de fallback para tolerância a falhas na leitura de disco
    String missionData = "TIPO:NENHUM, CID:" + String(index) + ", TID:0, Size:0, TS:0.000";
    
    // Abertura e varredura do arquivo de metadados (Complexidade O(n) no disco local)
    File log = SD_MMC.open(LOG_FILE, FILE_READ);
    if (log) {
        String searchTag = "CID:" + String(index) + ",";
        while (log.available()) {
            String line = log.readStringUntil('\n');
            if (line.indexOf(searchTag) != -1) {
                missionData = line; 
            }
        }
        log.close();
    }

    // Protocolo de Transmissão UART: Injeção dos metadados extraídos no cabeçalho
    Serial.printf("START_STORAGE:%d:%zu:%s\n", index, size, missionData.c_str());
    Serial.flush();
    // Stream de Transmissão Binária
    uint8_t buffer[1024];
    while (file.available()) {
        int bytesRead = file.read(buffer, sizeof(buffer));
        Serial.write(buffer, bytesRead);
    }
    file.close();
    Serial.print("\nEND_FRAME\n");
    Serial.flush();
}