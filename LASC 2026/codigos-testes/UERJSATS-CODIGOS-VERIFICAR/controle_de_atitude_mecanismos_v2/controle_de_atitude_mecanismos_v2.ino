#include <Arduino.h>
#include <SoftwareSerial.h>

// ===========================================
// --- GERENCIAMENTO DE PORTAS SERIAIS -------
// ===========================================
#define RX_CT 4    // Pino RX (Recebe dados da Placa Mestre - Heltec V3)
#define TX_CT 13     // Pino TX (Envia dados para a Placa Mestre - Heltec V3)

SoftwareSerial controleSerial(RX_CT, TX_CT); 

#define MEU_ENDERECO 3  // Endereço fixo do Controle de Mecanismos

// ===========================================
// --- CONFIGURAÇÃO DE DEBUG E OPERAÇÃO -----
// ===========================================
#define MODO_TESTE_COMUNICACAO 0  // 0 = Modo Normal de Voo, 1 = Modo Bancada
#define SERIAL_DEBUG_ENABLE 1

// ===========================================
// --- CONSTANTES TEMPORAIS DE SEGURANÇA -----
// ===========================================
#define TEMPO_ACIONAMENTO_PAINEL 5000
#define TIMEOUT_ANTENA 10000

// ===========================================
// --- MAPEAMENTO DOS DRIVERS DE MOTOR -------
// ===========================================
// DRIVER 7.4V (Roda de Reação e Mecanismos Pesados)
#define DRIVER74_IN1  6
#define DRIVER74_IN2  12
#define DRIVER74_IN3  10  // Antigo IN1 (Controle PWM da Roda de Reação)
#define DRIVER74_IN4  11  // Antigo IN2 (Controle PWM da Roda de Reação)

// DRIVER 5.0V (Mecanismos dos Painéis Solares e Antena)
#define DRIVER5_IN1   9   // Antigo PAINEL_IN1
#define DRIVER5_IN2   8   // Antigo PAINEL_IN4
#define DRIVER5_IN3   7   // Antigo PAINEL_IN3
#define DRIVER5_IN4   5   // Antigo ANTIN2

// Chaves de Fim de Curso (Entradas Analógicas/Digitais)
#define SW_1          A2  // Chave fim de curso da Antena (Indica Aberto)
#define SW_2          A3  // Chave fim de curso da Antena (Indica Fechado)

// Variáveis Globais de Controle
String bufferSerial = "";
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 5000;

// ===========================================
// --- FUNÇÕES AUXILIARES DE TRATAMENTO ------
// ===========================================

String limparString(String str) {
  str.trim();
  String resultado = "";
  for (unsigned int i = 0; i < str.length(); i++) {
    char c = str.charAt(i);
    if ((c >= 32 && c <= 126) || c == '\n' || c == '\r') {
      resultado += c;
    }
  }
  resultado.trim();
  return resultado;
}

void enviarResposta(String dados) {
  String resposta = String(MEU_ENDERECO) + ":" + dados;
  controleSerial.println(resposta);
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.print(">>> Enviando para OBC: ");
    Serial.println(resposta);
  }
}

int lerEntradaAnalogicaComoDigital(uint8_t pinoAnalogico, int limiar = 512) {
  int valor = analogRead(pinoAnalogico);
  return (valor > limiar) ? HIGH : LOW;
}

bool aguardaValorChave(int pinoChave, int valorEsperado, unsigned long tempoTimeout) {
  unsigned long inicio = millis();
  while (millis() - inicio < tempoTimeout) {
    int valorAtual = lerEntradaAnalogicaComoDigital(pinoChave);
    if (valorAtual == valorEsperado) {
      return true;
    }
  }
  return false;
}

// Controla a velocidade da roda de reação (Malha aberta via PWM)
void defineVelocidadeRoda(int valorPwm) {
  valorPwm = constrain(valorPwm, -255, 255);

  if (valorPwm == 0) {
    digitalWrite(DRIVER74_IN3, LOW);
    digitalWrite(DRIVER74_IN4, LOW);
  } else if (valorPwm > 0) {
    analogWrite(DRIVER74_IN3, valorPwm);
    digitalWrite(DRIVER74_IN4, LOW);
  } else {
    digitalWrite(DRIVER74_IN3, LOW);
    analogWrite(DRIVER74_IN4, abs(valorPwm));
  }
}

