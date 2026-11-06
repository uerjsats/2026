#ifndef PINOS_H_2026
#define PINOS_H_2026
// Nano (esquematico V2, 2026) - sinais logicos ligados aos drivers existentes.
// Motor DC de reacao: canal A do driver (MOTA+/-)
#define IN1 9          // Reacao - PWM / IN1 do driver
#define IN2 10         // Reacao - PWM / IN2 do driver
// Entradas adicionais do driver de atitude no desenho: D11 e D4 (canal B).
// Permanecem como saidas LOW para evitar flutuacao na placa.
#define REACAO_IN3 11
#define REACAO_IN4 4

#define LDRVERMELHO A0 // LDR0
#define LDRAZUL A1     // LDR1

// Mecanismos: canal A painel; canal B antena
#define PAINEL_IN1 5
#define PAINEL_IN2 7
#define ANTIN1 6
#define ANTIN2 8

// No esquematico V2 as chaves sao ligadas ao Nano A6/A7
// (redes marcadas A2/A3 no desenho), lidas apenas via analogRead.
#define SWANT1 A6
#define SWANT2 A7

// Comunicacao: Nano D12 recebe TX_CT5; Nano D13 envia a RX_CT5.
#define OBC_RX_PIN 12
#define OBC_TX_PIN 13

#endif
