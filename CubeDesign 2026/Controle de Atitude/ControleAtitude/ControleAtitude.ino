// UERJ Sats - ADCS 2026: base ControleAtitude_Integracao(2).ino (legado 2025).
// Alteracoes limitadas a: pinos V2, UART OBC numerica, calibracao/shift LDR 2026.
// Mantidos controlador de detumbling, controle PD antigo e funcoes de orientacao.
// AVISO: conferir eletricamente sentido do motor e mecanimos antes de energizar.
//Inclusão de bibliotecas de pinos
#include "pinosGiga.h"
#include <SoftwareSerial.h>
#if defined(ARDUINO_ARCH_AVR)
  #include <avr/pgmspace.h>
#endif
#include <math.h> // Necessário para a função acos() e PI

// ===========================================
// --- Controle de Atitude ---
// ===========================================
// Define o modo de controle (pode ser usado para alternar entre diferentes algoritmos)
#define CONTROLLERMODE 0
#define controleSerial_DEBUG_ENABLE 0  // Nao enviar debug livre ao mestre
#define DEBUG_SERIAL_USB 1 // Porta USB do computador: comandos e diagnostico
#define DEBUG_UART_OBC 1   // 1: envia 3:DBG:... para Heltec (requer encaminhamento pelo OBC)
#define INTERVALO_ESTADO_MS 5000UL
#define INTERVALO_RADIO_SHIFT_MS 2000UL
#define INTERVALO_SHIFT_MS 500UL

// ===========================================
// --- Variáveis para Teste Estático (Shift de Luz) ---
// ===========================================
double luzMaxima = 0;   // Valor lido quando a luz está 0 graus (de frente)
double luzMinima = 0;   // Valor lido da luz ambiente (sem o refletor)
bool luzCalibrada = false;
bool referenciaZeroCalibrada = false;
bool fundoCalibrado = false;
// ===== CURVAS CALIBRADAS ±60 GRAUS =====
// 1 = coleta MAIS RECENTE (03/11/2026, usada no ultimo firmware do ADCS).
// 0 = CSV ANEXADO NESTA CONVERSA (ensaio anterior, tambem ±60 graus).
// ATENCAO: as duas tabelas diferem. Para testar exatamente o CSV anexado, use 0.
#define CURVA_ULTIMA_COLETA 1
const byte NUM_PONTOS_LDR = 25;
const float ANGULO_INICIAL_LDR = -60.0f;
const float PASSO_LDR_GRAUS = 5.0f;
const float MAX_RESIDUAL_LDR = 0.06f;  // ADIMENSIONAL; limite experimental ainda nao validado
float referenciaZeroLDR0 = 0.0f, referenciaZeroLDR1 = 0.0f;
float ambienteLDR0 = 0.0f, ambienteLDR1 = 0.0f;

// No Nano AVR, armazenar as tabelas na memoria de programa poupa 200 bytes de SRAM.
#if defined(ARDUINO_ARCH_AVR)
  #define LDR_FLASH PROGMEM
  float valorCurva(const float *v, byte idx) { return pgm_read_float(&v[idx]); }
#else
  #define LDR_FLASH
  float valorCurva(const float *v, byte idx) { return v[idx]; }
#endif