// ===========================================
// --- MODO DE TESTE DE COMUNICAÇÃO (BANCADA) -
// ===========================================
#if MODO_TESTE_COMUNICACAO == 1

void setup() {
  Serial.begin(9600);
  controleSerial.begin(9600);
  
  Serial.println("\n\n=== MODO TESTE COMUNICACAO ===");
  Serial.print("RX_CT pino: "); Serial.println(RX_CT);
  Serial.print("TX_CT pino: "); Serial.println(TX_CT);
  Serial.println("================================\n");
  
  delay(1000);
  enviarResposta("MODO TESTE - Controle Pronto");
}

void loop() {
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      controleSerial.println(cmd);
    }
  }
  
  if (controleSerial.available()) {
    String msg = controleSerial.readStringUntil('\n');
    msg.trim();
    if (msg.length() > 0) {
      enviarResposta("ECO:" + msg);
    }
  }
  
  if (millis() - lastHeartbeat > heartbeatInterval) {
    lastHeartbeat = millis();
    enviarResposta("HEARTBEAT_OK");
  }
}

#else

// ===========================================
// --- MODO NORMAL DE OPERAÇÃO (VOO) --------
// ===========================================

void setup() {
  Serial.begin(9600);          // Debug USB local
  controleSerial.begin(9600);  // Link de Hardware com o OBC
  
  delay(500);
  
  Serial.println("\n=== SUBSISTEMA DE CONTROLE E MECANISMOS ===");
  Serial.print("Endereço Serial: "); Serial.println(MEU_ENDERECO);
  
  // Configuração das saídas do Driver 7.4V
  pinMode(DRIVER74_IN1, OUTPUT);
  pinMode(DRIVER74_IN2, OUTPUT);
  pinMode(DRIVER74_IN3, OUTPUT);
  pinMode(DRIVER74_IN4, OUTPUT);
  digitalWrite(DRIVER74_IN1, LOW);
  digitalWrite(DRIVER74_IN2, LOW);
  digitalWrite(DRIVER74_IN3, LOW);
  digitalWrite(DRIVER74_IN4, LOW);
  
  // Configuração das saídas do Driver 5.0V
  pinMode(DRIVER5_IN1, OUTPUT);
  pinMode(DRIVER5_IN2, OUTPUT);
  pinMode(DRIVER5_IN3, OUTPUT);
  pinMode(DRIVER5_IN4, OUTPUT);
  digitalWrite(DRIVER5_IN1, LOW);
  digitalWrite(DRIVER5_IN2, LOW);
  digitalWrite(DRIVER5_IN3, LOW);
  digitalWrite(DRIVER5_IN4, LOW);
  
  delay(1000);
  
  Serial.println("Drivers e chaves prontos para execucao!");
  enviarResposta("Controle de Mecanismos inicializado");
}

void loop() {
  // Varre a porta serial procurando chamadas da mestre (OBC)
  while (controleSerial.available()) {
    char c = controleSerial.read();
    
    if (c == '\n' || c == '\r') {
      if (bufferSerial.length() > 0) {
        String msg = limparString(bufferSerial);
        
        if (msg.length() > 0) {
          if (SERIAL_DEBUG_ENABLE) {
            Serial.print("<<< Recebido do OBC: '"); Serial.print(msg); Serial.println("'");
          }
          
          int idx = msg.indexOf(':');
          if (idx > 0) {
            String addrStr = msg.substring(0, idx);
            int addr = addrStr.toInt();
            String cmd = msg.substring(idx + 1);
            cmd.trim();
            
            // Só executa se for direcionado a esta placa (3) ou broadcast geral (0)
            if (addr == MEU_ENDERECO || addr == 0) {
              processarComando(cmd);
            }
          } else {
            processarComando(msg);
          }
        }
        bufferSerial = "";
      }
    } else {
      bufferSerial += c;
    }
  }
}

