# CONTEXTO PARA IA — Módulo ADS-B do Orange Pi Zero 3 (CubeDesign 2026)

> **Cole este arquivo inteiro numa IA antes de pedir qualquer código.** É o contexto-base do projeto.
> Idioma: **português (BR)** em comentários, logs e mensagens. Documentação para a organização do evento: inglês.
> Versão para humanos (GitHub): [README.md](README.md).

Equipe **UERJsats** · CubeDesign 2026 (categoria CubeSat, UNSAM) · usuário Linux do Orange: `uerjsats`

---

## 1. O que o Orange Pi faz

Orange Pi Zero 3 (+ RTL-SDR por USB) dentro do CubeSat, **sem tela**, computador de missão:

1. **Missão principal (ADS-B):** capta 1090 MHz, decodifica a bordo e entrega ao OBC, **pela serial**, ICAO, lat, lon, altitude, velocidade e timestamp de cada aeronave.
2. **Missão secundária (clandestinas):** responde ao OBC se coordenadas constam na lista de **aeronaves autorizadas (não clandestinas)** do seu banco SQLite.

Regras:
- **Um único script**, `OrangePiZero3/adsb_cubesat.py`, orientado a objetos, **sobe sozinho no boot** (systemd) e fica em **IDLE** só ouvindo a serial. **O receptor só liga com `START`.**
- **Sem API, sem servidor web, sem JSON, sem internet.** Tudo vai pela serial para o OBC.
- Robusto: sem operador, pode perder energia/reboot a qualquer momento.

```
 simulador SDR ))) 1090 MHz ))) RTL-SDR (USB)
                                    │
                    ┌───────────────▼───────────────┐   UART 3,3 V    ┌────────────────────────┐
                    │ Orange Pi Zero 3 (este módulo)│◄───────────────►│ Heltec WiFi LoRa 32 V3 │──► downlink LoRa
                    │ dump1090 · SQLite · QRY       │  TX, RX, GND    │ (OBC, serial por SW)   │◄── TC da base
                    └───────────────────────────────┘                 └───────────▲────────────┘
                                                                                  │ Wi-Fi TCP: foto + ADS-B
                                                                      ESP32-S3 (payload de detecção de aeronaves)
```

### Fluxo da missão secundária
1. O payload ESP32 detecta uma aeronave e manda **foto + info ADS-B (coordenadas)** ao OBC por Wi-Fi/TCP. A foto **não passa pelo Orange**.
2. O OBC manda ao Orange: `QRY,<lat>,<lon>[,<icao>]`.
3. O Orange consulta a tabela `autorizadas`: **achou** → `QRES,FOUND,...` = **não clandestina**; **não achou** → `QRES,NONE` = **clandestina**.
4. O **OBC** repassa à base se é clandestina ou não (firmware do OBC, fora do Orange).

O ESP32 simula 5 aeronaves; **só duas estão no banco** (as não clandestinas). As outras três **não existem em nenhum banco** e devem sempre dar `QRES,NONE`.

---

## 2. Hardware e sistema

- **Orange Pi Zero 3**, SoC Allwinner H618, **4 GB RAM**, **Ubuntu 22.04 (Jammy) da Orange Pi** (Python 3.10). Manual: *OrangePi_Zero3_H618_user manual_v1.5.pdf*.
- **RTL-SDR** em 1090 MHz, ganho 49.6 (validado).
- **Serial com o OBC = UART0 de debug (3 pinos TX, RX, GND), 3,3 V TTL, 8N1.**
  - Device `/dev/ttyS0` (kernel 6.1) ou `/dev/ttyAS0` (kernel 5.4) — o script tenta os dois.
  - Ligação cruzada: TX Orange → RX Heltec, RX Orange → TX Heltec, GND comum. **Nunca 5 V.**
  - **Baud 115200 no código, a validar**: o Heltec usa serial **simulada por software**, que pode não aguentar. Mudar só em `Config.BAUD`.
  - Por padrão é o **console do Linux**: `instalacao/instalar_servico.py` desativa o `serial-getty` e põe `console=display` em `/boot/orangepiEnv.txt`. O U-Boot ainda imprime algo no boot → o protocolo tolera lixo (checksum + ressincroniza no `$`).
  - UART0 divide pinos com PWM3/PWM4 (não habilitar `ph-pwm34`). Alternativa: UART5 do header de 26 pinos (`ph-uart5`, `/dev/ttyS5`).

---

## 3. Protocolo serial Orange ↔ OBC

ASCII, uma mensagem por linha, estilo NMEA: `$TIPO,campo,campo,...*XX\n` — `XX` = XOR dos caracteres entre `$` e `*`, hex 2 dígitos. Classe `Protocolo` em `adsb_cubesat.py`. O OBC precisa gerar/validar o mesmo checksum. Linha de teste: `python3 adsb_cubesat.py frame "START,600"`. O Orange **ignora** TC sem checksum válido (`Config.EXIGIR_CHECKSUM_TC`; `--sem-checksum` só em bancada).

