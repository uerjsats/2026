# Missão Secundária_V2 — CubeDesign 2026 (`deteccao-aviao`)

Firmware para ESP32-S3 (XIAO Sense) que detecta **silhuetas de aeronaves**
(UCAVs tipo asa delta) por visão computacional, salva a foto no cartão SD,
simula um registro ADS-B associado à detecção e expõe tudo isso (foto +
telemetria + ADS-B) num painel web servido pelo próprio ESP32.

---

## Sumário

- [Como funciona](#como-funciona)
- [O algoritmo de detecção de avião](#o-algoritmo-de-detecção-de-avião)
- [Depuração pela Serial: motivo da rejeição](#depuração-pela-serial-motivo-da-rejeição)
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
identifyAircraft() ──► Otsu + maior mancha + eixo de simetria + perfil x 7 modelos
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

Cada detecção já nasce salva na pasta definitiva da sessão.

## O algoritmo de detecção de avião

`identifyAircraft()` (em `VisionSystem.cpp`) reconhece **vários tipos de aeronave
vistas de cima, em qualquer rotação**, comparando a silhueta com 7 modelos e com
todas as misturas entre eles:

| Etapa | O que faz |
|---|---|
| Limiar de Otsu | Separa "tinta" de "papel" adaptando-se ao contraste real do quadro |
| Duas polaridades | Testa primeiro silhueta **escura** em fundo claro; se não passar, inverte as cores e testa silhueta **clara** em fundo escuro (avião branco). Se qualquer uma passar, é `AVIAO` |
| Maior mancha interior | Escolhe o maior blob escuro que não encosta na borda do quadro (8-conectado) |
| Eixo de simetria | Testa ângulos de 0° a 180° e escolhe o eixo que deixa os dois lados mais iguais (todo avião visto de cima é espelhado no eixo da fuselagem) |
| Perfil de largura | Mede a largura da silhueta em 24 fatias ao longo do eixo, nos dois sentidos (não se sabe de antemão qual ponta é o nariz) |
| Comparação com modelos | Compara o perfil (e a razão envergadura/comprimento) com os 7 modelos e com **todas as misturas de dois modelos** (ex.: 70% caça + 30% delta) |

Modelos (`AircraftTemplates.h`): **Comercial** (jato de passageiros), **Caça**,
**Delta** (Mirage), **Asa voadora** (B-2), **UCAV** (X-47B), **Hélice** (Cessna)
e **Triângulo**. As misturas cobrem os intermediários, ex.: um caça de asa mais
enflechada fica entre Caça e Delta.

Classifica como `AVIAO` quando **todos** estes critérios batem:

| Critério | Limite | O que verifica |
|---|---|---|
| Contraste | ≥ 40 (`AC_CONTRAST_MIN`) | Diferença entre as médias de papel e tinta |
| Tamanho | 2% – 60% do quadro (`AC_AREA_MIN_FRAC`/`AC_AREA_MAX_FRAC`), lado maior ≥ 30 px (`AC_SIZE_MIN`) | Alvo nem longe nem perto demais |
| Mancha isolada | não encosta na borda do quadro | O avião está inteiro no quadro |
| Assimetria | ≤ 0.060 (`AC_ASYM_MAX`) | Os dois lados do eixo são espelhados |
| Distância ao modelo | ≤ 0.130 (`AC_DIST_MAX`) | O perfil bate com algum modelo ou mistura |

Os limites ficam no topo de `VisionSystem.cpp` (`AC_ASYM_MAX`, `AC_DIST_MAX`...).
A distância ao modelo combina a diferença do perfil de largura (erro RMS) com a
diferença de envergadura/comprimento, esta com peso `AC_ASPECT_WEIGHT` (0.15).

### Validação

Resultados reportados pelo autor do detector, em simulação no PC com o mesmo
código C++ (imagens sintéticas com rotação aleatória de 0 a 360°, tamanhos
variados, desfoque, ruído e vinheta):

| Grupo | Resultado |
|---|---|
| Os 7 modelos e variações deles (envergadura ±27%, vértices deslocados) | Nenhum recusado pela forma. Distância máxima 0.092 (limite 0.130) |
| Não-aviões: círculo, elipses, quadrado, retângulos, estrelas, cruz, letras T/L/H, coração, mão, hexágono, casinha, seta, manchas irregulares | 0/330 aceitos. O mais perto foi a cruz (+), com distância 0.147 |
| Folha `alvos_7_tipos.pdf` fotografada sobre mesa escura, 5 distâncias/rotações por página | 35/35 reconhecidos, cada um com o modelo certo |

Limites conhecidos do método:
- Tipos fora dos modelos: jumbo e "diamante" (tipo F-22) quase sempre passam; Concorde, caça canard e bombardeiro às vezes; A-10 e planador não (asa reta muito diferente do Hélice).
- Uma mancha borrada e simétrica pode ser aceita por acaso (em teste com ruído suavizado, uma passou com distância 0.123 contra o limite 0.130). Distância perto do limite merece desconfiança.
- Só vista de cima, e com contraste entre avião e fundo (escuro sobre claro ou claro sobre escuro).

## Depuração pela Serial: motivo da rejeição

A cada quadro analisado (a cada 500 ms) o `StorageHandler.cpp` imprime uma linha
no Serial Monitor (115200 baud), desde que `VISION_SERIAL_DEBUG` seja `1` em
`VisionSystem.h` (mude para `0` para silenciar).

Quadro aceito:
```
[VISAO #43] AVIAO detectado (silhueta escura): Comercial | dist 0.118, eixo 273 graus | 87 ms
```

Quadro rejeitado:
```
[VISAO #42] NENHUM | escura: motivo 7: forma nao parece com nenhum modelo de aviao (distancia 0.176, maximo 0.130; mais perto: Caca) | invertida: motivo 7 | 87 ms
```

`escura` é a tentativa de silhueta escura em fundo claro; `invertida` é a segunda
tentativa, com as cores trocadas (avião claro em fundo escuro). O quadro só é
`AVIAO` se uma das duas passar.

| Motivo | Mensagem | Limite (em `VisionSystem.cpp`) | O que fazer |
|---|---|---|---|
| 1 | erro interno (PSRAM/buffer ou quadro inválido) | — | Conferir se a PSRAM foi inicializada (`[OK] PSRAM Inicializada` no boot) |
| 2 | pouco contraste entre silhueta e fundo | contraste ≥ `AC_CONTRAST_MIN` (40) | Mais luz, mais ganho/exposição da câmera, papel branco e impressão bem preta |
| 3 | nenhuma mancha escura isolada | mancha não pode encostar na borda do quadro | Afastar a câmera e centralizar; tirar objetos escuros do fundo |
| 4 | mancha muito pequena ou muito grande | área de 2% a 60% do quadro (`AC_AREA_MIN_FRAC`/`AC_AREA_MAX_FRAC`); lado maior ≥ 30 px (`AC_SIZE_MIN`) | Ajustar a distância (mostra a área e a caixa em px) |
| 6 | silhueta não é simétrica | assimetria ≤ `AC_ASYM_MAX` (0.060) | Câmera paralela à folha; impressão sem deformação |
| 7 | forma não parece com nenhum modelo de avião | distância ≤ `AC_DIST_MAX` (0.130) | Ver a distância e o "mais perto" na linha; forma muito diferente dos 7 modelos |

Não existe motivo 5. Se a linha mostrar `AVIAO`, ela traz o modelo (ou a mistura
de dois, ex.: `Caca 70% + Delta 30%`), a distância ao modelo e o ângulo do eixo.
Uma distância próxima do limite (ex.: 0.12 contra 0.13) significa aceitação
"na raça": vale desconfiar de falso positivo.

## O que é herdado do VANTsat_TX_V3 

- **`initVisionBuffers()`** — aloca o buffer `contour[]` na PSRAM.
- **`perpendicularDistance()`** — mantida sem alteração, mas o detector atual
  não a usa mais.

## Módulos

| Arquivo | Responsabilidade |
|---|---|
| `missao_secundaria_cubedesign.ino` | `setup()`/`loop()`: inicializa câmera, SD, PSRAM e WiFi; orquestra captura → detecção → broadcast → log |
| `VisionSystem.h/.cpp` | Buffer de contorno herdado do VANTsat + `identifyAircraft()` (detecção de silhueta de aeronave) + linha de depuração da Serial |
| `AircraftTemplates.h` | Perfis de largura dos 7 modelos de aeronave |
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

O payload leva **só latitude e longitude** do ADS-B simulado (mais o índice/tamanho da foto). O OBC usa lat/lon para perguntar ao Orange Pi se a aeronave consta no banco de autorizadas. O registro completo (ICAO, callsign, altitude...) continua só no painel web e no `adsb.txt` do SD.

```json
{
  "lat": -22.9068,
  "lon": -43.1729,
  "photo_index": 3,
  "photo_size": 18234
}
```

## Painel web

Conectado no Wi-Fi do ESP (AP `AMARAL_I`), acesse
`http://www.missao.maverick.com` (use `http://` e o `www.`; a rede não tem
internet, então o navegador não deve trocar para `https://`). Se o nome não
abrir, use o IP direto: `http://192.168.4.1/`. Lá você vê:

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
- **Precisa de contraste entre avião e fundo:** escuro sobre claro ou claro
  sobre escuro (as duas polaridades são testadas). Fundo com muita textura ou
  avião da mesma cor do fundo não funciona.
- **Só vista de cima:** o detector compara a silhueta em planta. Avião de lado
  ou de frente não bate com os modelos.
- **Asa reta muito diferente do Cessna** (A-10, planador) não passa.
- **Falso positivo por acaso:** uma mancha borrada e simétrica pode ser aceita.
  Confira a distância na linha `[VISAO]` da Serial (ver seção de depuração).

## Créditos

Baseado no firmware **VANTsat_TX_V3** — Equipe UERJsats, Missão Atlas (LASC 2026).
Desenvolvedores originais: Carlos Leal e Vitor Forny.

Detector de aeronaves (`VisionSystem.cpp` e `AircraftTemplates.h`): **Asaf Teixeira**.
O reconhecimento por eixo de simetria e perfil de largura, a comparação com os
7 modelos (Comercial, Caça, Delta, Asa voadora, UCAV, Hélice e Triângulo) e as
misturas entre eles, o teste das duas polaridades (silhueta escura e clara) e as
mensagens de depuração pela Serial foram desenvolvidos por ele na versão para a
ESP32-CAM AI-Thinker (que também inclui o painel `/live` e a folha de alvos
`alvos_7_tipos.pdf`). Aqui o detector foi adaptado para a XIAO ESP32S3 Sense,
sem o painel `/live`.

Verificação de aeronave clandestina ou não (consulta `QRY` à tabela `autorizadas`
no Orange Pi, usando a latitude/longitude do ADS-B enviado por esta missão): **Kataryne Cunha**.

- https://github.com/uerjsats
- https://github.com/Caduleal
- https://github.com/vitorforny04
- https://github.com/asafTeixeira
- https://github.com/katarynecunha28
