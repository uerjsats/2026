// //Inclusão de bibliotecas de pinos
// #include "pinos_placa_v1.h"
// #include <SoftwareSerial.h>

// // ===========================================
// // --- Comunicação Serial com Placa Mestre ---
// // ===========================================
// // Definição dos pinos para SoftwareSerial
// #define RX_PIN 4    // Pino RX (recebe dados da placa mestre) - D12
// #define TX_PIN 12     // Pino TX (envia dados para placa mestre) - D4

// // Cria objeto SoftwareSerial para comunicação com a placa mestre
// SoftwareSerial controleSerial(RX_PIN, TX_PIN); // RX, TX

// // Endereço deste dispositivo na comunicação serial
// #define MEU_ENDERECO 3  // Endereço do Controle de Atitude

// // ===========================================
// // --- Controle de Atitude ---
// // ===========================================
// // Define o modo de controle (pode ser usado para alternar entre diferentes algoritmos)
// #define CONTROLLERMODE 0
// #define SERIAL_DEBUG_ENABLE 1

// //===========================================
// // --- Constantes para Painel e ANtenas  ---
// // ===========================================
// #define TEMPO_ACIONAMENTO_PAINEL 5000
// #define TIMEOUT_ANTENA 10000

// double anguloOffset = 0;          // Ângulo adicional a ser adicionado após encontrar a luz
// bool luzEncontrada = false;       // Flag indicando se já encontrou a luz
// double anguloLuzEncontrada = 0;   // Ângulo onde a luz foi encontrada
// bool executandoOffset = false;    // Flag indicando se está executando o movimento para o offset

// // ===========================================
// // --- Variáveis para Comando 8 - Toggle PID ---
// // ===========================================
// bool modoEdicaoPID = false;
// // PID
// double Kp_pos = 1.5;    // Reduzido para menos oscilação
// double Kd_pos = 0.3;
// double Kd_vel = 0.5;

// // --- ESTADOS DO SISTEMA (Máquina de Estados) ---
// enum MODOSistema 
// {
//   MODOPARADO,          // O motor de reação está desligado
//   MODOESTABILIZAR,     // Modo de controle de velocidade angular (cancelamento de giro)
//   MODOORIENTARLUZ,    // Modo de busca e travamento na fonte de luz (LDR)
//   MODOORIENTADOIS,
//   BUSCAAZIMUTH
// };

// // Variável que armazena o estado atual de operação do satélite
// MODOSistema modoAtual = MODOPARADO;

// //Variável para Azimuth
// double total = 0;
// const int numReadings = 8;
// double readings[numReadings];
// int readIndex = 0;

// //Variáveis de orientação para luz
// bool orientado = false;
// int ValorFixoVermelho, ValorFixoAzul;
// int leitura = 0;
// int leitura2 = 0;
// int menorLuz = 0;
// int anguloMaisClaro = 0;
// int controle = 0;
// double anguloDesejado = 0;
// int alvo = 0;
// // ------------------------------

// // ===========================================
// // ----- Motor de Reação -----
// // ===========================================
// // Velocidade angular alvo para a roda de reação (usado no controle)
// double velocidadeAlvo = 0;
// // Fator de microstepping do driver (influencia no cálculo de velocidade)
// #define MICROSTEPPING 4
// // Velocidade angular máxima base da roda em passos/segundo (15 RPS * 4 microsteps)
// #define VELOCIDADEMAXIMA (15 * MICROSTEPPING)
// // ------------------------------------

// // ===========================================
// // ----- TACÔMETRO (Medidor de RPM) -----
// // ===========================================
// // Velocidade angular atual da roda de reação em RPS (Revoluções por segundo)
// double velocidadeAtual = 0;
// // Variável volátil para contar os pulsos do tacômetro (interrupt)
// volatile int contagemPulsos;
// // Variável para controle de tempo da ldr1 do tacômetro
// unsigned long tempoAnteriorTaco;
// // Número de pulsos do encoder a cada volta completa do motor
// unsigned int pulsosPorVolta = 6;
// // Pino digital configurado como interrupção para o tacômetro
// const byte pinoInterrupcao = 2;
// // ------------------------------------
// bool modoEstabilizacao = false;
// bool modoAnguloAlvo = false;
// double anguloAlvo = 0;
// unsigned long ultimoEnvioAngulo = 0;

// // ===========================================
// // ------ MPU6050 (Giroscópio e Acelerômetro) -----
// // ===========================================
// #include <Wire.h>
// // Endereço I2C do módulo MPU6050
// #define ENDERECOMPU 0x68
// // Offset de calibração para corrigir o erro do giroscópio (drift)
// double erro = 0;
// #define CALIBRATION_MEASUREMENTS_COUNT 200
// // Ângulo de rotação acumulado (integrado do giroscópio)
// double alfaSat = 0;
// // Velocidade angular lida pelo giroscópio
// double omegaSat = 0;
// double yawAngle = 0, yawAngularSpeed = 0;

// // -----------------------------------------

// // ===========================================
// // Variáveis para Controle de Estabilização Otimizado
// // ===========================================
// // Velocidade atual da roda de reação (Flywheel) em passos/s
// double velocidadeAtualRoda = 0;
// // Velocidade da roda no momento em que o satélite atingiu o equilíbrio
// double velocidadeEquilibrio = 0;
// // Flag que indica se o satélite está em estado de equilíbrio (baixa velocidade angular)
// bool equilibrado = false;
// // Tolerância (em °/s) para considerar o satélite "parado" ou "equilibrado"
// const double zonaMorta = 1.5;
// // Ganho de correção aplicado quando o satélite gira muito rápido
// const double ganhoRapido = 0.03;
// // Ganho de correção aplicado quando o satélite gira lentamente
// const double ganhoLento = 0.01;
// // Aceleração máxima permitida para a roda de reação
// const double aceleracaoMaxima = 0.08;
// // ------------------------------------------

// // ===========================================
// // ------ VARIÁVEIS DE TEMPO E FILTRO ------
// // ===========================================
// // Variáveis de tempo para o loop principal e cálculo de dt
// long tempoAtual, tempoAnterior, tempoInicio;
// // Tamanho do filtro de média móvel
// const int numldr1s = 8;
// // Array para armazenar as últimas ldr1s de velocidade angular (giroscópio)
// double ldr1s[numldr1s];
// // Índice atual para a ldr1 no array (circular buffer)
// int indiceldr1 = 0;
// // Soma acumulada das ldr1s para a média
// double somaldr1s = 0;
// // Média móvel resultante da velocidade angular
// double mediaMovel = 0;
// //Booleana para saber se achou fonte luminosa
// bool achouLuz = false;
// // -----------------------------------------

// // ===========================================
// // --- Constantes de Controle de Orientação ---
// // ===========================================
// #define TOLERANCIA 50     // pode ajustar entre 100–150 se quiser
// #define MINIMOLDR 10
// #define VELOCIDADEMAX 150

