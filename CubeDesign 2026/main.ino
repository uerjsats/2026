//Inclusão de bibliotecas de pinos
#include "pinos.h"
#include <SoftwareSerial.h>


// ===========================================
// --- Controle de Atitude ---
// ===========================================
// Define o modo de controle (pode ser usado para alternar entre diferentes algoritmos)
#define CONTROLLERMODE 0
#define linkSerial_DEBUG_ENABLE 1

// Define os códigos de retorno

#define CA_READY "0"
#define CA_ININT_ESTAB "1" // incializar modo de estabilização
#define CA_ORIENT_LUZ  "3"
#define CA_AZIMUT_ENCONTRADO "4"
#define CA_MOTOR_PARADO    "5"
#define CA_ABRINDO_PAINEL "6"
#define CA_ABRINDO_ANTENA  "7"
#define CA_CHAVE_ABRINDO_DEFEITO "70"
#define CA_FECHANDO_ANTENA "8"
#define CA_CHAVE_FECHANDO_DEFEITO "80"

//===========================================
// --- Constantes para Painel e ANtenas  ---
// ===========================================
#define TEMPO_ACIONAMENTO_PAINEL 5000
#define TIMEOUT_ANTENA 10000
SoftwareSerial linkSerial(12,4);
double anguloOffset = 0;          // Ângulo adicional a ser adicionado após encontrar a luz
bool luzEncontrada = false;       // Flag indicando se já encontrou a luz
double anguloLuzEncontrada = 0;   // Ângulo onde a luz foi encontrada
bool executandoOffset = false;    // Flag indicando se está executando o movimento para o offset

// ===========================================
// --- Variáveis para Comando 8 - Toggle PID ---
// ===========================================
bool modoEdicaoPID = false;
// PID
double Kp_pos = 1.5;    // Reduzido para menos oscilação
double Kd_pos = 0.3;
double Kd_vel = 0.5;

// --- ESTADOS DO SISTEMA (Máquina de Estados) ---
enum MODOSistema 
{
  MODOPARADO,          // O motor de reação está desligado
  MODOESTABILIZAR,     // Modo de controle de velocidade angular (cancelamento de giro)
  MODOORIENTARLUZ,    // Modo de busca e travamento na fonte de luz (LDR)
  MODOORIENTAUM,
  MODOORIENTADOIS
};

// Variável que armazena o estado atual de operação do satélite
MODOSistema modoAtual = MODOPARADO;

//Variável para Azimuth
double total = 0;
const int numReadings = 8;
double readings[numReadings];
int readIndex = 0;

//Variáveis de orientação para luz
bool orientado = false;
int ValorFixoVermelho, ValorFixoAzul;
int leitura = 0;
int leitura2 = 0;
int menorLuz = 0;
int anguloMaisClaro = 0;
int controle = 0;
double anguloDesejado = 0;
int alvo = 0;
bool noAlvo = false;
float angulo1, angulo2; // Angulos de orientar para dois
// ------------------------------

// ===========================================
// ----- Motor de Reação -----
// ===========================================
// Velocidade angular alvo para a roda de reação (usado no controle)
double velocidadeAlvo = 0;
// Fator de microstepping do driver (influencia no cálculo de velocidade)
#define MICROSTEPPING 4
// Velocidade angular máxima base da roda em passos/segundo (15 RPS * 4 microsteps)
#define VELOCIDADEMAXIMA (15 * MICROSTEPPING)
// ------------------------------------

// ===========================================
// ----- TACÔMETRO (Medidor de RPM) -----
// ===========================================
// Velocidade angular atual da roda de reação em RPS (Revoluções por Segundo)
double velocidadeAtual = 0;
// Variável volátil para contar os pulsos do tacômetro (interrupt)
volatile int contagemPulsos;
// Variável para controle de tempo da ldr1 do tacômetro
unsigned long tempoAnteriorTaco;
// Número de pulsos do encoder a cada volta completa do motor
unsigned int pulsosPorVolta = 6;
// Pino digital configurado como interrupção para o tacômetro
const byte pinoInterrupcao = 2;
// ------------------------------------
bool modoEstabilizacao = false;
bool modoAnguloAlvo = false;
double anguloAlvo = 0;
unsigned long ultimoEnvioAngulo = 0;
// ===========================================
// ------ MPU6050 (Giroscópio) -----
// ===========================================
#include <Wire.h>
// Endereço I2C do módulo MPU6050
#define ENDERECOMPU 0x68
// Offset de calibração para corrigir o erro do giroscópio (drift)
double erro = 0;
#define CALIBRATION_MEASUREMENTS_COUNT 200
// Ângulo de rotação acumulado (integrado do giroscópio)
double alfaSat = 0;
// Velocidade angular lida pelo giroscópio
double omegaSat = 0;
double yawAngle = 0, yawAngularSpeed = 0;

