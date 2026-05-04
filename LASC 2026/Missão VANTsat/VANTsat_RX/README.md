# VANTsat Receiver (Heltec V3)

Este projeto implementa a estação receptora de imagens para a missão **VANTsat**. O firmware foi desenvolvido para o hardware **Heltec WiFi LoRa 32 V3**, utilizando uma arquitetura modular que separa o controle de periféricos (OLED), a gestão da pilha de rede (WiFi/TCP) e a lógica do protocolo de aplicação para ponte serial.

## 🚀 Arquitetura do Software

O código segue o princípio de separação de interesses (*Separation of Concerns*), dividido nos seguintes módulos:

### 1. Camada de Aplicação (`VANTsat_RX.ino`)
Orquestrador principal. Inicializa os subsistemas e executa o loop de requisição de imagens. Atua como o nó central de telemetria, recebendo dados via TCP e despachando-os via Serial para processamento externo.

### 2. Gestão de Rede e Transporte (`NetworkManager.h/.cpp`)
Camada responsável pela conectividade e integridade dos dados.
- **Resiliência de Rádio:** Implementa a reinicialização a frio do driver `esp_wifi` caso a conexão falhe.
- **Protocolo de Imagem (Atualizado):** Implementa um fluxo baseado em requisição `GET:index`. Realiza o parsing de headers estruturados (`START:ID:SIZE:TOTAL:TIME`).
- **Sincronização de Missão:** Ao estabelecer a primeira conexão bem-sucedida, o sistema envia o comando `R` ao servidor para resetar o armazenamento (SD) e iniciar a missão do zero.
- **Buffer Estático:** Utiliza um buffer pré-alocado de 100KB (`fb_buf`) para evitar fragmentação de memória (Heap).

### 3. Ponte de Dados (`SerialBridge.h/.cpp`)
Módulo responsável pela interface com a estação de monitoramento em Python.
- Encapsula os dados recebidos (Header + Payload JPEG) e os transmite via Serial.
- Permite que scripts Python em terra processem, salvem e exibam as imagens e telemetria em tempo real.

### 4. Interface de Hardware (`DisplayHandler.h/.cpp`)
Abstração do display OLED SSD1306 via barramento I2C.
- Gerencia o pino `Vext` para controle de energia.
- Exibe o status da conexão e metadados da imagem atual (ID, Total e Tempo de Missão).

## 🛠 Especificações Técnicas

- **Hardware:** Heltec V3 (ESP32-S3).
- **Transporte:** TCP Client (Porta 8888).
- **Interface de Saída:** Serial (USB-CDC) para processamento em Python.
- **Timeouts:** 5 segundos para handshake/header; 10 segundos para stream do payload.
- **Protocolo de Aplicação:**
  - **Handshake Inicial:** Comando `R` enviado na primeira conexão.
  - **Request:** String formatada `GET:[index]\n`.
  - **Header de Resposta:** String `START:[ID]:[Size]:[TotalIndex]:[MissionTime]\n`.
  - **Payload:** Bytes brutos do JPEG (verificação de magic bytes `0xFF 0xD8`).

## 🔧 Configuração e Compilação

1. **Dependências:**
   - [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)
   - [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library)
2. **Placa na IDE Arduino:** `Heltec WiFi LoRa 32(V3)`.
3. **Credenciais:** Ajuste as constantes `ssid` e `password` no arquivo `NetworkManager.cpp`.

## 📡 Lógica de Recuperação de Falhas

O sistema monitora a saúde do socket através da função `monitorConnection()`. Caso ocorra um *timeout* ou erro de integridade no download:
1. O socket é encerrado (`client.stop()`).
2. O flag `conectedOnce` é setado como `false`, garantindo que uma nova tentativa de conexão siga o fluxo completo de inicialização.
3. O rádio passa pelo ciclo de reset forçado (`esp_wifi_stop` / `esp_wifi_deinit`) para limpar falhas críticas da pilha de rede.