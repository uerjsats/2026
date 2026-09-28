# Missão Secundária — CubeDesign 2026 (branch `deteccao-aviao`)

Firmware para ESP32-S3 (XIAO Sense) que detecta **silhuetas de aeronaves**
(UCAVs tipo asa delta) por visão computacional, salva a foto no cartão SD,
simula um registro ADS-B associado à detecção e expõe tudo isso (foto +
telemetria + ADS-B) num painel web servido pelo próprio ESP32.

> Esta branch é dedicada **só** a avião — o caminho original de detecção de
> `TRIANGULO`/`QUADRADO` do VANTsat_TX_V3 foi removido daqui e continua
> intacto na `main`. Veja [O que é herdado do VANTsat_TX_V3](#o-que-é-herdado-do-vantsat_tx_v3-x-o-que-é-novo)
> pro mapeamento exato do que foi reaproveitado.

---

## Sumário

- [Como funciona](#como-funciona)
- [O algoritmo de detecção de avião](#o-algoritmo-de-detecção-de-avião)
- [O que é herdado do VANTsat_TX_V3 x o que é novo](#o-que-é-herdado-do-vantsat_tx_v3-x-o-que-é-novo)
- [Validação](#validação)
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
identifyAircraft() ──► Otsu + maior mancha escura + contorno + polígono
     │
     ├── NENHUM ──► descarta o quadro
     │
     └── AVIAO
           │
           ▼
     StorageHandler ──► converte p/ JPEG e salva direto em
     │                  /missao/missao_XXX/<index>.jpg (pasta da sessão atual)
     ▼
     ADSBSimulator ──► sorteia 1 de 5 aeronaves fictícias
     │
     ▼
     NetworkManager ──► broadcast UDP do ADS-B + grava o registro
                         em adsb.txt na pasta da missão
```

Cada detecção já nasce salva na pasta definitiva da sessão — não existe mais
buffer circular nem etapa de "mover depois". Isso porque, numa versão
anterior, a foto só era arquivada quando o computador de bordo baixava ela
via TCP, e sem esse download a foto nunca saía do buffer temporário e era
apagada no próximo reinício. Salvar direto elimina esse problema.

## O algoritmo de detecção de avião

`identifyAircraft()` (em `VisionSystem.cpp`) é bem diferente do teste de
ângulo do VANTsat original — ele olha a silhueta inteira, combinando:

| Etapa | O que faz |
|---|---|
| Limiar de Otsu | Separa "tinta" de "papel" adaptando-se ao contraste real do quadro, em vez de uma fração fixa do brilho médio |
| Maior mancha interior | Escolhe o maior blob escuro que não encosta na borda do quadro (8-conectado, busca exaustiva) |
| Contorno Moore-Neighbor | Rastreia a borda da mancha a partir do primeiro pixel dela (variante do algoritmo original, mas com backtrack em pixel de fundo em vez de limiar de intensidade) |
| Polígono (RDP fechado) | Simplifica o contorno num polígono, dividindo primeiro no ponto mais distante da semente (variante fechada do RDP original, que era pra uma curva aberta) |
| Casco convexo | Calcula a área do casco convexo do polígono pra medir a solidez |

E só classifica como `AVIAO` quando **todos** esses testes batem ao mesmo tempo:

| Critério | Faixa aceita | O que verifica |
|---|---|---|
| Proporção (aspecto) | 0.60 – 2.40 | Largura vs altura da caixa envolvente |
| Preenchimento | 0.30 – 0.75 | Quanto da caixa está "pintado" (separa sólido de contorno vazado) |
| Solidez | ≥ 0.65 | Área real ÷ área do casco convexo (mede reentrâncias, tipo as juntas das asas) |
| Ângulo do nariz | 30° – 100° | Quão pontudo/rombudo é o topo da silhueta |
| Topo estreito | ≤ 15% da largura máxima | O nariz não pode já começar largo |
| Parte mais larga | na metade de baixo (≥ 55% da altura) | Asas atrás, não na frente |
| Centro de massa | 50% – 80% da altura | Deslocado pra trás, não centralizado |
| Simetria | assimetria ≤ 10% | Nariz e asas alinhados no eixo central |
| Nº de vértices do polígono | 5 – 16 | Triângulo/quadrado puro tem 3-4; a silhueta de asa delta tem mais por causa dos recortes das asas |

As faixas de aspecto (era até 2.10), solidez (era ≥ 0.72) e ângulo do nariz
(era até 85°) foram alargadas depois de testar o B-2 Spirit real, que é bem
mais largo e "rombudo" que as deltas mais compactas usadas na calibração
inicial — cada alargamento foi revalidado contra as 337 formas de teste antes
de ser aplicado (ver [Validação](#validação)).

## O que é herdado do VANTsat_TX_V3 x o que é novo

Só duas coisas desta branch vêm do firmware original (marcadas com
`NÃO MUDE NADA AQUI!` no `VisionSystem.cpp`):

- **`initVisionBuffers()`** — aloca o buffer `contour[]` na PSRAM.
- **`perpendicularDistance()`** — usada pela simplificação de polígono
  (`acRdp`/`acPolygon`) do detector de avião.

Todo o resto do caminho original (`findContour`, `drawLine`,
`simplifyContour`, `calculateCentroid`, `identifyShape`, e o
`processVisionFrame` — que já nem era chamado por ninguém) foi removido
**só nesta branch**. Continua 100% intacto na `main`. Tudo daqui pra baixo em
`VisionSystem.cpp` (Otsu, blob-fill, `acTrace`, `acPolygon`, `acHullArea`,
`identifyAircraft`) é código novo.

## Validação

Antes de mexer no hardware, o algoritmo foi validado rodando o
`VisionSystem.cpp` real do projeto (compilado, não simulado) contra centenas
de quadros sintéticos 320×240 gerados em Python, cobrindo pouca luz,
desfoque, ruído e reflexo de brilho:

- **337 quadros de controle** (triângulos, quadrados, vazio, e 12 formas
  "armadilha": círculo, elipse, estrela, cruz, casa, losango, T, pentágono,
  trapézio, retângulo largo, triângulo reto e invertido) → **0 falsos
  positivos** em todos.
- **189 quadros das aeronaves de referência** (Euro, X-47C, X-45C, a mesma
  família do X-45A/X-47B/B-2) → **187 reconhecidos** (98,9%).
- **X-45A e X-47B** (silhuetas desenhadas a partir de fotos reais) → **9/9**
  variações de escala/posição reconhecidas.
- **B-2 Spirit** → **6/9** — as 3 falhas foram só em escala muito pequena
  (recorte ocupando pouco do quadro); em escala normal, passa.

Três silhuetas de teste prontas pra imprimir (X-45A, B-2, X-47B) estão salvas
em `~/Downloads/silhuetas_avioes_teste/` no computador da equipe.

**Importante:** o algoritmo (como o original) exige uma silhueta **escura e
sólida sobre fundo claro**, vista de cima, nariz pra cima — tipo um recorte
de papel/cartolina. Fotos de aviões em voo (céu/nuvens de fundo, cores
claras) não funcionam como alvo físico; isso foi testado e confirmado com 7
fotos reais, todas rejeitadas pelo mesmo motivo (a mancha de fundo, não o
avião, fica marcada como "escura").

## Módulos

| Arquivo | Responsabilidade |
|---|---|
| `missao_secundaria_cubedesign.ino` | `setup()`/`loop()`: inicializa câmera, SD, PSRAM e WiFi; orquestra captura → detecção → broadcast → log |
| `VisionSystem.h/.cpp` | Buffer de contorno herdado do VANTsat + `identifyAircraft()` (detecção de silhueta de aeronave) |
| `StorageHandler.h/.cpp` | Captura de frame, conversão para JPEG, pasta/contador de missão, gravação direta na pasta da sessão, log de ADS-B |
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
| Broadcast ADS-B | 4444 | UDP | Envia um JSON com os dados da aeronave sorteada sempre que um avião é detectado |
| Servidor de fotos | 8888 | TCP | Recebe `GET:<index>\n` e responde `START:<tipo>:<index>:<size>:<id>:<timestamp>\n` + bytes do JPEG + `\nEND_FRAME\n` |
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

Acesse `http://192.168.4.1/` (conectado no AP) pra ver:

1. **Último ADS-B enviado** — um painel no topo com o registro mais recente
   (callsign, ICAO24, lat/lon, altitude, velocidade, squawk), direto da
   memória — não depende de ler nenhum arquivo, atualiza a cada refresh.
2. **Galeria de fotos da missão atual** — todas as fotos já capturadas nesta
   sessão, com a telemetria de detecção (`data.txt`: tipo, índice, tamanho,
   timestamp) embaixo de cada uma.

O `adsb.txt` da pasta da missão continua sendo gravado no SD como histórico
(1 linha por detecção), só não é mais usado pra montar a página.

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
  └── missao_XXX/                 (pasta desta sessão, criada no boot)
        ├── <index>.jpg           (fotos capturadas nesta sessão, 0, 1, 2, ...)
        ├── data.txt              (telemetria: TIPO, CID, TID, Size, TS)
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
- **Precisa de silhueta escura sobre fundo claro:** fotos reais em voo (céu
  de fundo, avião claro) não funcionam como alvo — só recorte físico ou
  desenho de contraste alto, como as silhuetas em `~/Downloads/silhuetas_avioes_teste/`.
- **B-2 no limite:** dos três aviões testados, o B-2 é o que passou puxando
  três limites do classificador ao mesmo tempo (proporção, solidez, ângulo do
  nariz) — funciona, mas com menos margem que X-45A/X-47B. Vale testar o
  recorte físico com antecedência.

## Créditos

Baseado no firmware **VANTsat_TX_V3** — Equipe UERJsats, Missão Atlas (LASC 2026).
Desenvolvedores originais: Carlos Leal e Vitor Forny.

- https://github.com/uerjsats
- https://github.com/Caduleal
- https://github.com/vitorforny04