void processarComando(String comando) {
  comando = limparString(comando);
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.print("Executando: '"); Serial.print(comando); Serial.println("'");
  }
  
  // Resposta de Telemetria de Rotina solicitada pelo Mestre
  if (comando.startsWith("REQ:") || comando == "REQ_STATUS") {
    enviarResposta("STATUS:OPERACIONAL");
    return;
  }
  
  // Comando de Handshake inicial / Parada (0)
  if (comando == "0" || comando == "5") {
    defineVelocidadeRoda(0);
    enviarResposta("Motores e atuadores desligados");
  }
  
  // Comando 6: Abertura Completa dos Painéis Solares
  else if (comando == "6") {
    enviarResposta("Iniciando abertura dos paineis");
    
    // Garante que o sentido oposto das pontes H está desligado
    digitalWrite(DRIVER5_IN2, LOW);
    digitalWrite(DRIVER74_IN1, LOW);
    
    // Liga as duas linhas de tensão combinadas para os atuadores de abertura
    digitalWrite(DRIVER5_IN1, HIGH); // barramento de 5V
    digitalWrite(DRIVER5_IN3, HIGH); // barramento de 7.4V auxiliar
    
    delay(TEMPO_ACIONAMENTO_PAINEL);
    
    // Desliga por segurança após o tempo estimado de expansão mecânica
    digitalWrite(DRIVER5_IN1, LOW);
    digitalWrite(DRIVER5_IN3, LOW);
    
    enviarResposta("Paineis expandidos");
  }
  
  // Comando 7: Abertura Controlada da Antena com Fim de Curso
  else if (comando == "7") {
    enviarResposta("Abrindo antena...");
    
    // Aciona o canal da ponte H para rotação de avanço (Pinos 7 e 5)
    digitalWrite(DRIVER5_IN3, HIGH);
    digitalWrite(DRIVER5_IN4, LOW);
    
    // Monitora a chave mecânica de curso para interromper o motor imediatamente
    if (aguardaValorChave(SW_1, HIGH, TIMEOUT_ANTENA)) {
      digitalWrite(DRIVER5_IN3, LOW);
      enviarResposta("Antena aberta (Fim de curso atingido)");
    } else {
      digitalWrite(DRIVER5_IN3, LOW);
      enviarResposta("AVISO: Timeout na abertura da antena!");
    }
  }
  
  // Comando 8: Recolhimento / Fechamento da Antena
  else if (comando == "8") {
    enviarResposta("Recolhendo antena...");
    
    // Inverte a polaridade do canal do motor da antena
    digitalWrite(DRIVER5_IN3, LOW);
    digitalWrite(DRIVER5_IN4, HIGH);
    
    if (aguardaValorChave(SW_2, HIGH, TIMEOUT_ANTENA)) {
      digitalWrite(DRIVER5_IN4, LOW);
      enviarResposta("Antena recolhida (Fim de curso atingido)");
    } else {
      digitalWrite(DRIVER5_IN4, LOW);
      enviarResposta("AVISO: Timeout no fechamento da antena!");
    }
  }
  
  // Comando 10: EMERGÊNCIA CRÍTICA - Corta toda energia dos drivers imediatamente
  else if (comando == "10") {
    defineVelocidadeRoda(0);
    digitalWrite(DRIVER74_IN1, LOW);
    digitalWrite(DRIVER74_IN2, LOW);
    digitalWrite(DRIVER5_IN1, LOW);
    digitalWrite(DRIVER5_IN2, LOW);
    digitalWrite(DRIVER5_IN3, LOW);
    digitalWrite(DRIVER5_IN4, LOW);
    enviarResposta("EMERGENCIA ATIVADA: Todos os motores travados!");
  }
  
  // Comando 11: Teste de Pulso de Carga dos Painéis
  else if (comando == "11") {
    enviarResposta("Executando pulso de teste nos paineis");
    digitalWrite(DRIVER5_IN2, LOW);
    digitalWrite(DRIVER74_IN1, LOW);
    
    digitalWrite(DRIVER5_IN1, HIGH);
    digitalWrite(DRIVER5_IN3, HIGH);
    delay(1000);
    digitalWrite(DRIVER5_IN1, LOW);
    digitalWrite(DRIVER5_IN3, LOW);
    enviarResposta("Teste concluido");
  }
  
  // Comando 13: Teste Dinâmico da Roda de Reação (Atuador Principal)
  else if (comando == "13") {
    enviarResposta("Testando roda de reacao...");
    defineVelocidadeRoda(150);  // Rotação Horária
    delay(2000);
    defineVelocidadeRoda(-150); // Rotação Anti-horária
    delay(2000);
    defineVelocidadeRoda(0);    // Frenagem
    enviarResposta("Teste de atuador concluido com sucesso");
  }
  
  else if (comando == "14") {
    enviarResposta("MECANISMOS_PRONTOS");
  }
  else {
    enviarResposta("CMD_IGNORADO_OU_INVALIDO");
  }
}

#endif // MODO_TESTE_COMUNICACAO