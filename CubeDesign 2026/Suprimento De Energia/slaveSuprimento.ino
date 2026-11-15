#include <Arduino.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_INA219.h>

SoftwareSerial suprimentoSerial(8, 9);   // RX=8 (<- TX do Heltec), TX=9 (-> RX do Heltec)

#define DS18B20_1_PIN 2
#define DS18B20_2_PIN 3
#define DS18B20_3_PIN 4

OneWire oneWire1(DS18B20_1_PIN);
DallasTemperature sensor1(&oneWire1);
OneWire oneWire2(DS18B20_2_PIN);
DallasTemperature sensor2(&oneWire2);
OneWire oneWire3(DS18B20_3_PIN);
DallasTemperature sensor3(&oneWire3);

Adafruit_INA219 ina219;

#define mosfet 6
#define ENDERECO_SUPRIMENTO 1
#define SERIAL_BAUDRATE 115200
#define INTERVALO_LEITURA_MS 2000UL

// Ultima leitura valida (enviada quando o OBC pede)
float temp1 = 0, temp3 = 0, busVoltage = 0, correnteA = 0;
unsigned long ultimaLeitura = 0;

void lerSensores()
{
  // Le o resultado da conversao pedida no ciclo anterior
  temp1 = sensor1.getTempCByIndex(0);
  temp3 = sensor3.getTempCByIndex(0);
  busVoltage = ina219.getBusVoltage_V();
  correnteA  = ina219.getCurrent_mA() / 1000.0;

  // Ja pede a proxima conversao (nao bloqueia)
  sensor1.requestTemperatures();
  sensor3.requestTemperatures();

  bool t1_valida = (temp1 > -100.0 && temp1 < 125.0);
  bool t3_valida = (temp3 > -100.0 && temp3 < 125.0);
  bool abaixo_25 = (t1_valida && temp1 < 25.0) || (t3_valida && temp3 < 25.0);
  bool acima_40  = (t1_valida && temp1 >= 40.0) || (t3_valida && temp3 >= 40.0);

  if (abaixo_25) {
    digitalWrite(mosfet, HIGH);
    Serial.println("MOSFET: LIGADO");
  } else if (acima_40) {
    digitalWrite(mosfet, LOW);
    Serial.println("MOSFET: DESLIGADO");
  }
}

String montarMensagem()
{
  // Formato esperado pelo solo: 1:tempBat1:tempBat2:tensao(V):corrente(A)
  String msg = String(ENDERECO_SUPRIMENTO) + ":" +
               String(temp1, 2) + ":" +
               String(temp3, 2) + ":" +
               String(busVoltage, 2) + ":" +
               String(correnteA, 3);
  return msg;
}

void setup()
{
  Serial.begin(SERIAL_BAUDRATE);
  suprimentoSerial.begin(9600);
  suprimentoSerial.setTimeout(50);
  Wire.begin();

  sensor1.begin();
  sensor2.begin();
  sensor3.begin();
  sensor1.setWaitForConversion(false);
  sensor3.setWaitForConversion(false);
  sensor1.requestTemperatures();
  sensor3.requestTemperatures();

  pinMode(mosfet, OUTPUT);
  digitalWrite(mosfet, LOW);

  if (ina219.begin()) Serial.println("INA219 Encontrado!!!");
  else                Serial.println("INA219 NAO encontrado");
  ina219.setCalibration_32V_2A();

  delay(800);   // primeira conversao dos DS18B20
  lerSensores();
  ultimaLeitura = millis();
  

  Serial.println("Suprimento pronto");
}

void loop()
{
  // Leitura periodica sem delay
  if (millis() - ultimaLeitura >= INTERVALO_LEITURA_MS) {
    lerSensores();                      // le sensores e atualiza o aquecedor a cada 2 s
    ultimaLeitura = millis();
    Serial.println(montarMensagem());   // debug USB
  }

  // Responde o pedido do OBC na hora
  if (suprimentoSerial.available()) {
    String comando = suprimentoSerial.readStringUntil('\n');
    comando.trim();
    Serial.print("RX: "); Serial.println(comando);

    if (comando == "REQ_INA") {
      String msg = montarMensagem();
      suprimentoSerial.println(msg);
      //suprimentoSerial.println("1:20:20:20:20");
      Serial.print("TX: "); Serial.println(msg);
    }
  }
}