// ===========================================
// --- TESTE DE MECANISMOS - PAINEL SOLAR ---
// ===========================================
// Versão com controle de velocidade PWM

#include "pinos_placa_v1.h"

// ===========================================
// --- Configurações ---
// ===========================================
#define TEMPO_ACIONAMENTO_PAINEL 5000  // 5 segundos

// ===========================================
// --- Variáveis de controle ---
// ===========================================
bool painelAberto = false;
char cmdBuffer[20];
byte cmdIndex = 0;

// Controle de velocidade (0-255)
int velocidadeMotor = 128;  // Velocidade máxima por padrão
// Use valores menores para mais lento: 128 (50%), 64 (25%), etc.

// ===========================================
// --- SETUP ---
// ===========================================
void setup() {
  Serial.begin(9600);
  delay(500);
  
  Serial.println(F("\n--- TESTE PAINEL SOLAR COM PWM ---"));
  
  // Configura os pinos
  pinMode(PAINEL_IN1, OUTPUT);
  pinMode(PAINEL_IN2, OUTPUT);
  pinMode(PAINEL_IN3, OUTPUT);
  pinMode(PAINEL_IN4, OUTPUT);
  pinMode(LED, OUTPUT);
  
  // Desliga tudo
  digitalWrite(PAINEL_IN1, LOW);
  digitalWrite(PAINEL_IN2, LOW);
  digitalWrite(PAINEL_IN3, LOW);
  digitalWrite(PAINEL_IN4, LOW);
  digitalWrite(LED, LOW);
  
  Serial.println(F("Pinos:"));
  Serial.print(F("IN1=")); Serial.println(PAINEL_IN1);
  Serial.print(F("IN2=")); Serial.println(PAINEL_IN2);
  Serial.print(F("IN3=")); Serial.println(PAINEL_IN3);
  Serial.print(F("IN4=")); Serial.println(PAINEL_IN4);
  
  Serial.print(F("\nVelocidade atual: "));
  Serial.print(velocidadeMotor);
  Serial.print(F(" ("));
  Serial.print(map(velocidadeMotor, 0, 255, 0, 100));
  Serial.println(F("%)"));
  
  Serial.println(F("\nComandos:"));
  Serial.println(F("A - Abrir"));
  Serial.println(F("F - Fechar"));
  Serial.println(F("T - Testar"));
  Serial.println(F("S - Status"));
  Serial.println(F("L - LED on/off"));
  Serial.println(F("V - Velocidade (ex: V128, V64, V255)"));
  Serial.println(F("+ - Aumentar velocidade"));
  Serial.println(F("- - Diminuir velocidade"));
  Serial.println(F("? - Ajuda\n"));
}

// ===========================================
// --- LOOP PRINCIPAL ---
// ===========================================
void loop() {
  // Leitura serial simples
  if (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (cmdIndex > 0) {
        cmdBuffer[cmdIndex] = '\0';
        processarComando(cmdBuffer);
        cmdIndex = 0;
      }
    } else if (cmdIndex < 19) {
      cmdBuffer[cmdIndex++] = c;
    }
  }
}

// ===========================================
// --- FUNÇÕES DE CONTROLE DOS MOTORES ---
// ===========================================

void motorAbrir(int velocidade) {
  // Desliga lado oposto primeiro
  digitalWrite(PAINEL_IN4, LOW);
  digitalWrite(PAINEL_IN2, LOW);
  
  // Aciona com PWM para abrir
  analogWrite(PAINEL_IN3, velocidade);
  analogWrite(PAINEL_IN1, velocidade);
}

void motorFechar(int velocidade) {
  // Desliga lado oposto primeiro
  digitalWrite(PAINEL_IN3, LOW);
  digitalWrite(PAINEL_IN1, LOW);
  
  // Aciona com PWM para fechar
  analogWrite(PAINEL_IN4, velocidade);
  analogWrite(PAINEL_IN2, velocidade);
}

void motorParar() {
  digitalWrite(PAINEL_IN1, LOW);
  digitalWrite(PAINEL_IN2, LOW);
  digitalWrite(PAINEL_IN3, LOW);
  digitalWrite(PAINEL_IN4, LOW);
}

void abrirPainel() {
  if (painelAberto) {
    Serial.println(F("Ja esta aberto!"));
    return;
  }
  
  Serial.print(F("Abrindo com velocidade "));
  Serial.print(velocidadeMotor);
  Serial.print(F(" ("));
  Serial.print(map(velocidadeMotor, 0, 255, 0, 100));
  Serial.println(F("%)..."));
  
  digitalWrite(LED, HIGH);
  motorAbrir(velocidadeMotor);
  
  delay(TEMPO_ACIONAMENTO_PAINEL);
  
  motorParar();
  digitalWrite(LED, LOW);
  
  painelAberto = true;
  Serial.println(F("ABERTO!"));
}

void fecharPainel() {
  if (!painelAberto) {
    Serial.println(F("Ja esta fechado!"));
    return;
  }
  
  Serial.print(F("Fechando com velocidade "));
  Serial.print(velocidadeMotor);
  Serial.print(F(" ("));
  Serial.print(map(velocidadeMotor, 0, 255, 0, 100));
  Serial.println(F("%)..."));
  
  digitalWrite(LED, HIGH);
  motorFechar(velocidadeMotor);
  
  delay(TEMPO_ACIONAMENTO_PAINEL);
  
  motorParar();
  digitalWrite(LED, LOW);
  
  painelAberto = false;
  Serial.println(F("FECHADO!"));
}

