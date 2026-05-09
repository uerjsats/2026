#include "wifi.h"

static const char* ssid = "VANTsat_AP";
static const char* password = "password123";
static const char* serverIP = "192.168.4.1";
static const uint16_t port = 8888;

#define MAX_IMG_SIZE 80000

WiFiClient client;

typedef enum {
    WIFI_DISABLED,
    WIFI_IDLE,
    WIFI_CONNECTING,
    WIFI_RUNNING,
    WIFI_ERROR
} wifi_state_t;

static wifi_state_t state = WIFI_IDLE;

static uint8_t fb_buf[MAX_IMG_SIZE];
static uint32_t imgSize = 0;
static uint32_t imgID = 0;
static size_t totalRead = 0;

static bool imageReady = false;
static bool hasLink = false;

static unsigned long lastAttempt = 0;
static uint8_t retryCount = 0;

void wifiInit()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    Serial.println("[WiFi] Init feito (não bloqueante)");
}

void wifiDisable()
{
    state = WIFI_DISABLED;
    client.stop();
    hasLink = false;
}

void wifiEnable()
{
    if (state == WIFI_DISABLED)
    {
        state = WIFI_IDLE;
        retryCount = 0;
    }
}

bool wifiHasLink()
{
    return hasLink;
}

void wifiLoop()
{
    if (state == WIFI_DISABLED)
        return;

    // tenta conexão ESP32 WiFi (leve, não bloqueante)
    if (WiFi.status() != WL_CONNECTED)
    {
        hasLink = false;
        client.stop();
        return;
    }

    //tenta conectar TCP SOMENTE em janela controlada
    if (!client.connected() && (millis() - lastAttempt > (2000 + retryCount * 1000)))
    {
        lastAttempt = millis();

        if (client.connect(serverIP, port))
        {
            client.setNoDelay(true);
            client.setTimeout(2000);

            state = WIFI_RUNNING;
            retryCount = 0;
            hasLink = true;

            Serial.println("[WiFi] Servidor conectado");
        }
        else
        {
            retryCount++;

            if (retryCount > 10)
            {
                state = WIFI_ERROR;
            }
            else
            {
                state = WIFI_CONNECTING;
            }
        }
    }
}

void wifiStep()
{
    wifiLoop();
    if (state == WIFI_DISABLED || !client.connected()) return;

    switch (state)
    {
        case WIFI_RUNNING:
            // Envia Comando 0x01 + Índice 0 (ou o índice que você quiser)
            uint8_t req[2];
            req[0] = 0x01; 
            req[1] = 0x00; // Pede a foto do index 0
            client.write(req, 2);
            client.flush();
            
            state = WIFI_IDLE; // Muda para aguardar o Header
            break;

        case WIFI_IDLE:
            // O Header do Transmissor tem 12 bytes (Size, ID, Timestamp)
            if (client.available() >= 12) 
            {
                uint32_t header[3];
                client.readBytes((uint8_t*)header, 12);
                
                imgSize = header[0];
                imgID   = header[1];
                // header[2] é o timestamp, podemos ignorar ou salvar
                
                totalRead = 0;
                imageReady = false;
                
                if (imgSize > 0 && imgSize < MAX_IMG_SIZE) {
                    state = WIFI_CONNECTING; // Agora sim, vai ler a imagem
                } else {
                    state = WIFI_RUNNING; // Erro ou vazio, tenta de novo
                }
            }
            break;

        case WIFI_CONNECTING:
            // Leitura incremental da imagem
            if (client.available() && totalRead < imgSize)
            {
                size_t toRead = min((size_t)client.available(), (size_t)(imgSize - totalRead));
                totalRead += client.readBytes(fb_buf + totalRead, toRead);

                if (totalRead >= imgSize)
                {
                    imageReady = true;
                    state = WIFI_RUNNING; // Volta para o início para a próxima foto
                }
            }
            break;

        case WIFI_ERROR:
            client.stop();
            hasLink = false;
            state = WIFI_IDLE;
            break;
    }
}

bool wifiIsConnected()
{
    return (WiFi.status() == WL_CONNECTED && client.connected());
}

bool wifiImageReady()
{
    return imageReady;
}

uint8_t* wifiGetBuffer()
{
    return fb_buf;
}

uint32_t wifiGetImageSize()
{
    return imgSize;
}

uint32_t wifiGetImageID()
{
    return imgID;
}

void wifiConsumeImage()
{
    imageReady = false;
}