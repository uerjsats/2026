#include "telemetria.h"

//Frequencia de operacao do LoRa em MHz
#define RF_FREQUENCY 915.0

//Potencia de transmissao em dBm
#define TX_OUTPUT_POWER 10

//Largura de banda do LoRa em kHz
#define LORA_BANDWIDTH 125.0

//Fator de espalhamento do LoRa
#define LORA_SPREADING_FACTOR 7

//Taxa de codificacao do LoRa
#define LORA_CODINGRATE 5

//Instancia do modulo SX1262 com pinos definidos
SX1262 radio = new Module(8, 14, 12, 13);

//Flag global indicando recepcao de pacote
volatile bool LoraTelemetria::_receivedFlag = false;

//Construtor da classe com endereco local e destino
LoraTelemetria::LoraTelemetria(uint8_t myAddress, uint8_t destAddress)
: _myAddress(myAddress), _destAddress(destAddress)
{
    //Estado inicial ocioso
    _idle = true;

    //Intervalo minimo entre transmissões
    _txInterval = 2000;

    //Ultimo tempo de transmissao
    _lastTxTime = 0;

    //Callback de recepcao
    _callback = nullptr;
}

//Funcao chamada por interrupcao para indicar recebimento
void LoraTelemetria::setFlag(void) {
    _receivedFlag = true;
}

//Inicializacao do modulo LoRa
void LoraTelemetria::init()
{
    //Inicializa radio com parametros definidos
    int state = radio.begin(RF_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODINGRATE);

    //Se falhar trava o sistema
    if(state != RADIOLIB_ERR_NONE) {
        while(true);
    }

    //Define potencia de transmissao
    radio.setOutputPower(TX_OUTPUT_POWER);

    //Coloca em modo de recepcao
    radio.startReceive();
}

//Loop de processamento para verificar recepcao
void LoraTelemetria::process()
{
    //Se chegou pacote
    if(_receivedFlag) {
        _receivedFlag = false;

        //Tamanho do pacote recebido
        int len = radio.getPacketLength();

        //Valida tamanho dentro do buffer
        if(len > 0 && len <= BUFFER_SIZE) {

            //Le dados recebidos
            int state = radio.readData(_rxBuffer, len);

            //Se leitura ok processa pacote
            if(state == RADIOLIB_ERR_NONE) {
                handleReceived(_rxBuffer, len);
            }
        }

        //Volta para recepcao continua
        radio.startReceive();
    }
}

//Envio de pacote a partir de string
void LoraTelemetria::sendPacket(const char* payload)
{
    //Evita envio se ocupado
    if (!_idle) return;

    //Respeita intervalo minimo entre envios
    if (millis() - _lastTxTime < _txInterval) return;

    //Zera buffer de transmissao
    memset(_txBuffer, 0, BUFFER_SIZE);

    //Adiciona endereco de origem
    _txBuffer[0] = _myAddress;

    //Adiciona endereco de destino
    _txBuffer[1] = _destAddress;

    //Calcula tamanho da mensagem
    int len = strlen(payload);

    //Limita tamanho ao buffer
    if (len > BUFFER_SIZE - 2) len = BUFFER_SIZE - 2;

    //Copia payload para buffer
    memcpy(&_txBuffer[2], payload, len);

    //Marca como ocupado
    _idle = false;

    //Transmite pacote completo
    int state = radio.transmit(_txBuffer, len + 2);

    //Libera estado
    _idle = true;

    //Atualiza tempo de envio
    _lastTxTime = millis();

    //Retorna para recepcao
    radio.startReceive();
}

//Envio de pacote em formato binario
void LoraTelemetria::sendPacket(uint8_t* data, uint16_t size)
{
    //Evita envio se ocupado
    if (!_idle) return;

    //Respeita intervalo minimo
    if (millis() - _lastTxTime < _txInterval) return;

    //Verifica limite de buffer
    if (size + 2 > BUFFER_SIZE) return;

    //Define endereco origem
    _txBuffer[0] = _myAddress;

    //Define endereco destino
    _txBuffer[1] = _destAddress;

    //Copia dados para buffer
    memcpy(&_txBuffer[2], data, size);

    //Marca como ocupado
    _idle = false;

    //Transmite pacote
    int state = radio.transmit(_txBuffer, size + 2);

    //Libera estado
    _idle = true;

    //Atualiza tempo
    _lastTxTime = millis();

    //Volta para recepcao
    radio.startReceive();
}

//Retorna se modulo esta livre
bool LoraTelemetria::isIdle()
{
    return _idle;
}

//Define intervalo entre transmissões
void LoraTelemetria::setTxInterval(unsigned long interval)
{
    _txInterval = interval;
}

//Define callback de recepcao
void LoraTelemetria::onPacketReceived(void (*callback)(uint8_t*, uint16_t))
{
    _callback = callback;
}

//Processa pacote recebido
void LoraTelemetria::handleReceived(uint8_t *payload, uint16_t size)
{
    //Ignora pacotes muito pequenos
    if (size < 2) return;

    //Extrai endereco do remetente
    uint8_t sender = payload[0];

    //Extrai endereco do destinatario
    uint8_t receiver = payload[1];

    //Filtra apenas pacotes validos para este nodo
    if (receiver != _myAddress || sender != _destAddress)
        return;

    //Limpa buffer de recepcao
    memset(_rxBuffer, 0, BUFFER_SIZE);

    //Copia dados uteis
    memcpy(_rxBuffer, &payload[2], size - 2);

    //Chama callback se definido
    if (_callback)
        _callback(_rxBuffer, size - 2);
}