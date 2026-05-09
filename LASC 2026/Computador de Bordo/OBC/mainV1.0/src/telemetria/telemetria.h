#ifndef TELEMETRIA_H
#define TELEMETRIA_H

#include <RadioLib.h>
#include <Arduino.h>

#define BUFFER_SIZE 300

struct sensorsData {
    uint32_t seconds;

    int16_t temperature;
    int16_t humidity;

    uint32_t pressure;
    int16_t altitude;

    int32_t latitude;
    int32_t longitude;

    uint8_t sats;

    int16_t accelX;
    int16_t accelY;
    int16_t accelZ;
};

class LoraTelemetria {
public:
    LoraTelemetria(uint8_t myAddress, uint8_t destAddress);

    void init();
    void process();

    void sendPacket(const char* payload);
    void sendPacket(uint8_t* data, uint16_t size);

    void setTxInterval(unsigned long interval);
    bool isIdle();

    void onPacketReceived(void (*callback)(uint8_t*, uint16_t));

private:
    void handleReceived(uint8_t *payload, uint16_t size);

    static void setFlag(void);

    static volatile bool _receivedFlag;

    uint8_t _myAddress;
    uint8_t _destAddress;

    bool _idle;
    unsigned long _lastTxTime;
    unsigned long _txInterval;

    uint8_t _txBuffer[BUFFER_SIZE];
    uint8_t _rxBuffer[BUFFER_SIZE];

    void (*_callback)(uint8_t*, uint16_t);
};

#endif