# 📟 Guia do OBC — como falar com o Orange Pi (endereço `2`)

Para quem faz o firmware do **computador de bordo (Heltec WiFi LoRa 32 V3)**. Aqui está tudo que o OBC precisa mandar para o Orange Pi, tudo que ele recebe de volta e como tratar.

> Versão técnica do lado do Orange: [README.md](README.md) · código: [`OrangePiZero3/adsb_cubesat.py`](OrangePiZero3/adsb_cubesat.py)

---

## 1. Resumo

- O Orange Pi é o subsistema **`2`**. Toda mensagem é **uma linha de texto** terminada em `\n`.
- **OBC → Orange:** `2:COMANDO[,arg1,arg2,...]`
- **Orange → OBC:** `2:TIPO[,campo1,campo2,...]`
- O Orange **só responde a linhas que começam com `2:`**. O resto (outros subsistemas, lixo de boot) é ignorado.
- Comandos não diferenciam maiúscula/minúscula e toleram espaços (`2: start ,600` = `2:START,600`).
- Ao ligar, o Orange fica **parado (IDLE)**: não liga o rádio até receber `2:START`.

```
OBC  ──►  2:START,600\n
OBC  ◄──  2:ACK,START,1\n
OBC  ◄──  2:EVT,SDR_OK\n          (1–3 s depois)
OBC  ◄──  2:AC,1,1790950771.056,40621D,52.26578,3.93891,38000,450\n   (uma por aeronave, continuamente)
```

## 2. Ligação física

UART0 de debug do Orange Pi Zero 3 (3 pinos), **3,3 V**, 8N1:

| Orange Pi | Heltec (OBC) |
|---|---|
| TX | RX |
| RX | TX |
| GND | GND |

> ⚠️ Nunca 5 V. **Baud: hoje 115200 no Orange** (`Config.BAUD`). O Controle de Atitude usa 9600 por SoftwareSerial no mesmo OBC. **OBC e Orange têm que usar o mesmo baud — combinem e avisem quem mexe no Orange para ajustar.** Em 9600 o fluxo de 20 aeronaves ainda cabe (≈ 550 B/s de 960 B/s), mas com folga pequena.

---

## 3. Comandos (OBC → Orange)

| Comando | Argumentos | O que faz | Resposta |
|---|---|---|---|
| `2:PING` | — | teste de vida | `2:ACK,PING` |
| `2:START` | `[,segundos]` | liga o receptor ADS-B e inicia a missão. Sem argumento = **600 s** (10 min); `0` = sem limite | `2:ACK,START,<sessao>` ou `2:NAK,START,JA_EM_EXECUCAO` |
| `2:STOP` | — | encerra a missão e salva os dados | `2:ACK,STOP` ou `2:NAK,STOP,OCIOSO` |
| `2:STATUS` | — | pede o estado do Orange | `2:STATUS,...` (ver §4) |
| `2:TIME` | `,<epoch>` | acerta o relógio do script (segundos desde 1970). O Orange **não tem RTC** | `2:ACK,TIME` |
| `2:MODE` | `,0` ou `,1` | `0` grava **e** transmite os `AC` (padrão); `1` só grava (recupera depois com `DUMP`) | `2:ACK,MODE,<n>` ou `2:NAK,MODE,ARGUMENTO` |
| `2:DUMP` | `[,n]` | reenvia os últimos *n* registros gravados (máx. **200**; padrão 200) | `n` linhas `2:AC,...` + `2:ACK,DUMP,<n>` |
| `2:QRY` | `,lat,lon[,icao]` | **missão secundária:** a aeronave consta no banco de autorizadas? | `2:QRES,FOUND,...` ou `2:QRES,NONE` |

Comando desconhecido → `2:NAK,<CMD>,DESCONHECIDO`. Erro interno → `2:NAK,<CMD>,ERRO_INTERNO`.

### Detalhes importantes
- **`START` sem `TIME` antes:** os timestamps dos `AC` saem com o relógio do Linux do Orange (sem RTC, pode estar errado). **Mande `2:TIME,<epoch>` antes do `START`.**
- **`2:START,600`** roda 10 min e termina sozinho com `2:EVT,DONE,<sessao>`. `START` durante missão → `NAK,START,JA_EM_EXECUCAO`.
- **`QRY` funciona em qualquer estado** (inclusive IDLE, sem receptor ligado).
- **`QRY` com ICAO:** se mandar o 3º argumento, o ICAO também precisa bater (maiúsc./minúsc. tanto faz). Sem ICAO, só as coordenadas (tolerância ±0,01° ≈ 1 km).
- **`DUMP`:** as linhas `AC` do dump e o `ACK,DUMP,<n>` podem chegar em qualquer ordem — **o `ACK` pode vir antes das linhas**. Use o `n` do ACK para saber quantas esperar.
- Checksum é **opcional** (veja §8).

---

## 4. Mensagens do Orange → OBC

