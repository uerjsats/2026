#!/usr/bin/env python3
"""
instalar_servico.py — deixa o módulo ADS-B subir sozinho quando o Orange Pi
(Ubuntu 22.04 da Orange Pi) for alimentado, e libera a UART de debug para o OBC.

O que faz (precisa de sudo; rode como usuário normal, DEPOIS do instalacao/instalar_ubuntu.py):
  1. Coloca o usuário nos grupos dialout (serial) e plugdev (RTL-SDR)
  2. Cria /var/lib/adsb (banco e logs no SD) com dono = usuário
  3. Desativa o login/console da UART de debug (serial-getty em ttyS0/ttyAS0)
     e tira o console do kernel dela (/boot/orangepiEnv.txt: console=display)
  4. Instala e habilita /etc/systemd/system/adsb-cubesat.service

Uso:
    python3 instalacao/instalar_servico.py              # instala
    python3 instalacao/instalar_servico.py --remover    # desinstala o serviço

Depois reinicie o Orange Pi (sudo reboot). Logs do serviço:
    journalctl -u adsb-cubesat -f      ou      tail -f /var/lib/adsb/orange.log
"""

import getpass
import os
import shutil
import subprocess
import sys

USUARIO = getpass.getuser()
AQUI = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # pasta OrangePiZero3
PY_VENV = os.path.join(os.path.expanduser("~"), "venv_adsb", "bin", "python")
SERVICO = "/etc/systemd/system/adsb-cubesat.service"
ENV_ORANGE = "/boot/orangepiEnv.txt"
PASTA_DADOS = "/var/lib/adsb"

UNIT = f"""[Unit]
Description=Missao ADS-B CubeDesign 2026 (Orange Pi Zero 3)
After=local-fs.target systemd-udevd.service

[Service]
Type=simple
User={USUARIO}
SupplementaryGroups=dialout plugdev
WorkingDirectory={AQUI}
ExecStart={PY_VENV} {os.path.join(AQUI, "adsb_cubesat.py")}
Restart=always
RestartSec=2
Environment=PYTHONUNBUFFERED=1

[Install]
WantedBy=multi-user.target
"""


def sudo(*cmd, obrigatorio=True, **kw):
    print("$ sudo " + " ".join(cmd), flush=True)
    r = subprocess.run(["sudo", *cmd], **kw)
    if r.returncode != 0 and obrigatorio:
        sys.exit(f"\nFalhou: sudo {' '.join(cmd)}")
    return r.returncode == 0


def ajustar_orangepienv():
    """console=display: o kernel para de escrever na UART de debug."""
    if not os.path.exists(ENV_ORANGE):
        print(f"AVISO: {ENV_ORANGE} não existe — confira o console da serial à mão.")
        return
    sudo("cp", ENV_ORANGE, ENV_ORANGE + ".bak-adsb")
    with open(ENV_ORANGE) as f:
        linhas = f.read().splitlines()
    novas, tem_console, tem_verb = [], False, False
    for l in linhas:
        if l.startswith("console="):
            novas.append("console=display")
            tem_console = True
        elif l.startswith("verbosity="):
            novas.append("verbosity=1")
            tem_verb = True
        else:
            novas.append(l)
    if not tem_console:
        novas.append("console=display")
    if not tem_verb:
        novas.append("verbosity=1")
    sudo("tee", ENV_ORANGE, input="\n".join(novas) + "\n", text=True, stdout=subprocess.DEVNULL)
    print(f"{ENV_ORANGE} atualizado (backup .bak-adsb).")


def main():
    if os.geteuid() == 0:
        sys.exit("Rode como usuário normal (sem sudo): python3 instalacao/instalar_servico.py")

    if "--remover" in sys.argv[1:]:
        sudo("systemctl", "disable", "--now", "adsb-cubesat.service", obrigatorio=False)
        sudo("rm", "-f", SERVICO)
        sudo("systemctl", "daemon-reload")
        print("Serviço removido. (O console da UART de debug continua desativado.)")
        return

    if not os.path.exists(PY_VENV):
        sys.exit(f"Ambiente {PY_VENV} não encontrado — rode antes instalacao/instalar_ubuntu.py")

    print("1/4 Grupos")
    sudo("usermod", "-aG", "dialout,plugdev", USUARIO)

    print("2/4 Pasta de dados")
    sudo("mkdir", "-p", PASTA_DADOS)
    sudo("chown", f"{USUARIO}:{USUARIO}", PASTA_DADOS)

    print("3/4 Liberando a UART de debug (console/getty)")
    for tty in ("ttyS0", "ttyAS0"):
        sudo("systemctl", "disable", "--now", f"serial-getty@{tty}.service",
             obrigatorio=False, stderr=subprocess.DEVNULL)
        sudo("systemctl", "mask", f"serial-getty@{tty}.service",
             obrigatorio=False, stderr=subprocess.DEVNULL)
    ajustar_orangepienv()

    print("4/4 Serviço systemd")
    sudo("tee", SERVICO, input=UNIT, text=True, stdout=subprocess.DEVNULL)
    sudo("systemctl", "daemon-reload")
    sudo("systemctl", "enable", "adsb-cubesat.service")

    print("\n" + "=" * 60)
    print("Pronto. Reinicie o Orange Pi:  sudo reboot")
    print("Depois do boot o script fica em IDLE esperando comando na serial.")
    print("Ver o status:  systemctl status adsb-cubesat")
    print("=" * 60)


if __name__ == "__main__":
    main()
