#!/usr/bin/env python3
"""
Instala tudo que o módulo ADS-B (adsb_cubesat.py) precisa no Ubuntu (PC ou ARM):
  - driver/biblioteca do RTL-SDR e bloqueio do driver de TV do kernel
  - readsb (compilado do código-fonte)
  - ambiente Python ~/venv_adsb com pyModeS 2.x
  - Thonny instalado DENTRO do venv (já abre usando o venv) + atalho no menu

Uso (como usuário normal, SEM sudo; ele pede a senha quando precisar):
    python3 instalar_ubuntu.py
    python3 instalar_ubuntu.py --completo   # + libs dos scripts de teste e análise
"""

import getpass
import grp
import os
import shutil
import subprocess
import sys

HOME = os.path.expanduser("~")
VENV = os.path.join(HOME, "venv_adsb")
PY_VENV = os.path.join(VENV, "bin", "python")
READSB_DIR = os.path.join(HOME, "readsb")
BLACKLIST = "/etc/modprobe.d/blacklist-rtl-sdr.conf"

PACOTES_APT = [
    "git", "curl", "build-essential", "pkg-config",
    "rtl-sdr", "librtlsdr-dev", "libusb-1.0-0-dev",
    "libncurses-dev", "zlib1g-dev", "libzstd-dev",
    "python3", "python3-venv", "python3-pip", "python3-tk",   # tk: interface do Thonny
]
LIBS_BASE = ["pyModeS>=2.19,<3", "thonny"]
ATALHO = os.path.join(HOME, ".local", "share", "applications", "thonny-adsb.desktop")
# pyrtlsdr 0.2.93 porque a librtlsdr do Ubuntu é antiga para o pyrtlsdr novo
LIBS_COMPLETO = ["numpy", "pyrtlsdr==0.2.93", "setuptools<81", "pandas", "scipy", "matplotlib"]


def passo(txt):
    print(f"\n==================== {txt} ====================", flush=True)


def rodar(cmd, obrigatorio=True, **kw):
    print("$ " + " ".join(cmd), flush=True)
    r = subprocess.run(cmd, **kw)
    if r.returncode != 0 and obrigatorio:
        sys.exit(f"\nFalhou: {' '.join(cmd)}")
    return r.returncode == 0


def main():
    if os.geteuid() == 0:
        sys.exit("Rode como usuário normal (sem sudo): python3 instalar_ubuntu.py")
    completo = "--completo" in sys.argv[1:]
    usuario = getpass.getuser()
    precisa_relogar = False

    # 1 ----------------------------------------------------------
    passo("1/5 Pacotes do sistema")
    rodar(["sudo", "apt-get", "update"])
    rodar(["sudo", "apt-get", "install", "-y", "--no-install-recommends", *PACOTES_APT])

    # 2 ----------------------------------------------------------
    passo("2/5 Liberando o dongle (driver de TV e permissões USB)")
    if not os.path.exists(BLACKLIST):
        conteudo = "blacklist dvb_usb_rtl28xxu\nblacklist rtl2832\nblacklist rtl2830\n"
        rodar(["sudo", "tee", BLACKLIST], input=conteudo, text=True, stdout=subprocess.DEVNULL)
        print("Driver de TV bloqueado.")
    rodar(["sudo", "modprobe", "-r", "dvb_usb_rtl28xxu"], obrigatorio=False,
          stderr=subprocess.DEVNULL)
    grupos = [g.gr_name for g in grp.getgrall() if usuario in g.gr_mem]
    if "plugdev" not in grupos:
        rodar(["sudo", "usermod", "-aG", "plugdev", usuario])
        precisa_relogar = True
    rodar(["sudo", "udevadm", "control", "--reload-rules"], obrigatorio=False)
    rodar(["sudo", "udevadm", "trigger"], obrigatorio=False)

    # 3 ----------------------------------------------------------
    passo("3/5 Compilando o readsb")
    if os.path.isdir(os.path.join(READSB_DIR, ".git")):
        rodar(["git", "-C", READSB_DIR, "pull", "--ff-only"], obrigatorio=False)
    else:
        rodar(["git", "clone", "--depth", "20", "https://github.com/wiedehopf/readsb.git", READSB_DIR])
    rodar(["make", "-C", READSB_DIR, f"-j{os.cpu_count() or 2}", "RTLSDR=yes"])
    rodar(["sudo", "install", "-m", "755", os.path.join(READSB_DIR, "readsb"), "/usr/local/bin/readsb"])

    # 4 ----------------------------------------------------------
    passo(f"4/5 Ambiente Python ({VENV})")
    if not os.path.exists(PY_VENV):
        rodar(["python3", "-m", "venv", VENV])
    rodar([PY_VENV, "-m", "pip", "install", "--upgrade", "pip"])
    rodar([PY_VENV, "-m", "pip", "install", *LIBS_BASE])
    if completo:
        rodar([PY_VENV, "-m", "pip", "install", *LIBS_COMPLETO])

    # atalho no menu de aplicativos: Thonny já usando o venv
    os.makedirs(os.path.dirname(ATALHO), exist_ok=True)
    with open(ATALHO, "w", encoding="utf-8") as f:
        f.write("[Desktop Entry]\nType=Application\nName=Thonny (ADS-B)\n"
                "Comment=Thonny usando o ambiente ~/venv_adsb\n"
                f"Exec={os.path.join(VENV, 'bin', 'thonny')} %F\n"
                "Icon=thonny\nTerminal=false\nCategories=Development;IDE;\n")
    print(f"Atalho criado: {ATALHO}")

    # 5 ----------------------------------------------------------
    passo("5/5 Verificação")
    teste = ('import pyModeS as pms; from importlib.metadata import version; '
             'assert pms.adsb.callsign("8D4840D6202CC371C32CE0576098").strip("_") == "KLM1023"; '
             'print("pyModeS", version("pyModeS"), "OK")')
    rodar([PY_VENV, "-c", teste])
    print("readsb OK:" if shutil.which("readsb") else "AVISO: readsb não encontrado no PATH:",
          shutil.which("readsb") or "")
    lsusb = subprocess.run(["lsusb"], capture_output=True, text=True).stdout.lower()
    if "0bda:2838" in lsusb or "0bda:2832" in lsusb:
        print("Dongle RTL-SDR detectado no USB.")
    else:
        print("AVISO: dongle RTL-SDR não encontrado no USB (está plugado?).")

    print("\n" + "=" * 64)
    print("Instalação concluída!")
    if precisa_relogar:
        print(">> Faça logout/login (ou reinicie) para valer a permissão USB.")
    print("\nTestar o receptor (Ctrl+C para sair):")
    print("  readsb --device-type rtlsdr --gain 49.6 --net --net-ro-port 30002 --interactive")
    print("\nAbrir o Thonny já usando o venv: menu de aplicativos > 'Thonny (ADS-B)'")
    print(f"  ou no terminal: {os.path.join(VENV, 'bin', 'thonny')}")
    print("\nEm outro terminal, rodar o módulo ADS-B:")
    print(f"  {PY_VENV} adsb_cubesat.py --pasta-sd dados_teste --tabela sim")
    print("=" * 64)


if __name__ == "__main__":
    main()