#if CURVA_ULTIMA_COLETA
// Calibracao de novembro: LDR0=A0 (LDR2 da coleta); LDR1=A1 (LDR1 da coleta).
const float curvaNormalLDR0[NUM_PONTOS_LDR] LDR_FLASH = {
  0.30952381f, 0.39521842f, 0.48880034f, 0.55935983f,
  0.64064891f, 1.15775358f, 1.40718877f, 1.47869387f,
  1.48940408f, 1.48209366f, 1.45680834f, 1.32664305f,
  1.00000000f, 0.61999182f, 0.58517831f, 0.52101390f,
  0.45533524f, 0.38505724f, 0.35406601f, 0.29636512f,
  0.27067150f, 0.26308258f, 0.24284546f, 0.24174162f,
  0.24836468f
};
const float curvaNormalLDR1[NUM_PONTOS_LDR] LDR_FLASH = {
  0.06599633f, 0.06131272f, 0.06386742f, 0.07493777f,
  0.07607319f, 0.08837358f, 0.10179484f, 0.11311629f,
  0.13578600f, 0.15210271f, 0.17797720f, 0.27289401f,
  1.00000000f, 1.21431309f, 1.24108656f, 1.24454876f,
  1.23178770f, 1.23161525f, 1.14394955f, 0.86395861f,
  0.47250431f, 0.23187466f, 0.18248010f, 0.14864917f,
  0.10879827f
};
#else
// Curva fornecida no CSV anexado nesta conversa (ensaio anterior).
const float curvaNormalLDR0[NUM_PONTOS_LDR] LDR_FLASH = {
  0.27806620f, 0.33676190f, 0.38402670f, 0.46235000f,
  0.56212610f, 0.83637120f, 1.27694850f, 1.38764440f,
  1.44321190f, 1.44343950f, 1.40378460f, 1.30748890f,
  1.00000000f, 0.69727360f, 0.62551210f, 0.51815230f,
  0.44158780f, 0.36315160f, 0.31693510f, 0.28238450f,
  0.22021710f, 0.21641470f, 0.20782130f, 0.19225880f,
  0.19889810f
};
const float curvaNormalLDR1[NUM_PONTOS_LDR] LDR_FLASH = {
  0.07389310f, 0.07962730f, 0.08614340f, 0.09348890f,
  0.10376960f, 0.11125230f, 0.12416050f, 0.13228860f,
  0.14924340f, 0.17568830f, 0.21078640f, 0.31499040f,
  1.00000000f, 1.27564540f, 1.32386290f, 1.36060650f,
  1.35746480f, 1.28783640f, 1.02961800f, 0.36087970f,
  0.27242410f, 0.21568090f, 0.17180030f, 0.12853440f,
  0.08277560f
};
#endif


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
SoftwareSerial controleSerial(OBC_RX_PIN, OBC_TX_PIN);  // RX D12 TX_CT5; TX D13 RX_CT5; 9600 baud
double anguloOffset = 0;          // Ângulo adicional a ser adicionado após encontrar a luz
bool luzEncontrada = false;       // Flag indicando se já encontrou a luz
double anguloLuzEncontrada = 0;   // Ângulo onde a luz foi encontrada
bool executandoOffset = false;    // Flag indicando se está executando o movimento para o offset

// ===========================================
// --- Ganhos PID ajustaveis por comando 3 ---
// ===========================================
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
  MODOORIENTADOIS,
  MODOMEDIRSHIFT     // Modo de cálculo do delta que a luz vai dar
};

// Variável que armazena o estado atual de operação do satélite
MODOSistema modoAtual = MODOESTABILIZAR;

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
// Constante historica usada na escala matematica do PWM.
// O motor e DC: NAO ha microstepping fisico no MX1508.
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
  // Inicia comunicação |controleSerial
  controleSerial.begin(9600);
Serial.begin(115200); // USB: tambem recebe comandos, independentemente do debug
  controleSerial.setTimeout(150);  // Evita bloqueios de 1 s em comandos malformados
  delay(500); // espera estabilizar
  while (controleSerial.available()) controleSerial.read(); // descarta lixo inicial
 
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

  // LED de D13 desativado: D13 e o TX da comunicacao com o Heltec.

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
  pinMode(REACAO_IN3, OUTPUT); // Canal B disponivel no driver V2, manter inativo
  pinMode(REACAO_IN4, OUTPUT);
  digitalWrite(REACAO_IN3, LOW);
  digitalWrite(REACAO_IN4, LOW);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);

  // Inicializacao dos drivers ja presentes na placa (sem mudar suas ligacoes).
  pinMode(PAINEL_IN1, OUTPUT);
  pinMode(PAINEL_IN2, OUTPUT);
  pinMode(ANTIN1, OUTPUT);
  pinMode(ANTIN2, OUTPUT);
  digitalWrite(PAINEL_IN1, LOW);
  digitalWrite(PAINEL_IN2, LOW);
  digitalWrite(ANTIN1, LOW);
  digitalWrite(ANTIN2, LOW);

  controleSerial.println(F("3:0"));  // CA pronto; prefixo do subsistema 3
  Serial.println(F("[ADCS] PRONTO: USB=115200, OBC=9600; comandos em ambas as portas"));
  Serial.println(F("[ADCS] LDR: envie 5 (0 grau), 5 (fundo), 2 (angulo continuo)"));
#if CURVA_ULTIMA_COLETA
  Serial.println(F("[ADCS] CURVA: NOVEMBRO_2026, -60..60, 25 pontos"));
