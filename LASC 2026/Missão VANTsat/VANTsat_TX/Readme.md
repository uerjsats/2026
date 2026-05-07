# VANTsat Flight Controller & Vision System (ESP32-CAM)

Este projeto implementa o núcleo de processamento de imagem, telemetria e gestão de armazenamento para a missão **VANTsat**. O firmware foi desenvolvido para o hardware **ESP32-CAM**, utilizando uma arquitetura modular otimizada para processamento digital de sinais em tempo real e persistência de dados em sistemas de arquivos embarcados.

## 🚀 Arquitetura do Software

O código segue o princípio de separação de interesses (*Separation of Concerns*), estruturado nos seguintes módulos técnicos:

### 1. Sistema de Visão Computacional (`VisionSystem.h/.cpp`)
O core analítico do projeto. Realiza a extração de primitivas geométricas diretamente do framebuffer da câmera.
- **Segmentação e Seguimento:** Implementa o algoritmo **Moore-Neighbor** para rastreio de contornos em buffers Grayscale com limiar adaptativo de brilho.
- **Simplificação Poligonal:** Utiliza o algoritmo **Ramer-Douglas-Peucker (RDP)** para redução da dimensionalidade do contorno, transformando nuvens de pontos em polígonos de poucos vértices.
- **Geometria Analítica:** Identifica formas (triângulos e quadrados) através do **Produto Escalar** de vetores nos vértices da base, garantindo robustez contra distorções de perspectiva.
- **Gestão de Memória:** Aloca buffers críticos na **PSRAM** para evitar transbordamento da memória interna (SRAM).

### 2. Gestão de Armazenamento e Logs (`StorageHandler.h/.cpp`)
Camada de persistência e serialização de dados no cartão SD via barramento **SD_MMC**.
- **Buffer Circular:** Implementa uma lógica de armazenamento cíclico limitado a 10 imagens JPEG para otimização de espaço.
- **Log de Missão:** Registra metadados cruciais (Tipo de forma, ID, Timestamp e Tamanho) no arquivo `data.txt` a cada captura.
- **Stream de Dados:** Provê funções de streaming binário para transmissão de arquivos via protocolos UART e TCP.

### 3. Servidor de Rede (`NetworkManager.h/.cpp`)
Módulo responsável pela extração de dados sem fio.
- **Modo Access Point:** Configura o ESP32 como servidor (`VANTsat_AP`) com IP estático (`192.168.4.1`).
- **Transporte TCP:** Mantém um servidor na porta `8888` para entrega de imagens e telemetria sob demanda via comandos `GET`.
- **Protocolo de Cabeçalho:** Insere metadados de missão no início do stream binário para sincronização com o cliente.

### 4. Ponte de Comando Serial (`SerialBridge.h/.cpp`)
Interface UART para comando e controle (C2).
- **Interface Humano-Máquina:** Traduz caracteres únicos ('C' para capturar, 'R' para reset) em chamadas de sistema.
- **Download Serial:** Permite a extração física das fotos do cartão SD através da porta serial para estações de solo sem Wi-Fi.

## 🛠 Especificações Técnicas

- **Hardware:** ESP32-CAM (AI-Thinker).
- **Sensor de Imagem:** OV2640 em resolução QVGA (320x240).
- **Formato de Arquivo:** JPEG (Compressão em hardware/software via `fmt2jpg`).
- **Armazenamento:** SD_MMC (Modo 1-bit/4-bit).
- **Protocolo de Comunicação:**
  - **Captura:** Comando Serial `C`.
  - **Header UART:** `START_STORAGE:[index]:[size]:[metadata]`.
  - **Header TCP:** `START:[tipo]:[id]:[size]:[total]:[time]`.

## 🔧 Configuração e Compilação

1. **Placa na IDE Arduino:** `AI Thinker ESP32-CAM`.
2. **Configuração de Flash:** `CPU Frequency: 240MHz`, `Flash Mode: QIO`, `Partition Scheme: Huge APP`.
3. **Dependências:**
   - Driver nativo `esp_camera`.
   - Bibliotecas `WiFi` e `SD_MMC` do core ESP32.

## 📡 Fluxo de Processamento de Missão

Ao receber o comando de captura, o firmware executa o seguinte pipeline atômico:
1. **Aquisição:** Captura do frame Grayscale.
2. **Análise:** Detecção de bordas e classificação geométrica.
3. **Compressão:** Conversão dinâmica para JPEG.
4. **Escrita:** Gravação do binário + atualização do Log no SD.
5. **Feedback:** Retorno de telemetria via Serial/TCP.