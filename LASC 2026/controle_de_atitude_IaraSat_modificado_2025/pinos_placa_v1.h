// pinos_placa_v1.h

// ===========================================
// ------- PINS DO HARDWARE -------
// ===========================================
#define IN1 10            // Pino de controle 1 do driver de motor  para a roda de reacao(PWM)
#define IN2 11            // Pino de controle 2 do driver de motor (PWM)para a roda de reacao(PWM)
#define LDRVERMELHO A0  // Sensor LDR 0 (Analog 0), Direita
#define LDRAZUL A1      // Sensor LDR 1 (Analog 1), Esquerda
#define SWANT1    A2 // chave de fim de curso da Antena (Abrir)
#define SWANT2    A3 //chave de fim de cursso Antena (fechar)
#define ANTIN1 3 // IN1 Pino de controle 1 do driver de motor da Antena
#define ANTIN2 5 // IN2 Pino de controle 1 do driver de motor
#define PAINEL_IN1 9 // controle do Painel (+5V)
#define PAINEL_IN2 6 //
#define PAINEL_IN3 7 // controle do Painel (+7.4V)
#define PAINEL_IN4 8 //
#define LED        13