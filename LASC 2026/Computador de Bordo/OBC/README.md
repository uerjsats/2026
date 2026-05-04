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

A arquitetura é orientada a tarefas, com separação clara entre aquisição, processamento e comunicação.

---

## Hardware Utilizado

* Heltec WiFi LoRa 32 V3 (ESP32 + SX1262)
* Sensor de temperatura e umidade DHT
* Sensor ambiental BME (pressão, temperatura, umidade)
* Sensor inercial MPU (aceleração e giroscópio)
* Módulo GPS

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

Funcionalidades:

* Envio de telemetria
* Recepção de comandos
* Envio de imagens fragmentadas

Parâmetros principais:

* Frequência: 915 MHz
* Bandwidth: 125 kHz
* Spreading Factor: 7
* Coding Rate: 5

---

### WiFi

Responsável pela comunicação entre drone e satélite.

Uso típico:

* Recebimento de imagens do drone
* Transferência de dados em maior largura de banda

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

* Ausência de confirmação de recebimento (ACK)
* Sem retransmissão de pacotes
* Sem verificação de integridade (CRC)
* Reconstrução de imagem depende de implementação externa
* Falta a respeito de um gerenciamento sobre a EEPROM
* Falta de Comunicação Serial (Mestre - Escravo)

---

## Aplicação

Projeto desenvolvido pela equipe de computador de bordo da UERJ SATS, para a competição LASC
---