#else
  Serial.println(F("[ADCS] CURVA: CSV_ANEXADO, -60..60, 25 pontos"));
#endif
}

// ===== DIAGNOSTICOS SEPARADOS DA TELEMETRIA NUMERICA =====
// USB: [DBG] mensagem, e UART Heltec: 3:DBG:mensagem.
// Importante: o OBC precisa ENCAMINHAR 3:DBG: pelo LoRa; nao eh um angulo.
void avisar(const __FlashStringHelper *mensagem, bool mandarRadio=true) {
#if DEBUG_SERIAL_USB
  Serial.print(F("[DBG] "));
  Serial.println(mensagem);
#endif
#if DEBUG_UART_OBC
  if (mandarRadio) {
    controleSerial.print(F("3:DBG:"));
    controleSerial.println(mensagem);
  }
#endif
}

// Usar tipo fundamental evita erro do pre-processador de prototipos da Arduino IDE.
// O valor recebido continua sendo um dos estados definidos em MODOSistema.
const __FlashStringHelper *nomeModo(int modo) {
  switch (modo) {
    case MODOPARADO: return F("PARADO");
    case MODOESTABILIZAR: return F("ESTABILIZAR");
    case MODOMEDIRSHIFT: return F("MEDIR_SHIFT");
    case MODOORIENTARLUZ: return F("ORIENTAR_LUZ");
    case MODOORIENTAUM: return F("ORIENTAR_UM");
    case MODOORIENTADOIS: return F("ORIENTAR_DOIS");
    default: return F("ESTADO_DESCONHECIDO");
  }
}

void diagnosticarModo() {
  static int ultimoModo = -1;
  static unsigned long ultimoHeartbeat = 0;
  unsigned long agora = millis();
  if (ultimoModo != (int)modoAtual ||
      (modoAtual != MODOPARADO && agora - ultimoHeartbeat >= INTERVALO_ESTADO_MS)) {
    bool alterou = ultimoModo != (int)modoAtual;
    ultimoModo = (int)modoAtual;
    ultimoHeartbeat = agora;
    if (alterou) avisar(F("MODO: transicao de estado"));
#if DEBUG_SERIAL_USB
    Serial.print(F("[ESTADO] antes de executar: "));
    Serial.println(nomeModo(modoAtual));
#endif
#if DEBUG_UART_OBC
    // Somente na transicao ou a cada 5 s, nunca a cada 10 ms do controle.
    controleSerial.print(F("3:DBG:ESTADO="));
    controleSerial.println(nomeModo(modoAtual));
#endif
  }
}

// Leitura nao bloqueante de linhas de comando nas DUAS portas.
// Uma linha so executa ao receber '\n' ou '\r'; sem comandos parciais.
void executarComando(String comando, bool origemUsb);
void lerLinha(Stream &porta, char *buffer, byte &n, bool origemUsb) {
  byte recebidosNesteLoop = 0;
  while (porta.available() > 0 && recebidosNesteLoop++ < 48) {
    int dado = porta.read();
    if (dado < 0) break;
    char c = (char)dado;
    if (c == '\n' || c == '\r') {
      if (n > 0) {
        buffer[n] = 0;
        executarComando(String(buffer), origemUsb);
        n = 0;
      }
    } else if (c >= 32 && c <= 126) {
      if (n < 63) buffer[n++] = c;
      else { n = 0; avisar(F("ERRO: linha de comando muito longa")); }
    }
  }
}

// ===========================================
// --- Comunicacao com o mestre Heltec + notebook USB ---
// ===========================================
// O OBC recebe 3:<numero> para angulos; linhas 3:DBG:... sao SOMENTE diagnostico.
// A biblioteca do Heltec deve reconhecer/encaminhar a etiqueta DBG para aparecer no LoRa.
void responder(const String &mensagem) {
#if DEBUG_SERIAL_USB
  Serial.println(mensagem);
#endif
  String valor = mensagem;
  valor.trim();
  bool digito = false, ponto = false;
  for (unsigned int i = 0; i < valor.length(); i++) {
    char c = valor.charAt(i);
    if (c >= '0' && c <= '9') digito = true;
    else if (c == '-' && i == 0) { }
    else if (c == '.' && !ponto) ponto = true;
    else return; // Nenhum status ou palavra na UART OBC
  }
  if (!digito) return;
  controleSerial.print(F("3:"));
  controleSerial.println(valor);
}

