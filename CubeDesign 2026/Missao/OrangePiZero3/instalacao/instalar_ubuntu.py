#!/usr/bin/env python3
"""
instalar_ubuntu.py — instala tudo que o adsb_cubesat.py precisa no Orange Pi Zero 3
(Ubuntu 22.04 da Orange Pi, também funciona em Ubuntu 22.04 de PC).

Passos:
  1. Pacotes do sistema (apt): rtl-sdr, librtlsdr, compilador, python3-venv ...
  2. Libera o dongle: bloqueia o driver de TV do kernel e dá permissão USB (plugdev)
  3. Compila o dump1090-fa (FlightAware) — o decodificador rápido em C — e instala
     em /usr/local/bin/dump1090-fa. Se falhar, compila o readsb como alternativa.
  4. Cria o ambiente Python ~/venv_adsb (pyserial; pyModeS/numpy/pyrtlsdr só para o
     modo de reserva --fonte pymodes) e instala o Thonny (IDE) dentro dele, com
     atalho no menu — a equipe configura o Orange com tela
  5. Verificação (binário, libs Python, dongle no USB)

Uso (como usuário normal, SEM sudo; ele pede a senha quando precisar):
    python3 instalar_ubuntu.py
    python3 instalar_ubuntu.py --sem-thonny   # não instala o Thonny (ex.: imagem do CubeSat sem tela)
    python3 instalar_ubuntu.py --completo     # + pandas/scipy/matplotlib (análise dos scripts de teste)
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
DUMP1090_DIR = os.path.join(HOME, "dump1090-fa")
READSB_DIR = os.path.join(HOME, "readsb")
BLACKLIST = "/etc/modprobe.d/blacklist-rtl-sdr.conf"
ATALHO = os.path.join(HOME, ".local", "share", "applications", "thonny-adsb.desktop")

PACOTES_APT = [
    "git", "curl", "build-essential", "pkg-config",
    "rtl-sdr", "librtlsdr-dev", "libusb-1.0-0-dev",
    "libncurses-dev", "zlib1g-dev", "libzstd-dev",
    "python3", "python3-venv", "python3-pip",
]
PACOTES_APT_THONNY = ["python3-tk"]       # interface gráfica do Thonny

# pyserial: serial com o OBC. Os demais só servem ao modo de reserva (--fonte pymodes).
# pyModeS<3 (a v3 quebra o RtlReader); pyrtlsdr 0.2.93 porque a librtlsdr do Ubuntu é antiga.
LIBS_BASE = ["pyserial", "pyModeS>=2.19,<3", "numpy", "pyrtlsdr==0.2.93", "setuptools<81"]
LIBS_COMPLETO = ["pandas", "scipy", "matplotlib"]


def passo(txt):
    print(f"\n==================== {txt} ====================", flush=True)


def rodar(cmd, obrigatorio=True, **kw):
    print("$ " + " ".join(cmd), flush=True)
    r = subprocess.run(cmd, **kw)
    if r.returncode != 0 and obrigatorio:
        sys.exit(f"\nFalhou: {' '.join(cmd)}")
    return r.returncode == 0


def clonar_ou_atualizar(url, destino, profundidade=1):
    if os.path.isdir(os.path.join(destino, ".git")):
        return rodar(["git", "-C", destino, "pull", "--ff-only"], obrigatorio=False)
    return rodar(["git", "clone", "--depth", str(profundidade), url, destino], obrigatorio=False)


def compilar_dump1090_fa():
    """Compila o dump1090-fa só com RTL-SDR (sem bladeRF/HackRF/LimeSDR/Soapy)."""
    if not clonar_ou_atualizar("https://github.com/flightaware/dump1090.git", DUMP1090_DIR):
        return False
    ok = rodar(["make", "-C", DUMP1090_DIR, f"-j{os.cpu_count() or 2}", "RTLSDR=yes",
                "BLADERF=no", "HACKRF=no", "LIMESDR=no", "SOAPYSDR=no"], obrigatorio=False)
    binario = os.path.join(DUMP1090_DIR, "dump1090")
    if not (ok and os.path.exists(binario)):
        return False
    return rodar(["sudo", "install", "-m", "755", binario, "/usr/local/bin/dump1090-fa"],
                 obrigatorio=False)


def compilar_readsb():
    """Alternativa ao dump1090-fa (mesmas opções de rede usadas pelo script)."""
    if not clonar_ou_atualizar("https://github.com/wiedehopf/readsb.git", READSB_DIR, 20):
        return False
    ok = rodar(["make", "-C", READSB_DIR, f"-j{os.cpu_count() or 2}", "RTLSDR=yes"],
               obrigatorio=False)
    return ok and rodar(["sudo", "install", "-m", "755", os.path.join(READSB_DIR, "readsb"),
                         "/usr/local/bin/readsb"], obrigatorio=False)


def main():
    if os.geteuid() == 0:
        sys.exit("Rode como usuário normal (sem sudo): python3 instalar_ubuntu.py")
    args = sys.argv[1:]
    com_thonny, completo = "--sem-thonny" not in args, "--completo" in args
    usuario = getpass.getuser()
    precisa_relogar = False

    # 1 ----------------------------------------------------------
    passo("1/5 Pacotes do sistema")
    rodar(["sudo", "apt-get", "update"])
    rodar(["sudo", "apt-get", "install", "-y", "--no-install-recommends", *PACOTES_APT,
           *(PACOTES_APT_THONNY if com_thonny else [])])

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
    passo("3/5 Compilando o dump1090-fa (alguns minutos no Orange Pi)")
    decodificador = None
    if compilar_dump1090_fa():
        decodificador = "dump1090-fa"
    else:
        print("\nAVISO: dump1090-fa não compilou — tentando o readsb como alternativa.")
        if compilar_readsb():
            decodificador = "readsb"

    # 4 ----------------------------------------------------------
    passo(f"4/5 Ambiente Python ({VENV})")
    if not os.path.exists(PY_VENV):
        rodar(["python3", "-m", "venv", VENV])
    rodar([PY_VENV, "-m", "pip", "install", "--upgrade", "pip"])
    rodar([PY_VENV, "-m", "pip", "install", *LIBS_BASE])
    if com_thonny:
        rodar([PY_VENV, "-m", "pip", "install", "thonny"])
        os.makedirs(os.path.dirname(ATALHO), exist_ok=True)
        with open(ATALHO, "w", encoding="utf-8") as f:
            f.write("[Desktop Entry]\nType=Application\nName=Thonny (ADS-B)\n"
                    "Comment=Thonny usando o ambiente ~/venv_adsb\n"
                    f"Exec={os.path.join(VENV, 'bin', 'thonny')} %F\n"
                    "Icon=thonny\nTerminal=false\nCategories=Development;IDE;\n")
        print(f"Atalho criado: {ATALHO}")
    if completo:
        rodar([PY_VENV, "-m", "pip", "install", *LIBS_COMPLETO])

    # 5 ----------------------------------------------------------
    passo("5/5 Verificação")
    rodar([PY_VENV, "-c", "import serial; print('pyserial', serial.__version__, 'OK')"])
    rodar([PY_VENV, "-c", "import pyModeS as p; assert p.adsb.callsign("
                          "'8D4840D6202CC371C32CE0576098').strip('_') == 'KLM1023'; print('pyModeS OK')"],
          obrigatorio=False)
    achado = next((n for n in ("dump1090-fa", "readsb") if shutil.which(n)), None)
    print(f"Decodificador em C: {achado}" if achado else
          "AVISO: nenhum dump1090-fa/readsb instalado — o script usará o modo de reserva "
          "(pyModeS, bem mais lento). Veja os erros do passo 3.")
    lsusb = subprocess.run(["lsusb"], capture_output=True, text=True).stdout.lower()
    if "0bda:2838" in lsusb or "0bda:2832" in lsusb:
        print("Dongle RTL-SDR detectado no USB.")
    else:
        print("AVISO: dongle RTL-SDR não encontrado no USB (está plugado?).")

    print("\n" + "=" * 64)
    print("Instalação concluída!" + (f" (decodificador: {decodificador})" if decodificador else ""))
    if precisa_relogar:
        print(">> Faça logout/login (ou reinicie) para valer a permissão USB.")
    print("\nPróximos passos (na pasta OrangePiZero3):")
    print(f"  {PY_VENV} testes_offline.py          # sanidade, sem hardware")
    print("  python3 instalacao/instalar_servico.py   # sobe sozinho no boot")
    print("=" * 64)


if __name__ == "__main__":
    main()
