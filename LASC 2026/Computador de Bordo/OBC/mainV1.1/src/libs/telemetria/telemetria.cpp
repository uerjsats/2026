#include "telemetria.h"

#define RF_FREQUENCY 915.0
#define TX_OUTPUT_POWER 10
#define LORA_BANDWIDTH 125.0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODINGRATE 5

#define START_BYTE 0x7E

#define PACKET_SIZE 180
#define MAX_IMAGE_SIZE 5000

SX1262 radio = new Module(8, 14, 12, 13);

volatile bool LoraTelemetria::_receivedFlag = false;

static uint8_t binaryBuffer[MAX_IMAGE_SIZE];
static uint8_t rleBuffer[MAX_IMAGE_SIZE];

ImageTransaction imgTx;

LoraTelemetria::LoraTelemetria(uint8_t myAddress, uint8_t destAddress)
: _myAddress(myAddress), _destAddress(destAddress)
{
    _idle = true;
    _txInterval = 300;
    _lastTxTime = 0;
    _callback = nullptr;
}

void LoraTelemetria::init()
{
    int state = radio.begin(RF_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODINGRATE);
    if(state != RADIOLIB_ERR_NONE) return;

    radio.setOutputPower(TX_OUTPUT_POWER);
    radio.setPacketReceivedAction(setFlag);
    radio.startReceive();
}

void LoraTelemetria::setFlag(void) {
    _receivedFlag = true;
}

void LoraTelemetria::process()
{
    if(_receivedFlag) {
        _receivedFlag = false;

        // Se o rádio estava transmitindo e o pino disparou, a transmissão acabou
        if (!_idle) {
            _idle = true;
            radio.startReceive(); // Volta a ouvir
            return;
        }

        int len = radio.getPacketLength();

        if(len > 0 && len <= BUFFER_SIZE) {
            int state = radio.readData(_rxBuffer, len);
            if(state == RADIOLIB_ERR_NONE) {
                handleReceived(_rxBuffer, len);
            }
        }

        radio.startReceive();
    }
}

void LoraTelemetria::sendPacket(uint8_t* data, uint16_t size, uint8_t type)
{
    if (!_idle) return;
    if (millis() - _lastTxTime < _txInterval) return;
    if (size + 4 > BUFFER_SIZE) return;

    _txBuffer[0] = START_BYTE;
    _txBuffer[1] = type;
    _txBuffer[2] = _myAddress;
    _txBuffer[3] = _destAddress;

    memcpy(&_txBuffer[4], data, size);

    _idle = false;
    radio.startTransmit(_txBuffer, size + 4);
    _lastTxTime = millis();
}

void LoraTelemetria::sendPacket(const char* payload, uint8_t type)
{
    sendPacket((uint8_t*)payload, strlen(payload), type);
}

void LoraTelemetria::handleReceived(uint8_t *payload, uint16_t size)
{
    if (size < 4) return;

    uint8_t sender = payload[2];
    uint8_t receiver = payload[3];

    if (receiver != _myAddress || sender != _destAddress)
        return;

    memcpy(_rxBuffer, &payload[4], size - 4);

    if (_callback)
        _callback(_rxBuffer, size - 4);
}

void LoraTelemetria::onPacketReceived(void (*callback)(uint8_t*, uint16_t))
{
    _callback = callback;
}

void LoraTelemetria::binarize(uint8_t *input, uint8_t *output, int size, uint8_t threshold)
{
    for (int i = 0; i < size; i++) {
        output[i] = (input[i] > threshold) ? 1 : 0;
    }
}

int LoraTelemetria::encodeRLE(uint8_t *input, int size, uint8_t *output)
{
    int outIndex = 0;
    uint8_t current = input[0];
    uint8_t count = 1;

    for (int i = 1; i < size; i++) {
        if (input[i] == current && count < 255) {
            count++;
        } else {
            output[outIndex++] = count;
            output[outIndex++] = current;
            current = input[i];
            count = 1;
        }
    }

    output[outIndex++] = count;
    output[outIndex++] = current;

    return outIndex;
}

void LoraTelemetria::sendImageRaw(uint8_t* data, uint32_t size)
{
    const int QVGA_W = 320;
    const int QVGA_H = 240;
    const int OUT_W = 80;
    const int OUT_H = 60;

    if (size != (QVGA_W * QVGA_H)) return;

    static uint8_t downsampled[OUT_W * OUT_H];

    // downsample 4:1
    for (int y = 0; y < OUT_H; y++) {
        for (int x = 0; x < OUT_W; x++) {
            int srcX = x * 4;
            int srcY = y * 4;
            downsampled[y * OUT_W + x] = data[srcY * QVGA_W + srcX];
        }
    }

    // binarização
    binarize(downsampled, binaryBuffer, OUT_W * OUT_H);

    // compressão RLE
    int rleSize = encodeRLE(binaryBuffer, OUT_W * OUT_H, rleBuffer);

    // segurança contra overflow
    if (rleSize > MAX_IMAGE_SIZE) return;

    // inicia envio
    sendImage(rleBuffer, rleSize);
}

void LoraTelemetria::sendImage(uint8_t* data, uint16_t size)
{
    if (imgTx.ativo) return;

    imgTx.data = data;
    imgTx.size = size;
    imgTx.index = 0;
    imgTx.total = (size + PACKET_SIZE - 1) / PACKET_SIZE;
    imgTx.ativo = true;
}

void LoraTelemetria::sendImageChunk()
{
    if (!imgTx.ativo) return;
    if (!_idle) return;
    if (millis() - _lastTxTime < _txInterval) return;

    if (imgTx.index >= imgTx.total) {
        imgTx.ativo = false;
        return;
    }

    int start = imgTx.index * PACKET_SIZE;
    int remaining = imgTx.size - start;
    int len = (remaining > PACKET_SIZE) ? PACKET_SIZE : remaining;

    uint8_t packet[PACKET_SIZE + 7];

    packet[0] = START_BYTE;
    packet[1] = TYPE_IMAGE;
    packet[2] = _myAddress;
    packet[3] = _destAddress;
    packet[4] = (uint8_t)imgTx.index;
    packet[5] = (uint8_t)imgTx.total;
    packet[6] = (uint8_t)len;

    memcpy(packet + 7, imgTx.data + start, len);

    _idle = false;

    int state = radio.startTransmit(packet, len + 7);

    if (state != RADIOLIB_ERR_NONE) {
        imgTx.ativo = false;
        _idle = true;
        return;
    }

    _lastTxTime = millis();
    imgTx.index++;
}

bool LoraTelemetria::isIdle()
{
    return _idle;
}

void LoraTelemetria::setTxInterval(unsigned long interval)
{
    _txInterval = interval;
}

bool LoraTelemetria::isImageSending() {
    return imgTx.ativo;
}