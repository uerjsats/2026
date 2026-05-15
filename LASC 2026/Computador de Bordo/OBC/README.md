# Sistema de Computador de Bordo - Drone/Satélite (Heltec WiFi LoRa 32 V3)

## Visão Geral

Este projeto implementa um computador de bordo embarcado para um sistema distribuído composto por drone e satélite, utilizando a placa Heltec WiFi LoRa 32 V3 (ESP32 + SX1262).

O sistema é responsável por:

* Aquisição de dados de sensores ambientais e inerciais
* Leitura de dados de posicionamento via GPS
* Processamento e organização de telemetria
* Transmissão de dados via LoRa (satélite → base)
* Comunicação via WiFi (drone → satélite)
* Envio de imagens fragmentadas
* Execução concorrente utilizando RTOS (FreeRTOS)
* Salvamento de dados referentes a missão

A arquitetura é orientada a tarefas, com separação clara entre aquisição, processamento e comunicação.

---

## Hardware Utilizado

* Heltec WiFi LoRa 32 V3 (ESP32 + SX1262)
* Sensor de temperatura e umidade DHT
* Sensor ambiental BME (pressão, temperatura, umidade)
* Sensor inercial MPU (aceleração e giroscópio)
* Módulo GPS
* Memória não volátil EEPROM

---

## Arquitetura do Sistema

O sistema é baseado em FreeRTOS, com múltiplas tarefas executando em paralelo nos dois núcleos do ESP32.

### Organização Geral

* Tasks independentes para:

  * Sensores
  * Telemetria
  * Comunicação LoRa
  * Comunicação WiFi
  * Debug/monitoramento
  * Gerenciamento de Memória

* Comunicação entre tasks via filas (Queue)

* Execução distribuída entre os dois núcleos do ESP32

* Separação entre drivers de hardware e lógica de aplicação

---

## Sensores

### DHT

* Medição de temperatura e umidade
* Utilizado para dados ambientais básicos

### BME

* Pressão atmosférica
* Altitude referente ao nível do mar

### MPU

* Aceleração (eixo X, Y, Z)
* Giroscópio
* Base para análise de movimento e estabilidade

### GPS

* Latitude e longitude
---

## Comunicação

### LoRa (SX1262)

Responsável pela comunicação de longa distância entre satélite e base.

**Funcionalidades:**

- Envio de telemetria
- Recepção de comandos
- Envio de imagens fragmentadas

**Parâmetros principais:**

- Frequência: **915 MHz**
- Bandwidth: **125 kHz**
- Spreading Factor: **7**
- Coding Rate: **5**

---

### WiFi

Responsável pela comunicação entre drone e satélite.

**Uso típico:**

- Recebimento de imagens do drone
- Transferência de dados em maior largura de banda

---

### Master ↔ Slave (UART)

Responsável pela comunicação interna entre a OBC (*On-Board Computer*) e os subsistemas embarcados do satélite via UART.

**Arquitetura:**

**Master:**
- OBC (ESP32 principal)

**Slaves:**
- Placa de Controle de Atitude
- Placa de Suprimento de Energia

**Funcionalidades:**

- Envio de comandos para subsistemas
- Recebimento de dados dos módulos
- Solicitação de informações específicas
- Comunicação de emergência

**Parâmetros de comunicação:**

- Interface: **UART**
- Baud Rate: **115200**
- Formato serial: **SERIAL_8N1**

**Estrutura do protocolo:**

Os slaves enviam respostas no seguinte formato:

```txt
<endereco>:<dados>
```

**Exemplo:**

```txt
3:gyro=12,pitch=5,roll=2
```

ou

```txt
1:battery=82,temp=34
```

Onde:

| Endereço | Sistema |
|----------|----------|
| `1` | Suprimento de energia |
| `3` | Controle de atitude |

**Comandos suportados:**

| Código | Destino | Função |
|--------|----------|---------|
| `1` | Controle de atitude | Abrir mecanismo do drone |
| `2` | Controle de atitude | Fechar mecanismo do drone |
| `3` | Controle de atitude | Emergência |
| `4` | Suprimento de energia | Solicitar informações da bateria |
| `5` | Suprimento de energia | Emergência |

---

## Armazenamento de Dados

### EEPROM (Memória Não Volátil)

Responsável pelo armazenamento persistente de telemetria crítica durante a missão.

A EEPROM atua como um sistema de **backup de voo**, garantindo retenção dos dados mesmo em situações de falha energética, perda de comunicação ou desligamento inesperado do sistema.

O armazenamento é realizado de forma seletiva, priorizando momentos relevantes da missão e reduzindo desgaste desnecessário da memória.

**Funcionalidades:**

- Armazenamento persistente de telemetria
- Backup de dados críticos da missão
- Registro de sensores durante voo
- Recuperação posterior dos dados

**Tecnologia utilizada:**

- EEPROM externa via **I2C**
- Escrita e leitura paginada
- Endereçamento sequencial de memória
- Persistência não volátil

**Critérios de salvamento**

Os dados são armazenados somente quando todas as condições abaixo são satisfeitas:

