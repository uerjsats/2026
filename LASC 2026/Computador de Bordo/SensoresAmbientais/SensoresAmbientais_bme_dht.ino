#include <Wire.h>          // Comunicação I2C (usada pelo BME280)
#include <Adafruit_BME280.h> // Biblioteca do sensor BME280
#include <DHT.h>           // Biblioteca do sensor DHT22

#define DHTPIN  38         // Pino de dados do DHT22
#define DHTTYPE DHT22      // Tipo do sensor DHT

Adafruit_BME280 bme;       // Objeto do sensor BME280
DHT dht(DHTPIN, DHTTYPE);  // Objeto do sensor DHT22

float refPressure = 0;     // Pressão de referência para cálculo de altitude

// Agrupa todos os dados dos sensores em um único lugar
struct sensorsData {
    float temperatureDHT;  // Temperatura lida pelo DHT22 (°C)
    float humidityDHT;     // Umidade lida pelo DHT22 (%)
    float pressure;        // Pressão atmosférica lida pelo BME280 (hPa)
    float altitude;        // Altitude calculada pelo BME280 (m)
    float temperatureBME;  // Temperatura lida pelo BME280 (°C)
    bool dhtValid;         // true se a leitura do DHT22 foi válida
    bool bmeValid;         // true se a leitura do BME280 foi válida
};

void setup() {
    Serial.begin(115200);  // Inicia comunicação serial para debug
    Wire.begin(42, 41);    // Inicia I2C com SDA=42 e SCL=41 (padrão do Heltec LoRa WiFi V3)

    // Tenta inicializar o BME280 no endereço I2C 0x76
    // Não trava o sistema caso falhe — missão continua mesmo sem o BME
    if (!bme.begin(0x76)) {
        Serial.println("[ERRO] BME280 não encontrado!");
    } else {
        Serial.println("[OK] BME280 inicializado.");
        // Calibra a pressão de referência logo no início
        // Usada depois para calcular altitude relativa ao ponto de lançamento
        refPressure = bme.readPressure() / 100.0F;
        Serial.print("[OK] Pressão de referência: ");
        Serial.println(refPressure);
    }

    dht.begin();           // Inicializa o DHT22
    Serial.println("[OK] DHT22 inicializado.");
    delay(2000);           // Aguarda o DHT22 estabilizar antes da primeira leitura
}

void loop() {
    Serial.println("-------------------------------");
    sensorsData dados;     // Cria uma instância da struct para armazenar as leituras

    // --- Leitura do DHT22 ---
    dados.temperatureDHT = dht.readTemperature(); // Lê temperatura em °C
    dados.humidityDHT    = dht.readHumidity();    // Lê umidade em %

    // O ! inverte: !isnan() = "é um número válido"
    // && exige que temperatura E umidade sejam válidas ao mesmo tempo
    // dhtValid = true somente se ambas as leituras forem válidas
    dados.dhtValid = !isnan(dados.temperatureDHT) && !isnan(dados.humidityDHT);

    if (!dados.dhtValid) {
        Serial.println("[AVISO] Leitura DHT22 inválida.");
    } else {
        Serial.print("Temperatura DHT22: "); Serial.print(dados.temperatureDHT); Serial.println(" °C");
        Serial.print("Umidade DHT22:     "); Serial.print(dados.humidityDHT);    Serial.println(" %");
    }

    // --- Leitura do BME280 ---
    dados.pressure       = bme.readPressure() / 100.0F;   // Converte de Pa para hPa
    dados.temperatureBME = bme.readTemperature();          // Lê temperatura em °C
    dados.altitude       = bme.readAltitude(refPressure); // Calcula altitude relativa à pressão de referência
    // Verifica se os valores são válidos
    dados.bmeValid = !isnan(dados.pressure) && !isnan(dados.temperatureBME);

    if (!dados.bmeValid) {
        Serial.println("[AVISO] Leitura BME280 inválida.");
    } else {
        Serial.print("Temperatura BME280: "); Serial.print(dados.temperatureBME); Serial.println(" °C");
        Serial.print("Pressão:            "); Serial.print(dados.pressure);        Serial.println(" hPa");
        Serial.print("Altitude:           "); Serial.print(dados.altitude);        Serial.println(" m");
    }

    delay(5000); 
}