void pararAtuadores() {
  modoAtual = MODOPARADO;
  velocidadeAlvo = 0;
  velocidadeAtualRoda = 0;
  digitalWrite(REACAO_IN3, LOW);
  digitalWrite(REACAO_IN4, LOW);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(PAINEL_IN1, LOW);
  digitalWrite(PAINEL_IN2, LOW);
  digitalWrite(ANTIN1, LOW);
  digitalWrite(ANTIN2, LOW);
}

// Mantem o comando de calibracao 5 em duas etapas; agora grava cada LDR individualmente.
void calibrarLDRFrontal() {
  avisar(F("CAL: iniciando 50 leituras da luz em 0 graus"));
  unsigned long soma0=0, soma1=0;
  for (int i=0; i<50; i++) {
    soma0 += analogRead(LDRVERMELHO);
    soma1 += analogRead(LDRAZUL);
    delay(10);
  }
  avisar(F("CAL: 50 leituras de referencia concluidas"));
  referenciaZeroLDR0 = soma0 / 50.0f;
  referenciaZeroLDR1 = soma1 / 50.0f;
  luzMaxima = (referenciaZeroLDR0 + referenciaZeroLDR1) / 2.0;
  referenciaZeroCalibrada = referenciaZeroLDR0 > 0 && referenciaZeroLDR1 > 0;
  luzCalibrada = referenciaZeroCalibrada && fundoCalibrado;
#if DEBUG_SERIAL_USB
  Serial.print(F("[CAL_ZERO] A0=")); Serial.print(referenciaZeroLDR0, 2);
  Serial.print(F(" A1=")); Serial.println(referenciaZeroLDR1, 2);
#endif
#if DEBUG_UART_OBC
  controleSerial.print(F("3:DBG:ZERO A0=")); controleSerial.print(referenciaZeroLDR0, 1);
  controleSerial.print(F(";A1=")); controleSerial.println(referenciaZeroLDR1, 1);
#endif
  avisar(referenciaZeroCalibrada ? F("CAL: zero salvo; apague a fonte e envie 5")
                               : F("CAL: ERRO referencia zero invalida"));
  responder(String(F("LDR_0G=")) + String(luzMaxima, 1));
}

void calibrarLDRAmbiente() {
  avisar(F("CAL: iniciando 50 leituras da luz ambiente"));
  unsigned long soma0=0, soma1=0;
  for (int i=0; i<50; i++) {
    soma0 += analogRead(LDRVERMELHO);
    soma1 += analogRead(LDRAZUL);
    delay(10);
  }
  avisar(F("CAL: 50 leituras de ambiente concluidas"));
  ambienteLDR0 = soma0 / 50.0f;
  ambienteLDR1 = soma1 / 50.0f;
  luzMinima = (ambienteLDR0 + ambienteLDR1) / 2.0;
  fundoCalibrado = true;
  luzCalibrada = referenciaZeroCalibrada && fundoCalibrado;
#if DEBUG_SERIAL_USB
  Serial.print(F("[CAL_FUNDO] A0=")); Serial.print(ambienteLDR0, 2);
  Serial.print(F(" A1=")); Serial.println(ambienteLDR1, 2);
#endif
#if DEBUG_UART_OBC
  controleSerial.print(F("3:DBG:FUNDO A0=")); controleSerial.print(ambienteLDR0, 1);
  controleSerial.print(F(";A1=")); controleSerial.println(ambienteLDR1, 1);
#endif
  avisar(luzCalibrada ? F("CAL: PRONTO; envie 2 com a fonte acesa")
                    : F("CAL: ERRO falta referencia de zero"));
  responder(String(F("LDR_AMBIENTE=")) + String(luzMinima, 1));
}