// -----------------------------------------

// ===========================================
// Variáveis para Controle de Estabilização Otimizado
// ===========================================
// Velocidade atual da roda de reação (Flywheel) em passos/s
double velocidadeAtualRoda = 0;
// Velocidade da roda no momento em que o satélite atingiu o equilíbrio
double velocidadeEquilibrio = 0;
// Flag que indica se o satélite está em estado de equilíbrio (baixa velocidade angular)
bool equilibrado = false;
// Tolerância (em °/s) para considerar o satélite "parado" ou "equilibrado"
const double zonaMorta = 1.5;
// Ganho de correção aplicado quando o satélite gira muito rápido
const double ganhoRapido = 0.03;
// Ganho de correção aplicado quando o satélite gira lentamente
const double ganhoLento = 0.01;
// Aceleração máxima permitida para a roda de reação
const double aceleracaoMaxima = 0.08;
// ------------------------------------------

// ===========================================
// ------ VARIÁVEIS DE TEMPO E FILTRO ------
// ===========================================
// Variáveis de tempo para o loop principal e cálculo de dt
long tempoAtual, tempoAnterior, tempoInicio;
// Tamanho do filtro de média móvel
const int numldr1s = 8;
// Array para armazenar as últimas ldr1s de velocidade angular (giroscópio)
double ldr1s[numldr1s];
// Índice atual para a ldr1 no array (circular buffer)
int indiceldr1 = 0;
// Soma acumulada das ldr1s para a média
double somaldr1s = 0;
// Média móvel resultante da velocidade angular
double mediaMovel = 0;
//Booleana para saber se achou fonte luminosa
bool achouLuz = false;
// -----------------------------------------

// ===========================================
// --- Constantes de Controle de Orientação ---
// ===========================================
#define TOLERANCIA 50     // pode ajustar entre 100–150 se quiser
#define MINIMOLDR 10
#define VELOCIDADEMAX 150

// Variáveis globais de referência de luz
double referenciaLuz = 0;   // ângulo salvo quando a luz é encontrada
bool luzTravada = false;    // indica se já definiu o zero da luz

// ===========================================
// --- FUNÇÕES ---
// ===========================================

void ativaPinoPorTempo(int pino1, int pino2, unsigned long tempoAtivo) {
  digitalWrite(pino1, HIGH);           // Ativa o pino
  digitalWrite(pino2, HIGH);
  delay(tempoAtivo);                  // Aguarda o tempo especificado
  digitalWrite(pino1, LOW);            // Desativa o pino
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
      return true; // Valor esperado atingido dentro do tempo
    }
  }

  return false; // Timeout atingido sem atingir o valor esperado
}

// Função de interrupção: incrementa o contador de pulsos do tacômetro
void contarPulso() 
{
  contagemPulsos++;
}

// Realiza a ldr1 do valor RAW do giroscópio (eixo Z)
int16_t readMPU() 
{
  // Envia o endereço do registrador (0x47) para ldr1 do Gyro Z High
  Wire.beginTransmission(ENDERECOMPU);
  Wire.write(0x47);
  Wire.endTransmission(false);
  // Solicita 2 bytes (Gyro Z High e Low)
  Wire.requestFrom(ENDERECOMPU, 2, true);
  // Combina os 2 bytes e retorna o valor RAW
  return Wire.read() << 8 | Wire.read();
}

