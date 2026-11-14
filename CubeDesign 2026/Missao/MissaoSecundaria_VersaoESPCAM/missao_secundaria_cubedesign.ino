/**************************************************************************************************
 * PROJETO: missao_secundaria_cubedesign (branch deteccao-aviao)
 * BASE: Reaproveita do VANTsat_TX_V3 o buffer de contorno e a simplificação RDP
 *       (VisionSystem — ver cabeçalho do .cpp pro mapeamento exato do que foi
 *       herdado x o que é novo) e o fluxo de captura/gravação/envio de foto
 *       (StorageHandler + NetworkManager), restrito apenas a AVIAO.
 * OBJETIVO: Ao identificar a silhueta de uma aeronave, salva a foto no SD (como
 *           antes) e dispara um broadcast UDP com um registro ADS-B fictício
 *           sorteado, já informando o índice da foto para o computador de bordo
 *           puxá-la via TCP.
 **************************************************************************************************/

#include "esp_camera.h"
#include <WiFi.h>
#include <SD_MMC.h> //slot do esp32-Cam 

#include "VisionSystem.h"
#include "StorageHandler.h"
#include "NetworkManager.h"
#include "ADSBSimulator.h"

// --- ORIENTAÇÃO DA IMAGEM ---
// O detector funciona em QUALQUER rotação, então isto é só para a imagem do /live
// ficar "certa" pra quem olha: CAM_VFLIP = 1 vira de cabeça pra baixo, CAM_HMIRROR = 1
// espelha esquerda/direita.
#define CAM_VFLIP    0
#define CAM_HMIRROR  0

// --- DEFINIÇÃO DE PINOS ESP32-CAM AI-Thinker (câmera) ---
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define FLASH_LED_PIN 4

const unsigned long CAPTURE_INTERVAL_MS = 500;
unsigned long lastCaptureTime = 0;

void setup() {
    Serial.begin(115200);

    pinMode(FLASH_LED_PIN, OUTPUT); 
    digitalWrite(FLASH_LED_PIN, LOW); 

    if (!SD_MMC.begin("/sdcard", true)) {
        Serial.println("[ERR] Falha ao montar SD");
    } else if (SD_MMC.cardType() == CARD_NONE) {
        Serial.println("[ERR] Nenhum cartao SD inserido");
    } else {
        Serial.println("[OK] SD montado com sucesso.");
    }

    if (psramInit()) {
        initVisionBuffers();
        Serial.println("[OK] PSRAM Inicializada");
    } else {
        Serial.println("[ERR] PSRAM não encontrada! O sistema pode falhar.");
    }

    // Configuração da Câmera (base do VANTsat_TX_V3, com pinos e exposição da ESP32-CAM)
    camera_config_t config;
    memset(&config, 0, sizeof(camera_config_t));
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;   
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_PSRAM;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[ERR] Inicialização da câmera falhou com erro 0x%x", err);
        return;
    }

    sensor_t * s = esp_camera_sensor_get();
    if (s) {
        // --- Exposicao/ganho ajustados para a ESP32-CAM (os da XIAO deixavam a imagem preta) ---
        s->set_exposure_ctrl(s, 1);                 // exposicao automatica ligada
        s->set_aec2(s, 1);                          // AEC do DSP: melhor em pouca luz
        s->set_ae_level(s, 0);                      // era -2 (escurecia)
        s->set_gain_ctrl(s, 1);                     // era 0: ganho automatico LIGADO
        s->set_gainceiling(s, GAINCEILING_16X);     // era 2x: deixa amplificar ate 16x
        s->set_brightness(s, 0);                    // era -1
        s->set_contrast(s, 2);
        s->set_sharpness(s, 2);
        s->set_whitebal(s, 1);
        s->set_wb_mode(s, 1);
        s->set_bpc(s, 1);
        s->set_wpc(s, 1);
        s->set_vflip(s, CAM_VFLIP);
        s->set_hmirror(s, CAM_HMIRROR);
    }

    // Descarta os primeiros quadros: a exposicao automatica leva ~1 s pra estabilizar
    for (int i = 0; i < 10; i++) {
        camera_fb_t * fb = esp_camera_fb_get();
        if (fb) esp_camera_fb_return(fb);
        delay(100);
    }

    resetMission();
    initMissionFolder();
    missionStartTime = millis();
    setupWiFi();

    lastCaptureTime = millis();
    Serial.println("\n--- missao_secundaria_cubedesign: ONLINE ---");
}

void loop() {
    // Atende a qualquer momento pedidos "GET:<index>" de foto do computador de bordo
    handleClient();

    if (millis() - lastCaptureTime < CAPTURE_INTERVAL_MS) {
        return;
    }
    lastCaptureTime = millis();

    // Captura, identifica e (se for AVIAO) salva a foto no SD
    String detected = captureAndSave();

    if (detected == "AVIAO") {
        ADSBRecord record = getRandomADSB();
        lastADSBRecord = record;
        adsbDataAvailable = true;
        sendADSB(record, lastSavedIndex, lastSavedSize);
        logADSBRecord(record, lastSavedIndex);
    }
}