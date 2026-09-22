# Missão Secundária — CubeDesign 2026

Firmware para ESP32-S3 (XIAO Sense) que detecta triângulos por visão computacional,
salva a foto no cartão SD, simula um registro ADS-B associado à detecção e expõe
tudo isso (fotos + telemetria + ADS-B) num painel web servido pelo próprio ESP32.

> Base: projeto **VANTsat** — Equipe UERJsats (Missão Atlas, LASC 2026).
> Reaproveita o `VisionSystem` original (detecção geométrica) e o fluxo de
> captura/gravação/envio de foto, restringindo o disparo apenas a `TRIANGULO`.

---

## Sumário

- [Como funciona](#como-funciona)
- [Módulos](#módulos)
- [Hardware](#hardware)
- [Rede](#rede)
- [Painel web](#painel-web)
- [Como compilar e gravar](#como-compilar-e-gravar)
- [Estrutura de dados no SD](#estrutura-de-dados-no-sd)
- [Limitações conhecidas](#limitações-conhecidas)
- [Créditos](#créditos)

---

## Como funciona

```
Câmera (OV2640)
     │
     ▼
VisionSystem ──► identifica forma (contorno + RDP + ângulos)
     │
     ├── QUADRADO / DESCONHECIDO ──► descarta
     │
     └── TRIANGULO
           │
           ▼
     StorageHandler ──► converte p/ JPEG e salva em /missao/<index>.jpg (buffer circular)
           │
           ▼
     ADSBSimulator ──► sorteia 1 de 5 aeronaves fictícias
           │
           ▼
     NetworkManager ──► broadcast UDP do ADS-B + grava o registro no log da missão
```

Quando o "computador de bordo" baixa uma foto via TCP (`GET:<index>`), o
`StorageHandler` **move** o arquivo do buffer circular para a pasta da sessão de
missão atual — assim ela não é sobrescrita depois nem apagada num reinício.

## Módulos

| Arquivo | Responsabilidade |
|---|---|
| `missao_secundaria_cubedesign.ino` | `setup()`/`loop()`: inicializa câmera, SD, PSRAM e WiFi; orquestra captura → detecção → broadcast → log |
| `VisionSystem.h/.cpp` | Detecção de contorno (Moore-Neighbor), simplificação (Douglas-Peucker) e classificação geométrica por ângulos |
| `StorageHandler.h/.cpp` | Captura de frame, conversão para JPEG, buffer circular no SD, pasta/contador de missão, arquivamento de fotos enviadas e log de ADS-B |
| `NetworkManager.h/.cpp` | Access Point WiFi, DNS (captive portal simples), broadcast UDP do ADS-B, servidor TCP de fotos e servidor HTTP (painel web) |
| `ADSBSimulator.h/.cpp` | Base fixa de 5 aeronaves fictícias e serialização do payload JSON |

## Hardware

- **Placa:** Seeed XIAO ESP32S3 Sense
- **Câmera:** OV2640 (módulo integrado da placa Sense), escala de cinza, QVGA
- **Armazenamento:** cartão microSD via SPI (formatado em FAT32)

### Pinagem da câmera

| Sinal | GPIO |
|---|---|
| XCLK | 10 |
| SIOD | 40 |
| SIOC | 39 |
| Y9–Y2 | 48, 11, 12, 14, 16, 18, 17, 15 |
| VSYNC | 38 |
| HREF | 47 |
| PCLK | 13 |

### Pinagem do SD (SPI)

| Sinal | GPIO |
|---|---|
| SCK | 7 |
| MISO | 8 |
| MOSI | 9 |
| CS | 21 |

## Rede

O ESP32 sobe seu próprio Access Point — não há acesso à internet nessa rede.

| Parâmetro | Valor |
|---|---|
| SSID | `AMARAL_I` |
| Senha | `uerjsats123` |
| IP do AP | `192.168.4.1` |
| DNS | resolve `www.missao.maverick.com` para o IP do AP (captive portal simples) |

| Serviço | Porta | Protocolo | Descrição |
|---|---|---|---|
| Broadcast ADS-B | 4444 | UDP | Envia um JSON com os dados da aeronave sorteada sempre que um triângulo é detectado |
| Servidor de fotos | 8888 | TCP | Recebe `GET:<index>\n` e responde `START:<tipo>:<index>:<size>:<id>:<timestamp>\n` + bytes do JPEG + `\nEND_FRAME\n`. Ao concluir, arquiva a foto na pasta da missão atual |
| Painel web | 80 | HTTP | `GET /` mostra o painel; `GET /download?file=<path>` baixa um arquivo do SD |

### Exemplo de payload ADS-B (UDP)

```json
{
  "icao24": "A1B2C3",
  "callsign": "TAM3251",
  "lat": -22.9068,
  "lon": -43.1729,
  "alt_ft": 35000,
  "gs_kt": 450,
  "track_deg": 90,
  "vrate_fpm": 0,
  "squawk": "2200",
  "photo_index": 3,
  "photo_size": 18234
}
```

## Painel web

Acesse `http://192.168.4.1/` (conectado no AP) pra ver duas galerias:

1. **Buffer circular (ao vivo)** — as até 20 fotos mais recentes capturadas
   nesta sessão, com a telemetria de detecção (`data.txt`: tipo, índice,
   tamanho, timestamp da missão). É apagado a cada reinício do ESP32.
2. **Fotos enviadas (missão atual)** — fotos que já foram efetivamente
   baixadas pelo "computador de bordo" via TCP, junto com o registro ADS-B
   correspondente (`adsb.txt` da pasta da missão). Essas sobrevivem a
   reinícios, já que ficam fora do que o buffer circular limpa.

## Como compilar e gravar

1. Arduino IDE (ou `arduino-cli`) com o core **esp32** by Espressif instalado.
2. Placa: `XIAO_ESP32S3` (com PSRAM habilitada).
3. Bibliotecas usadas (já inclusas no core esp32): `esp_camera`, `WiFi`, `WiFiUdp`,
   `WebServer`, `DNSServer`, `SD`, `SPI`.
4. Abra `missao_secundaria_cubedesign.ino`, selecione a placa/porta corretas e grave.
5. Acompanhe o boot pelo Serial Monitor em `115200` baud.

## Estrutura de dados no SD

```
/missao_id.txt                    (contador persistente de sessões de missão)
/missao/
  ├── 0.jpg ... 19.jpg            (buffer circular: fotos ainda não enviadas)
  ├── data.txt                    (telemetria do buffer circular: TIPO, CID, TID, Size, TS)
  └── missao_XXX/                 (pasta desta sessão, criada no boot)
        ├── <index>.jpg           (fotos já enviadas via TCP, arquivadas aqui)
        └── adsb.txt              (1 linha por ADS-B enviado: CID, ICAO24, CALLSIGN, LAT, LON, ALT_FT, GS_KT, TRACK_DEG, VRATE_FPM, SQUAWK)
```

Sem internet nem RTC, não há como usar data/hora real — por isso as sessões
são numeradas sequencialmente (`missao_001`, `missao_002`, ...) em vez de
organizadas por data.

## Limitações conhecidas

- **SD obrigatório para persistência:** sem cartão SD montado, a detecção e o
  broadcast ADS-B continuam funcionando, mas nenhuma foto é salva e o painel
  web retorna erro.
- **`GET /download` sem validação de caminho:** o parâmetro `file` é usado
  diretamente para abrir arquivos no SD, sem restringir a um diretório — vale
  travar isso antes de expor a rede a mais gente.
- **Buffer circular de 20 posições:** se o computador de bordo não baixar as
  fotos em ritmo suficiente, uma detecção pode ser sobrescrita antes de ser
  arquivada.

## Créditos

Baseado no firmware **VANTsat_TX_V3** — Equipe UERJsats, Missão Atlas (LASC 2026).
Desenvolvedores originais: Carlos Leal e Vitor Forny.

- https://github.com/uerjsats
- https://github.com/Caduleal
- https://github.com/vitorforny04