// Função de inicialização
void setup() 
{
  // Inicia comunicação |linkSerial
  linkSerial.begin(9600);
  delay(500); // espera estabilizar
  while (linkSerial.available()) linkSerial.read(); // descarta lixo inicial
 
  pinMode(pinoInterrupcao, INPUT);

  // Configuração do MPU
  Wire.begin();
  // Acorda o MPU (registrador Power Management 1)
  Wire.beginTransmission(ENDERECOMPU);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);
  // Configura a escala do giroscópio para +/- 1000 °/s (registrador Gyro Config, 0x1B)
  Wire.beginTransmission(ENDERECOMPU);
  Wire.write(0x1B);
  Wire.write(0x10); // 0x10 = FS_SEL=2 (+/- 1000 °/s)
  Wire.endTransmission(true);

  // Configura e acende o LED de status
  pinMode(LED, OUTPUT);
  digitalWrite(LED, 0);

  // Inicializa as variáveis de tempo globais
  tempoAtual = millis();
  // Garante que dt não seja gigante no início
  tempoAnterior = tempoAtual;
  tempoInicio = tempoAtual;

  // Anexa a interrupção ao pino do tacômetro (chamará contarPulso em borda de subida)
  attachInterrupt(digitalPinToInterrupt(pinoInterrupcao), contarPulso, RISING);
  contagemPulsos = 0;
  tempoAnteriorTaco = 0;

  // Configura os pinos de controle do motor como saída e desliga o motor
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);


 linkSerial.println(CA_READY); // setup do CA terminado 
  
}

