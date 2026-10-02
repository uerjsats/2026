#!/usr/bin/env python3
"""
setup_orangepi_adsb.py

Automatiza toda a configuração que fizemos manualmente no Orange Pi
Zero 3 (Arch Linux ARM / Orange Pi OS) para rodar o decodificador
ADS-B com RTL-SDR + pyModeS.

Resumo do que esse script faz, na ordem:
  1. Faz backup do mirrorlist e do pacman.conf atuais
  2. Remove o mirror chinês problemático (iplaystore.cn) e coloca o
     mirror oficial do Arch Linux ARM
  3. Desativa o repositório [opios], que não tem mirror funcional
  4. Roda pacman -Syu --overwrite "*" pra resolver os conflitos de
     arquivo (gcc-libs, firmware nvidia, etc.)
  5. Instala rtl-sdr, base-devel e git
  6. Cria um ambiente virtual Python em ~/adsb-venv
  7. Baixa o get-pip.py (contorna o ensurepip quebrado do Arch) e
     instala o pip de verdade dentro do venv
  8. Instala setuptools<81 (pra manter o pkg_resources disponível),
     pyModeS<3 (API v2, compatível com RtlReader) e
     pyrtlsdr==0.2.93 (compatível com a librtlsdr do sistema)

Uso:
    python3 setup_orangepi_adsb.py

Precisa de privilégios de sudo (vai pedir senha quando necessário)
e de conexão com a internet. Idealmente rode como o usuário normal
(uerjsats), não como root.
"""

import subprocess
import sys
import os
import shutil
from datetime import datetime

HOME = os.path.expanduser("~")
VENV_PATH = os.path.join(HOME, "adsb-venv")
MIRRORLIST = "/etc/pacman.d/mirrorlist"
PACMAN_CONF = "/etc/pacman.conf"
GOOD_MIRROR = "Server = http://mirror.archlinuxarm.org/$arch/$repo\n"


def banner(step, title):
    print("\n" + "=" * 60)
    print(f"[{step}] {title}")
    print("=" * 60)


def run(cmd, check=True, use_shell=False):
    """Roda um comando e mostra o que está sendo executado."""
    print(f"$ {cmd if isinstance(cmd, str) else ' '.join(cmd)}")
    result = subprocess.run(cmd, shell=use_shell)
    if check and result.returncode != 0:
        print(f"\n[ERRO] Comando falhou (código {result.returncode}). Abortando.")
        sys.exit(1)
    return result.returncode


def backup_file(path):
    if os.path.exists(path):
        ts = datetime.now().strftime("%Y%m%d-%H%M%S")
        backup_path = f"{path}.bak-{ts}"
        run(["sudo", "cp", path, backup_path])
        print(f"Backup salvo em: {backup_path}")


def fix_mirrorlist():
    banner(1, "Corrigindo o mirrorlist do pacman")
    backup_file(MIRRORLIST)

    # Remove qualquer linha do mirror chinês problemático
    run(f"sudo sed -i '/iplaystore\\.cn/d' {MIRRORLIST}", use_shell=True)

    # Garante que o mirror oficial do Arch Linux ARM está presente e no topo
    with open(MIRRORLIST) as f:
        content = f.read()

    if "archlinuxarm.org" not in content:
        with open("/tmp/mirrorlist.new", "w") as f:
            f.write(GOOD_MIRROR + content)
        run(["sudo", "cp", "/tmp/mirrorlist.new", MIRRORLIST])
        print("Mirror oficial do Arch Linux ARM adicionado no topo.")
    else:
        print("Mirror oficial já presente, nada a fazer.")


def disable_opios_repo():
    banner(2, "Desativando o repositório [opios] (sem mirror funcional)")
    backup_file(PACMAN_CONF)

    with open(PACMAN_CONF) as f:
        lines = f.readlines()

    changed = False
    new_lines = []
    skip_next_include = False
    for line in lines:
        stripped = line.strip()
        if stripped == "[opios]":
            new_lines.append("#" + line)
            skip_next_include = True
            changed = True
            continue
        if skip_next_include and stripped.startswith("Include"):
            new_lines.append("#" + line)
            skip_next_include = False
            continue
        new_lines.append(line)

    if changed:
        with open("/tmp/pacman.conf.new", "w") as f:
            f.writelines(new_lines)
        run(["sudo", "cp", "/tmp/pacman.conf.new", PACMAN_CONF])
        print("Repositório [opios] comentado com sucesso.")
    else:
        print("Repositório [opios] já estava desativado ou não encontrado.")


def full_system_upgrade():
    banner(3, "Atualizando o sistema (resolvendo conflitos de arquivo)")
    run("sudo pacman -Syyu --noconfirm --overwrite '*'", use_shell=True)


def install_base_packages():
    banner(4, "Instalando rtl-sdr, base-devel e git")
    run("sudo pacman -S --needed --noconfirm rtl-sdr base-devel git", use_shell=True)


def create_venv():
    banner(5, f"Criando o ambiente virtual em {VENV_PATH}")
    if os.path.isdir(VENV_PATH):
        print("Venv já existe, pulando criação.")
    else:
        run([sys.executable, "-m", "venv", VENV_PATH, "--without-pip"])


def venv_bin(name):
    return os.path.join(VENV_PATH, "bin", name)


def install_pip_in_venv():
    banner(6, "Instalando o pip dentro do venv (via get-pip.py)")
    get_pip_path = "/tmp/get-pip.py"
    run(["curl", "-sS", "-o", get_pip_path, "https://bootstrap.pypa.io/get-pip.py"])
    run([venv_bin("python"), get_pip_path])


def install_python_libs():
    banner(7, "Instalando bibliotecas Python (versões compatíveis)")
    pip = venv_bin("pip")
    run([pip, "install", "--upgrade", "setuptools<81"])
    run([pip, "install", "pyModeS<3"])
    run([pip, "install", "pyrtlsdr==0.2.93"])
    run([pip, "install", "matplotlib"])  # usado pelo script do fluxograma, opcional


def main():
    if os.geteuid() == 0:
        print("Não rode este script como root/sudo diretamente — rode como o "
              "usuário normal (ex: uerjsats). Ele vai pedir sudo quando precisar.")
        sys.exit(1)

    fix_mirrorlist()
    disable_opios_repo()
    full_system_upgrade()
    install_base_packages()
    create_venv()
    install_pip_in_venv()
    install_python_libs()

    print("\n" + "=" * 60)
    print("Tudo pronto!")
    print("=" * 60)
    print(f"""
Pra usar o ambiente a partir de agora, em cada sessão de terminal:

    source {VENV_PATH}/bin/activate

Depois disso, rode o decodificador normalmente:

    python3 adsb_decoder.py
""")


if __name__ == "__main__":
    main()
