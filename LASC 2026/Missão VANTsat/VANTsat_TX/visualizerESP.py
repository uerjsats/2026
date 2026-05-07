import serial
import cv2
import numpy as np
import time

# --- Configurações da Porta Serial ---
PORTA = 'COM7' 
BAUD = 115200

# --- Dimensões da imagem raw (QVGA) ---
# Mantidas apenas como referência, pois o OpenCV calculará automaticamente a partir do JPEG
ALTURA = 240
LARGURA = 320

def conectar_serial():
    """
    Cria a conexão serial configurando DTR e RTS ANTES de abrir a porta.
    Aumenta a capacidade de memória do driver USB do SO para evitar gargalos.
    Retorna o objeto serial ativo ou None em caso de falha.
    """
    try:
        ser = serial.Serial()     
        ser.port = PORTA
        ser.baudrate = BAUD
        ser.timeout = 1  # Timeout ajustado para 1s para acelerar a resposta do driver
        
        # Configurações críticas: define o estado lógico antes de abrir a porta
        ser.dtr = False 
        ser.rts = False 
        
        ser.open()                
        
        # Aumenta o buffer do OS para 256KB (Específico para Windows/Portas COM)
        # Garante que o PC absorva todos os bytes enviados pelo ESP32 sem causar backpressure
        try:
            ser.set_buffer_size(rx_size=256000, tx_size=256000)
        except AttributeError:
            pass # Ignora caso a execução ocorra em sistemas Linux/macOS
            
        time.sleep(1)             
        print(f"[SISTEMA] Conexão {PORTA} aberta com sucesso e mantida ativa.")
        return ser
    except Exception as e:
        print(f"[ERRO] Falha ao abrir porta serial: {e}")
        return None

def comunicar_esp(ser, comando):
    """
    Utiliza a conexão serial persistente para enviar comandos e receber respostas.
    Refatorado com drenagem assíncrona de buffer para evitar Watchdog no ESP32.
    """
    img_decodificada = None
    
    try:
        ser.reset_input_buffer() 
        print(f"\n[TX] Enviando comando: {comando}")
        ser.write(comando.encode('utf-8'))
        
        # Lógica para Comandos de Ação Simples (Telemetria)
        if comando in ['C', 'R']:
            inicio_cmd = time.time()
            # Substituição do range estático por controle temporal (Timeout global de 3s)
            while time.time() - inicio_cmd < 3.0:
                # Drena rapidamente tudo que está na fila para liberar o TX do ESP32
                while ser.in_waiting > 0:
                    resposta = ser.readline().decode('utf-8', errors='ignore').strip()
                    if "DONE:" in resposta:
                        print(f"[TELEMETRIA] {resposta}")
                        return None
                    elif resposta:
                        print(f"[RX] {resposta}")
                
                # Pequeno sleep apenas se o buffer estiver VAZIO, para otimização de CPU local
                if ser.in_waiting == 0:
                    time.sleep(0.01)
                
        # Lógica para Download de Imagem (Comandos de 0 a 9)
        elif comando.isdigit():
            header = ""
            print("[INFO] Aguardando sincronização com o ESP32...")
            
            inicio_sync = time.time()
            # Busca pelo cabeçalho drenando ativamente as linhas sem causar bloqueio de I/O
            while time.time() - inicio_sync < 5.0: 
                while ser.in_waiting > 0:
                    linha = ser.readline().decode('utf-8', errors='ignore').strip()
                    
                    if linha.startswith("START"):
                        header = linha
                        break  # Sai do loop interno ao achar o cabeçalho
                    
                    elif linha:
                        print(f"\033[93m[ESP32 LOG] {linha}\033[0m")
                
                if header:
                    break # Sai do loop externo
                    
                if ser.in_waiting == 0:
                    time.sleep(0.01)
        
            # Início do processamento caso o cabeçalho seja validado
            if header.startswith("START"):
                partes = header.split(":")
                tamanho_total = int(partes[2])
                
                # Exibição da telemetria guardada no Log
                if len(partes) > 3:
                    dados_missao = ":".join(partes[3:])
                    print(f"\033[96m[LOG DA FOTO {comando}] {dados_missao}\033[0m")
                else:
                    print(f"[LOG DA FOTO {comando}] Sem metadados disponíveis.")

                print(f"[INFO] Recebendo imagem bruta de {tamanho_total} bytes...")
                
                dados_brutos = bytearray()
                inicio_download = time.time()
                
                # Download binário contínuo otimizado
                while len(dados_brutos) < tamanho_total:
                    faltam = tamanho_total - len(dados_brutos)
                    
                    # O método ser.read() bloqueia de forma nativa e super eficiente na camada C do SO
                    # até ler 'faltam' bytes ou até o timeout expirar. 
                    # Isso anula o gargalo de velocidade imposto pelo interpretador Python.
                    chunk = ser.read(faltam) 
                    if chunk:
                        dados_brutos.extend(chunk)
                    
                    if time.time() - inicio_download > 15:
                        print(f"[ERRO] Timeout. Recebidos {len(dados_brutos)}/{tamanho_total} bytes.")
                        break
                        
                # Processamento e montagem da matriz de pixels
                if len(dados_brutos) == tamanho_total:
                    # Limpeza residual rápida de qualquer quebra de linha ou END_FRAME final
                    time.sleep(0.05) 
                    if ser.in_waiting > 0:
                        ser.read_until(b"END_FRAME\n")
                        
                    print("[OK] Download concluído. Decodificando matriz JPEG/Grayscale...")
                    
                    nparr = np.frombuffer(dados_brutos, dtype=np.uint8)
                    img_decodificada = cv2.imdecode(nparr, cv2.IMREAD_UNCHANGED)
                    
                    if img_decodificada is not None:
                        h, w = img_decodificada.shape[:2]
                        print(f"[INFO] Imagem decodificada com sucesso. Resolução lida: {w}x{h}")
                    else:
                        print(f"[ERRO] Falha dimensional ou de decodificação. O array de {len(nparr)} bytes está corrompido ou não possui formato suportado.")
            else:
                print("[ERRO] Cabeçalho de imagem não encontrado. Analise os logs do ESP32 acima.")
                
    except Exception as e:
        print(f"[ERRO] Falha de I/O na porta serial: {e}")
        
    return img_decodificada

def main():
    """
    Função principal que gerencia o ciclo de vida da serial e a interface com o usuário.
    """
    print("=" * 40)
    print("   VISUALIZADOR E CONTROLE VANTsat   ")
    print("=" * 40)
    print("COMANDOS:\n[C] Capturar Foto\n[R] Resetar Missão\n[0-9] Baixar Foto do SD\n[Q] Sair")
    
    # 1. Abre a conexão uma única vez no início do programa
    ser = conectar_serial()
    if not ser:
        return # Encerra o script se não conseguir abrir a porta

    while True:
        cmd = input("\nDigite o comando >> ").strip().upper()
        
        if cmd == 'Q':
            print("Encerrando o sistema...")
            break
            
        elif cmd in ['C', 'R'] or (cmd.isdigit() and 0 <= int(cmd) <= 9):
            # 2. Passa o objeto serial aberto para a função de comunicação
            img = comunicar_esp(ser, cmd)
            
            if img is not None:
                cv2.imshow("VANTsat - Visualizador", img)
                cv2.waitKey(1)
        else:
            print("[!] Comando não reconhecido. Tente novamente.")
            
    # 3. Liberação de Hardware ao sair do loop principal
    if ser and ser.is_open:
        ser.close()
        print(f"[SISTEMA] Conexão {PORTA} fechada com segurança.")
        
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()