// Função principal do programa, executada repetidamente
void loop() 
{

  // === Comunicação linkSerial (Recebimento de Comandos) ===
  if (linkSerial.available() > 0) 
  {
    String comando = linkSerial.readStringUntil('\n');
    comando.trim();

    if (comando == "1") 
    {
      linkSerial.print("3");
      linkSerial.print(":");
      linkSerial.println(CA_ININT_ESTAB);
      modoAtual = MODOESTABILIZAR;
    }
    else if (comando == "2") 
    {
      
      linkSerial.print("3 : Informe os Angulos ");
      comando = linkSerial.readStringUntil('\n');
      // Encontra a posição do caractere delimitador ':'
      int indiceDoisPontos = comando.indexOf(":");
      // Se o delimitador foi encontrado e não está no início nem no fim da string
      if (indiceDoisPontos != -1 && indiceDoisPontos > 0 && indiceDoisPontos < comando.length() - 1) {
        // Extrai a primeira parte (antes dos dois pontos)
        String strAngulo1 = comando.substring(0, indiceDoisPontos);
        // Extrai a segunda parte (depois dos dois pontos)
        String strAngulo2 = comando.substring(indiceDoisPontos + 1);

        // Converte as substrings para inteiros
        angulo1 = strAngulo1.toInt();
        angulo2 = strAngulo2.toInt();
      }

      //Implementação de giro para dois angulos
      modoAtual = MODOORIENTADOIS;
      noAlvo = false;

    }
    else if (comando == "3") 
    {
      linkSerial.print("3");
      linkSerial.print(":");
      linkSerial.println(CA_ORIENT_LUZ);// orienta para a Luz
      orientado = false;
      modoAtual = MODOORIENTARLUZ;
    }

    else if (comando == "4")
    {

      float alfaAzimuth = girar360();
      linkSerial.print("3");
      linkSerial.print(":");
      linkSerial.print(CA_AZIMUT_ENCONTRADO); // Azimut encontrado 
      linkSerial.println(alfaAzimuth);
       modoAtual = MODOPARADO;

    }

    else if (comando == "5") 
    {
      linkSerial.print("3");
      linkSerial.print(":");
      linkSerial.println(CA_MOTOR_PARADO);
      // Desliga a alimentação do motor
      digitalWrite(IN1, LOW);
      digitalWrite(IN2, LOW);
      modoAtual = MODOPARADO;
    }
    else if(comando == "6")
    {
      digitalWrite(LED, HIGH);
      linkSerial.print("3");
      linkSerial.print(":");
      linkSerial.println(CA_ABRINDO_PAINEL);

      digitalWrite(PAINEL_IN4, LOW);
      digitalWrite(PAINEL_IN2, LOW);
      ativaPinoPorTempo(PAINEL_IN3,PAINEL_IN1, TEMPO_ACIONAMENTO_PAINEL);
      digitalWrite(PAINEL_IN3, LOW);
      digitalWrite(PAINEL_IN1, LOW);

      digitalWrite(LED, LOW);
       modoAtual = MODOPARADO;
    }

    else if(comando == "7")
    {
      linkSerial.print("3");
      linkSerial.print(":");
      linkSerial.println(CA_ABRINDO_ANTENA);
      digitalWrite(ANTIN1, HIGH);
      digitalWrite(ANTIN2, LOW);
      if (aguardaValorChave(SWANT1, HIGH,TEMPO_ACIONAMENTO_PAINEL)) 
      { // aguarda a chave fechar e colocar +%V na entrada do pino do arduino
          digitalWrite(ANTIN1,LOW);
          digitalWrite(ANTIN2,LOW);
      }
      else 
      {
          linkSerial.print("3");
          linkSerial.print(":");
          linkSerial.println(CA_CHAVE_ABRINDO_DEFEITO);
          digitalWrite(ANTIN1,LOW);
          digitalWrite(ANTIN2,LOW);
      }
       modoAtual = MODOPARADO;
    }

    else if(comando == "8")
    {
      linkSerial.print("3");
      linkSerial.print(":");
      linkSerial.println(CA_FECHANDO_ANTENA);
      digitalWrite(LED, HIGH);
      digitalWrite(ANTIN1, LOW);
      digitalWrite(ANTIN2, HIGH);
      if (aguardaValorChave(SWANT2, HIGH,TEMPO_ACIONAMENTO_PAINEL)) { // aguarda a chave fechar e colocar +%V na entrada do pino do arduino
          digitalWrite(ANTIN1,LOW);
          digitalWrite(ANTIN2,LOW);
      }
      else {
        linkSerial.print("3");
        linkSerial.print(":");
        linkSerial.println(CA_CHAVE_FECHANDO_DEFEITO);
        digitalWrite(ANTIN1,LOW);
        digitalWrite(ANTIN2,LOW);
      }
      digitalWrite(LED, LOW);
       modoAtual = MODOPARADO;
    }

    // COMANDO 9 - TOGGLE PID
    else if (comando == "9") 
    {
       if (!modoEdicaoPID) {
        // Primeiro comando 9 → mostra valores atuais
        linkSerial.print("3: ");
        linkSerial.print(Kp_pos, 3);
        linkSerial.print(" ");
        linkSerial.print(Kd_pos, 3);
        linkSerial.print(" ");
        linkSerial.println(Kd_vel, 3);

        linkSerial.println("Digite 9 novamente para editar valores.");
        modoEdicaoPID = true;
       } // if !modoEdicaoPID

       else {
          // Segundo comando 9 → entra em modo edição
          linkSerial.println("3: Edição de PID");
          linkSerial.println("Digite os novos valores (KP KD KV):");
          linkSerial.println("Exemplo: 2.0 0.5 0.8");
          linkSerial.print(">> ");

          // Espera até 5 segundos pela entrada
          unsigned long inicio = millis();
          while (linkSerial.available() == 0 && (millis() - inicio) < 50000) {
           // não bloqueia outras tarefas se você colocar aqui
          }


          if (linkSerial.available() > 0) {
          String valores = linkSerial.readStringUntil('\n');
          valores.trim();
          processarValoresPID(valores);
          } else {
             linkSerial.println("Tempo limite atingido (5s) sem entrada.");
             }

          modoEdicaoPID = false; // sai do modo edição
           modoAtual = MODOPARADO;


       } // else



    } // comando 9 
      


  
   


    else if(comando == "10")
    {

      //Em fase de testes
      
    }

    else if(comando == "11")
    {

      //Em fase de testes

    }

    else 
    {
      linkSerial.println("Comando inválido!");
    }
  }
  // === Execução dos modos (Máquina de Estados) ===
  switch (modoAtual) {
    case MODOPARADO:
      // Não faz nada no loop, o motor já está desligado
      break;

    case MODOESTABILIZAR:
      // Chama a função principal de controle de estabilização
      estabilizar();
      break;

    case MODOORIENTARLUZ:
      orientarLuz();
      break;
    case MODOORIENTADOIS:
      executarDoisAngulos();
      break;
  }
}