// // Variáveis globais de referência de luz
// double referenciaLuz = 0;   // ângulo salvo quando a luz é encontrada
// bool luzTravada = false;    // indica se já definiu o zero da luz

// // Buffer para acumular mensagens seriais
// String bufferSerial = "";

// // ===========================================
// // --- FUNÇÕES ---
// // ===========================================

// // Função para limpar caracteres não imprimíveis de uma string
// String limparString(String str) {
//   str.trim();  // Remove espaços em branco no início e fim
  
//   // Remove caracteres de controle (\r, \n, \t, etc.)
//   String resultado = "";
//   for (unsigned int i = 0; i < str.length(); i++) {
//     char c = str.charAt(i);
//     // Aceita apenas caracteres imprimíveis (32-126) e alguns especiais
//     if ((c >= 32 && c <= 126) || c == '\n' || c == '\r') {
//       resultado += c;
//     }
//   }
  
//   // Remove \r e \n do final
//   while (resultado.length() > 0 && (resultado.charAt(resultado.length()-1) == '\r' || 
//                                      resultado.charAt(resultado.length()-1) == '\n')) {
//     resultado = resultado.substring(0, resultado.length()-1);
//   }
  
//   resultado.trim();
//   return resultado;
// }

// // Função para enviar resposta formatada para a placa mestre
// void enviarResposta(String dados) {
//   String resposta = String(MEU_ENDERECO) + ":" + dados;
//   controleSerial.println(resposta);
  
//   if (SERIAL_DEBUG_ENABLE) {
//     Serial.print("Enviando resposta: ");
//     Serial.println(resposta);
//   }
// }

// // Função para enviar dados de telemetria do controle de atitude
// void enviarDadosTelemetria() {
//   String dados = String(alfaSat, 2) + "," + 
//                  String(omegaSat, 2) + "," + 
//                  String(velocidadeAtual, 2) + "," + 
//                  String(equilibrado ? "1" : "0") + "," +
//                  String((int)modoAtual);
//   enviarResposta(dados);
// }

// void ativaPinoPorTempo(int pino1, int pino2, unsigned long tempoAtivo) {
//   digitalWrite(pino1, HIGH);           // Ativa o pino
//   digitalWrite(pino2, HIGH);
//   delay(tempoAtivo);                  // Aguarda o tempo especificado
//   digitalWrite(pino1, LOW);            // Desativa o pino
//   digitalWrite(pino2, LOW);    
// }

// int lerEntradaAnalogicaComoDigital(uint8_t pinoAnalogico, int limiar = 512) {
//   int valor = analogRead(pinoAnalogico);
//   return (valor > limiar) ? HIGH : LOW;
// }

// bool aguardaValorChave(int pinoChave, int valorEsperado, unsigned long tempoTimeout) {
//   unsigned long inicio = millis();

//   while (millis() - inicio < tempoTimeout) {
//     int valorAtual = lerEntradaAnalogicaComoDigital(pinoChave);
//     if (valorAtual == valorEsperado) {
//       return true; // Valor esperado atingido dentro do tempo
//     }
//   }

//   return false; // Timeout atingido sem atingir o valor esperado
// }

// // Função de interrupção: incrementa o contador de pulsos do tacômetro
// void contarPulso() 
// {
//   contagemPulsos++;
// }

// // Realiza a leitura do valor RAW do giroscópio (eixo Z)
// int16_t readMPU() 
// {
//   // Envia o endereço do registrador (0x47) para leitura do Gyro Z High
//   Wire.beginTransmission(ENDERECOMPU);
//   Wire.write(0x47);
//   Wire.endTransmission(false);
//   // Solicita 2 bytes (Gyro Z High e Low)
//   Wire.requestFrom(ENDERECOMPU, 2, true);
//   // Combina os 2 bytes e retorna o valor RAW
//   return Wire.read() << 8 | Wire.read();
// }

// // Função de inicialização
// void setup() 
// {
//   // Inicia comunicação Serial padrão (para debug no computador)
//   Serial.begin(9600);
  
//   // Inicia a comunicação Serial via software com a placa mestre
//   controleSerial.begin(9600);
  
//   pinMode(pinoInterrupcao, INPUT);

//   // Configuração do MPU
//   Wire.begin();
//   // Acorda o MPU (registrador Power Management 1)
//   Wire.beginTransmission(ENDERECOMPU);
//   Wire.write(0x6B);
//   Wire.write(0);
//   Wire.endTransmission(true);
//   // Configura a escala do giroscópio para +/- 1000 °/s (registrador Gyro Config, 0x1B)
//   Wire.beginTransmission(ENDERECOMPU);
//   Wire.write(0x1B);
//   Wire.write(0x10); // 0x10 = FS_SEL=2 (+/- 1000 °/s)
//   Wire.endTransmission(true);
  
//   // Calibra o MPU
//   calibrateMPU();

//   // Configura e acende o LED de status
//   pinMode(LED, OUTPUT);
//   digitalWrite(LED, 1);

//   // Inicializa as variáveis de tempo globais
//   tempoAtual = millis();
//   // Garante que dt não seja gigante no início
//   tempoAnterior = tempoAtual;
//   tempoInicio = tempoAtual;

//   // Anexa a interrupção ao pino do tacômetro (chamará contarPulso em borda de subida)
//   attachInterrupt(digitalPinToInterrupt(pinoInterrupcao), contarPulso, RISING);
//   contagemPulsos = 0;
//   tempoAnteriorTaco = 0;

//   // Configura os pinos de controle do motor como saída e desliga o motor
//   pinMode(IN1, OUTPUT);
//   pinMode(IN2, OUTPUT);
//   digitalWrite(IN1, LOW);
//   digitalWrite(IN2, LOW);
  
//   // Configura pinos dos mecanismos
//   pinMode(PAINEL_IN1, OUTPUT);
//   pinMode(PAINEL_IN2, OUTPUT);
//   pinMode(PAINEL_IN3, OUTPUT);
//   pinMode(PAINEL_IN4, OUTPUT);
//   pinMode(ANTIN1, OUTPUT);
//   pinMode(ANTIN2, OUTPUT);
  
//   // Aguarda um pouco para estabilizar
//   delay(1000);
  
//   // Envia mensagem de inicialização
//   Serial.println("Controle de Atitude inicializado!");
//   enviarResposta("Controle de Atitude inicializado");
// }

// // Função principal do programa, executada repetidamente
// void loop() 
// {
//   // === Comunicação Serial com Placa Mestre (via SoftwareSerial) ===
//   while (controleSerial.available() > 0) {
//     char c = controleSerial.read();
    
//     if (c == '\n' || c == '\r') {
//       // Se encontrou fim de linha, processa o buffer
//       if (bufferSerial.length() > 0) {
//         String mensagem = limparString(bufferSerial);
        
//         if (SERIAL_DEBUG_ENABLE) {
//           Serial.print("Recebido: '");
//           Serial.print(mensagem);
//           Serial.println("'");
//         }
        
