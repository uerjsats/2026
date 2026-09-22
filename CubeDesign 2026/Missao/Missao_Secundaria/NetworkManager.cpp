/************************************************************************************************** 
 __     ___    _   _ _____          _             _   _ _____ ____     _           _       
 \ \   / / \  | \ | |_   _|__  __ _| |_          | | | | ____|  _ \   | |___  __ _| |_ ___ 
  \ \ / / _ \ |  \| | | |/ __|/ _` | __|  _____  | | | |  _| | |_) |  | / __|/ _` | __/ __|
   \ V / ___ \| |\  | | |\__ \ (_| | |_  |_____| | |_| | |___|  _ < |_| \__ \ (_| | |_\__ \
    \_/_/   \_\_| \_| |_||___/\__,_|\__|          \___/|_____|_| \_\___/|___/\__,_|\__|___/
                                                                                               
 **************************************************************************************************
 * PROJETO: VANTsat - Equipe UERJsats: Missão Atlas LASC 2026
 * DESENVOLVEDORES: Carlos Leal e Vitor Forny
 * GITHUB: https://github.com/uerjsats https://github.com/Caduleal https://github.com/vitorforny04
 * OBJETIVO:  Sistema de reconhecimento computacional de imagens focado na detecção 
 *          e classificação autônoma de formas geométricas (triângulos e quadrados).
 * CONTEXTO: Gerenciador da camada de conectividade e telemetria. Responsável por 
 *           estabelecer o enlace de comunicação (transmissão de dados de 
 *           reconhecimento para a estação base). Atua em integração 
 *           direta com o `StorageHandler` para garantir a persistência local das 
 *           imagens e parâmetros da missão.
 **************************************************************************************************/

#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <SD.h>
#include <DNSServer.h>

#include "NetworkManager.h"
#include "StorageHandler.h"
#include "Arduino.h"

const char* ssid = "AMARAL_I";
const char* password = "uerjsats123";
const int port = 8888;
const int adsbPort = 4444;

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress broadcastIP(192, 168, 4, 255);

WiFiServer server(port);
WebServer webServer(80);
WiFiUDP udp;

const byte DNS_PORT = 53;
DNSServer dnsServer;

#define LOG_FILE "/missao/data.txt"