// Funcao principal: continua executando os modos de controle existentes.
void executarComando(String comando, bool origemUsb) {
  comando.trim();
  if (comando.length() == 0) return;
#if DEBUG_SERIAL_USB
  Serial.print(F("[RX-")); Serial.print(origemUsb ? F("USB") : F("OBC"));
  Serial.print(F("] comando=")); Serial.println(comando);
#endif
#if DEBUG_UART_OBC
  controleSerial.print(F("3:DBG:RX_"));
  controleSerial.print(origemUsb ? F("USB=") : F("OBC="));
  controleSerial.println(comando);
#endif

  // Rotas idênticas para USB e OBC. Os IDs permanecem os da versao original.
  if (comando == "1") {
    avisar(F("CMD1: antes de iniciar estabilizacao"));
    equilibrado = false;
    modoAtual = MODOESTABILIZAR;
    responder(F("Estabilizar"));
  }
  else if (comando == "2") {
    avisar(F("CMD2: preparando medicao continua de angulo"));
    pararAtuadores();
    if (!luzCalibrada) {
      avisar(F("CMD2 BLOQUEADO: faca 5 (zero) e 5 (fundo)"));
      responder(F("ERRO_LDR_nao_calibrado_envie_5_duas_vezes"));
    } else {
      modoAtual = MODOMEDIRSHIFT;
      avisar(F("CMD2: estado SHIFT ativo ate proximo comando"));
      responder(F("Determinar Azimuth"));
    }
  }
  else if (comando == "3") {
    avisar(F("CMD3: consultando parametros PID"));
    responder(String(F("PID=")) + String(Kp_pos, 3) + "," + String(Kd_pos, 3) + "," + String(Kd_vel, 3));
  }
  else if (comando.startsWith("3 ")) {
    avisar(F("CMD3: atualizando parametros PID"));
    processarValoresPID(comando.substring(2));
  }
  else if (comando == "4") {
    avisar(F("CMD4: parando atuadores e iniciando MPU"));
    pararAtuadores();
    responder(F("Calibrando MPU"));
    calibrateMPU();
    avisar(F("CMD4: calibracao MPU finalizada"));
    responder(F("MPU_CALIBRADO"));
  }
  else if (comando == "5") {
    avisar(F("CMD5: parando atuadores antes da calibracao"));
    pararAtuadores();
    if (!referenciaZeroCalibrada || fundoCalibrado) {
      avisar(F("CMD5: etapa 1/2; luz acesa em 0 graus"));
      fundoCalibrado = false;
      luzCalibrada = false;
      calibrarLDRFrontal();
      responder(F("LDR:apague_a_lampada_e_envie_5_novamente"));
    } else {
      avisar(F("CMD5: etapa 2/2; fonte apagada, ambiente"));
      calibrarLDRAmbiente();
      responder(F("LDR_CALIBRADO"));
    }
  }
  else if (comando == "5:AMBIENTE") {
    avisar(F("CMD5: calibracao explicita do ambiente"));
    pararAtuadores();
    calibrarLDRAmbiente();
  }
  else if (comando == "6") {
    avisar(F("CMD6: iniciando abertura do painel"));
    pararAtuadores();
    responder(F("Abrindo Painel Solar"));
    digitalWrite(PAINEL_IN2, LOW);
    digitalWrite(PAINEL_IN1, HIGH);
    delay(TEMPO_ACIONAMENTO_PAINEL);
    avisar(F("CMD6: desligando painel apos tempo"));
    pararAtuadores();
    responder(F("Painel_Abertura_Finalizada"));
  }
  else if (comando == "7") {
    avisar(F("CMD7: fechamento de painel nao implementado"));
    responder(F("Fechar Painel Solar_NAO_IMPLEMENTADO"));
  }
  else if (comando == "8") {
    avisar(F("CMD8: iniciando abertura da antena"));
    pararAtuadores();
    responder(F("Abrindo Antena"));
    digitalWrite(ANTIN1, HIGH);
    digitalWrite(ANTIN2, LOW);
    avisar(F("CMD8: aguardando fim de curso da antena"));
    if (aguardaValorChave(SWANT1, HIGH, TEMPO_ACIONAMENTO_PAINEL)) {
      responder(F("Antena_Aberta"));
    } else {
      avisar(F("CMD8: TIMEOUT fim de curso abertura"));
      responder(F("ERRO_Chave_Abertura_Antena"));
    }
    pararAtuadores();
  }
  else if (comando == "9") {
    avisar(F("CMD9: iniciando fechamento da antena"));
    pararAtuadores();
    responder(F("Fechando Antena"));
    digitalWrite(ANTIN1, LOW);
    digitalWrite(ANTIN2, HIGH);
    avisar(F("CMD9: aguardando fim de curso da antena"));
    if (aguardaValorChave(SWANT2, HIGH, TEMPO_ACIONAMENTO_PAINEL)) {
      responder(F("Antena_Fechada"));
    } else {
      avisar(F("CMD9: TIMEOUT fim de curso fechamento"));
      responder(F("ERRO_Chave_Fechamento_Antena"));
    }
    pararAtuadores();
  }
  else if (comando == "10" || comando == "11") {
    avisar(F("EMERGENCIA: parando todos os atuadores"));
    pararAtuadores();
    responder(F("EMERGENCIA_Atuadores_Desligados"));
  }
  else if (comando == "12") {
    avisar(F("CMD12: reservado EPS, nenhuma acao"), false);
  }
  else {
    avisar(F("CMD INVALIDO: ignorado"));
    responder(F("ERRO_Comando_Invalido"));
  }
}

