#include "src/missao/espNow.h"

//Máquina de Estados
enum Estado
{
  AGUARDANDO,
  PROCESSANDO,
  ENVIANDO
};

String input = "";
Estado estado = AGUARDANDO;

void setup() {
  nowInit();
  Serial.begin(115200);
  Serial.println(WiFi.macAddress());
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
}

void loop() {

  switch(estado) {
    case AGUARDANDO:
      if(Serial.available() > 0) {
        estado = PROCESSANDO;
      }
      break;

    case PROCESSANDO:
      input = Serial.readStringUntil('\n');
      input.trim();
      estado = ENVIANDO;
      break;

    case ENVIANDO:
      sendNow("9C:13:9E:E7:06:74", "Drone", "-22.89886" ,"-43.21852"); 
      
      estado = AGUARDANDO;
      break;
  }
}
