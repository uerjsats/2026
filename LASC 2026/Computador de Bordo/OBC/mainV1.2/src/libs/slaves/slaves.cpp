#include <HardwareSerial.h>
#include <Arduino.h>
#include <cstring>

HardwareSerial controleSerial(2);
HardwareSerial supSerial(1);

void serialInit()
{
    controleSerial.begin(115200, SERIAL_8N1, 19, 20);
    supSerial.begin(115200, SERIAL_8N1, 21, 22);
}

void enviarOrdem(uint8_t comando)
{
    switch (comando)
    {
        case 1:
            controleSerial.println("1");
            break;

        case 2:
            controleSerial.println("2");
            break;

        case 3:
            controleSerial.println("3");
            break;

        case 4:
            supSerial.println("4");
            break;

        case 5:
            supSerial.println("5");
            break;

        default:
            Serial.print("Ordem inválida: ");
            Serial.println(comando);
    }
}

int receberResposta(
    char cData[64],
    bool &cDataUpdated,
    char sData[64],
    bool &sDataUpdated)
{
    char resposta[64];

    //Serial de Controle de Atitude
    if (controleSerial.available())
    {
        size_t len = controleSerial.readBytesUntil(
            '\n',
            resposta,
            sizeof(resposta) - 1
        );

        resposta[len] = '\0';

        if (len == 0)
            return -1;

        // procura ':'
        char *separador = strchr(resposta, ':');

        if (separador == nullptr)
            return -1;

        *separador = '\0';

        int endereco = atoi(resposta);

        char *dadosControle = separador + 1;

        switch (endereco)
        {
            case 3:
                strncpy(cData, dadosControle, 63);
                cData[63] = '\0';
                cDataUpdated = true;
                break;

            default:
                return -1;
        }

        return 0;
    }
    
    //Serial de Suprimento de Energia
    else if (supSerial.available())
    {
        size_t len = supSerial.readBytesUntil(
            '\n',
            resposta,
            sizeof(resposta) - 1
        );

        resposta[len] = '\0';

        if (len == 0)
            return -1;

        char *separador = strchr(resposta, ':');

        if (separador == nullptr)
            return -1;

        *separador = '\0';

        int endereco = atoi(resposta);

        char *dadosSup = separador + 1;

        switch (endereco)
        {
            case 1:
                strncpy(sData, dadosSup, 63);
                sData[63] = '\0';
                sDataUpdated = true;
                break;

            default:
                return -1;
        }

        return 0;
    }

    return -1;
}