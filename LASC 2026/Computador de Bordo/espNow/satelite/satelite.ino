#include "src/missao/espNow.h"

bool houveRecebimento = false;
String ultimaMensagemBruta = "";

//Máquina de Estados
enum Estado { AGUARDANDO, PROCESSANDO, ENVIANDO };
Estado estado = AGUARDANDO;
String input = "";

void setup() {
  Serial.begin(115200);
  nowInit();        //Inicializa ESP-NOW
  //Obtém endereço MAC do esp
  Serial.print("OBC MAC: ");
  Serial.println(WiFi.macAddress());

  //Callbacks ESP-NOW (Híbrido)
  esp_now_register_send_cb((esp_now_send_cb_t)OnDataSent);
  esp_now_register_recv_cb((esp_now_recv_cb_t)OnDataRecv);
  
}

void loop() {

  if (houveRecebimento) {
    Serial.println("[ESP-NOW] Resposta recebida:");
    Serial.println(ultimaMensagemBruta);
    houveRecebimento = false;
  }
  switch(estado) {
    case AGUARDANDO:
      if(Serial.available() > 0) estado = PROCESSANDO;
      break;

    case PROCESSANDO:
      input = Serial.readStringUntil('\n');
      input.trim();
      Serial.print("[Serial Local] Input recebido: ");
      Serial.println(input);
      estado = ENVIANDO;
      break;

    case ENVIANDO:
      Serial.print("[ESP-NOW] OBC -> Drone: Enviando comando [");
      Serial.print(input);
      Serial.println("]...");
      
      //Envio (Endereço MAC do esp de destino)
      sendNow("E4:B3:23:C2:01:D0", "OBC", input, ""); 
      
      estado = AGUARDANDO;
      break;
  }
}
