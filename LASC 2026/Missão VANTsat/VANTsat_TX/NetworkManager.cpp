#include <WiFi.h>

#include "NetworkManager.h"
#include "StorageHandler.h"
#include "Arduino.h"

// Configurações de IP
const char* ssid = "VANTsat_AP";
const char* password = "password123";
const int port = 8888;

IPAddress local_IP(192, 168, 4, 1);
IPAddress gateway(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

WiFiServer server(port);

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

    server.begin();
    server.setNoDelay(true); 
}

void handleClient() {
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

            if (request == "R") {
                resetMission();
                client.println("DONE:MISSION_RESET");
            } 
            else if (request.startsWith("GET:")) {
                int requestedIndex = request.substring(4).toInt();
                sendImageToClient(client, requestedIndex);
            }
        }
        client.stop();
        Serial.println("[TCP] Cliente desconectado.");
    }
}