| Mensagem | Quando | Campos |
|---|---|---|
| `2:EVT,BOOT` | script do Orange subiu (IDLE) | — |
| `2:EVT,SDR_OK` | receptor ADS-B ligou (1–3 s após o `START`) | — |
| `2:EVT,SDR_ERR,<msg>` | receptor falhou (dongle solto, etc.); o Orange tenta reabrir sozinho | `msg` ≤ 60 caracteres, sem vírgula |
| `2:EVT,DONE,<sessao>` | missão terminou por tempo | nº da sessão |
| `2:ACK,<cmd>[,..]` / `2:NAK,<cmd>,<motivo>` | resposta a comando | — |
| `2:AC,...` | **uma aeronave decodificada** (ver abaixo) | — |
| `2:STATUS,...` | resposta ao `STATUS` | — |
| `2:QRES,...` | resposta ao `QRY` | — |

### `AC` — aeronave decodificada
```
2:AC,<seq>,<ts>,<icao24>,<lat>,<lon>,<alt_ft>,<gs_kt>
2:AC,17,1790950771.056,40621D,52.26578,3.93891,38000,450
```
| Campo | Tipo | Significado |
|---|---|---|
| `seq` | inteiro | contador da sessão (volta a 1 a cada `START`) |
| `ts` | decimal | época Unix, com ms (já com o ajuste do `TIME`) |
| `icao24` | 6 hex | identificador ICAO |
| `lat`, `lon` | decimal (5 casas) | graus |
| `alt_ft` | inteiro | altitude em pés |
| `gs_kt` | inteiro | velocidade em nós |

Cada aeronave gera **no máximo 1 `AC` a cada 2 s**. Com `MODE 1` **não** saem `AC`.

### `STATUS`
```
2:STATUS,<estado>,<modo>,<uptime_s>,<temp_cpu_C>,<msgs_rx>,<msgs_validas>,<aeronaves>,<ac_enviados>,<lat_media_ms>,<lat_max_ms>
2:STATUS,RUNNING,0,3,48.2,180,180,20,60,0.7,1.0
```
`estado` = `IDLE` ou `RUNNING` · `modo` = 0/1 · `temp_cpu_C` pode vir vazio · `aeronaves` = quantas distintas na sessão · `lat_*` = atraso decodificação→serial.

### `QRES` — resultado da missão secundária
```
2:QRES,FOUND,<icao24>,<callsign>,<lat>,<lon>,<alt_ft>,<gs_kt>,<track_deg>,<vrate_fpm>,<squawk>     ← NÃO clandestina
2:QRES,NONE                                                                                         ← CLANDESTINA
```
Se os argumentos do `QRY` forem inválidos: `2:NAK,QRY,ARGUMENTO`.

---

## 5. Regras que o OBC precisa seguir

1. **Termine toda linha com `\n`.** O Orange lê linha a linha.
2. **Case as respostas pelo TIPO, não pela ordem.** Linhas `AC` e `EVT` chegam a qualquer momento, inclusive entre o comando e a resposta dele.
3. **Um comando por vez:** espere o `ACK`/`NAK`/`QRES`/`STATUS` antes do próximo. Timeouts sugeridos: `PING`, `STATUS`, `TIME`, `MODE`, `QRY` → **2 s**; `START` e `STOP` → **10 s** (o `STOP` espera o receptor fechar e gravar no SD).
4. **Depois de ligar o Orange, espere o `2:EVT,BOOT`** antes de mandar comandos (o Linux leva dezenas de segundos para subir; comandos enviados antes se perdem). Alternativa: mande `2:PING` a cada 2 s até chegar `ACK,PING`.
5. **Ignore linhas que não começam com `2:`** (lixo de boot do Orange) e **linhas incompletas**.
6. **Buffer de leitura ≥ 128 bytes** por linha (as maiores, `QRES,FOUND` e `EVT,SDR_ERR`, têm ~75).
7. **Drene a serial continuamente** durante a missão: o Orange manda `AC` o tempo todo. Se o OBC não ler, o Orange descarta os `AC` mais antigos (a fila dele é limitada), mas **nunca** descarta `ACK/NAK/EVT/QRES/STATUS`.
8. Se o Orange reiniciar (queda de energia), ele volta a **IDLE** e manda `2:EVT,BOOT` de novo: **reenvie `TIME` e `START`** se a missão devia continuar.

---

## 6. Sequência típica da missão

```
(liga o Orange)               OBC ◄ 2:EVT,BOOT
OBC ► 2:TIME,1790950700       OBC ◄ 2:ACK,TIME
OBC ► 2:START,600             OBC ◄ 2:ACK,START,1
                              OBC ◄ 2:EVT,SDR_OK
                              OBC ◄ 2:AC,1,...   2:AC,2,...   (continuamente, 10 min)
OBC ► 2:STATUS                OBC ◄ 2:STATUS,RUNNING,0,...     (a qualquer momento)
                              OBC ◄ 2:EVT,DONE,1               (ou OBC ► 2:STOP → 2:ACK,STOP)
```

## 7. Missão secundária (clandestinas)