// Função que controla a velocidade e o sentido do Motor de Corrente Contínua
void defineVelocidade(double velocidadeAlvo) 
{
  // Limite mínimo de PWM para que o motor comece a girar
  int pwmMin = 20;
  // Limite máximo de PWM (o motor não precisa de 255 para máxima performance aqui)
  int pwmMax = 255; //

  // Variável para armazenar o valor de PWM calculado
  int valorPwm;
  // Relação da velocidade alvo (absoluta) com a velocidade máxima de referência (60 RPS * 4 microsteps)
  double relacaoVelocidade = fabs(velocidadeAlvo) / (60 * MICROSTEPPING);

  // --- Mapeamento de Curva Não-Linear (Otimizada) ---
  // Mapeia a velocidade para o PWM em seções para melhorar o torque em baixas velocidades
  if (relacaoVelocidade < 0.2)
  {
    // 0% a 20% da velocidade máxima -> mapeia pwmMin (20) a 40
    valorPwm = map(relacaoVelocidade * 100, 0, 20, pwmMin, 40);
  } 
  else if (relacaoVelocidade < 0.5)
  {
    // 20% a 50% da velocidade máxima -> mapeia 40 a 80
    valorPwm = map(relacaoVelocidade * 100, 20, 50, 40, 80);
  } 
  else
  {
    // 50% a 100% da velocidade máxima -> mapeia 80 a pwmMax (150)
    valorPwm = map(relacaoVelocidade * 100, 50, 100, 80, pwmMax);
  }

  // Garante que o valorPwm esteja dentro dos limites definidos
  valorPwm = constrain(valorPwm, pwmMin, pwmMax);

  // Define o sentido e a aplicação do PWM
  if (fabs(velocidadeAlvo) < 1.5) 
  {
    // Desliga o motor se a velocidade alvo for muito baixa (parada ou inércia)
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, LOW);
  } 
  else if (velocidadeAlvo > 0) 
  {
    // Gira em um sentido (IN1 = PWM, IN2 = LOW)
    analogWrite(IN1, valorPwm);
    digitalWrite(IN2, LOW);
  } 
  else 
  {
    // Gira no sentido oposto (IN1 = LOW, IN2 = PWM)
    digitalWrite(IN1, LOW);
    analogWrite(IN2, valorPwm);
  }
}


