#ifndef SLAVES_H
#define SLAVES_H

void serialInit();
void enviarOrdem(uint8_t comando);

int receberResposta(
    char cData[64],
    bool &cDataUpdated,
    char sData[64],
    bool &sDataUpdated
);


#endif