//         if (mensagem.length() > 0) {
//           processarMensagem(mensagem);
//         }
        
//         bufferSerial = "";  // Limpa o buffer
//       }
//     } else {
//       // Adiciona caractere ao buffer
//       bufferSerial += c;
//     }
//   }
  
//   // === Execução dos modos (Máquina de Estados) ===
//   switch (modoAtual) {
//     case MODOPARADO:
//       // Não faz nada no loop, o motor já está desligado
//       break;

//     case MODOESTABILIZAR:
//       // Chama a função principal de controle de estabilização
//       estabilizar();
//       break;

//     case MODOORIENTARLUZ:
//       orientarLuz();
//       break;
//   }
// }

// // Função para processar mensagens recebidas
// void processarMensagem(String mensagem) {
//   // Verifica se a mensagem tem o formato esperado (endereço:comando)
//   int indiceDoisPontos = mensagem.indexOf(':');
  
//   if (indiceDoisPontos != -1) {
//     // Mensagem com endereço
//     String enderecoStr = mensagem.substring(0, indiceDoisPontos);
//     enderecoStr.trim();
//     int enderecoDestino = enderecoStr.toInt();
//     String comando = mensagem.substring(indiceDoisPontos + 1);
//     comando = limparString(comando);
    
//     if (SERIAL_DEBUG_ENABLE) {
//       Serial.print("Endereço: ");
//       Serial.print(enderecoDestino);
//       Serial.print(", Comando: '");
//       Serial.print(comando);
//       Serial.println("'");
//     }
    
//     // Só processa se for para este endereço ou broadcast (0)
//     if (enderecoDestino == MEU_ENDERECO || enderecoDestino == 0) {
//       processarComando(comando);
//     } else {
//       if (SERIAL_DEBUG_ENABLE) {
//         Serial.print("Mensagem ignorada - endereço ");
//         Serial.print(enderecoDestino);
//         Serial.print(" não corresponde a ");
//         Serial.println(MEU_ENDERECO);
//       }
//     }
//   } else {
//     // Mensagem sem endereço (modo legado)
//     String comando = limparString(mensagem);
//     if (SERIAL_DEBUG_ENABLE) {
//       Serial.print("Processando comando sem endereço: '");
//       Serial.print(comando);
//       Serial.println("'");
//     }
//     processarComando(comando);
//   }
// }

// // Função para processar comandos recebidos
// void processarComando(String comando) {
  
//   // Comando para solicitar dados (REQ)
//   if (comando.startsWith("REQ:")) {
//     if (SERIAL_DEBUG_ENABLE) {
//       Serial.println("Solicitação de telemetria recebida");
//     }
//     enviarDadosTelemetria();
//     return;
//   }
  
//   // Comandos numéricos (modo legado)
//   if (comando == "0") {
//     enviarResposta("Pronto para operação");
//     modoAtual = MODOPARADO;
//   }
//   else if (comando == "1") 
//   {
//     enviarResposta("Iniciando estabilização...");
//     modoAtual = MODOESTABILIZAR;
//     orientado = false;  // Reset da orientação por luz
//   }
//   else if (comando.startsWith("2:")) 
//   {
//     // Comando para orientar para ângulos específicos
//     String angulos = comando.substring(2);
//     angulos.trim();
//     int espaco = angulos.indexOf(' ');
    
//     if (espaco != -1) {
//       float angulo1 = angulos.substring(0, espaco).toFloat();
//       float angulo2 = angulos.substring(espaco + 1).toFloat();
      
//       enviarResposta("Orientando para ângulos: " + String(angulo1, 2) + " e " + String(angulo2, 2));
      
//       normaliza360(angulo1);
//       irParaAngulo(angulo1);
//       delay(1000);
//       irParaAngulo(abs(angulo1 - 360));
//       normaliza360(angulo2);
//       irParaAngulo(angulo2);
//       irParaAngulo(abs(angulo2 - 360));
//     } else {
//       enviarResposta("Erro: Formato inválido. Use 2:angulo1 angulo2");
//     }
//   }
//   else if (comando == "3") 
//   {
//     enviarResposta("Orientando pela luz...");
//     orientado = false;  // Reset para iniciar nova busca
//     modoAtual = MODOORIENTARLUZ;
//   }
//   else if (comando == "4")
//   {
//     enviarResposta("Buscando azimuth...");
//     float alfaAzimuth = girar360();
//     enviarResposta("Azimuth encontrado: " + String(alfaAzimuth, 2));
//   }
//   else if (comando == "5") 
//   {
//     enviarResposta("Motores desligados. Sistema parado.");
//     // Desliga a alimentação do motor
//     digitalWrite(IN1, LOW);
//     digitalWrite(IN2, LOW);
//     modoAtual = MODOPARADO;
//     equilibrado = false;
//     orientado = false;
//   }
//   else if(comando == "6")
//   {
//     enviarResposta("Abrindo Painéis Solares");
//     digitalWrite(LED, HIGH);
    
//     digitalWrite(PAINEL_IN4, LOW);
//     digitalWrite(PAINEL_IN2, LOW);
//     ativaPinoPorTempo(PAINEL_IN3, PAINEL_IN1, TEMPO_ACIONAMENTO_PAINEL);
//     digitalWrite(PAINEL_IN3, LOW);
//     digitalWrite(PAINEL_IN1, LOW);
    
//     digitalWrite(LED, LOW);
//     enviarResposta("Painéis solares abertos");
//   }
//   else if(comando == "7")
//   {
//     enviarResposta("Abrindo Antena");
//     digitalWrite(ANTIN1, HIGH);
//     digitalWrite(ANTIN2, LOW);
    
//     if (aguardaValorChave(SWANT1, HIGH, TEMPO_ACIONAMENTO_PAINEL)) 
//     { 
//       digitalWrite(ANTIN1, LOW);
//       digitalWrite(ANTIN2, LOW);
//       enviarResposta("Antena aberta com sucesso");
//     }
//     else 
//     {
//       enviarResposta("Erro: Timeout - chave da antena não ativou");
//       digitalWrite(ANTIN1, LOW);
//       digitalWrite(ANTIN2, LOW);
//     }
//   }
//   else if(comando == "8")
//   {
//     enviarResposta("Fechando Antena");
//     digitalWrite(LED, HIGH);
//     digitalWrite(ANTIN1, LOW);
//     digitalWrite(ANTIN2, HIGH);
    