// ==== Função responsável por estabilizar o Satélite (cancelar o giro) =====
void estabilizar()
{
  // --- ldr1 do Tacômetro ---
  // Calcula a velocidade do motor a cada 1000 ms (1 segundo)
  if (millis() - tempoAnteriorTaco >= 1000) 
  {
    // Desabilita a interrupção para ldr1 segura
    detachInterrupt(0);
    // Calcula a velocidade em RPS (Revoluções por Segundo)
    velocidadeAtual = (60.0 * 1000.0 / pulsosPorVolta) / (double)(millis() - tempoAnteriorTaco) * contagemPulsos;
    // Atualiza o tempo de referência
    tempoAnteriorTaco = millis();
    // Zera o contador de pulsos
    contagemPulsos = 0;
    // Reabilita a interrupção
    attachInterrupt(digitalPinToInterrupt(pinoInterrupcao), contarPulso, RISING);
  }

  // --- Loop de Controle (Executa a cada 10ms) ---
  if (millis() - tempoAtual > 10) 
  {
    tempoAnterior = tempoAtual;
    tempoAtual = millis();

    // ldr1 do MPU (converte RAW para °/s)
    omegaSat = ((double)readMPU() - erro) / 32.768;

    // --- FILTRO DE MÉDIA MÓVEL (Rolling Average) ---
    // Subtrai a ldr1 mais antiga da soma
    somaldr1s = somaldr1s - ldr1s[indiceldr1];
    // Adiciona a nova ldr1 ao array
    ldr1s[indiceldr1] = omegaSat;
    // Adiciona a nova ldr1 à soma
    somaldr1s = somaldr1s + ldr1s[indiceldr1];
    // Avança o índice (circular)
    indiceldr1 = indiceldr1 + 1;
    // Reseta o índice se atingir o final do array
    if (indiceldr1 >= numldr1s) indiceldr1 = 0;
    // Calcula a média móvel (velocidade angular filtrada)
    mediaMovel = somaldr1s / numldr1s;

    // *** LIMITE DINÂMICO DE VELOCIDADE DA RODA DE REAÇÃO ***
    // A roda pode acelerar mais se o satélite estiver girando mais rápido
    // dynamicMaxSpeed = (Velocidade Base) * (1 + |Giro do Satélite| / 10)
    double velocidade_maxima_dinamica = VELOCIDADEMAXIMA * (1.0 + fabs(mediaMovel) / 10.0);

    // Limite máximo absoluto para segurança da roda
    const double VELOCIDADE_MAXIMA_ABSOLUTA = 60 * MICROSTEPPING; // Ex: 60 RPS
    if (velocidade_maxima_dinamica > VELOCIDADE_MAXIMA_ABSOLUTA) 
    {
      velocidade_maxima_dinamica = VELOCIDADE_MAXIMA_ABSOLUTA;
    }

    // --- ALGORITMO DE CONTROLE DE ESTABILIZAÇÃO ---
    double correcao = 0;

    if (!equilibrado) { // Modo de Desaceleração Rápida
      if (fabs(mediaMovel) > zonaMorta) 
      {
        // Cálculo do Ganho Adaptativo (aumenta com a velocidade angular do satélite)
        double fator_velocidade = 1.0 + (fabs(mediaMovel) / 20.0);
        // Usa ganho rápido para giros fortes e ganho lento para giros leves
        double ganho_adaptativo = (fabs(mediaMovel) > 5.0) ? ganhoRapido : ganhoLento;
        // Calcula a correção (sentido oposto ao giro do satélite)
        correcao = mediaMovel * -ganho_adaptativo * fator_velocidade;
        // LED ligado: satélite não estabilizado
        digitalWrite(LED, 1);
      } 
      else 
      {
        // Entra no estado de equilíbrio
        equilibrado = true;
        velocidadeEquilibrio = velocidadeAtualRoda;
        // LED desligado: satélite estabilizado
        digitalWrite(LED, 0);
      }

      // --- Suavização da Correção (Limitador de Aceleração/Jerk) ---
      static double ultimaCorrecao = 0;
      // Define o limite máximo de mudança na correção
      double mudancaMaxima = aceleracaoMaxima * (1.0 + fabs(mediaMovel) / 15.0);

      // Limita a variação da correção para evitar picos
      if (fabs(correcao - ultimaCorrecao) > mudancaMaxima) 
      {
        if (correcao > ultimaCorrecao) 
        {
          correcao = ultimaCorrecao + mudancaMaxima;
        } 
        else 
        {
          correcao = ultimaCorrecao - mudancaMaxima;
        }
      }
      ultimaCorrecao = correcao;

      // Aplica a correção à velocidade da roda de reação
      velocidadeAtualRoda += correcao;

    } 
    else 
    { // Modo Equilibrado (Apenas mantém a velocidade base)
      // Mantém a velocidade da roda na velocidade registrada no equilíbrio
      velocidadeAtualRoda = velocidadeEquilibrio;

      // Ajuste fino: Se houver pequeno desvio, faz uma micro-correção
      if (fabs(mediaMovel) > zonaMorta * 1.5) 
      {
        double correcao_fina = mediaMovel * -0.005;
        velocidadeAtualRoda += correcao_fina;
        velocidadeEquilibrio = velocidadeAtualRoda; // Atualiza a velocidade base
      }

      // Se o desequilíbrio for muito grande, retorna ao modo de Desaceleração Rápida
      if (fabs(mediaMovel) > zonaMorta * 4) 
      {
        equilibrado = false;
        digitalWrite(LED, 1);
      }
    }

    // *** APLICA LIMITE DINÂMICO À VELOCIDADE FINAL DA RODA ***
    if (velocidadeAtualRoda > velocidade_maxima_dinamica) velocidadeAtualRoda = velocidade_maxima_dinamica;
    else if (velocidadeAtualRoda < -velocidade_maxima_dinamica) velocidadeAtualRoda = -velocidade_maxima_dinamica;

    // Define a velocidade alvo do motor
    velocidadeAlvo = velocidadeAtualRoda;

    // Aplica a velocidade ao motor DC
    defineVelocidade(velocidadeAlvo);

  }

}
float girar360() {

  anguloatual();
  int anguloInicial = normaliza360(alfaSat);
  int ultimoAngulo = anguloInicial;

  int maiorLuz = 0;
  int anguloMaisClaro = anguloInicial;

  unsigned long inicioBusca = millis();
  const unsigned long tempoMaximo = 30000;

  if (linkSerial_DEBUG_ENABLE) {
    linkSerial.println("Iniciando busca 360 graus...");
  }

  defineVelocidade(80);

  long avancototal = 0;

  while (true) {

    // ----- PARADA SEGURA -----
    if (avancototal >= 360) break;                          // PARADA REAL
    if (millis() - inicioBusca > tempoMaximo) break;        // TEMPO MÁXIMO

    // ----- Atualiza angulo -----
    anguloatual();
    int anguloAtual = normaliza360(alfaSat);

    // ----- Delta corrigido -----
    int delta = anguloAtual - ultimoAngulo;

    if (delta > 180) delta -= 360;
    if (delta < -180) delta += 360;

    ultimoAngulo = anguloAtual;

    // ----- Acumula avanço SOMENTE SE AINDA NÃO COMPLETOU 360 -----
    if (avancototal < 360) {
        avancototal += abs(delta);
    }

    // ----- LDRs -----
    int A = analogRead(LDRAZUL);
    int B = analogRead(LDRVERMELHO);
    int media = (A + B) / 2;

    if (media > maiorLuz) {
      maiorLuz = media;
      anguloMaisClaro = anguloAtual;
    }

    delay(20);
  }

  // ---------- PARAR MOTOR ----------
  defineVelocidade(0);
  delay(300);

  if (linkSerial_DEBUG_ENABLE) {
    linkSerial.println("Varredura completa!");
    linkSerial.print("Melhor angulo: ");
    linkSerial.println(anguloMaisClaro);
    linkSerial.print("Maior luz: ");
    linkSerial.println(maiorLuz);
  }
  modoAtual = MODOPARADO;
  return anguloMaisClaro;
}

