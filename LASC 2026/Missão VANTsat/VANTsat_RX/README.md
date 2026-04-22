# VANTsat Receiver (Heltec V3)

Este projeto implementa a estação receptora de imagens para a missão **VANTsat**. O firmware foi desenvolvido para o hardware **Heltec WiFi LoRa 32 V3**, utilizando uma arquitetura modular que separa o controle de periféricos (OLED), a gestão da pilha de rede (WiFi/TCP) e a lógica do protocolo de aplicação.

## 🚀 Arquitetura do Software

O código segue o princípio de separação de interesses (*Separation of Concerns*), dividido nos seguintes módulos:

### 1. Camada de Aplicação (`VANTsat_RX.ino`)
Orquestrador principal. Inicializa os subsistemas e executa o loop de requisição de imagens. É agnóstico aos detalhes de implementação do rádio ou do display.

### 2. Gestão de Rede e Transporte (`NetworkManager.h/.cpp`)
Camada responsável por manter a conectividade persistente.
- **Resiliência de Rádio:** Implementa a reinicialização a frio do driver `esp_wifi` caso a conexão TCP ou a autenticação AP falhem.
- **Protocolo de Imagem:** Realiza o *handshake* binário (CMD `0x01`), lê o header de 8 bytes (Size + ID) e gerencia o buffer de recepção para fragmentos JPEG.
- **Buffer Estático:** Utiliza um buffer pré-alocado de 100KB (`fb_buf`) para evitar fragmentação de memória (Heap).

### 3. Interface de Hardware (`DisplayHandler.h/.cpp`)
Abstração do display OLED SSD1306 via barramento I2C.
- Gerencia o pino `Vext` para controle de energia do periférico.
- Encapsula as chamadas da biblioteca `Adafruit_GFX` para fornecer uma interface de status limpa.

## 🛠 Especificações Técnicas

- **Hardware:** Heltec V3 (ESP32-S3).
- **Transporte:** TCP Client (Porta 8888).
- **Display:** OLED 128x64 (SDA: 17, SCL: 18).
- **Timeout de Conexão:** 4 segundos para handshake; 6 segundos para stream de imagem.
- **Protocolo Binário:**
  - Request: `0x01` (1 byte).
  - Header: `uint32_t Size` | `uint32_t ID`.
  - Payload: Bytes brutos do JPEG (verificação de magic bytes `0xFF 0xD8`).

## 🔧 Configuração e Compilação

1. **Dependências:**
   - [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
   - [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
2. **Placa na IDE Arduino:** `Heltec WiFi LoRa 32(V3)`.
3. **Credenciais:** Ajuste as constantes `ssid` e `password` no arquivo `NetworkManager.cpp`.

## 📡 Lógica de Recuperação de Falhas

O sistema monitora a saúde do socket TCP. Caso ocorra um *timeout* ou desconexão inesperada, a função `resetRadio()` é disparada, realizando o seguinte fluxo:
1. `esp_wifi_stop()` -> Desliga o rádio.
2. `esp_wifi_deinit()` -> Libera recursos do driver.
3. Re-inicialização completa da pilha WiFi.
Isso evita o travamento da pilha de rede em ambientes com alta interferência de RF.