//     if (aguardaValorChave(SWANT2, HIGH, TEMPO_ACIONAMENTO_PAINEL)) { 
//       digitalWrite(ANTIN1, LOW);
//       digitalWrite(ANTIN2, LOW);
//       enviarResposta("Antena fechada com sucesso");
//     }
//     else {
//       enviarResposta("Erro: Timeout - chave da antena não ativou");
//       digitalWrite(ANTIN1, LOW);
//       digitalWrite(ANTIN2, LOW);
//     }
//     digitalWrite(LED, LOW);
//   }
//   else if (comando == "9") 
//   {
//     if (!modoEdicaoPID) {
//       // Primeiro comando 9 - Mostra valores atuais
//       enviarResposta("Valores PID atuais - Kp:" + String(Kp_pos, 3) + 
//                     " Kd:" + String(Kd_pos, 3) + 
//                     " Kv:" + String(Kd_vel, 3));
//       enviarResposta("Envie 'PID:Kp Kd Kv' para alterar");
//       modoEdicaoPID = true;
//     }
//   }
//   else if (comando.startsWith("PID:")) {
//     // Comando para ajustar PID: "PID:Kp Kd Kv"
//     String valores = comando.substring(4);
//     valores.trim();
//     processarValoresPID(valores);
//     modoEdicaoPID = false;
//   }
//   else if(comando == "11")
//   {
//     // Teste de painéis solares
//     enviarResposta("Testando painéis solares...");
    
//     // Teste de abertura
//     digitalWrite(PAINEL_IN4, LOW);
//     digitalWrite(PAINEL_IN2, LOW);
//     ativaPinoPorTempo(PAINEL_IN3, PAINEL_IN1, 1000);
//     digitalWrite(PAINEL_IN3, LOW);
//     digitalWrite(PAINEL_IN1, LOW);
//     delay(500);
    
//     // Teste de fechamento
//     digitalWrite(PAINEL_IN3, LOW);
//     digitalWrite(PAINEL_IN1, LOW);
//     ativaPinoPorTempo(PAINEL_IN4, PAINEL_IN2, 1000);
//     digitalWrite(PAINEL_IN4, LOW);
//     digitalWrite(PAINEL_IN2, LOW);
    
//     enviarResposta("Teste de painéis concluído");
//   }
//   else if(comando == "13")
//   {
//     // Teste de atuador
//     enviarResposta("Testando atuador (motor de reação)...");
    
//     // Teste de rotação em baixa velocidade
//     defineVelocidade(30);
//     delay(2000);
//     defineVelocidade(-30);
//     delay(2000);
//     defineVelocidade(0);
    
//     enviarResposta("Teste do atuador concluído");
//   }
//   else if(comando == "14")
//   {
//     // Status dos mecanismos
//     String status = "Status: Motor=" + String(velocidadeAtual, 1) + "rps, ";
//     status += "Angulo=" + String(alfaSat, 1) + "°, ";
//     status += "Modo=" + String((int)modoAtual) + ", ";
//     status += "Estabilizado=" + String(equilibrado ? "Sim" : "Nao");
    
//     enviarResposta(status);
//   }
//   else 
//   {
//     enviarResposta("ERRO: Comando inválido: " + comando);
//     if (SERIAL_DEBUG_ENABLE) {
//       Serial.print("Comando não reconhecido: '");
//       Serial.print(comando);
//       Serial.println("'");
//     }
//   }
// }

// // Função que controla a velocidade e o sentido do Motor de Corrente Contínua
// void defineVelocidade(double velocidadeAlvo) 
// {
//   // Limite mínimo de PWM para que o motor comece a girar
//   int pwmMin = 20;
//   // Limite máximo de PWM (o motor não precisa de 255 para máxima performance aqui)
//   int pwmMax = 150;

//   // Variável para armazenar o valor de PWM calculado
//   int valorPwm;
//   // Relação da velocidade alvo (absoluta) com a velocidade máxima de referência (60 RPS * 4 microsteps)
//   double relacaoVelocidade = fabs(velocidadeAlvo) / (60 * MICROSTEPPING);

//   // --- Mapeamento de Curva Não-Linear (Otimizada) ---
//   // Mapeia a velocidade para o PWM em seções para melhorar o torque em baixas velocidades
//   if (relacaoVelocidade < 0.2)
//   {
//     // 0% a 20% da velocidade máxima -> mapeia pwmMin (20) a 40
//     valorPwm = map(relacaoVelocidade * 100, 0, 20, pwmMin, 40);
//   } 
//   else if (relacaoVelocidade < 0.5)
//   {
//     // 20% a 50% da velocidade máxima -> mapeia 40 a 80
//     valorPwm = map(relacaoVelocidade * 100, 20, 50, 40, 80);
//   } 
//   else
//   {
//     // 50% a 100% da velocidade máxima -> mapeia 80 a pwmMax (150)
//     valorPwm = map(relacaoVelocidade * 100, 50, 100, 80, pwmMax);
//   }

//   // Garante que o valorPwm esteja dentro dos limites definidos
//   valorPwm = constrain(valorPwm, pwmMin, pwmMax);

//   // Define o sentido e a aplicação do PWM
//   if (fabs(velocidadeAlvo) < 1.5) 
//   {
//     // Desliga o motor se a velocidade alvo for muito baixa (parada ou inércia)
//     digitalWrite(IN1, LOW);
//     digitalWrite(IN2, LOW);
//   } 
//   else if (velocidadeAlvo > 0) 
//   {
//     // Gira em um sentido (IN1 = PWM, IN2 = LOW)
//     analogWrite(IN1, valorPwm);
//     digitalWrite(IN2, LOW);
//   } 
//   else 
//   {
//     // Gira no sentido oposto (IN1 = LOW, IN2 = PWM)
//     digitalWrite(IN1, LOW);
//     analogWrite(IN2, valorPwm);
//   }
// }

// // ==== Função responsável por estabilizar o Satélite (cancelar o giro) =====
// void estabilizar()
// {
//   // --- Leitura do Tacômetro ---
//   // Calcula a velocidade do motor a cada 1000 ms (1 segundo)
//   if (millis() - tempoAnteriorTaco >= 1000) 
//   {
//     // Desabilita a interrupção para leitura segura
//     detachInterrupt(0);
//     // Calcula a velocidade em RPS (Revoluções por Segundo)
//     velocidadeAtual = (60.0 * 1000.0 / pulsosPorVolta) / (double)(millis() - tempoAnteriorTaco) * contagemPulsos;
//     // Atualiza o tempo de referência
//     tempoAnteriorTaco = millis();
//     // Zera o contador de pulsos
//     contagemPulsos = 0;
//     // Reabilita a interrupção
//     attachInterrupt(digitalPinToInterrupt(pinoInterrupcao), contarPulso, RISING);
//   }

//   // --- Loop de Controle (Executa a cada 10ms) ---
//   if (millis() - tempoAtual > 10) 
//   {
//     tempoAnterior = tempoAtual;
//     tempoAtual = millis();

//     // Leitura do MPU (converte RAW para °/s)
//     omegaSat = ((double)readMPU() - erro) / 32.768;

