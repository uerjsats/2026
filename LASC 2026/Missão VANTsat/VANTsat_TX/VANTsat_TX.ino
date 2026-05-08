#include "esp_camera.h"
#include "FS.h"
#include "SD_MMC.h"
#include <WiFi.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "VisionSystem.h"
#include "NetworkManager.h"
#include "SerialBridge.h"
#include "StorageHandler.h"

// --- DEFINIÇÃO DE PINOS AI-THINKER (PADRÃO ESP32-CAM) ---
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);

    if(!SD_MMC.begin("/sdcard", true)) { 
        Serial.println("[ERR] Falha ao montar SD");
    } else {
        Serial.println("[OK] SD montado com sucesso.");
    }
    
    // 1. Inicializa a PSRAM
    if (psramInit()) {
        initVisionBuffers(); // Aloca o array de contornos na PSRAM
        Serial.println("[OK] PSRAM Inicializada");
    } else {
        Serial.println("[ERR] PSRAM não encontrada! O sistema pode falhar.");
    }
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW); // Mantém o Flash apagado
    
    // 2. Configuração da Câmera
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
    config.xclk_freq_hz = 10000000;
    config.pixel_format = PIXFORMAT_GRAYSCALE;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;

    esp_err_t err = esp_camera_init(&config);
    sensor_t * s = esp_camera_sensor_get();
    if (s) {
        // --- AJUSTES PARA AMBIENTES COM MUITO BRILHO ---
        
        s->set_exposure_ctrl(s, 1);      // 1 = Mantém automático para se adaptar
        s->set_ae_level(s, -2);          // FORÇA a exposição para o mínimo (-2 a 2)
        
        s->set_gain_ctrl(s, 0);          // DESLIGA o ganho automático 
        s->set_agc_gain(s, 0);           // Define o ganho no mínimo manual (0 a 30)
        s->set_gainceiling(s, (gainceiling_t)0); // Garante que o teto do ganho seja o menor

        // --- AJUSTES DE IMAGEM PARA RECONHECIMENTO ---
        
        s->set_brightness(s, -1);        // Reduz o brilho base
        s->set_contrast(s, 2);           // Aumenta o contraste
        s->set_sharpness(s, 2);          // Mantém as bordas nítidas
        
        // Ajuste de Branco (WB)
        s->set_whitebal(s, 1);           // Ativa Balanço de Branco
        s->set_wb_mode(s, 1);            // 1 = Modo ensolarado (Sunny)
        
        s->set_bpc(s, 1);                // Correção de pixels pretos
        s->set_wpc(s, 1);                // Correção de pixels brancos (ajuda com reflexos)
    }    
    // 5. Inicializa o Access Point WiFi
    setupWiFi();
    // 6. Configura o tempo inicial da missão
    missionStartTime = millis();
    Serial.println("\n--- VANTsat TX: ONLINE ---");
}

void loop() {
    handleClient();
    handleSerialCommands();
    yield(); 
}