void testeMovimento(char direcao, int duracao) {
  digitalWrite(LED, HIGH);
  
  if (direcao == 'A') {
    motorAbrir(velocidadeMotor);
  } else {
    motorFechar(velocidadeMotor);
  }
  
  delay(duracao);
  motorParar();
  digitalWrite(LED, LOW);
}

void mostrarStatus() {
  Serial.println(F("\n--- STATUS ---"));
  Serial.print(F("Painel: "));
  Serial.println(painelAberto ? F("ABERTO") : F("FECHADO"));
  
  Serial.print(F("Velocidade: "));
  Serial.print(velocidadeMotor);
  Serial.print(F(" ("));
  Serial.print(map(velocidadeMotor, 0, 255, 0, 100));
  Serial.println(F("%)"));
  
  Serial.print(F("LED: "));
  Serial.println(digitalRead(LED) ? F("ON") : F("OFF"));
  
  Serial.print(F("Pinos: IN1="));
  Serial.print(digitalRead(PAINEL_IN1));
  Serial.print(F(" IN2="));
  Serial.print(digitalRead(PAINEL_IN2));
  Serial.print(F(" IN3="));
  Serial.print(digitalRead(PAINEL_IN3));
  Serial.print(F(" IN4="));
  Serial.println(digitalRead(PAINEL_IN4));
  Serial.println(F("-------------\n"));
}

void ajustarVelocidade(int novaVel) {
  if (novaVel < 0) novaVel = 0;
  if (novaVel > 255) novaVel = 255;
  
  velocidadeMotor = novaVel;
  
  Serial.print(F("Velocidade ajustada para: "));
  Serial.print(velocidadeMotor);
  Serial.print(F(" ("));
  Serial.print(map(velocidadeMotor, 0, 255, 0, 100));
  Serial.println(F("%)"));
}

void processarComando(char* cmd) {
  // Converte para maiuscula (apenas letras)
  for (byte i = 0; cmd[i]; i++) {
    if (cmd[i] >= 'a' && cmd[i] <= 'z') {
      cmd[i] -= 32;
    }
  }
  
  Serial.print(F("Cmd: "));
  Serial.println(cmd);
  
  // Comandos de velocidade com número (ex: V128)
  if (cmd[0] == 'V') {
    int vel = atoi(&cmd[1]);
    if (vel > 0 || cmd[1] == '0') {
      ajustarVelocidade(vel);
    } else {
      Serial.print(F("Velocidade atual: "));
      Serial.println(velocidadeMotor);
    }
  }
  // Aumentar velocidade
  else if (cmd[0] == '+') {
    int novaVel = velocidadeMotor + 32;
    if (novaVel > 255) novaVel = 255;
    ajustarVelocidade(novaVel);
  }
  // Diminuir velocidade
  else if (cmd[0] == '-') {
    int novaVel = velocidadeMotor - 32;
    if (novaVel < 32) novaVel = 32;  // Mínimo de 32 para ter torque
    ajustarVelocidade(novaVel);
  }
  // Abrir
  else if (cmd[0] == 'A') {
    abrirPainel();
  }
  // Fechar
  else if (cmd[0] == 'F') {
    fecharPainel();
  }
  // Teste completo
  else if (cmd[0] == 'T') {
    Serial.println(F("\n--- TESTE COMPLETO ---"));
    
    // Teste com velocidade atual
    Serial.println(F("1. Abrindo..."));
    abrirPainel();
    delay(2000);
    
    Serial.println(F("2. Fechando..."));
    fecharPainel();
    delay(2000);
    
    // Teste de movimento curto
    Serial.println(F("3. Movimento curto (1s)..."));
    testeMovimento('A', 1000);
    delay(500);
    testeMovimento('F', 1000);
    
    painelAberto = false;
    Serial.println(F("Teste concluido!\n"));
  }
  // Teste de velocidade (varredura)
  else if (cmd[0] == 'X') {
    Serial.println(F("\n--- TESTE DE VELOCIDADES ---"));
    int velocidades[] = {64, 128, 192, 255};
    
    for (int i = 0; i < 4; i++) {
      ajustarVelocidade(velocidades[i]);
      Serial.print(F("Testando velocidade "));
      Serial.println(velocidadeMotor);
      
      testeMovimento('A', 500);
      delay(500);
      testeMovimento('F', 500);
      delay(1000);
    }
    
    painelAberto = false;
    Serial.println(F("Teste de velocidades concluido!\n"));
  }
  // Status
  else if (cmd[0] == 'S') {
    mostrarStatus();
  }
  // LED
  else if (cmd[0] == 'L') {
    digitalWrite(LED, !digitalRead(LED));
    Serial.print(F("LED "));
    Serial.println(digitalRead(LED) ? F("ON") : F("OFF"));
  }
  // Ajuda
  else if (cmd[0] == '?') {
    Serial.println(F("\n--- COMANDOS ---"));
    Serial.println(F("A - Abrir painel"));
    Serial.println(F("F - Fechar painel"));
    Serial.println(F("T - Teste completo"));
    Serial.println(F("X - Teste de velocidades"));
    Serial.println(F("S - Status"));
    Serial.println(F("L - LED on/off"));
    Serial.println(F("V<num> - Velocidade (ex: V128)"));
    Serial.println(F("+/- - Aumentar/Diminuir velocidade"));
    Serial.println(F("? - Esta ajuda"));
    Serial.println(F("----------------\n"));
  }
  else {
    Serial.println(F("Erro! Digite ? para ajuda"));
  }
}