void handleRoot() {
    File root = SD.open("/missao");
    if (!root) {
        webServer.send(500, "text/plain", "Erro: Falha ao abrir o diretório raiz do SD Card.");
        return;
    }

    String telemetryData = "";
    File dataFile = SD.open(LOG_FILE, FILE_READ);
    if (dataFile) {
        telemetryData = dataFile.readString();
        dataFile.close();
    }

    String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\">";
    html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">";
    html += "<title>Missão Maverick - Resultados</title>";

    // Página 100% CSS, sem depender de nenhuma imagem de logo/fundo no SD
    html += "<style>";
    html += "body { font-family: sans-serif; background: #121212; color: #fff; margin: 0; padding: 20px; display: flex; flex-direction: column; align-items: center; }";

    html += ".header-container { margin-bottom: 40px; width: 100%; max-width: 1200px; text-align: center; }";

    html += ".title-box { background: rgba(30,30,30,0.85); border: 1px solid #333; border-radius: 8px; padding: 15px 30px; margin: 0 auto; width: fit-content; text-align: center; color: #00ffcc; font-family: 'Consolas', 'Monaco', 'Courier New', monospace; font-size: 28px; font-weight: 900; text-transform: uppercase; letter-spacing: 2px; text-shadow: 0 0 12px rgba(0, 255, 204, 0.9); }";

    html += ".gallery { display: grid; grid-template-columns: repeat(4, fit-content(100%)); justify-content: center; gap: 20px; margin-bottom: 60px; max-width: 95%; box-sizing: border-box; }";

    html += ".card { background: rgba(30,30,30,0.85); border: 1px solid rgba(0, 255, 204, 0.25); border-radius: 5px; display: flex; flex-direction: column; width: 260px; overflow: hidden; box-shadow: 0 4px 12px rgba(0, 255, 204, 0.1); transition: transform 0.3s ease, box-shadow 0.3s ease, border-color 0.3s ease; }";
    html += ".card:hover { transform: translateY(-12px); box-shadow: 0 16px 32px rgba(0, 255, 204, 0.4); border-color: rgba(0, 255, 204, 0.8); cursor: pointer; }";

    html += ".card-title { margin: 5px 10px; font-size: 11px; color: #aaa; text-align: center; font-family: 'Consolas', monospace; }";
    html += ".card img { width: 100%; height: auto; max-height: 200px; object-fit: cover; border-radius: 5px 5px 0 0; display: block; margin: 0 auto; }";
    html += ".telemetry-box { background: rgba(0,0,0,0.8); color: #00ffcc; padding: 8px; font-family: 'Consolas', monospace; font-size: 11px; border-radius: 0 0 5px 5px; word-break: break-all; border-top: 1px solid #333; text-align: center; }";
    html += ".telemetry-box.adsb { color: #ffb454; }";
    html += ".section-title { color: #aaa; font-family: 'Consolas', monospace; text-transform: uppercase; letter-spacing: 1px; font-size: 14px; margin: 10px 0 20px; text-align: center; }";

    html += "@media (max-width: 768px) {";
    html += "  .title-box { font-size: 18px; padding: 12px 20px; width: 90%; }";
    html += "  .gallery { grid-template-columns: 1fr; max-width: 100%; }";
    html += "  .card { width: 100%; max-width: 320px; }";
    html += "  .card img { max-height: none; }";
    html += "}";
    html += "</style>";

    html += "</head><body>";

    // Cabeçalho (sem logos: nenhum arquivo de imagem obrigatório no SD)
    html += "<header class='header-container'>";
    html += "<h2 class='title-box'>Missao Maverick - Imagens e Dados</h2>";
    html += "</header>";

    html += "<div class=\"gallery\">";

    File file = root.openNextFile();
    bool foundImages = false;

    while (file) {
        if (!file.isDirectory()) {
            String fileName = String(file.name());
            
            if (fileName.startsWith("/")) {
                fileName = fileName.substring(1);
            }

            String lowerName = fileName;
            lowerName.toLowerCase();

            if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg") || lowerName.endsWith(".png")) {

                foundImages = true;
                
                String baseName = fileName;
                baseName.replace(".jpg", "");
                baseName.replace(".jpeg", "");
                baseName.replace(".png", "");

                String searchTag = "CID:" + baseName + ",";
                String imageInfo = "Telemetria não encontrada.";
                
                if (telemetryData.length() > 0) {
                    int startIndex = telemetryData.indexOf(searchTag);
                    if (startIndex != -1) {
                        int lineStart = telemetryData.lastIndexOf('\n', startIndex);
                        if (lineStart == -1) lineStart = 0; else lineStart++;
                        
                        int lineEnd = telemetryData.indexOf('\n', startIndex);
                        if (lineEnd == -1) lineEnd = telemetryData.length();
                        
                        imageInfo = telemetryData.substring(lineStart, lineEnd);
                        imageInfo.trim();
                    }
                }

                String fullPath = "/missao/" + fileName;

                html += "<div class='card'>";
                html += "<p class='card-title'>/" + fileName + "</p>";
                html += "<a href='/download?file=" + fullPath + "' target='_blank'>";
                html += "<img src='/download?file=" + fullPath + "' alt='" + fileName + "'>";
                html += "</a>";
                html += "<div class='telemetry-box'>" + imageInfo + "</div>";
                html += "</div>";
            }
        }
        file = root.openNextFile();
    }

    if (!foundImages) {
        html += "<p>Nenhuma imagem encontrada no diretório raiz.</p>";
    }

    html += "</div>";

    // --- Fotos já enviadas via TCP nesta sessão, com os dados de ADS-B associados ---
    html += "<h3 class='section-title'>Fotos Enviadas (" + currentMissionDir + ") — Dados ADS-B</h3>";
    html += "<div class=\"gallery\">";

    bool foundSent = false;

    if (currentMissionDir.length() > 0) {
        String adsbData = "";
        File adsbFile = SD.open((currentMissionDir + "/adsb.txt").c_str(), FILE_READ);
        if (adsbFile) {
            adsbData = adsbFile.readString();
            adsbFile.close();
        }

        File sentDir = SD.open(currentMissionDir);
        if (sentDir && sentDir.isDirectory()) {
            File sfile = sentDir.openNextFile();
            while (sfile) {
                if (!sfile.isDirectory()) {
                    String fileName = String(sfile.name());
                    if (fileName.startsWith("/")) {
                        fileName = fileName.substring(fileName.lastIndexOf('/') + 1);
                    }
                    String lowerName = fileName;
                    lowerName.toLowerCase();

                    if (lowerName.endsWith(".jpg") || lowerName.endsWith(".jpeg")) {
                        foundSent = true;

                        String baseName = fileName;
                        baseName.replace(".jpg", "");
                        baseName.replace(".jpeg", "");

                        String searchTag = "CID:" + baseName + ",";
                        String adsbInfo = "ADS-B não encontrado para esta foto.";
                        if (adsbData.length() > 0) {
                            int startIndex = adsbData.indexOf(searchTag);
                            if (startIndex != -1) {
                                int lineEnd = adsbData.indexOf('\n', startIndex);
                                if (lineEnd == -1) lineEnd = adsbData.length();
                                adsbInfo = adsbData.substring(startIndex, lineEnd);
                                adsbInfo.trim();
                            }
                        }

                        String fullPath = currentMissionDir + "/" + fileName;

                        html += "<div class='card'>";
                        html += "<p class='card-title'>" + fullPath + "</p>";
                        html += "<a href='/download?file=" + fullPath + "' target='_blank'>";
                        html += "<img src='/download?file=" + fullPath + "' alt='" + fileName + "'>";
                        html += "</a>";
                        html += "<div class='telemetry-box adsb'>" + adsbInfo + "</div>";
                        html += "</div>";
                    }
                }
                sfile = sentDir.openNextFile();
            }
            sentDir.close();
        }
    }

    if (!foundSent) {
        html += "<p>Nenhuma foto enviada ainda nesta sessão.</p>";
    }

    html += "</div>";

    html += "</body></html>";

    root.close();
    webServer.send(200, "text/html", html);
}
void handleDownload() {
    if (webServer.hasArg("file")) {
        String path = webServer.arg("file");
        File file = SD.open(path, FILE_READ);
        if (!file) {
            String upperPath = path;
            upperPath.toUpperCase();
            file = SD.open(upperPath, FILE_READ);
        }
        if (!file) {
            Serial.printf("[HTTP ERRO FATAL] Arquivo ausente na camada física: %s\n", path.c_str());
            webServer.send(404, "text/plain", "Erro 404: Arquivo não encontrado.");
            return;
        }
        String mimeType = "image/jpeg";
        String lowerPath = path;
        lowerPath.toLowerCase();
        
        if (lowerPath.endsWith(".png")) {
            mimeType = "image/png"; // Ajuste do cabeçalho HTTP para os PNGs
        } else if (lowerPath.endsWith(".gif")) {
            mimeType = "image/gif";
        }
        webServer.streamFile(file, mimeType);
        file.close();
    } else {
        webServer.send(400, "text/plain", "Erro 400: Parâmetro 'file' ausente.");
    }
}