### TC — OBC → Orange
| Comando | Efeito |
|---|---|
| `PING` | `ACK,PING` |
| `START[,seg]` | inicia a missão; `seg` = duração (padrão 600 s; `0` = sem limite). `ACK,START,<sessão>` ou `NAK,START,JA_EM_EXECUCAO` |
| `STOP` | encerra (faz flush). `ACK,STOP` ou `NAK,STOP,OCIOSO` |
| `STATUS` | responde `STATUS,...` |
| `TIME,<epoch>` | ajusta o relógio **do script** (sem RTC; não mexe no relógio do Linux). `ACK,TIME` |
| `MODE,<0\|1>` | `0` = grava e transmite `AC` (padrão); `1` = só grava (recuperar com `DUMP`) |
| `DUMP,<n>` | reenvia os últimos *n* (máx. 200) como `AC`, depois `ACK,DUMP,<n>` |
| `QRY,<lat>,<lon>[,<icao>]` | consulta `autorizadas` (funciona em **qualquer estado**, inclusive IDLE) |

### TM — Orange → OBC
| Mensagem | Significado |
|---|---|
| `EVT,BOOT` | script no ar, IDLE |
| `EVT,SDR_OK` / `EVT,SDR_ERR,<msg>` | receptor iniciou / falhou (reabre sozinho com backoff) |
| `EVT,DONE,<sessão>` | missão terminou por tempo |
| `ACK,<cmd>[,..]` / `NAK,<cmd>,<motivo>` | resposta a TC |
| `AC,<seq>,<ts>,<icao24>,<lat>,<lon>,<alt_ft>,<gs_kt>` | uma aeronave (5 parâmetros do regulamento + timestamp) |
| `STATUS,<estado>,<modo>,<uptime_s>,<temp_cpu_C>,<msgs_rx>,<msgs_validas>,<aeronaves>,<ac_enviados>,<lat_media_ms>,<lat_max_ms>` | estado |
| `QRES,FOUND,<icao24>,<callsign>,<lat>,<lon>,<alt_ft>,<gs_kt>,<track_deg>,<vrate_fpm>,<squawk>` | **não clandestina** |
| `QRES,NONE` | **clandestina** |

`ts` = época Unix com ms; `lat`/`lon` 5 casas. Cada aeronave gera no máx. 1 `AC` a cada `Config.AC_INTERVALO_MIN_S` (2 s). `QRY` casa por coordenadas com `Config.TOLERANCIA_GRAUS` (±0,01° ≈ 1 km); com ICAO exige igual; vários → o mais próximo.

---

## 4. Arquitetura do código (`OrangePiZero3/adsb_cubesat.py`, arquivo único)

| Classe | Papel |
|---|---|
| `Config` | **todas** as constantes (porta, baud, ganho, tempos, pastas, tolerância, fonte) |
| `Protocolo` | `montar` / `interpretar` / `checksum` |
| `Metricas` | contadores (rx, válidas, registros, enviados, descartados, nº aeronaves, latência média/máx) → `STATUS` e log |
| `BancoMissao` | SQLite no SD (`autorizadas`, `adsb`) + buffer em RAM disk; `flush`, `consultar_autorizada`, `cli` |
| `EnlaceSerial` | serial resiliente; thread de escrita; respostas > AC; fila de AC limitada |
| `FonteADSB` (ABC) | estado por aeronave + `_emitir` (só com ICAO, lat, lon, alt, vel; ≤1 AC/aeronave/2 s) |
| ↳ `FonteDump1090` | **padrão.** Sobe `dump1090-fa`/`dump1090`/`readsb` como subprocesso e lê a **porta SBS** (TCP 127.0.0.1:30003): demodulação PPM, CRC e CPR em C; Python só faz `split(",")` |
| ↳ `FontePyModeS` | reserva. `RtlReader` do pyModeS 2.x (tudo em Python/numpy; bem mais CPU/RAM) |
| ↳ `FonteSimulada` | 20 aeronaves falsas, sem antena |
| `Missao` | máquina de estados, comandos, escritor, flush, timer |

`--fonte auto` (padrão) usa dump1090 se achar o binário, senão pyModeS. Para criar outra fonte, herde `FonteADSB` e implemente `_executar()` (registre em `criar_fonte`).

### Estados e pipeline
```
BOOT ─► IDLE ──START──► RUNNING ──STOP / tempo──► IDLE
```
Em RUNNING (threads separadas, filas limitadas): **fonte** → fila → **escritor** (grava no RAM disk `/dev/shm/adsb/live.db`; no `MODE 0` enfileira `AC`) → **serial-tx**. **flush** copia RAM → SD (`/var/lib/adsb/adsb.db`) a cada 5 s numa transação (RF07). Em IDLE o receptor está desligado (zero CPU de SDR).

### Banco (SQLite `/var/lib/adsb/adsb.db`, WAL, `synchronous=FULL`)
- **`autorizadas`** — não clandestinas, preenchidas à mão; **única tabela do `QRY`**. Campos = `ADSBRecord` do ESP32: `icao24, callsign, lat, lon, alt_ft, gs_kt, track_deg, vrate_fpm, squawk`. Iniciais (criadas só na 1ª vez):