void loop() {
  static char bufferOBC[64] = {0};
  static char bufferUSB[64] = {0};
  static byte nOBC = 0, nUSB = 0;
  lerLinha(controleSerial, bufferOBC, nOBC, false);
  lerLinha(Serial, bufferUSB, nUSB, true);
  diagnosticarModo();
  switch (modoAtual) {
    case MODOPARADO: break;
    case MODOESTABILIZAR: estabilizar(); break;
    case MODOMEDIRSHIFT: medirShiftLuz(); break;
    case MODOORIENTARLUZ: orientarLuz(); break;
    case MODOORIENTADOIS: executarDoisAngulos(); break;
    default: pararAtuadores(); break;
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
    detachInterrupt(digitalPinToInterrupt(pinoInterrupcao));
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

      } 
      else 
      {
        // Entra no estado de equilíbrio
        equilibrado = true;
        velocidadeEquilibrio = velocidadeAtualRoda;
        // LED desligado: satélite estabilizado

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

  if (controleSerial_DEBUG_ENABLE) {
    responder(F("Iniciando_busca_360"));
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

  if (controleSerial_DEBUG_ENABLE) {
    responder(F("Varredura_completa"));
    responder(String(F("Melhor_angulo=")) + String(anguloMaisClaro));
    responder(String(F("Maior_luz=")) + String(maiorLuz));
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
  responder(F("PID_recebido"));
  int primeiroEspaco = valores.indexOf(' ');
  int segundoEspaco = valores.indexOf(' ', primeiroEspaco + 1);
  
  if (primeiroEspaco == -1 || segundoEspaco == -1) {
    responder(F("ERRO_PID_formato_KP_KD_KV"));
    responder(F("Exemplo:3 2.0 0.5 0.8"));
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
    responder(F("ERRO_PID_Kp_fora_faixa"));
    valoresValidos = false;
  }
  
  if (novoKd < 0 || novoKd > 5.0) {
    responder(F("ERRO_PID_Kd_fora_faixa"));
    valoresValidos = false;
  }
  
  if (novoKv < 0 || novoKv > 5.0) {
    responder(F("ERRO_PID_Kv_fora_faixa"));
    valoresValidos = false;
  }
  
  if (valoresValidos) {
    Kp_pos = novoKp;
    Kd_pos = novoKd;
    Kd_vel = novoKv;
    
    responder(F("PID_ATUALIZADO"));
    responder(String(F("Kp_pos=")) + String(Kp_pos, 3));
    responder(String(F("Kd_pos=")) + String(Kd_pos, 3));
    responder(String(F("Kd_vel=")) + String(Kd_vel, 3));
  } else {
    responder(F("ERRO_PID_nao_alterado"));
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
} // CHAVE CORRIGIDA: Agora a função medirShiftLuz não está presa aqui dentro!

// Estimador do shift: tabela calibrada no ultimo ensaio, interpolacao linear.
// Retem detumbling, PD legado e demais rotinas do codigo anterior.
float estimarAnguloPelasCurvas(float ldr0, float ldr1, float &residual) {
  const float r0 = ldr0 / referenciaZeroLDR0;
  const float r1 = ldr1 / referenciaZeroLDR1;
  float menor = 1.0e10f;
  float angulo = 0.0f;
  for (byte i=0; i<NUM_PONTOS_LDR-1; i++) {
    for (byte j=0; j<=20; j++) { // 5 graus / 20 = 0,25 grau (passo numerico)
      const float t = j / 20.0f;
      const float f0 = valorCurva(curvaNormalLDR0, i) + t*(valorCurva(curvaNormalLDR0, i+1) - valorCurva(curvaNormalLDR0, i));
      const float f1 = valorCurva(curvaNormalLDR1, i) + t*(valorCurva(curvaNormalLDR1, i+1) - valorCurva(curvaNormalLDR1, i));
      const float e0 = r0-f0, e1=r1-f1;
      const float J = e0*e0 + e1*e1;
      if (J < menor) { menor=J; angulo=ANGULO_INICIAL_LDR+PASSO_LDR_GRAUS*(i+t); }
    }
  }
  residual = sqrt(menor); // adimensional, NAO erro angular
  return angulo;
}

void medirShiftLuz() {
  static unsigned long ultimoEnvio = 0;
  static unsigned long ultimoRadioDetalhado = 0;
  unsigned long agora = millis();
  if (agora - ultimoEnvio < INTERVALO_SHIFT_MS) return;
  ultimoEnvio = agora;
  const bool radioDetalhado = agora - ultimoRadioDetalhado >= INTERVALO_RADIO_SHIFT_MS;
  if (radioDetalhado) ultimoRadioDetalhado = agora;

  avisar(F("SHIFT [1]: iniciando ciclo de medicao"), radioDetalhado);
  avisar(F("SHIFT [2]: verificando calibracao"), radioDetalhado);
  if (!luzCalibrada || referenciaZeroLDR0 <= 0.0f || referenciaZeroLDR1 <= 0.0f) {
    avisar(F("SHIFT BLOQUEADO: falta 5+5 ou zero invalido"), radioDetalhado);
    return;
  }
  avisar(F("SHIFT [3]: adquirindo 12 pares de LDR"), radioDetalhado);
  unsigned long soma0=0, soma1=0;
  for (byte i=0; i<12; i++) {
    soma0 += analogRead(LDRVERMELHO);
    soma1 += analogRead(LDRAZUL);
    delay(4);
  }
  const float l0 = soma0/12.0f, l1 = soma1/12.0f;
  const float r0 = l0 / referenciaZeroLDR0;
  const float r1 = l1 / referenciaZeroLDR1;
#if DEBUG_SERIAL_USB
  Serial.print(F("[SHIFT] LDR0/A0=")); Serial.print(l0, 1);
  Serial.print(F(" LDR1/A1=")); Serial.print(l1, 1);
  Serial.print(F(" r0=")); Serial.print(r0, 3);
  Serial.print(F(" r1=")); Serial.println(r1, 3);
#endif
  avisar(F("SHIFT [4]: verificando iluminacao"), radioDetalhado);
  if (l0 <= ambienteLDR0+10.0f && l1 <= ambienteLDR1+10.0f) {
    avisar(F("SHIFT REJEITADO: luz insuficiente"), radioDetalhado);
    return;
  }
  avisar(F("SHIFT [5]: comparando com as curvas"), radioDetalhado);
  avisar(F("SHIFT [6]: estimando angulo"), radioDetalhado);
  float residual;
  const float anguloShift = estimarAnguloPelasCurvas(l0,l1,residual);
  avisar(F("SHIFT [7]: estimativa calculada"), radioDetalhado);
  const bool valido = residual <= MAX_RESIDUAL_LDR;
#if DEBUG_SERIAL_USB
  Serial.print(F("[SHIFT] angulo_candidato=")); Serial.print(anguloShift, 2);
  Serial.print(F(" residual=")); Serial.print(residual, 4);
  Serial.print(F(" limite=")); Serial.print(MAX_RESIDUAL_LDR, 3);
  Serial.println(valido ? F(" ACEITO") : F(" REJEITADO"));
#endif
#if DEBUG_UART_OBC
  // Resultado candidato e erro tambem aparecem no radio a cada ~2 s,
  // mesmo quando a estimativa foi rejeitada; NAO eh um angulo valido.
  if (radioDetalhado) {
    controleSerial.print(F("3:DBG:ANG=")); controleSerial.print(anguloShift, 1);
    controleSerial.print(F(";R=")); controleSerial.print(residual, 3);
    controleSerial.println(valido ? F(";OK") : F(";REJ"));
  }
#endif
  if (!valido) {
    avisar(F("SHIFT [8]: rejeitado por residual; sem 3:angulo"), radioDetalhado);
    return;
  }
  avisar(F("SHIFT [8]: angulo aceito; enviando 3:angulo"), radioDetalhado);
  responder(String(anguloShift, 1)); // 3:<angulo> a cada ~500 ms enquanto MODO MEDIRSHIFT
  avisar(F("SHIFT [9]: responder retornou"), radioDetalhado);
}
