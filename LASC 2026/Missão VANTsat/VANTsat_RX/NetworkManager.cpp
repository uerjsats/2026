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

                updateDisplay("MISSAO", "STARTING...");
                
                client.println("R"); // Envia o comando de Reset
                client.flush();
                
                firstConnectionEver = false; // Bloqueia para que reboots do ESP32 não apaguem os dados
                delay(5000); // Latência necessária para o ESP32 limpar o SD e reindexar
                Serial.println("[MASTER] Primeira conexão. Resetando servidor...");
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

    // 1. Solicitação pelo índice físico do anel
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
    if (!header.startsWith("START:")) {
        Serial.println("[ERR] Header invalido. Fechando conexao.");
        client.stop();
        return false;
    }

    // 3. Parsing do Header
    int partsFound = 0;
    String parts[6];
    int lastPos = 0;
    for (int i = 0; i < header.length() && partsFound < 6; i++) {
        if (header[i] == ':' || i == header.length() - 1) {
            parts[partsFound++] = header.substring(lastPos, (i == header.length() - 1) ? i + 1 : i);
            lastPos = i + 1;
        }
    }

    if (partsFound < 6) {
        client.stop();
        return false;
    }

    // Extração dos dados
    String currentTipoFigura = parts[1];
    currentImgSize = (size_t)parts[3].toInt();
    currentTotalIndex = (uint32_t)parts[4].toInt();
    currentMissionTime = parts[5].toFloat();

    // 4. Lógica de "Foto Nova" com Prevenção de Underflow
    bool isNewImage = (currentTotalIndex > lastTotalIndex) || (lastTotalIndex == 0xFFFFFFFF);

    // Validação estrita de Hard Reset: garante que lastTotalIndex seja grande o suficiente 
    // antes de realizar a subtração, prevenindo underflow.
    if (lastTotalIndex != 0xFFFFFFFF && lastTotalIndex >= 100) {
        if (currentTotalIndex < (lastTotalIndex - 100)) {
            isNewImage = true; 
        }
    }

    if (!isNewImage) {
        updateDisplay("AGUARDANDO", "Captura em andamento");
        
        // CORTA O FLUXO: Aborta o download imediatamente.
        // Força a camada TCP do servidor ESP a descartar o payload não lido.
        client.stop(); 
        
        // Retorna sem incrementar nextIndexToRequest, forçando o cliente a 
        // realizar um novo polling no mesmo slot até que a câmera atualize o buffer.
        return false;
    }

    // --- PROTOCOLO SERIAL: DEFINIÇÃO DE FRAMING ---
    const uint8_t SYNC_START[] = {0xAA, 0xBB, 0xCC, 0xDD};
    const uint8_t SYNC_END[]   = {0xEE, 0xFF};

    // 5. Envio do LOG via Serial
    String logPayload = "TIPO:" + currentTipoFigura + 
                        ", ID:" + String(nextIndexToRequest) + 
                        ", TOT:" + String(currentTotalIndex) + 
                        ", T:" + String(currentMissionTime, 3);
    
    uint32_t logSize = logPayload.length();
    
    Serial.write(SYNC_START, 4);
    Serial.write(0x01);
    Serial.write((uint8_t*)&logSize, 4);
    Serial.print(logPayload);
    Serial.write(SYNC_END, 2);

    // 6. Download Binário e Repasse Imediato via Serial
    Serial.write(SYNC_START, 4);
    Serial.write(0x02);
    uint32_t imgSize32 = (uint32_t)currentImgSize;
    Serial.write((uint8_t*)&imgSize32, 4);

    size_t totalRead = 0;
    uint8_t buffer[1024]; 
    unsigned long downloadStart = millis();
    
    while (totalRead < currentImgSize && (millis() - downloadStart < 10000)) {
        if (client.available()) {
            size_t bytesToRead = min((size_t)client.available(), currentImgSize - totalRead);
            if (bytesToRead > sizeof(buffer)) bytesToRead = sizeof(buffer);
            
            size_t n = client.read(buffer, bytesToRead);
            Serial.write(buffer, n); 
            
            totalRead += n;
            downloadStart = millis(); 
        }
        yield(); 
    }

    Serial.write(SYNC_END, 2);

    // 7. Validação de Integridade e Avanço de Ponteiro
    if (totalRead == currentImgSize) {
        client.readStringUntil('\n'); // Consome terminador END_FRAME

        lastTotalIndex = currentTotalIndex; // Sincroniza índice global absoluto
        
        String linha1 = "ID:" + String(nextIndexToRequest) + " TOT:" + String(currentTotalIndex);
        String linha2 = "T:" + String(currentMissionTime, 2) + "s";
        updateDisplay(linha1.c_str(), linha2.c_str());

        // Apenas avança o ponteiro de leitura se o ciclo do slot atual for concluído com sucesso
        nextIndexToRequest = (nextIndexToRequest + 1) % 10;
        
        return true; 
    } else {
        Serial.println("\n[ERRO] Download incompleto.");
        client.stop();
        return false;
    }
}