| Condição | Descrição |
|----------|------------|
| Intervalo mínimo | Pelo menos **2000 ms** desde a última gravação |
| Altitude mínima | Altitude superior a **5000** |
| Memória disponível | EEPROM ainda possui espaço livre |
| Rádio livre | Sistema LoRa encontra-se em estado **idle** |

Essa estratégia reduz escrita excessiva na memória e evita interferência com transmissões críticas de telemetria.

**Fluxo de armazenamento:**

1. A task de EEPROM aguarda novos dados via Queue.
2. Um pacote de sensores é recebido.
3. O sistema verifica os critérios de salvamento.
4. Os dados são serializados e gravados na EEPROM.
5. O endereço de memória é atualizado automaticamente.

**Estratégia de armazenamento**

Os dados do sistema são gravados diretamente a partir da estrutura de telemetria (`sensorsData`) em formato binário, reduzindo overhead de processamento e espaço ocupado.

Exemplo conceitual:

```cpp
eepromWriteBytes(
    currentAddress,
    (uint8_t*)&dados,
    sizeof(sensorsData)
);
```

Após cada gravação, o endereço é incrementado automaticamente:

```cpp
currentAddress += sizeof(sensorsData);
```

permitindo armazenamento sequencial de toda a telemetria da missão.

**Organização da memória**

| Região | Função |
|--------|---------|
| `0 - 9` | Reservada |
| `10+` | Dados de telemetria |

O armazenamento inicia no endereço:

```cpp
currentAddress = 10;
```

**Integração com FreeRTOS**

O gerenciamento da memória é executado em uma task dedicada:

```txt
taskEEPROM
```

Características:

- Execução independente
- Operação assíncrona
- Comunicação via Queue
- Não bloqueia o restante do sistema

**Proteções implementadas:**

- Controle de frequência de escrita
- Prevenção de overflow da memória
- Bloqueio de escrita durante transmissão LoRa
- Verificação de disponibilidade de armazenamento

**Mensagens de debug:**

Durante operação, o sistema pode reportar:

```txt
EEPROM SALVA
```

ou

```txt
EEPROM LOTADA
```

quando o limite físico de armazenamento é atingido.

---

**Fluxo de comunicação:**

1. Um comando é recebido via LoRa.
2. O callback de recepção interpreta o primeiro byte do pacote.
3. O comando é encaminhado ao slave correspondente.
4. O slave processa a solicitação e envia uma resposta.
5. A OBC interpreta os dados recebidos e os incorpora à telemetria.

**Características da implementação:**

- Comunicação assíncrona
- Buffers fixos de **64 bytes**
- Parsing por delimitador `:`
- Atualização controlada por flags de estado
- Leitura serial não bloqueante
- Integração direta com o sistema de telemetria LoRa

---

## Protocolo de Comunicação

Todos os dados trafegam em pacotes estruturados.

### Estrutura Base

| Byte | Descrição         |
| ---- | ----------------- |
| 0    | Start byte (0x7E) |
| 1    | Tipo              |
| 2    | Origem            |
| 3    | Destino           |
| 4+   | Payload           |

### Tipos de Pacote

* Sensores
* GPS
* Dados inerciais (MPU)
* Imagem

---

## Envio de Imagens

Devido à limitação de payload do LoRa, imagens são fragmentadas.

### Funcionamento

* A imagem é dividida em blocos de até 180 bytes
* Cada bloco é enviado separadamente
* O receptor deve reconstruir a imagem com base nos índices

### Estrutura do Pacote de Imagem

| Byte | Descrição            |
| ---- | -------------------- |
| 0    | Start byte           |
| 1    | Tipo (imagem)        |
| 2    | Origem               |
| 3    | Destino              |
| 4    | Índice do fragmento  |
| 5    | Total de fragmentos  |
| 6    | Tamanho do fragmento |
| 7+   | Dados                |

---

## Fluxo do Sistema

1. Sensores coletam dados periodicamente
2. Dados são enviados para filas internas
3. Task de telemetria organiza os dados
4. Dados são enviados via LoRa para a base
5. Imagens recebidas via WiFi são fragmentadas
6. Fragmentos são enviados via LoRa

---

## Controle de Execução

* Uso de temporização baseada em `millis()`
* Controle de intervalo de transmissão
* Estado de rádio (idle / transmitindo)
* Processamento de recepção baseado em interrupção

---

## Estrutura do Código

Organização modular:

* `libs/`:

  * Implementação dos sensores (DHT, BME, MPU, GPS)
  * Implementação de Ccomunicação via LoRa w WiFi

* `tasks/`:

  * Tasks do FreeRTOS


* `main`:

  * Inicialização do sistema e criação das tasks

---

## Integração com RTOS

* Cada subsistema roda em uma task independente
* Comunicação entre tasks via Queue
* Possibilidade de uso de mutex para barramentos compartilhados (I2C), para evitar conflito entre tasks

---

## Limitações Atuais

* Sem retransmissão de pacotes
* Sem verificação de integridade (CRC)
* Reconstrução de imagem depende de implementação externa

---

## Aplicação

Projeto desenvolvido pela equipe de computador de bordo da UERJ SATS, para a competição LASC
---