//     // --- FILTRO DE MÉDIA MÓVEL (Rolling Average) ---
//     // Subtrai a leitura mais antiga da soma
//     somaldr1s = somaldr1s - ldr1s[indiceldr1];
//     // Adiciona a nova leitura ao array
//     ldr1s[indiceldr1] = omegaSat;
//     // Adiciona a nova leitura à soma
//     somaldr1s = somaldr1s + ldr1s[indiceldr1];
//     // Avança o índice (circular)
//     indiceldr1 = indiceldr1 + 1;
//     // Reseta o índice se atingir o final do array
//     if (indiceldr1 >= numldr1s) indiceldr1 = 0;
//     // Calcula a média móvel (velocidade angular filtrada)
//     mediaMovel = somaldr1s / numldr1s;

//     // *** LIMITE DINÂMICO DE VELOCIDADE DA RODA DE REAÇÃO ***
//     // A roda pode acelerar mais se o satélite estiver girando mais rápido
//     // dynamicMaxSpeed = (Velocidade Base) * (1 + |Giro do Satélite| / 10)
//     double velocidade_maxima_dinamica = VELOCIDADEMAXIMA * (1.0 + fabs(mediaMovel) / 10.0);

//     // Limite máximo absoluto para segurança da roda
//     const double VELOCIDADE_MAXIMA_ABSOLUTA = 60 * MICROSTEPPING; // Ex: 60 RPS
//     if (velocidade_maxima_dinamica > VELOCIDADE_MAXIMA_ABSOLUTA) 
//     {
//       velocidade_maxima_dinamica = VELOCIDADE_MAXIMA_ABSOLUTA;
//     }

//     // --- ALGORITMO DE CONTROLE DE ESTABILIZAÇÃO ---
//     double correcao = 0;

//     if (!equilibrado) { // Modo de Desaceleração Rápida
//       if (fabs(mediaMovel) > zonaMorta) 
//       {
//         // Cálculo do Ganho Adaptativo (aumenta com a velocidade angular do satélite)
//         double fator_velocidade = 1.0 + (fabs(mediaMovel) / 20.0);
//         // Usa ganho rápido para giros fortes e ganho lento para giros leves
//         double ganho_adaptativo = (fabs(mediaMovel) > 5.0) ? ganhoRapido : ganhoLento;
//         // Calcula a correção (sentido oposto ao giro do satélite)
//         correcao = mediaMovel * -ganho_adaptativo * fator_velocidade;
//         // LED ligado: satélite não estabilizado
//         digitalWrite(LED, 1);
//       } 
//       else 
//       {
//         // Entra no estado de equilíbrio
//         equilibrado = true;
//         velocidadeEquilibrio = velocidadeAtualRoda;
//         // LED desligado: satélite estabilizado
//         digitalWrite(LED, 0);
//       }

//       // --- Suavização da Correção (Limitador de Aceleração/Jerk) ---
//       static double ultimaCorrecao = 0;
//       // Define o limite máximo de mudança na correção
//       double mudancaMaxima = aceleracaoMaxima * (1.0 + fabs(mediaMovel) / 15.0);

//       // Limita a variação da correção para evitar picos
//       if (fabs(correcao - ultimaCorrecao) > mudancaMaxima) 
//       {
//         if (correcao > ultimaCorrecao) 
//         {
//           correcao = ultimaCorrecao + mudancaMaxima;
//         } 
//         else 
//         {
//           correcao = ultimaCorrecao - mudancaMaxima;
//         }
//       }
//       ultimaCorrecao = correcao;

//       // Aplica a correção à velocidade da roda de reação
//       velocidadeAtualRoda += correcao;

//     } 
//     else 
//     { // Modo Equilibrado (Apenas mantém a velocidade base)
//       // Mantém a velocidade da roda na velocidade registrada no equilíbrio
//       velocidadeAtualRoda = velocidadeEquilibrio;

//       // Ajuste fino: Se houver pequeno desvio, faz uma micro-correção
//       if (fabs(mediaMovel) > zonaMorta * 1.5) 
//       {
//         double correcao_fina = mediaMovel * -0.005;
//         velocidadeAtualRoda += correcao_fina;
//         velocidadeEquilibrio = velocidadeAtualRoda; // Atualiza a velocidade base
//       }

//       // Se o desequilíbrio for muito grande, retorna ao modo de Desaceleração Rápida
//       if (fabs(mediaMovel) > zonaMorta * 4) 
//       {
//         equilibrado = false;
//         digitalWrite(LED, 1);
//       }
//     }

//     // *** APLICA LIMITE DINÂMICO À VELOCIDADE FINAL DA RODA ***
//     if (velocidadeAtualRoda > velocidade_maxima_dinamica) velocidadeAtualRoda = velocidade_maxima_dinamica;
//     else if (velocidadeAtualRoda < -velocidade_maxima_dinamica) velocidadeAtualRoda = -velocidade_maxima_dinamica;

//     // Define a velocidade alvo do motor
//     velocidadeAlvo = velocidadeAtualRoda;

//     // Aplica a velocidade ao motor DC
//     defineVelocidade(velocidadeAlvo);
//   }
// }

// int girar360() {
//   anguloatual();
//   int angulobase = alfaSat;
//   int anguloatual_val = alfaSat;
//   menorLuz = 0; // Reset para nova busca
  
//   defineVelocidade(80); // Velocidade reduzida para busca mais precisa
  
//   unsigned long inicioBusca = millis();
//   const unsigned long tempoMaximoBusca = 30000; // 30 segundos máximo
  
//   while((abs(normaliza360(anguloatual_val) - normaliza360(angulobase)) > 30) && 
//         (millis() - inicioBusca < tempoMaximoBusca)) {
//     alvo = mirarluz();
//     anguloatual_val = alfaSat;
    
//     if(SERIAL_DEBUG_ENABLE) {
//       Serial.print("Diferença angular: ");
//       Serial.println(abs(normaliza360(anguloatual_val) - normaliza360(angulobase)));
//     }
    
//     delay(50);
//   }
  
//   defineVelocidade(0);
//   delay(500); // Pequena pausa antes de estabilizar
  
//   if(SERIAL_DEBUG_ENABLE) {
//     Serial.print("Ângulo mais claro encontrado: ");
//     Serial.println(alvo);
//   }
  
//   return alvo;
// }

// void calibrateMPU() {
//   erro = 0;
//   for (int i = 0; i < CALIBRATION_MEASUREMENTS_COUNT; i++) {
//     erro += readMPU();
//     delay(20);
//   }
//   erro = erro / (double)CALIBRATION_MEASUREMENTS_COUNT;
// }

// int mirarluz() {
//   anguloatual();
//   int ldr1 = analogRead(LDRAZUL);
//   int ldr0 = analogRead(LDRVERMELHO);

//   if (abs(ldr1 - ldr0) < TOLERANCIA) {
//     int mediaLuz = (ldr1 + ldr0) / 2;
//     if (mediaLuz > menorLuz) {
//       anguloMaisClaro = alfaSat;
//       menorLuz = mediaLuz;
//     }
//   }
  
//   delay(100); // Reduzido para melhor responsividade
//   return anguloMaisClaro;
// }