| icao24 | callsign | lat | lon | alt_ft | gs_kt | track_deg | vrate_fpm | squawk |
|---|---|---|---|---|---|---|---|---|
| `7788FF` | TAP0803 | -22.8800 | -43.3000 | 38000 | 460 | 180 | 600 | 5511 |
| `33CC99` | UAL1890 | -23.0000 | -43.1000 | 32000 | 430 | 315 | -300 | 1200 |

- **`adsb`** — log do que o receptor decodifica (`sessao, seq, ts, ts_mono, icao24, lat, lon, alt_ft, gs_kt` + opcionais). **Nunca** entra no `QRY` (senão uma clandestina vista no SDR viraria "autorizada").
- Editar à mão (pode com o serviço rodando): `python3 adsb_cubesat.py banco listar | adicionar <icao> <callsign> <lat> <lon> <alt> <gs> <track> <vrate> <squawk> | remover <icao> | ultimos [n]`.

---

## 5. Arquivos

```
Missao/
├── README.md            ← versão GitHub (humanos)
├── README_IA.md         ← este arquivo (contexto para IA)
├── OrangePiZero3/
│   ├── adsb_cubesat.py  ← SCRIPT ÚNICO DE MISSÃO (roda no boot)
│   ├── testes_offline.py← testa tudo sem SDR nem serial (porta falsa + dump1090 falso)
│   ├── instalacao/
│   │   ├── instalar_tudo.sh       ← roda os dois abaixo
│   │   ├── instalar_ubuntu.py     ← apt, blacklist do driver de TV, compila dump1090-fa, venv ~/venv_adsb (+ Thonny, pois a equipe configura com tela; `--sem-thonny` pula)
│   │   ├── instalar_servico.py    ← libera UART de debug + serviço systemd adsb-cubesat
│   │   └── obsoleto_arch_scriptdeconfiguracao.py  ← era p/ Arch Linux ARM; NÃO usar
│   └── Comunicacao Serial/Serialteste.py   ← teste antigo da serial
├── teste ADS-B/         ← scripts antigos de teste/medição com pyModeS
├── Teste de Consumo/    ← teste de consumo
└── Missao_Secundaria*/  ← firmware ESP32-S3 (visão computacional) — NÃO roda no Orange
```

## 6. Pegadinhas já descobertas
- **Instalação do dump1090-fa:** compila-se do GitHub da FlightAware com `make RTLSDR=yes BLADERF=no HACKRF=no LIMESDR=no SOAPYSDR=no` (não há `.deb` para Ubuntu 22.04 ARM). O instalador cai para `readsb` se falhar. Opções usadas: `--gain --freq --net --net-sbs-port --net-bind-address 127.0.0.1 --quiet` (`readsb` ainda recebe `--device-type rtlsdr`).
- **Só um processo pode usar o dongle:** em IDLE nada o abre; não rode `dump1090` à mão com o serviço em RUNNING.
- **Reserva pyModeS:** `pyModeS < 3` (a v3 quebra o `RtlReader`), `pyrtlsdr == 0.2.93`, `setuptools < 81`; `RtlReader.run()` lança `ValueError` sem tráfego (não é fatal, o código reinicia).
- Com dump1090 o `crc_falha` fica 0 (ele já descarta mensagens ruins) e `rx` conta só mensagens válidas.
- Instaladores rodam **como usuário normal** (chamam `sudo` quando precisam).
- Sem tela: tudo vai para log em arquivo (`/var/lib/adsb/orange.log`) e `journalctl -u adsb-cubesat`.

## 7. Regras para quem mexer no código
1. Não inventar hardware/pinos/baud: o que não está aqui ou no manual vira `TODO` ou pergunta.
2. Constantes só em `Config`. Nada interativo (`input()`, GUI).
3. Não bloquear a decodificação; filas com limite; disco em lote/transação; sem JSON/API/web.
4. **Python 3.10** (Ubuntu 22.04); manter o script único e leve (stdlib + pyserial).
5. Mudou protocolo, tabela ou comportamento? **Atualize os dois READMEs** e avise quem faz o firmware do Heltec.
6. Rode `python3 testes_offline.py` antes de entregar.

## 8. Pendências
- [ ] Validar o **baud** real com o Heltec (SoftwareSerial).
- [ ] Testar com **RTL-SDR e simulador SDR reais** (parser SBS e subprocesso foram testados offline com um dump1090 falso; a compilação do dump1090-fa no Orange e o dongle real ainda não).
- [ ] Confirmar `console=display` em `/boot/orangepiEnv.txt` e `ttyS0` livre no Ubuntu 22.04 da placa.
- [ ] Firmware do Heltec: checksum, tratar `QRES`, reportar clandestina/não clandestina à base.
- [ ] Metas quantitativas de sucesso (% decodificado, perda máx., latência máx.) usando os números do `STATUS`.
- [ ] Preencher mais aeronaves em `autorizadas` / ajustar a tolerância do `QRY`.

Créditos: UERJsats. Firmwares ESP32 derivados do VANTsat_TX_V3 (Carlos Leal e Vitor Forny) — https://github.com/uerjsats
