#include <Wire.h>
#include <Adafruit_INA219.h>
#include <SoftwareSerial.h>

// ---------- Comunicação com o Computador de Bordo (OBC) ----------
#define RX_SUP 8  // Recebe dados do TX do Heltec V3
#define TX_SUP 9  // Envia dados para o RX do Heltec V3
SoftwareSerial suprimentoSerial(RX_SUP, TX_SUP);

// ---------- INA219 ----------
Adafruit_INA219 ina219;

// ---------- Configuração ----------
#define ENDERECO_SUPRIMENTO 1  // Identificador desta placa escrava
#define SERIAL_BAUDRATE 9600   // Baudrate padrão do projeto
#define SERIAL_DEBUG_ENABLE 1  // Habilita monitoramento no PC via USB

// ---------- Variáveis de Controle ----------
String bufferSerial = "";

// ===========================================
// --- FUNÇÕES AUXILIARES ---
// ===========================================

// Função para limpar caracteres especiais invisíveis da serial
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

// Envia a resposta formatada de volta para o Computador de Bordo
void enviarResposta(String dados) {
  // Formato final: "1:DADOS"
  String resposta = String(ENDERECO_SUPRIMENTO) + ":" + dados;
  
  suprimentoSerial.println(resposta);
  suprimentoSerial.flush(); 
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.print(">>> Enviando para OBC: ");
    Serial.println(resposta);
  }
}

// Coleta os dados do INA219 e envia estruturado para o OBC
void enviarDadosTelemetria() {
  float busVoltage = 0.0f;
  float current_mA = 0.0f;
  float current_A = 0.0f;
  float power_W = 0.0f;
  
  // Leitura dos registradores do INA219
  busVoltage = ina219.getBusVoltage_V();
  current_mA = ina219.getCurrent_mA();
  
  // Conversão de escala para o formato float que o OBC espera receber
  current_A = current_mA / 1000.0f;
  power_W = busVoltage * current_A;
  
  // Formata a string exata que o OBC vai quebrar no indexOf: "TENSÃO:CORRENTE:POTÊNCIA"
  String dados = String(busVoltage, 2) + ":" + 
                 String(current_A, 3) + ":" + 
                 String(power_W, 2);
  
  enviarResposta(dados);
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.print("[INA219] V: "); Serial.print(busVoltage);
    Serial.print("V | I: "); Serial.print(current_A, 3);
    Serial.print("A | P: "); Serial.print(power_W); Serial.println("W");
  }
}

// Processa os comandos recebidos do Computador de Bordo
void processarComando(String comando) {
  comando = limparString(comando);
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.print("Comando reconhecido: '");
    Serial.print(comando);
    Serial.println("'");
  }
  
  // Responde à solicitação de telemetria elétrica do OBC
  if (comando == "REQ_INA" || comando == "REQ:1") {
    enviarDadosTelemetria();
    return;
  }

  if (comando == "REQ_STATUS") {
    enviarResposta("STATUS:OPERACIONAL");
    return;
  }
  
  // Comando de teste de comunicação (Handshake inicial)
  if (comando == "0") {
    enviarResposta("Suprimento pronto");
  }
  else {
    enviarResposta("CMD_DESCONHECIDO: " + comando);
  }
}

// ===========================================
// --- SETUP ---
// ============================================
void setup() 
{
  Serial.begin(SERIAL_BAUDRATE);          // USB de Debug local do PC
  suprimentoSerial.begin(SERIAL_BAUDRATE); // Link com o Computador de Bordo
  
  delay(500);
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.println("\n=== PLACA DE SUPRIMENTO (EPS) ===");
    Serial.print("Endereço do Subsistema: "); Serial.println(ENDERECO_SUPRIMENTO);
    Serial.println("Pinos da Serial OBC: RX(8), TX(9)");
  }

  // Inicializa barramento I2C local do Arduino Nano
  Wire.begin();

  if (!ina219.begin()) {
    if (SERIAL_DEBUG_ENABLE) {
      Serial.println("ERRO CRÍTICO: INA219 não encontrado no barramento I2C!");
    }
  } else {
    // Calibração para range de baterias de satélites (até 32V e precisão de até 2A)
    ina219.setCalibration_32V_2A();
    if (SERIAL_DEBUG_ENABLE) {
      Serial.println("INA219 inicializado com sucesso.");
    }
  }

  delay(1000);
  
  // Sinaliza ao OBC que o subsistema de energia terminou o boot completo
  enviarResposta("Suprimento inicializado");
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.println("Subsistema pronto e aguardando chamadas do OBC...");
    Serial.println("=============================================\n");
  }
}

// ===========================================
// --- LOOP PRINCIPAL ---
// ============================================
void loop() 
{
  // Escuta os pacotes vindos da placa mestre (OBC)
  if (suprimentoSerial.available()) {
    char c = suprimentoSerial.read();
    
    if (c == '\n' || c == '\r') {
      if (bufferSerial.length() > 0) {
        String mensagem = limparString(bufferSerial);
        
        if (SERIAL_DEBUG_ENABLE) {
          Serial.print("<<< Recebido do OBC: '");
          Serial.print(mensagem);
          Serial.println("'");
        }
        
        int idx = mensagem.indexOf(':');
        if (idx > 0) {
          // Protocolo padronizado: DESTINO:COMANDO
          String dstStr = mensagem.substring(0, idx);
          int dstAddr = dstStr.toInt();
          String comando = mensagem.substring(idx + 1);
          comando.trim();

          if (dstAddr == ENDERECO_SUPRIMENTO || dstAddr == 0) {
            processarComando(comando);
          }
        } else {
          // Se receber o comando puro sem cabeçalho, processa direto
          processarComando(mensagem);
        }
        
        bufferSerial = ""; // Reseta o buffer para a próxima mensagem
      }
    } else {
      bufferSerial += c;
    }
  }
}