// double normaliza360(double angulo) {
//   angulo = fmod(angulo, 360);
//   if (angulo < 0) angulo += 360;
//   return angulo;
// }

// void anguloatual() {
//   if (millis() - tempoAnterior >= 10) { // >= para garantir execução exata
//     tempoAtual = tempoAnterior;
//     tempoAnterior = millis();
//     double deltaT = (tempoAnterior - tempoAtual) / 1000.0;

//     // Medida do giroscópio
//     omegaSat = ((double)readMPU() - erro) / 32.768;
//     omegaSat += 1.057; // Correção da média

//     // Filtro mais suave para o giroscópio
//     if (fabs(omegaSat) > 0.08) { // Limite reduzido
//       alfaSat += (omegaSat * deltaT);
//     }

//     // Normaliza ângulo entre -180 e 180
//     while (alfaSat <= -180) alfaSat += 360;
//     while (alfaSat > 180) alfaSat -= 360;

//     // Filtro de média móvel
//     total -= readings[readIndex];
//     readings[readIndex] = omegaSat;
//     total += readings[readIndex];
//     readIndex = (readIndex + 1) % numReadings;
//     mediaMovel = total / numReadings;
//   }
// }

// void irParaAngulo(double anguloAlvo) {
//   anguloatual();

//   double erroAngulo = anguloAlvo - alfaSat;
//   while (erroAngulo > 180) erroAngulo -= 360;
//   while (erroAngulo < -180) erroAngulo += 360;

//   // Controle PID simplificado
//   double comando = Kp_pos * erroAngulo;

//   static double erroAnterior = 0;
//   double derivadaErro = erroAngulo - erroAnterior;
//   erroAnterior = erroAngulo;
//   comando += Kd_pos * derivadaErro;

//   comando -= Kd_vel * omegaSat;

//   // Saturação mais conservadora
//   comando = constrain(comando, -120, 120);

//   // Condição de parada mais rigorosa
//   if (fabs(erroAngulo) < 3.0 && fabs(omegaSat) < 1.0) {
//     defineVelocidade(0);
//     estabilizar();
//     if(SERIAL_DEBUG_ENABLE) {
//       Serial.println("Alvo atingido e estabilizado.");
//     }
//   } else {
//     defineVelocidade(comando);
//   }

//   if(SERIAL_DEBUG_ENABLE) {
//     Serial.print("Yaw: ");
//     Serial.print(alfaSat);
//     Serial.print(" | Alvo: ");
//     Serial.print(anguloAlvo);
//     Serial.print(" | Erro: ");
//     Serial.print(erroAngulo);
//     Serial.print(" | Cmd: ");
//     Serial.println(comando);
//   }
// }

// void orientarLuz()
// {
//   anguloatual();
//   if (!orientado)
//   {
//     anguloDesejado = girar360();
//     orientado = true;
//   }
//   irParaAngulo(anguloDesejado);
// }

// // ===========================================
// // --- Função para Processar Valores PID ---
// // ===========================================
// void processarValoresPID(String valores) {
//   // Divide a string pelos espaços
//   int primeiroEspaco = valores.indexOf(' ');
//   int segundoEspaco = valores.indexOf(' ', primeiroEspaco + 1);
  
//   if (primeiroEspaco == -1 || segundoEspaco == -1) {
//     enviarResposta("Erro: Formato inválido! Use: KP KD KV");
//     return;
//   }
  
//   String kpStr = valores.substring(0, primeiroEspaco);
//   String kdStr = valores.substring(primeiroEspaco + 1, segundoEspaco);
//   String kvStr = valores.substring(segundoEspaco + 1);
  
//   kpStr.trim();
//   kdStr.trim();
//   kvStr.trim();
  
//   float novoKp = kpStr.toFloat();
//   float novoKd = kdStr.toFloat();
//   float novoKv = kvStr.toFloat();
  
//   // Validação dos valores
//   bool valoresValidos = true;
  
//   if (novoKp <= 0 || novoKp > 10.0) {
//     enviarResposta("Erro: Kp deve ser entre 0.1 e 10.0");
//     valoresValidos = false;
//   }
  
//   if (novoKd < 0 || novoKd > 5.0) {
//     enviarResposta("Erro: Kd deve ser entre 0 e 5.0");
//     valoresValidos = false;
//   }
  
//   if (novoKv < 0 || novoKv > 5.0) {
//     enviarResposta("Erro: Kv deve ser entre 0 e 5.0");
//     valoresValidos = false;
//   }
  
//   if (valoresValidos) {
//     Kp_pos = novoKp;
//     Kd_pos = novoKd;
//     Kd_vel = novoKv;
    
//     enviarResposta("PID atualizado - Kp:" + String(Kp_pos, 3) + 
//                   " Kd:" + String(Kd_pos, 3) + 
//                   " Kv:" + String(Kd_vel, 3));
//   }
// }


//Inclusão de bibliotecas de pinos
#include "pinos_placa_v1.h"
#include <SoftwareSerial.h>

// ===========================================
// --- Comunicação Serial com Placa Mestre ---
// ===========================================
// Definição dos pinos para SoftwareSerial
#define RX_PIN 4    // Pino RX (recebe dados da placa mestre)
#define TX_PIN 12   // Pino TX (envia dados para placa mestre)

// Cria objeto SoftwareSerial para comunicação com a placa mestre
SoftwareSerial controleSerial(RX_PIN, TX_PIN); // RX, TX

// Endereço deste dispositivo na comunicação serial
#define MEU_ENDERECO 3  // Endereço do Controle de Atitude

// ===========================================
// --- Modo de Teste de Comunicação ---
// ===========================================
#define MODO_TESTE_COMUNICACAO 1  // Coloque 1 para testar comunicação, 0 para modo normal

// ===========================================
// --- Controle de Atitude ---
// ===========================================
#define CONTROLLERMODE 0
#define SERIAL_DEBUG_ENABLE 1

//===========================================
// --- Constantes para Painel e Antenas  ---
// ===========================================
#define TEMPO_ACIONAMENTO_PAINEL 5000
#define TIMEOUT_ANTENA 10000

double anguloOffset = 0;
bool luzEncontrada = false;
double anguloLuzEncontrada = 0;
bool executandoOffset = false;

bool modoEdicaoPID = false;
double Kp_pos = 1.5;
double Kd_pos = 0.3;
double Kd_vel = 0.5;

enum MODOSistema 
{
  MODOPARADO,
  MODOESTABILIZAR,
  MODOORIENTARLUZ,
  MODOORIENTADOIS,
  BUSCAAZIMUTH
};

MODOSistema modoAtual = MODOPARADO;

double total = 0;
const int numReadings = 8;
double readings[numReadings];
int readIndex = 0;

bool orientado = false;
int ValorFixoVermelho, ValorFixoAzul;
int leitura = 0;
int leitura2 = 0;
int menorLuz = 0;
int anguloMaisClaro = 0;
int controle = 0;
double anguloDesejado = 0;
int alvo = 0;

