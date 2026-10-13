/*
 * heltec_teste_orange.ino — Heltec WiFi LoRa 32 V3 conversando com o Orange Pi Zero 3
 * Teste de bancada da missão ADS-B (CubeDesign 2026) SEM o OBC de verdade.
 *
 * O que faz:
 *  1. Sequência automática ao ligar: PING, STATUS, QRY (autorizada), QRY (clandestina), START.
 *  2. Durante a missão, pede STATUS a cada 15 s e conta as linhas AC recebidas.
 *  3. Tudo que você digitar no Serial Monitor é enviado ao Orange
 *     (pode digitar "PING" ou "2:PING", tanto faz).
 *
 * Ligação (3,3 V — NUNCA 5 V):
 *   Orange TX  -> Heltec GPIO 6 (ORANGE_RX)
 *   Orange RX  -> Heltec GPIO 7 (ORANGE_TX)
 *   Orange GND -> Heltec GND
 *
 * Serial Monitor: 115200 baud, terminação "Newline" (ou "Both NL & CR").
 * Protocolo: "2:COMANDO,args\n"  (endereço do Orange = 2)
 */

#include <Arduino.h>

// ---------------- Configuração ----------------
#define ORANGE_RX      6        // recebe do TX do Orange  (confira no pinout se está livre)
#define ORANGE_TX      7        // envia para o RX do Orange
#define BAUD_PC        115200
#define BAUD_ORANGE    115200    // igual ao Config.BAUD do adsb_cubesat.py
#define ENDERECO       "2"
#define USAR_CHECKSUM  false     // true -> adiciona "*XX" (XOR do corpo, depois de "2:")

const uint32_t STATUS_INTERVALO_MS = 15000;   // STATUS automático durante a missão

struct Passo { uint32_t t_ms; const char* cmd; };
// Sequência automática (tempo desde o boot do Heltec)
const Passo SEQUENCIA[] = {
  { 3000, "PING" },
  { 5000, "STATUS" },
  { 7000, "QRY,-22.88,-43.30" },   // TAP0803 está em 'autorizadas' -> FOUND
  { 9000, "QRY,-22.50,-43.00" },   // não existe -> NONE (clandestina)
  {11000, "START,120" },           // 2 min de missão com a antena
};
const size_t N_PASSOS = sizeof(SEQUENCIA) / sizeof(SEQUENCIA[0]);

// ---------------- Estado ----------------
size_t   proximoPasso = 0;
uint32_t t0 = 0;
uint32_t ultimoStatus = 0;
bool     rodando = false;
uint32_t nAC = 0;
String   bufOrange;
String   bufPC;

// ---------------- Protocolo ----------------
uint8_t checksum(const String& corpo) {
  uint8_t c = 0;
  for (size_t i = 0; i < corpo.length(); i++) c ^= (uint8_t)corpo[i];
  return c;
}

void enviar(String corpo) {
  corpo.trim();
  if (corpo.startsWith(ENDERECO ":")) corpo = corpo.substring(2);   // aceita "2:PING" ou "PING"
  if (corpo.length() == 0) return;

  String linha = String(ENDERECO) + ":" + corpo;
  if (USAR_CHECKSUM) {
    char cs[4];
    snprintf(cs, sizeof(cs), "*%02X", checksum(corpo));
    linha += cs;
  }
  Serial1.print(linha);
  Serial1.print('\n');               // o Orange só processa a linha ao receber '\n'

  Serial.print("-> ");
  Serial.println(linha);
}

void tratarLinha(const String& l) {
  if (!l.startsWith(ENDERECO ":")) {           // lixo de boot (U-Boot) ou outro subsistema
    Serial.print("?? ");
    Serial.println(l);
    return;
  }
  String corpo = l.substring(2);

  if (corpo.startsWith("AC,")) {
    nAC++;
    Serial.print("<- [AC #");
    Serial.print(nAC);
    Serial.print("] ");
    Serial.println(corpo);
    return;
  }

  if (corpo.startsWith("ACK,START")) { rodando = true; nAC = 0; ultimoStatus = millis(); }
  if (corpo.startsWith("ACK,STOP") || corpo.startsWith("EVT,DONE")) rodando = false;

  Serial.print("<- ");
  Serial.println(corpo);

  if (corpo.startsWith("QRES,FOUND"))     Serial.println("   => NAO clandestina");
  else if (corpo.startsWith("QRES,NONE")) Serial.println("   => CLANDESTINA");
  else if (corpo.startsWith("EVT,SDR_ERR")) Serial.println("   => erro no receptor (veja /dev/shm/adsb/dump1090.log no Orange)");
  else if (corpo.startsWith("EVT,DONE")) {
    Serial.print("   => missão terminou, AC recebidos: ");
    Serial.println(nAC);
  }
}

// ---------------- Leitura ----------------
void lerOrange() {
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      bufOrange.trim();
      if (bufOrange.length()) tratarLinha(bufOrange);
      bufOrange = "";
    } else if (c != '\r' && bufOrange.length() < 300) {
      bufOrange += c;
    }
  }
}

void lerPC() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (bufPC.length()) enviar(bufPC);
      bufPC = "";
    } else if (bufPC.length() < 120) {
      bufPC += c;
    }
  }
}

// ---------------- Arduino ----------------
void setup() {
  Serial.begin(BAUD_PC);
  Serial1.begin(BAUD_ORANGE, SERIAL_8N1, ORANGE_RX, ORANGE_TX);
  delay(500);
  Serial.println();
  Serial.println("=== Heltec <-> Orange Pi (ADS-B) ===");
  Serial.println("Sequencia automatica em 3 s. Digite comandos a qualquer momento (ex.: STATUS, STOP).");
  t0 = millis();
}

void loop() {
  lerOrange();
  lerPC();

  // sequência automática
  if (proximoPasso < N_PASSOS && millis() - t0 >= SEQUENCIA[proximoPasso].t_ms) {
    enviar(SEQUENCIA[proximoPasso].cmd);
    proximoPasso++;
  }

  // STATUS periódico durante a missão
  if (rodando && millis() - ultimoStatus >= STATUS_INTERVALO_MS) {
    ultimoStatus = millis();
    enviar("STATUS");
  }
}
