#include "NetworkManager.h"
#include "DisplayHandler.h"
#include <Arduino.h>      
#include <WiFi.h>         
#include "esp_wifi.h"

// Variáveis de controle de missão
int nextIndexToRequest = 0;      // CID: 0 a 9
uint32_t lastTotalIndex = 0xFFFFFFFF; // Para verificar se a foto é realmente nova
unsigned long lastProcessTime = 0;
const unsigned long PROCESS_DELAY = 60000; // 1 minuto de simulação de processos

uint8_t fb_buf[MAX_IMG_SIZE];
WiFiClient client;
bool conectedOnce = false;
bool firstConnectionEver = true;

size_t currentImgSize = 0;
uint32_t currentTotalIndex = 0;
float currentMissionTime = 0.0;

// Configurações de Rede
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

    conectedOnce = false;
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
    // 1. Tenta estabelecer a conexão TCP se o WiFi estiver OK
    if(WiFi.status() == WL_CONNECTED && !client.connected()){
        if(client.connect(serverIP, port)){
            client.setNoDelay(true);
            client.setTimeout(4000);
            
            // 2. Lógica de Sincronização de Missão (Executa apenas UMA vez por boot do Heltec)
            if(firstConnectionEver){
                Serial.println("[MASTER] Primeira conexão. Resetando servidor...");
                updateDisplay("MISSAO", "STARTING...");
                
                client.println("R"); // Envia o comando de Reset
                client.flush();
                
                firstConnectionEver = false; // Bloqueia para que reboots do ESP32 não apaguem os dados
                delay(2000); // Latência necessária para o ESP32 limpar o SD e reindexar
            }

            conectedOnce = true;
            updateDisplay("CONECTADO", serverIP);
        }
    }

    // 3. Monitoramento de queda de conexão
    if(conectedOnce && (WiFi.status() != WL_CONNECTED || !client.connected())){
        // Note: firstConnectionEver CONTINUA false aqui. 
        // Se a conexão cair, ele reconecta sem enviar o "R".
        resetRadio();
    }
}

bool receiveImage() {
    monitorConnection();
    if (!client.connected()) return false;

    // 1. Solicitação
    client.printf("GET:%d\n", nextIndexToRequest); 
    client.flush();

    // 2. Timeout e Handshake
    unsigned long startMs = millis();
    while (!client.available()) {
        if ((millis() - startMs) > 5000) {
            updateDisplay("TIMEOUT", "Tentando Reconectar");
            Serial.println("[ERR] Servidor nao respondeu.");
            client.stop(); 
            conectedOnce = false; 
            return false; 
        }
        yield();
    }
    
    String header = client.readStringUntil('\n');
    if (!header.startsWith("START:")) return false;

    // 3. Parsing do Header
    int partsFound = 0;
    String parts[5];
    int lastPos = 0;
    for (int i = 0; i < header.length() && partsFound < 5; i++) {
        if (header[i] == ':' || i == header.length() - 1) {
            parts[partsFound++] = header.substring(lastPos, (i == header.length() - 1) ? i + 1 : i);
            lastPos = i + 1;
        }
    }

    if (partsFound < 5) return false;

    // Atribuição a variáveis globais ou membros de classe para uso posterior
    currentImgSize = (size_t)parts[2].toInt();
    currentTotalIndex = (uint32_t)parts[3].toInt();
    currentMissionTime = parts[4].toFloat();

    // 4. Lógica de "Foto Nova"
    if (currentTotalIndex == lastTotalIndex && lastTotalIndex != 0xFFFFFFFF) {
        updateDisplay("AGUARDANDO", "Nova captura...");
        return false;
    }

    // 5. Download Binário
    size_t totalRead = 0;
    unsigned long downloadStart = millis();
    while (totalRead < currentImgSize && (millis() - downloadStart < 10000)) {
        if (client.available()) {
            size_t n = client.read(fb_buf + totalRead, min((size_t)client.available(), currentImgSize - totalRead));
            totalRead += n;
        }
        yield();
    }

    // 6. Validação de Integridade
    if (totalRead == currentImgSize) {
        client.readStringUntil('\n'); // Consome terminador

        lastTotalIndex = currentTotalIndex;
        
        // Feedback Visual
        String linha1 = "ID:" + String(nextIndexToRequest) + " TOT:" + String(currentTotalIndex);
        String linha2 = "T:" + String(currentMissionTime, 2) + "s";
        updateDisplay(linha1.c_str(), linha2.c_str());
        Serial.printf("[MISSAO] %s | %s\n", linha1.c_str(), linha2.c_str());

        // Incremento do índice circular
        nextIndexToRequest = (nextIndexToRequest + 1) % 10;
        
        return true; // Sucesso absoluto no download
    } else {
        Serial.println("[ERRO] Download incompleto.");
        client.stop();
        return false;
    }
}