double velocidadeAlvo = 0;
#define MICROSTEPPING 4
#define VELOCIDADEMAXIMA (15 * MICROSTEPPING)

double velocidadeAtual = 0;
volatile int contagemPulsos;
unsigned long tempoAnteriorTaco;
unsigned int pulsosPorVolta = 6;
const byte pinoInterrupcao = 2;

bool modoEstabilizacao = false;
bool modoAnguloAlvo = false;
double anguloAlvo = 0;
unsigned long ultimoEnvioAngulo = 0;

#include <Wire.h>
#define ENDERECOMPU 0x68
double erro = 0;
#define CALIBRATION_MEASUREMENTS_COUNT 200
double alfaSat = 0;
double omegaSat = 0;
double yawAngle = 0, yawAngularSpeed = 0;

double velocidadeAtualRoda = 0;
double velocidadeEquilibrio = 0;
bool equilibrado = false;
const double zonaMorta = 1.5;
const double ganhoRapido = 0.03;
const double ganhoLento = 0.01;
const double aceleracaoMaxima = 0.08;

long tempoAtual, tempoAnterior, tempoInicio;
const int numldr1s = 8;
double ldr1s[numldr1s];
int indiceldr1 = 0;
double somaldr1s = 0;
double mediaMovel = 0;
bool achouLuz = false;

#define TOLERANCIA 50
#define MINIMOLDR 10
#define VELOCIDADEMAX 150

double referenciaLuz = 0;
bool luzTravada = false;

String bufferSerial = "";
unsigned long lastHeartbeat = 0;
const unsigned long heartbeatInterval = 5000; // Envia heartbeat a cada 5 segundos

// ===========================================
// --- FUNÇÕES ---
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
  
  // Envia múltiplas vezes para garantir
  for(int i = 0; i < 2; i++) {
    controleSerial.println(resposta);
    delay(5);
  }
  
  if (SERIAL_DEBUG_ENABLE) {
    Serial.print(">>> Enviando: ");
    Serial.println(resposta);
  }
}

void enviarDadosTelemetria() {
  String dados = String(alfaSat, 2) + "," + 
                 String(omegaSat, 2) + "," + 
                 String(velocidadeAtual, 2) + "," + 
                 String(equilibrado ? "1" : "0") + "," +
                 String((int)modoAtual);
  enviarResposta(dados);
}

void ativaPinoPorTempo(int pino1, int pino2, unsigned long tempoAtivo) {
  digitalWrite(pino1, HIGH);
  digitalWrite(pino2, HIGH);
  delay(tempoAtivo);
  digitalWrite(pino1, LOW);
  digitalWrite(pino2, LOW);    
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

void contarPulso() 
{
  contagemPulsos++;
}

int16_t readMPU() 
{
  Wire.beginTransmission(ENDERECOMPU);
  Wire.write(0x47);
  Wire.endTransmission(false);
  Wire.requestFrom(ENDERECOMPU, 2, true);
  return Wire.read() << 8 | Wire.read();
}

// ===========================================
// --- MODO DE TESTE DE COMUNICAÇÃO ---
// ===========================================
#if MODO_TESTE_COMUNICACAO == 1

void setup() {
  Serial.begin(9600);
  controleSerial.begin(9600);
  
  pinMode(LED, OUTPUT);
  digitalWrite(LED, HIGH);
  
  Serial.println("\n\n=== MODO TESTE COMUNICAÇÃO ===");
  Serial.println("Controle de Atitude - Teste Serial");
  Serial.print("RX pino: "); Serial.println(RX_PIN);
  Serial.print("TX pino: "); Serial.println(TX_PIN);
  Serial.print("Endereço: "); Serial.println(MEU_ENDERECO);
  Serial.println("Envie comandos via Serial Monitor (9600 baud)");
  Serial.println("Comandos disponíveis: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9");
  Serial.println("Ou envie '3:0' para testar formato endereçado");
  Serial.println("================================\n");
  
  delay(1000);
  enviarResposta("MODO TESTE - Pronto");
}

void loop() {
  // Verifica Serial do PC (debug)
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.length() > 0) {
      Serial.print("Recebido do PC: '");
      Serial.print(cmd);
      Serial.println("'");
      
      // Encaminha para o controle serial
      controleSerial.println(cmd);
      Serial.println("Encaminhado para controleSerial");
    }
  }
  
  // Verifica controleSerial (Heltec)
  if (controleSerial.available()) {
    String msg = controleSerial.readStringUntil('\n');
    msg.trim();
    if (msg.length() > 0) {
      Serial.print("<<< Recebido do Heltec: '");
      Serial.print(msg);
      Serial.println("'");
      
      // Processa mensagem
      if (msg == "0") {
        enviarResposta("ACK:0 - Pronto");
        Serial.println("Comando 0 reconhecido!");
      } else if (msg == "1") {
        enviarResposta("ACK:1 - Estabilizando");
        Serial.println("Comando 1 reconhecido!");
      } else {
        // Eco para qualquer outra mensagem
        String resposta = "ECO:" + msg;
        enviarResposta(resposta);
      }
    }
  }
  
  // Heartbeat periódico
  if (millis() - lastHeartbeat > heartbeatInterval) {
    lastHeartbeat = millis();
    enviarResposta("HEARTBEAT");
    Serial.println("Heartbeat enviado");
  }
  
  // Pisca LED para indicar que está vivo
  static unsigned long lastBlink = 0;
  if (millis() - lastBlink > 1000) {
    lastBlink = millis();
    digitalWrite(LED, !digitalRead(LED));
  }
}

#else

// ===========================================
// --- MODO NORMAL DE OPERAÇÃO ---
// ===========================================

void setup() 
{
  Serial.begin(9600);
  controleSerial.begin(9600);
  
  // Aguarda a serial inicializar
  delay(500);
  
  Serial.println("\n=== CONTROLE DE ATITUDE ===");
  Serial.print("RX: "); Serial.println(RX_PIN);
  Serial.print("TX: "); Serial.println(TX_PIN);
  
  pinMode(pinoInterrupcao, INPUT);
  
  Wire.begin();
  Wire.beginTransmission(ENDERECOMPU);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);
  Wire.beginTransmission(ENDERECOMPU);
  Wire.write(0x1B);
  Wire.write(0x10);
  Wire.endTransmission(true);
  
  calibrateMPU();
  
  pinMode(LED, OUTPUT);
  digitalWrite(LED, 1);
  
  tempoAtual = millis();
  tempoAnterior = tempoAtual;
  tempoInicio = tempoAtual;
  
  attachInterrupt(digitalPinToInterrupt(pinoInterrupcao), contarPulso, RISING);
  contagemPulsos = 0;
  tempoAnteriorTaco = 0;
  
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  
  pinMode(PAINEL_IN1, OUTPUT);
  pinMode(PAINEL_IN2, OUTPUT);
  pinMode(PAINEL_IN3, OUTPUT);
  pinMode(PAINEL_IN4, OUTPUT);
  pinMode(ANTIN1, OUTPUT);
  pinMode(ANTIN2, OUTPUT);
  
  delay(1000);
  
  Serial.println("Sistema inicializado!");
  
  // Envia mensagem de inicialização várias vezes
  for(int i = 0; i < 3; i++) {
    enviarResposta("Controle de Atitude inicializado");
    delay(100);
  }
}