O ESP32 manda ao OBC foto + dados ADS-B (JSON por UDP, porta 4444, com `icao24`, `lat`, `lon`...). Para cada detecção o OBC faz:

```
OBC ► 2:QRY,-22.8800,-43.3000,7788FF     (ICAO opcional)
OBC ◄ 2:QRES,FOUND,7788FF,TAP0803,-22.88,-43.3,38000,460,180,600,5511   → NÃO clandestina
        ou
OBC ◄ 2:QRES,NONE                                                       → CLANDESTINA
```
O **OBC** então manda à base terrestre o veredito (clandestina / não clandestina) junto com a foto e os dados. O Orange **não** recebe a foto.

O banco do Orange só tem **duas** aeronaves autorizadas: `7788FF` (TAP0803, -22.88/-43.30) e `33CC99` (UAL1890, -23.00/-43.10). As outras três que o ESP32 simula (`A1B2C3`, `D4E5F6`, `11AACC`) **devem** dar `QRES,NONE`.

---

## 8. Checksum (opcional)

Por padrão **não é necessário**. Se quiserem proteger o link, o Orange valida um sufixo `*XX` quando ele vier: `XX` = XOR de todos os caracteres **depois de `2:`** até antes do `*`, em hexadecimal com 2 dígitos maiúsculos.
```
2:PING*XX   com XX = 'P' ^ 'I' ^ 'N' ^ 'G'
```
Se o checksum vier errado a linha é ignorada. Para **exigir** checksum e/ou **enviar** checksum nas respostas, avisem quem mexe no Orange (`Config.EXIGIR_CHECKSUM_TC` e `Config.CHECKSUM_TX`).

---

## 9. Exemplo de código para o OBC (Arduino/ESP32)

> Exemplo ilustrativo, **ainda não testado no hardware**. Funciona com qualquer `Stream` (`SoftwareSerial`, `HardwareSerial`...).

```cpp
// ---- Link com o Orange Pi (endereço 2) ----
Stream* orange;                       // ex.: orange = &orangeSerial;
char linha[160]; uint8_t n = 0;

void orangeInit(Stream& s) { orange = &s; }

void orangeEnviar(const String& cmd) {            // orangeEnviar("START,600");
  orange->print("2:"); orange->print(cmd); orange->print('\n');
}

// Chame no loop(): devolve true quando chegou uma linha COMPLETA para o endereço 2
bool orangeLer(String& tipo, String& resto) {
  while (orange->available()) {
    char c = orange->read();
    if (c == '\r') continue;
    if (c != '\n') { if (n < sizeof(linha) - 1) linha[n++] = c; continue; }
    linha[n] = 0; n = 0;
    if (linha[0] != '2' || linha[1] != ':') continue;       // não é do Orange: ignora
    String s = String(linha + 2);
    int v = s.indexOf(',');
    tipo  = (v < 0) ? s : s.substring(0, v);                // ACK, NAK, EVT, AC, STATUS, QRES
    resto = (v < 0) ? "" : s.substring(v + 1);              // campos separados por vírgula
    return true;
  }
  return false;
}

// Espera uma resposta de um TIPO específico (ignora AC/EVT que chegarem no meio)
bool orangeAguardar(const char* esperado, String& resto, uint32_t timeoutMs) {
  uint32_t t0 = millis(); String tipo;
  while (millis() - t0 < timeoutMs) {
    if (orangeLer(tipo, resto)) {
      if (tipo == esperado) return true;
      if (tipo == "NAK")    return false;
      // AC / EVT / outros: trate ou descarte aqui
    }
  }
  return false;
}

// Missão secundária: true = CLANDESTINA
bool aeronaveClandestina(float lat, float lon) {
  orangeEnviar("QRY," + String(lat, 4) + "," + String(lon, 4));
  String r;
  if (!orangeAguardar("QRES", r, 2000)) return true;        // sem resposta: decida a política
  return r.startsWith("NONE");                              // FOUND,... = não clandestina
}
```

---

## 10. Como testar sem a missão completa

1. **Teste de vida:** `2:PING` → `2:ACK,PING`.
2. **Consulta:** `2:QRY,-22.88,-43.30` → `QRES,FOUND,7788FF,...`; `2:QRY,-22.9068,-43.1729` → `QRES,NONE`.
3. **Missão com aeronaves falsas (sem antena):** peça para rodarem o Orange com `python3 adsb_cubesat.py --fonte sim`; mande `2:START,30` e confira ~20 aeronaves em `AC`.
4. **Estado:** `2:STATUS`.
5. Para mexer na lista de autorizadas: `python3 adsb_cubesat.py banco listar|adicionar|remover` (no Orange).

## 11. Pontos em aberto (combinar com o time do Orange)
- [ ] **Baud** definitivo (9600 vs 115200).
- [ ] Mandar ou não o **ICAO** no `QRY`.
- [ ] Usar **checksum** ou não.
- [ ] Tratamento do OBC quando o `QRY` não tem resposta (sugestão: tentar de novo 1× e, se falhar, marcar como "não verificada").
