#include "NetworkManager.h"
#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"

// ======================================
// CONTROLE DE MISSÃO
// ======================================

int nextIndexToRequest = 0;
uint32_t lastTotalIndex = 0xFFFFFFFF;

unsigned long lastProcessTime = 0;
const unsigned long PROCESS_DELAY = 60000;

uint8_t fb_buf[MAX_IMG_SIZE];

WiFiClient client;

bool conectedOnce = false;
bool firstConnectionEver = true;

size_t currentImgSize = 0;
uint32_t currentTotalIndex = 0;
float currentMissionTime = 0.0;

// ======================================
// REDE
// ======================================

const char* ssid = "VANTsat_AP";
const char* password = "password123";
const char* serverIP = "192.168.4.1";
const uint16_t port = 8888;

// ======================================
// WIFI
// ======================================

void setupNetwork()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    Serial.print("Conectando ao WiFi");

    unsigned long startAttemptTime = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - startAttemptTime < 15000
    )
    {
        Serial.print(".");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\nConectado com sucesso.");
    }
    else
    {
        Serial.println("\nFalha na conexão.");
    }
}

// ======================================
// RESET RADIO
// ======================================

void resetRadio()
{
    Serial.println(
        "\n[ACTION] Resetando Radio e Buffers TCP..."
    );

    conectedOnce = false;

    client.stop();

    esp_wifi_stop();
    esp_wifi_deinit();

    vTaskDelay(pdMS_TO_TICKS(1500));

    wifi_init_config_t cfg =
        WIFI_INIT_CONFIG_DEFAULT();

    esp_wifi_init(&cfg);

    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    WiFi.begin(ssid, password);

    Serial.println(
        "[WIFI] Radio reiniciado como novo cliente."
    );
}

// ======================================
// MONITORAMENTO
// ======================================

void monitorConnection()
{
    // reconecta TCP
    if (
        WiFi.status() == WL_CONNECTED &&
        !client.connected()
    )
    {
        if (client.connect(serverIP, port))
        {
            client.setNoDelay(true);
            client.setTimeout(4000);

            // ======================================
            // RESET DE MISSÃO (MESMO FLUXO)
            // ======================================

            if (firstConnectionEver)
            {
                client.println("R");
                client.flush();

                firstConnectionEver = false;

                Serial.println(
                    "[MASTER] Primeira conexão. Resetando servidor..."
                );

                // espera sem travar watchdog
                unsigned long startWait =
                    millis();

                while (
                    millis() - startWait < 5000
                )
                {
                    vTaskDelay(
                        pdMS_TO_TICKS(50)
                    );
                }
            }

            conectedOnce = true;
        }
    }

    // queda de conexão
    if (
        conectedOnce &&
        (
            WiFi.status() != WL_CONNECTED ||
            !client.connected()
        )
    )
    {
        resetRadio();
    }
}

// ======================================
// RECEIVE IMAGE
// ======================================

