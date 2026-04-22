#include "NetworkManager.h"
#include "NetworkManager.h"
#include <Arduino.h>      
#include <WiFi.h>         
#include "esp_wifi.h"
#include "DisplayHandler.h"

uint8_t fb_buf[MAX_IMG_SIZE];
WiFiClient client;
bool conectouUmaVez = false;

const char* ssid = "VANTsat_AP";
const char* password = "password123";
const char*serverIP = "192.168.4.1";
const uint16_t port = 8888;

void setupNetwork() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    
    Serial.print("Conectando ao WiFi");
    unsigned long startAttemptTime = millis();
    
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
        delay(500);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        updateDisplay("WiFi: Conectado", WiFi.localIP().toString().c_str());
        Serial.println("\nConectado com sucesso.");
    } else {
        updateDisplay("WiFi: Erro", "Timeout");
        Serial.println("\nFalha na conexão.");
    }
}

void resetRadio(){
    Serial.println("\n[ACTION] Resetando Radio e Buffers TCP...");
    updateDisplay("WIFI RESET:", "LIMPANDO BUFFERS");

    conectouUmaVez = false;
    client.stop();

    esp_wifi_stop();
    esp_wifi_deinit();
    delay(1500);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    WiFi.begin(ssid, password);
    Serial.println("[WIFI] Radio reiniciado como novo cliente.");
}

void monitorConnection(){
    if(WiFi.status() == WL_CONNECTED && !client.connected() && !conectouUmaVez){
        if(client.connect(serverIP, port)){
            client.setNoDelay(true);
            client.setTimeout(4000);
            conectouUmaVez = true;
            updateDisplay("CONECTADO", serverIP);
        }
    }
    if(conectouUmaVez &&(WiFi.status() != WL_CONNECTED || !client.connected())){
        resetRadio();
    }
}

void receiveImage(){
    monitorConnection();
    if (!client.connected()) return;
    
    while (client.available() > 0) client.read();

    client.write(0x01);
    client.flush();
    unsigned long startMs = millis();
    while (client.available() < 8){
        if ((millis() - startMs) > 3000){
            updateDisplay("FILA VAZIA", "Aguardando...");
            return;
        }
        yield();
    }
    uint32_t imgSize = 0;
    uint32_t imgID = 0;
    client.readBytes((uint8_t*)&imgSize, 4);
    client.readBytes((uint8_t*)&imgID, 4);

    if (imgSize == 0 || imgSize > MAX_IMG_SIZE){
        if (imgSize > MAX_IMG_SIZE) client.stop();
        return;
    }
    size_t totalRead = 0;
    unsigned long downloadStart = millis();
    while (totalRead < imgSize && (millis() - downloadStart < 6000)) { 
        if (client.available()) { 
            size_t chunk = client.readBytes(fb_buf + totalRead, imgSize - totalRead);
            totalRead += chunk;
        }
        yield(); 
    }
    if (totalRead == imgSize && fb_buf[0] == 0xFF && fb_buf[1]==0xD8){
        updateDisplay("Sucesso", "ID: "+String(imgID));
        Serial.printf("[OK] Recebido ID: %u\n", imgID);
    }else{
        client.stop();
    }
}