void loop() 
{
  // === LEITURA SERIAL SIMPLIFICADA ===
  if (controleSerial.available()) {
    String msg = controleSerial.readString();
    msg.trim();
    
    if (msg.length() > 0) {
      Serial.print("RX: '");
      Serial.print(msg);
      Serial.println("'");
      
      // Tenta encontrar um comando válido na string
      int idx = msg.indexOf(':');
      
      if (idx > 0) {
        // Mensagem com endereço
        String addrStr = msg.substring(0, idx);
        int addr = addrStr.toInt();
        String cmd = msg.substring(idx + 1);
        cmd.trim();
        
        if (addr == MEU_ENDERECO || addr == 0) {
          processarComando(cmd);
        } else {
          Serial.print("Endereço ignorado: ");
          Serial.println(addr);
        }
      } else {
        // Mensagem sem endereço - processa diretamente
        processarComando(msg);
      }
    }
  }
  
  // === HEARTBEAT PERIÓDICO ===
  if (millis() - lastHeartbeat > heartbeatInterval) {
    lastHeartbeat = millis();
    // Não envia heartbeat para não sobrecarregar
  }
  
  // === Execução dos modos ===
  switch (modoAtual) {
    case MODOPARADO:
      break;
    case MODOESTABILIZAR:
      estabilizar();
      break;
    case MODOORIENTARLUZ:
      orientarLuz();
      break;
  }
}

void processarComando(String comando) {
  comando = limparString(comando);
  
  Serial.print("Processando: '");
  Serial.print(comando);
  Serial.println("'");
  
  // Comando para solicitar dados (REQ)
  if (comando.startsWith("REQ:")) {
    Serial.println("Solicitação de telemetria");
    enviarDadosTelemetria();
    return;
  }
  
  // Comandos numéricos
  if (comando == "0") {
    enviarResposta("Pronto para operacao");
    modoAtual = MODOPARADO;
  }
  else if (comando == "1") {
    enviarResposta("Iniciando estabilizacao");
    modoAtual = MODOESTABILIZAR;
    orientado = false;
  }
  else if (comando == "2") {
    enviarResposta("Comando 2 recebido - informe angulos");
  }
  else if (comando == "3") {
    enviarResposta("Orientando pela luz");
    orientado = false;
    modoAtual = MODOORIENTARLUZ;
  }
  else if (comando == "4") {
    enviarResposta("Buscando azimuth");
    float alfaAzimuth = girar360();
    enviarResposta("Azimuth: " + String(alfaAzimuth, 2));
  }
  else if (comando == "5") {
    enviarResposta("Motores desligados");
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
    modoAtual = MODOPARADO;
    equilibrado = false;
    orientado = false;
  }
  else if (comando == "6") {
    enviarResposta("Abrindo Paineis");
    digitalWrite(LED, HIGH);
    digitalWrite(PAINEL_IN4, LOW);
    digitalWrite(PAINEL_IN2, LOW);
    ativaPinoPorTempo(PAINEL_IN3, PAINEL_IN1, TEMPO_ACIONAMENTO_PAINEL);
    digitalWrite(PAINEL_IN3, LOW);
    digitalWrite(PAINEL_IN1, LOW);
    digitalWrite(LED, LOW);
    enviarResposta("Paineis abertos");
  }
  else if (comando == "7") {
    enviarResposta("Abrindo Antena");
    digitalWrite(ANTIN1, HIGH);
    digitalWrite(ANTIN2, LOW);
    if (aguardaValorChave(SWANT1, HIGH, TEMPO_ACIONAMENTO_PAINEL)) {
      digitalWrite(ANTIN1, LOW);
      digitalWrite(ANTIN2, LOW);
      enviarResposta("Antena aberta");
    } else {
      enviarResposta("Erro: timeout antena");
      digitalWrite(ANTIN1, LOW);
      digitalWrite(ANTIN2, LOW);
    }
  }
  else if (comando == "8") {
    enviarResposta("Fechando Antena");
    digitalWrite(LED, HIGH);
    digitalWrite(ANTIN1, LOW);
    digitalWrite(ANTIN2, HIGH);
    if (aguardaValorChave(SWANT2, HIGH, TEMPO_ACIONAMENTO_PAINEL)) {
      digitalWrite(ANTIN1, LOW);
      digitalWrite(ANTIN2, LOW);
      enviarResposta("Antena fechada");
    } else {
      enviarResposta("Erro: timeout antena");
      digitalWrite(ANTIN1, LOW);
      digitalWrite(ANTIN2, LOW);
    }
    digitalWrite(LED, LOW);
  }
  else if (comando == "9") {
    enviarResposta("PID - Kp:" + String(Kp_pos, 3) + " Kd:" + String(Kd_pos, 3) + " Kv:" + String(Kd_vel, 3));
  }
  else if (comando == "11") {
    enviarResposta("Testando paineis");
    digitalWrite(PAINEL_IN4, LOW);
    digitalWrite(PAINEL_IN2, LOW);
    ativaPinoPorTempo(PAINEL_IN3, PAINEL_IN1, 1000);
    digitalWrite(PAINEL_IN3, LOW);
    digitalWrite(PAINEL_IN1, LOW);
    delay(500);
    digitalWrite(PAINEL_IN3, LOW);
    digitalWrite(PAINEL_IN1, LOW);
    ativaPinoPorTempo(PAINEL_IN4, PAINEL_IN2, 1000);
    digitalWrite(PAINEL_IN4, LOW);
    digitalWrite(PAINEL_IN2, LOW);
    enviarResposta("Teste concluido");
  }
  else if (comando == "13") {
    enviarResposta("Testando atuador");
    defineVelocidade(30);
    delay(2000);
    defineVelocidade(-30);
    delay(2000);
    defineVelocidade(0);
    enviarResposta("Teste concluido");
  }
  else if (comando == "14") {
    String status = "Motor:" + String(velocidadeAtual, 1) + "rps Ang:" + String(alfaSat, 1);
    enviarResposta(status);
  }
  else {
    enviarResposta("ERRO: Comando invalido - " + comando);
  }
}

// ... (resto das funções permanece igual: defineVelocidade, estabilizar, girar360, calibrateMPU, etc.)

#endif // MODO_TESTE_COMUNICACAO

// Inclui as funções restantes aqui (defineVelocidade, estabilizar, etc.)
// Como são muitas, vou omitir por brevidade, mas você deve mantê-las iguais