# Missão Secundária — CubeDesign 2026

Firmware para ESP32-S3 (XIAO Sense) que detecta triângulos por visão computacional,
salva a foto no cartão SD e simula um registro ADS-B associado à detecção.

> Base: projeto **VANTsat** — Equipe UERJsats (Missão Atlas, LASC 2026).
> Reaproveita o `VisionSystem` original (detecção geométrica) e o fluxo de
> captura/gravação/envio de foto, restringindo o disparo apenas a `TRIANGULO`.

---

## Sumário

- [Como funciona](#como-funciona)
- [Módulos](#módulos)
- [Hardware](#hardware)
- [Rede](#rede)
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
     StorageHandler ──► converte p/ JPEG e salva em /missao/<index>.jpg no SD
           │
           ▼
     ADSBSimulator ──► sorteia 1 de 5 aeronaves fictícias
           │
           ▼
     NetworkManager ──► broadcast UDP com o JSON do ADS-B (inclui o índice da foto)
```

Em paralelo, o `NetworkManager` também sobe um servidor TCP (porta 8888) que
atende pedidos `GET:<index>` para o "computador de bordo" puxar uma foto
específica, e um servidor HTTP (porta 80) com uma galeria web das fotos salvas.

## Módulos

| Arquivo | Responsabilidade |
|---|---|
| `missao_secundaria_cubedesign.ino` | `setup()`/`loop()`: inicializa câmera, SD, PSRAM e WiFi; orquestra captura → detecção → broadcast |
| `VisionSystem.h/.cpp` | Detecção de contorno (Moore-Neighbor), simplificação (Douglas-Peucker) e classificação geométrica por ângulos |
| `StorageHandler.h/.cpp` | Captura de frame, conversão para JPEG, gravação/leitura no SD e log de telemetria |
| `NetworkManager.h/.cpp` | Access Point WiFi, broadcast UDP do ADS-B, servidor TCP de fotos e servidor HTTP (galeria web) |
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
| SSID | `VANTsat_AP` |
| Senha | `uerjsats123` |
| IP do AP | `192.168.4.1` |

| Serviço | Porta | Protocolo | Descrição |
|---|---|---|---|
| Broadcast ADS-B | 4444 | UDP | Envia um JSON com os dados da aeronave sorteada sempre que um triângulo é detectado |
| Servidor de fotos | 8888 | TCP | Recebe `GET:<index>\n` e responde `START:<tipo>:<index>:<size>:<id>:<timestamp>\n` + bytes do JPEG + `\nEND_FRAME\n` |
| Galeria web | 80 | HTTP | `GET /` lista as fotos salvas; `GET /download?file=<path>` baixa um arquivo do SD |

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

## Como compilar e gravar

1. Arduino IDE (ou `arduino-cli`) com o core **esp32** by Espressif instalado.
2. Placa: `XIAO_ESP32S3` (com PSRAM habilitada).
3. Bibliotecas usadas (já inclusas no core esp32): `esp_camera`, `WiFi`, `WiFiUdp`,
   `WebServer`, `DNSServer`, `SD`, `SPI`.
4. Abra `missao_secundaria_cubedesign.ino`, selecione a placa/porta corretas e grave.
5. Acompanhe o boot pelo Serial Monitor em `115200` baud.

## Estrutura de dados no SD

```
/missao/
  ├── 0.jpg ... 19.jpg   (buffer circular de até 20 fotos)
  └── data.txt           (log: TIPO, índice circular, índice total, tamanho, timestamp)
```

## Limitações conhecidas

- **SD obrigatório para persistência:** sem cartão SD montado, a detecção e o
  broadcast ADS-B continuam funcionando, mas nenhuma foto é salva e a galeria
  web retorna erro.
- **`GET /download` sem validação de caminho:** o parâmetro `file` é usado
  diretamente para abrir arquivos no SD, sem restringir a um diretório — vale
  travar isso antes de expor a rede a mais gente.
- **Frontend em revisão:** a galeria web ainda referencia imagens de branding
  de outro projeto (`atlas.png`, `solo.png`, `sats.png`, `wall.jpg`) que não
  existem neste SD.

## Créditos

Baseado no firmware **VANTsat_TX_V3** — Equipe UERJsats, Missão Atlas (LASC 2026).
Desenvolvedores originais: Carlos Leal e Vitor Forny.

- https://github.com/uerjsats
- https://github.com/Caduleal
- https://github.com/vitorforny04