bool receiveImage()
{
    monitorConnection();

    if (!client.connected())
    {
        return false;
    }

    // ======================================
    // REQUEST
    // ======================================

    client.printf(
        "GET:%d\n",
        nextIndexToRequest
    );

    client.flush();

    // ======================================
    // TIMEOUT HEADER
    // ======================================

    unsigned long startMs = millis();

    while (!client.available())
    {
        if (
            millis() - startMs > 5000
        )
        {
            Serial.println(
                "[ERR] Servidor nao respondeu."
            );

            client.stop();
            conectedOnce = false;

            return false;
        }

        // FIX WATCHDOG
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // ======================================
    // HEADER
    // ======================================

    String header =
        client.readStringUntil('\n');

    if (!header.startsWith("START:"))
    {
        Serial.println(
            "[ERR] Header invalido."
        );

        client.stop();

        return false;
    }

    // ======================================
    // PARSER
    // ======================================

    int partsFound = 0;
    String parts[6];

    int lastPos = 0;

    for (
        int i = 0;
        i < header.length() &&
        partsFound < 6;
        i++
    )
    {
        if (
            header[i] == ':' ||
            i == header.length() - 1
        )
        {
            parts[partsFound++] =
                header.substring(
                    lastPos,
                    (
                        i ==
                        header.length() - 1
                    )
                    ? i + 1
                    : i
                );

            lastPos = i + 1;
        }
    }

    if (partsFound < 6)
    {
        client.stop();
        return false;
    }

    String currentTipoFigura =
        parts[1];

    currentImgSize =
        (size_t)parts[3].toInt();

    currentTotalIndex =
        (uint32_t)parts[4].toInt();

    currentMissionTime =
        parts[5].toFloat();

    // ======================================
    // FOTO NOVA
    // ======================================

    bool isNewImage =
        (
            currentTotalIndex >
            lastTotalIndex
        )
        ||
        (
            lastTotalIndex ==
            0xFFFFFFFF
        );

    if (
        lastTotalIndex !=
        0xFFFFFFFF &&
        lastTotalIndex >= 100
    )
    {
        if (
            currentTotalIndex <
            (
                lastTotalIndex - 100
            )
        )
        {
            isNewImage = true;
        }
    }

    // ======================================
    // IMAGEM REPETIDA
    // CONSOME PAYLOAD
    // ======================================

    if (!isNewImage)
    {
        uint8_t discard[512];

        size_t totalDiscarded = 0;

        unsigned long timeout =
            millis();

        while (
            totalDiscarded <
            currentImgSize &&
            millis() - timeout <
            5000
        )
        {
            if (client.available())
            {
                size_t toRead =
                    min(
                        (
                            size_t
                        )
                        client.available(),
                        sizeof(discard)
                    );

                size_t n =
                    client.read(
                        discard,
                        toRead
                    );

                totalDiscarded += n;

                timeout = millis();
            }

            // FIX WATCHDOG
            vTaskDelay(
                pdMS_TO_TICKS(1)
            );
        }

        client.readStringUntil('\n');

        return false;
    }

    // ======================================
    // SERIAL PROTOCOL
    // ======================================

    const uint8_t
    SYNC_START[] =
    {
        0xAA,
        0xBB,
        0xCC,
        0xDD
    };

    const uint8_t
    SYNC_END[] =
    {
        0xEE,
        0xFF
    };

    // ======================================
    // LOG SERIAL
    // ======================================

    String logPayload =
        "TIPO:" +
        currentTipoFigura +
        ", ID:" +
        String(nextIndexToRequest) +
        ", TOT:" +
        String(currentTotalIndex) +
        ", T:" +
        String(
            currentMissionTime,
            3
        );

    uint32_t logSize =
        logPayload.length();

    Serial.write(
        SYNC_START,
        4
    );

    Serial.write(0x01);

    Serial.write(
        (uint8_t*)&logSize,
        4
    );

    Serial.print(logPayload);

    Serial.write(
        SYNC_END,
        2
    );

    // ======================================
    // STREAM BINÁRIO
    // ======================================

    Serial.write(
        SYNC_START,
        4
    );

    Serial.write(0x02);

    uint32_t imgSize32 =
        (
            uint32_t
        )
        currentImgSize;

    Serial.write(
        (uint8_t*)&imgSize32,
        4
    );

    size_t totalRead = 0;

    uint8_t buffer[1024];

    unsigned long
    downloadStart =
        millis();

    while (
        totalRead <
        currentImgSize &&
        (
            millis() -
            downloadStart <
            10000
        )
    )
    {
        if (client.available())
        {
            size_t bytesToRead =
                min(
                    (
                        size_t
                    )
                    client.available(),
                    currentImgSize
                    -
                    totalRead
                );

            if (
                bytesToRead >
                sizeof(buffer)
            )
            {
                bytesToRead =
                    sizeof(buffer);
            }

            size_t n =
                client.read(
                    buffer,
                    bytesToRead
                );

            Serial.write(
                buffer,
                n
            );

            totalRead += n;

            downloadStart =
                millis();
        }

        // FIX WATCHDOG
        vTaskDelay(
            pdMS_TO_TICKS(1)
        );
    }

    Serial.write(
        SYNC_END,
        2
    );

    // ======================================
    // SUCESSO
    // ======================================

    if (
        totalRead ==
        currentImgSize
    )
    {
        client.readStringUntil(
            '\n'
        );

        lastTotalIndex =
            currentTotalIndex;

        nextIndexToRequest =
            (
                nextIndexToRequest
                + 1
            ) % 10;

        return true;
    }

    Serial.println(
        "\n[ERRO] Download incompleto."
    );

    client.stop();

    return false;
}