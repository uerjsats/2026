#include "src/sensores/gps.h"
#include "src/sensores/mpu.h"
#include "src/telemetria/telemetria.h"

void setup() {
  Serial.begin(115200);
  Serial.println("Computador de Bordo Iniciado!!!");
  gpsInit();
  mpuInit();

}

void loop() {
  
  gpsUpdate();
  mpuUpdate();
  printTelemetria();
  delay(500);
}
