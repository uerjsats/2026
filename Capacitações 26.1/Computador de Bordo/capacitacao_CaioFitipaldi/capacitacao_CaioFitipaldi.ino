#include <DHT.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

//Pinout
#define DHTPIN 4
#define I2CSDA 41
#define I2CSCL 42
#define DHTTYPE DHT22 //Define o modelo DHT22
DHT dht(DHTPIN, DHTTYPE);

void setup() 
{
  Serial.begin(115200);
  dht.begin(); //Inicializa DHT
  // Inicializa I2C nos pinos corretos
  Wire.begin(I2CSDA, I2CSCL);
  // Inicializa MPU6050
  if (!mpu.begin()) 
  {
    //Serial.println("Falha ao encontrar MPU6050!");
  }
  
}

void loop() 
{
  //DHT sensors
  float umidade = dht.readHumidity();
  float temperatura = dht.readTemperature();
  //MPU Sensors
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  float X = a.acceleration.x;
  float Y = a.acceleration.y;
  float Z = a.acceleration.z;

  //Impressão de Valores
  Serial.print("Dados DHT: ");
  Serial.print("Umidade: ");
  Serial.print(umidade);
  Serial.print(", Temperatura: ");
  Serial.print(temperatura);
  Serial.print(", Eixo x: ");
  Serial.print(X);
  Serial.print(", Eixo Y: ");
  Serial.print(Y);
  Serial.print(", Eixo Z: ");
  Serial.println(Z);
  
  delay(1500);
}