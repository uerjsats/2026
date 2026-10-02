#!/usr/bin/env bash
# Instala dependências + serviço de boot no Orange Pi (Ubuntu 22.04). Rode SEM sudo.
set -e
cd "$(dirname "$0")"
python3 instalar_ubuntu.py "$@"
python3 instalar_servico.py
echo
echo "Tudo pronto. Reinicie:  sudo reboot"
