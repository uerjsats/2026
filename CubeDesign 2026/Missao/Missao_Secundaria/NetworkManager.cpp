/**************************************************************************************************
 * MÓDULO: NetworkManager
 * OBJETIVO: Reaproveita a camada de conectividade WiFi original (Access Point) e o
 *           servidor TCP de envio de foto (igual ao VANTsat_TX_V3), e adiciona um
 *           broadcast UDP com o registro ADS-B fictício sempre que um triângulo é
 *           identificado. O broadcast já informa o índice da foto salva no SD para
 *           o computador de bordo puxá-la via TCP (GET:<index>).
 **************************************************************************************************/
#include <WiFi.h>
#include <WiFiUdp.h>

#include "NetworkManager.h"
#include "StorageHandler.h"

// Configuração de rede reaproveitada do NetworkManager original
const char* ssid = "VANTsat_AP";
const char* password = "uerjsats123";
const int adsbPort = 4444;
const int imagePort = 8888;

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress broadcastIP(192, 168, 4, 255);

WiFiUDP udp;
WiFiServer imageServer(imagePort);

void setupWiFi() {
    Serial.println("\n[WIFI] Iniciando Access Point...");
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAPConfig(local_IP, gateway, subnet)) {
        Serial.println("[ERR] Falha no IP Estático");
    }
    if (WiFi.softAP(ssid, password)) {
        Serial.println("[OK] AP Pronto!");
        Serial.print("[WIFI] IP: "); Serial.println(WiFi.softAPIP());
    }

    udp.begin(adsbPort);
    Serial.printf("[UDP] Broadcast ADS-B pronto na porta %d\n", adsbPort);

    imageServer.begin();
    imageServer.setNoDelay(true);
    Serial.printf("[TCP] Servidor de fotos pronto na porta %d\n", imagePort);
}

void sendADSB(const ADSBRecord &record, int photoIndex, size_t photoSize) {
    String payload = buildADSBPayload(record, photoIndex, photoSize);

    udp.beginPacket(broadcastIP, adsbPort);
    udp.print(payload);
    udp.endPacket();

    Serial.println("[ADS-B] Enviado: " + payload);
}

void handleClient() {
    WiFiClient client = imageServer.available();
    if (!client) return;

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
