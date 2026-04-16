#include <Wire.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <Adafruit_INA219.h>

// ---------- DS18B20 ----------
#define DS18B20_1_PIN 2
#define DS18B20_2_PIN 3
#define DS18B20_3_PIN 4

OneWire oneWire1(DS18B20_1_PIN);
DallasTemperature sensor1(&oneWire1);

OneWire oneWire2(DS18B20_2_PIN);
DallasTemperature sensor2(&oneWire2);

OneWire oneWire3(DS18B20_3_PIN);
DallasTemperature sensor3(&oneWire3);

// ---------- INA219 ----------
Adafruit_INA219 ina219;

// ---------- Controle Térmico ----------
#define mosfet 6   // Gate do MOSFET

// ---------- Configuração ----------
#define ENDERECO_SUPRIMENTO 1  // Endereço para identificar este módulo
#define SERIAL_BAUDRATE 9600   // Alterado para 9600 para compatibilidade
#define SERIAL_DEBUG_ENABLE 1  // Habilita debug na Serial

// ---------- Variáveis de Controle ----------
bool dadosSolicitados = false;
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 10000; // Heartbeat a cada 10 segundos
String bufferSerial = "";

// ---------- Estados do Sistema ----------
bool aquecedorLigado = false;
unsigned long lastTempCheck = 0;
const unsigned long tempCheckInterval = 2000; // Verifica temperatura a cada 2s

// ===========================================
// --- FUNÇÕES AUXILIARES ---
// ===========================================

// Função para limpar caracteres não imprimíveis de uma string
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

// Função para enviar resposta formatada para a placa mestre
void enviarResposta(String dados) {
  String resposta = String(ENDERECO_SUPRIMENTO) + ":" + dados;
  
  // Envia com println para garantir a quebra de linha
  Serial.println(resposta);
  Serial.flush();  // Garante que todos os dados sejam enviados
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.print(">>> Enviando: ");
    Serial.println(resposta);
  }
}

// Função para enviar dados de telemetria do suprimento
void enviarDadosTelemetria() {
  // Lê os sensores
  sensor1.requestTemperatures();
  sensor3.requestTemperatures();
  
  float temp1 = sensor1.getTempCByIndex(0);
  float temp3 = sensor3.getTempCByIndex(0);
  
  // Valida temperaturas
  bool t1_valida = (temp1 > -100.0 && temp1 < 125.0);
  bool t3_valida = (temp3 > -100.0 && temp3 < 125.0);
  
  // Se inválido, coloca 0.0
  if (!t1_valida) temp1 = 0.0;
  if (!t3_valida) temp3 = 0.0;
  
  // Lê INA219
  float busVoltage = ina219.getBusVoltage_V();
  float current = ina219.getCurrent_mA();
  
  // Formata os dados: temp1:temp3:tensao:corrente:aquecedor
  String dados = String(temp1, 2) + ":" + 
                 String(temp3, 2) + ":" + 
                 String(busVoltage, 2) + ":" + 
                 String(current, 2) + ":" +
                 String(aquecedorLigado ? "1" : "0");
  
  enviarResposta(dados);
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.print("Dados enviados: ");
    Serial.println(dados);
  }
}

// Função para verificar e controlar aquecedor
void verificarControleTermico() {
  sensor1.requestTemperatures();
  sensor3.requestTemperatures();
  
  float temp1 = sensor1.getTempCByIndex(0);
  float temp3 = sensor3.getTempCByIndex(0);
  
  bool t1_valida = (temp1 > -100.0 && temp1 < 125.0);
  bool t3_valida = (temp3 > -100.0 && temp3 < 125.0);
  
  // Se ambas inválidas, mantém estado atual
  if (!t1_valida && !t3_valida) {
    return;
  }
  
  // Usa a temperatura válida (prioridade para temp1)
  float tempReferencia = t1_valida ? temp1 : temp3;
  
  bool abaixo_25 = false;
  bool acima_40 = false;
  
  if (t1_valida && t3_valida) {
    abaixo_25 = (temp1 < 25.0) || (temp3 < 25.0);
    acima_40 = (temp1 >= 40.0) || (temp3 >= 40.0);
  } else {
    abaixo_25 = (tempReferencia < 25.0);
    acima_40 = (tempReferencia >= 40.0);
  }
  
  // Controle do aquecedor
  if (abaixo_25 && !aquecedorLigado) {
    digitalWrite(mosfet, HIGH);
    aquecedorLigado = true;
    if (SERIAL_DEBUG_ENABLE) {
      Serial.print("Aquecedor LIGADO - Temp: ");
      Serial.println(tempReferencia);
    }
  } else if (acima_40 && aquecedorLigado) {
    digitalWrite(mosfet, LOW);
    aquecedorLigado = false;
    if (SERIAL_DEBUG_ENABLE) {
      Serial.print("Aquecedor DESLIGADO - Temp: ");
      Serial.println(tempReferencia);
    }
  }
}