void setupWiFi() {
    Serial.println("\n[WIFI] Iniciando Access Point...");
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
        Serial.println("[ERR] Falha no IP Estático");
    }
    if (WiFi.softAP(ssid, password, 1, 0, 4)) {
        Serial.println("[OK] AP Pronto!");
        Serial.print("[WIFI] IP: "); Serial.println(WiFi.softAPIP());
    }

    dnsServer.start(DNS_PORT, "www.missao.maverick.com", local_IP); 
    Serial.println("[DNS] Servidor DNS iniciado em missao.maverick");

    server.begin();
    server.setNoDelay(true);

    udp.begin(adsbPort);
    Serial.printf("[UDP] Broadcast ADS-B pronto na porta %d\n", adsbPort);

    webServer.on("/", HTTP_GET, handleRoot);
    webServer.on("/download", HTTP_GET, handleDownload);
    
    webServer.begin();
    Serial.println("[WEB] Servidor HTTP de visualização iniciado.");
}

void sendADSB(const ADSBRecord &record, int photoIndex, size_t photoSize) {
    String payload = buildADSBPayload(record, photoIndex, photoSize);

    udp.beginPacket(broadcastIP, adsbPort);
    udp.print(payload);
    udp.endPacket();

    Serial.println("[ADS-B] Enviado: " + payload);
}

void handleClient() {
    dnsServer.processNextRequest();
    webServer.handleClient();
    WiFiClient client = server.available();
    if (client) {
        Serial.println("\n[TCP] Cliente conectado.");
        unsigned long timeout = millis();
        while (!client.available() && millis() - timeout < 2000) {
            yield();
        }
        if (client.available()) {
            String request = client.readStringUntil('\n');
            request.trim();
           if (request.startsWith("GET:")) {
                int requestedIndex = request.substring(4).toInt();
                sendImageToClient(client, requestedIndex); 
            }
        }
        client.stop();
        Serial.println("[TCP] Cliente desconectado.");
    }
}
