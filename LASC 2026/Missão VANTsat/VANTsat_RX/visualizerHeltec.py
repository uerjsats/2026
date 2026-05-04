import serial
import cv2
import numpy as np

def listen_heltec_stream(porta_com, baudrate=115200):
    try:
        ser = serial.Serial()
        ser.port = porta_com
        ser.baudrate = baudrate
        ser.timeout = 0.5  # Timeout curto para manter o loop responsivo a textos
        
        # Prevenção de Reset (DTR/RTS)
        ser.dtr = False
        ser.rts = False
        ser.open()
        ser.setDTR(False)
        ser.setRTS(False)
        
        ser.flushInput()
        print(f"[PYTHON] Escutando {porta_com}...")
    except Exception as e:
        print(f"Erro ao abrir porta: {e}")
        return

    while True:
        try:
            # 1. Leitura de Linha (Captura logs e headers)
            line_raw = ser.readline().decode('utf-8', errors='ignore').strip()
            
            if not line_raw:
                continue

            # Verificação de mensagens de controle e Log do MASTER
            if "[MASTER]" in line_raw or "START MISSION" in line_raw or "STARTING..." in line_raw:
                print(f"\n[ESP32 LOG] {line_raw}")
                if "STARTING..." in line_raw:
                    print("[SISTEMA] Aguardando reindexação do SD Card (2s)...")
                continue

            # 2. Sincronização e Leitura dos Metadados da Imagem
            if "START:" in line_raw:
                # O readline já capturou a linha: "START:index:size:totalIndex:missionTime"
                # Removemos o prefixo para fazer o split nos valores
                content = line_raw.replace("START:", "")
                parts = content.split(":")
                
                if len(parts) < 4:
                    continue

                # Mapeamento conforme seu Serial.printf("START:%d:%zu:%u:%.3f\n", ...)
                # Normalização do índice: se o hardware envia 1, tratamos como 0 para coerência
                raw_index    = int(parts[0])
                next_index   = raw_index - 1 if raw_index > 0 else 0
                
                img_size     = int(parts[1])
                total_index  = int(parts[2])
                mission_time = float(parts[3])

                # 3. Leitura Binária Estrita do Payload
                img_data = b""
                # Temporariamente removemos o timeout para garantir a leitura total do binário
                ser.timeout = 2 
                while len(img_data) < img_size:
                    chunk = ser.read(img_size - len(img_data))
                    if not chunk: 
                        print(f"[AVISO] Timeout na recepção do buffer. {len(img_data)}/{img_size}")
                        break
                    img_data += chunk
                ser.timeout = 0.5 # Retorna timeout para modo de escuta de logs

                # 4. Decodagem e Exibição
                if len(img_data) == img_size:
                    nparr = np.frombuffer(img_data, np.uint8)
                    img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

                    if img is not None:
                        print(f"[FRAME] RX: Index {next_index} | Total {total_index} | {img_size} bytes | {mission_time:.2f}s")
                        
                        label = f"ID: {next_index} | Total: {total_index} | {mission_time:.2f}s"
                        cv2.putText(img, label, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                        cv2.imshow("VANTsat Mission View", img)
                    else:
                        print(f"[ERRO] Falha no imdecode do frame {next_index}")

                # 5. Sincronização de Final de Frame
                ser.read_until(b"END_FRAME\n")

            if cv2.waitKey(1) & 0xFF == ord('q'):
                break

        except Exception as e:
            print(f"Exceção no loop: {e}")
            continue

    ser.close()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    listen_heltec_stream('COM29', 115200)