void calibrateMPU() {
  erro = 0;
  for (int i = 0; i < CALIBRATION_MEASUREMENTS_COUNT; i++) {
    erro += readMPU();
    delay(20);
  }
  erro = erro / (double)CALIBRATION_MEASUREMENTS_COUNT;
}

int mirarluz() {
  anguloatual();
  int ldr1 = analogRead(LDRAZUL);
  int ldr0 = analogRead(LDRVERMELHO);

  if (abs(ldr1 - ldr0) < TOLERANCIA) {
    int mediaLuz = (ldr1 + ldr0) / 2;
    if (mediaLuz > menorLuz) {
      anguloMaisClaro = alfaSat;
      menorLuz = mediaLuz;
    }
  }
  
  delay(100); // Reduzido para melhor responsividade
  return anguloMaisClaro;
}

double normaliza360(double angulo) {
  angulo = fmod(angulo, 360);
  if (angulo < 0) angulo += 360;
  return angulo;
}

// Determina o angulo em que ele se encontra
void anguloatual() {
  if (millis() - tempoAnterior >= 10) { // >= para garantir execução exata
    tempoAtual = tempoAnterior;
    tempoAnterior = millis();
    double deltaT = (tempoAnterior - tempoAtual) / 1000.0;

    // Medida do giroscópio
    omegaSat = ((double)readMPU() - erro) / 32.768;
    omegaSat += 1.057; // Correção da média

    // Filtro mais suave para o giroscópio
    if (fabs(omegaSat) > 0.08) { // Limite reduzido
      alfaSat += (omegaSat * deltaT);
    }

    // Normaliza ângulo entre -180 e 180
    while (alfaSat <= -180) alfaSat += 360;
    while (alfaSat > 180) alfaSat -= 360;

    // Filtro de média móvel
    total -= readings[readIndex];
    readings[readIndex] = omegaSat;
    total += readings[readIndex];
    readIndex = (readIndex + 1) % numReadings;
    mediaMovel = total / numReadings;
  }
}