// Função para processar comandos recebidos
void processarComando(String comando) {
  comando = limparString(comando);
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.print("Processando comando: '");
    Serial.print(comando);
    Serial.println("'");
  }
  
  // Comando para solicitar dados (REQ)
  if (comando.startsWith("REQ:")) {
    if (SERIAL_DEBUG_ENABLE) {
      Serial.println("Solicitação de telemetria recebida");
    }
    enviarDadosTelemetria();
    dadosSolicitados = true;
    return;
  }
  
  // Comando para ligar aquecedor manualmente
  if (comando == "HEAT_ON") {
    digitalWrite(mosfet, HIGH);
    aquecedorLigado = true;
    enviarResposta("Aquecedor LIGADO manualmente");
    if (SERIAL_DEBUG_ENABLE) {
      Serial.println("Aquecedor ligado manualmente");
    }
  }
  // Comando para desligar aquecedor manualmente
  else if (comando == "HEAT_OFF") {
    digitalWrite(mosfet, LOW);
    aquecedorLigado = false;
    enviarResposta("Aquecedor DESLIGADO manualmente");
    if (SERIAL_DEBUG_ENABLE) {
      Serial.println("Aquecedor desligado manualmente");
    }
  }
  // Comando para status
  else if (comando == "STATUS") {
    String status = "Aquecedor:" + String(aquecedorLigado ? "ON" : "OFF");
    enviarResposta(status);
  }
  // Comando de teste (0)
  else if (comando == "0") {
    enviarResposta("Suprimento pronto");
    if (SERIAL_DEBUG_ENABLE) {
      Serial.println("Teste de comunicação OK");
    }
  }
  // Eco para outros comandos
  else {
    enviarResposta("Comando nao reconhecido: " + comando);
  }
}

// ===========================================
// --- SETUP ---
// ===========================================
void setup() 
{
  Serial.begin(SERIAL_BAUDRATE);
  
  // Aguarda inicialização
  delay(500);
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.println("\n=== PLACA DE SUPRIMENTO ===");
    Serial.print("Endereço: ");
    Serial.println(ENDERECO_SUPRIMENTO);
    Serial.print("Baudrate: ");
    Serial.println(SERIAL_BAUDRATE);
  }

  // Inicializa sensores
  sensor1.begin();
  sensor2.begin();
  sensor3.begin();
  
  pinMode(mosfet, OUTPUT);
  digitalWrite(mosfet, LOW); // garante que MOSFET inicia desligado
  aquecedorLigado = false;

  if (!ina219.begin()) 
  {
    if (SERIAL_DEBUG_ENABLE) {
      Serial.println("Falha ao encontrar INA219!");
    }
    // Continua mesmo sem INA219
  } else {
    ina219.setCalibration_32V_2A();
    if (SERIAL_DEBUG_ENABLE) {
      Serial.println("INA219 inicializado");
    }
  }

  delay(1000);
  
  // Envia mensagem de inicialização
  enviarResposta("Suprimento inicializado");
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.println("Sistema pronto!");
    Serial.println("Aguardando comandos...");
    Serial.println("Comandos disponiveis:");
    Serial.println("  REQ:1 - Solicita dados");
    Serial.println("  0 - Teste de comunicacao");
    Serial.println("  HEAT_ON - Liga aquecedor");
    Serial.println("  HEAT_OFF - Desliga aquecedor");
    Serial.println("  STATUS - Status do sistema");
    Serial.println("================================\n");
  }
}

// ===========================================
// --- LOOP PRINCIPAL ---
// ===========================================
void loop() 
{
  // === LEITURA SERIAL ===
  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      // Se encontrou fim de linha, processa o buffer
      if (bufferSerial.length() > 0) {
        String mensagem = limparString(bufferSerial);
        
        if (SERIAL_DEBUG_ENABLE) {
          Serial.print("<<< Recebido: '");
          Serial.print(mensagem);
          Serial.println("'");
        }
        
        if (mensagem.length() > 0) {
          // Verifica se tem formato endereçado
          int idx = mensagem.indexOf(':');
          
          if (idx > 0) {
            // Mensagem com endereço
            String enderecoStr = mensagem.substring(0, idx);
            int enderecoDestino = enderecoStr.toInt();
            String comando = mensagem.substring(idx + 1);
            comando.trim();
            
            // Só processa se for para este endereço ou broadcast (0)
            if (enderecoDestino == ENDERECO_SUPRIMENTO || enderecoDestino == 0) {
              processarComando(comando);
            } else {
              if (SERIAL_DEBUG_ENABLE) {
                Serial.print("Mensagem ignorada - endereço ");
                Serial.print(enderecoDestino);
                Serial.print(" != ");
                Serial.println(ENDERECO_SUPRIMENTO);
              }
            }
          } else {
            // Mensagem sem endereço - processa diretamente
            processarComando(mensagem);
          }
        }
        
        bufferSerial = "";  // Limpa o buffer
      }
    } else {
      // Adiciona caractere ao buffer
      bufferSerial += c;
    }
  }
  
  // === CONTROLE TÉRMICO AUTOMÁTICO ===
  if (millis() - lastTempCheck > tempCheckInterval) {
    lastTempCheck = millis();
    verificarControleTermico();
  }
  
  // === HEARTBEAT PERIÓDICO (opcional - desabilitado por padrão) ===
  // Só envia heartbeat se não estiver enviando dados constantemente
  /*
  if (millis() - lastHeartbeat > heartbeatInterval) {
    lastHeartbeat = millis();
    // Não envia heartbeat para não sobrecarregar
    // Apenas pisca LED interno se existir
  }
  */
  
  // Pequena pausa para não sobrecarregar
  delay(10);
}