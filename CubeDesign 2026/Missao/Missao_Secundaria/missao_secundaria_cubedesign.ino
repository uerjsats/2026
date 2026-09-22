/**************************************************************************************************
 * PROJETO: missao_secundaria_cubedesign
 * BASE: Reaproveita do VANTsat_TX_V3 a identificação geométrica de triângulos
 *       (VisionSystem, sem nenhuma alteração) e o fluxo de captura/gravação/envio
 *       de foto (StorageHandler + NetworkManager), restrito apenas a TRIANGULO.
 * OBJETIVO: Ao identificar um triângulo, salva a foto no SD (como antes) e dispara
 *           um broadcast UDP com um registro ADS-B fictício sorteado, já informando
 *           o índice da foto para o computador de bordo puxá-la via TCP.
 **************************************************************************************************/

#include "esp_camera.h"
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>

#include "VisionSystem.h"
#include "StorageHandler.h"
#include "NetworkManager.h"
#include "ADSBSimulator.h"

// --- DEFINIÇÃO DE PINOS XIAO ESP32S3 SENSE (câmera) ---
#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

// --- PINOS SPI PARA O CARTÃO MICROSD ---
#define SD_SCK  7
#define SD_MISO 8
#define SD_MOSI 9
#define SD_CS   21

const unsigned long CAPTURE_INTERVAL_MS = 500;
unsigned long lastCaptureTime = 0;

void setup() {
    Serial.begin(115200);

    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS, SPI)) {
        Serial.println("[ERR] Falha ao montar SD");
    } else {
        Serial.println("[OK] SD montado com sucesso.");
    }

    if (psramInit()) {
        initVisionBuffers();
        Serial.println("[OK] PSRAM Inicializada");
    } else {
        Serial.println("[ERR] PSRAM não encontrada! O sistema pode falhar.");
    }

    // Configuração da Câmera (idêntica ao VANTsat_TX_V3)
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
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
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
        s->set_exposure_ctrl(s, 1);
        s->set_ae_level(s, -2);
        s->set_gain_ctrl(s, 0);
        s->set_agc_gain(s, 0);
        s->set_gainceiling(s, (gainceiling_t)0);
        s->set_brightness(s, -1);
        s->set_contrast(s, 2);
        s->set_sharpness(s, 2);
        s->set_whitebal(s, 1);
        s->set_wb_mode(s, 1);
        s->set_bpc(s, 1);
        s->set_wpc(s, 1);
    }

    resetMission();
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

    // Captura, identifica e (se for TRIANGULO) salva a foto no SD
    String detected = captureAndSave();

    if (detected == "TRIANGULO") {
        ADSBRecord record = getRandomADSB();
        lastADSBRecord = record;
        adsbDataAvailable = true;
        sendADSB(record, lastSavedIndex, lastSavedSize);
    }
}