// Move a roda de reação para o ângulo escolhido
void irParaAngulo(double anguloAlvo) {

  anguloatual();

  // Calcula erro normalizado entre -180 e 180
  double erro = anguloAlvo - alfaSat;
  while (erro > 180) erro -= 360;
  while (erro < -180) erro += 360;

  // Se já está no alvo, para o motor e sai
  if (chegouNoAngulo(anguloAlvo)) {
    defineVelocidade(0);
    return;
  }

  // === Controle PID simplificado ===
  double comando = Kp_pos * erro;

  static double erroAnterior = 0;
  double derivadaErro = erro - erroAnterior;
  erroAnterior = erro;

  comando += Kd_pos * derivadaErro;

  // Compensa velocidade angular atual
  comando -= Kd_vel * omegaSat;

  // Saturação de segurança
  comando = constrain(comando, -120, 120);

  // Aplica comando ao motor
  defineVelocidade(comando);
}



void orientarLuz()
{

  anguloatual();
  if (!orientado)
  {
    anguloDesejado = girar360();
    orientado = true;
  }
  irParaAngulo(anguloDesejado);
}


// ===========================================
// --- Função para Processar Valores PID ---
// ===========================================
void processarValoresPID(String valores) {
  // Divide a string pelos espaços

  linkSerial.println("Entrando na função");
  int primeiroEspaco = valores.indexOf(' ');
  int segundoEspaco = valores.indexOf(' ', primeiroEspaco + 1);
  
  if (primeiroEspaco == -1 || segundoEspaco == -1) {
    linkSerial.println("Erro: Formato inválido! Use: KP KD KV");
    linkSerial.println("Exemplo: 2.0 0.5 0.8");
    return;
  }
  
  String kpStr = valores.substring(0, primeiroEspaco);
  String kdStr = valores.substring(primeiroEspaco + 1, segundoEspaco);
  String kvStr = valores.substring(segundoEspaco + 1);
  
  kpStr.trim();
  kdStr.trim();
  kvStr.trim();
  
  float novoKp = kpStr.toFloat();
  float novoKd = kdStr.toFloat();
  float novoKv = kvStr.toFloat();
  
  // Validação dos valores
  bool valoresValidos = true;
  
  if (novoKp <= 0 || novoKp > 10.0) {
    linkSerial.println("Erro: Kp deve ser entre 0.1 e 10.0");
    valoresValidos = false;
  }
  
  if (novoKd < 0 || novoKd > 5.0) {
    linkSerial.println("Erro: Kd deve ser entre 0 e 5.0");
    valoresValidos = false;
  }
  
  if (novoKv < 0 || novoKv > 5.0) {
    linkSerial.println("Erro: Kv deve ser entre 0 e 5.0");
    valoresValidos = false;
  }
  
  if (valoresValidos) {
    Kp_pos = novoKp;
    Kd_pos = novoKd;
    Kd_vel = novoKv;
    
    linkSerial.println("Valores PID atualizados com sucesso!");
    linkSerial.print("Kp_pos: "); linkSerial.println(Kp_pos, 3);
    linkSerial.print("Kd_pos: "); linkSerial.println(Kd_pos, 3);
    linkSerial.print("Kd_vel: "); linkSerial.println(Kd_vel, 3);
  } else {
    linkSerial.println("✗ Valores não alterados devido a erros.");
  }
}



bool chegouNoAngulo(double alvo) {
  anguloatual();

  double erro = alvo - alfaSat;
  while (erro > 180) erro -= 360;
  while (erro < -180) erro += 360;

  // Critérios para considerar que chegou
  if (fabs(erro) < 8.0 && fabs(omegaSat) < 5.0) {
    return true;
  }
  return false;
}

void executarDoisAngulos() {
    static bool indoParaPrimeiro = true;

    if (indoParaPrimeiro) {
        irParaAngulo(angulo1);
        if (chegouNoAngulo(angulo1)) {
            defineVelocidade(0);
            delay(300);
            indoParaPrimeiro = false; 
        }
    } else {
        irParaAngulo(angulo2);
        if (chegouNoAngulo(angulo2)) {
            defineVelocidade(0);
            delay(300);
            indoParaPrimeiro = true;
            modoAtual = MODOPARADO;  
        }
    }
}