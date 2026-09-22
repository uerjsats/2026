#!/usr/bin/env python3
"""
Decodificador de ADS-B ao vivo usando RTL-SDR + pyModeS.

Mostra em tempo real, numa tabela no terminal, as aeronaves
detectadas: ICAO, callsign, altitude, posição, velocidade e rumo.

Requisitos:
    pip install pyModeS pyrtlsdr --break-system-packages

Uso:
    python3 adsb_decoder.py
    (Ctrl+C para encerrar)
"""

import sys
import csv
import os
from datetime import datetime, timezone
import pyModeS as pms
from pyModeS.extra.rtlreader import RtlReader

aircraft_data = {}

CSV_PATH = "aircraft_log.csv"
CSV_FIELDS = ["icao", "callsign", "altitude", "lat", "lon", "speed", "heading", "vrate", "last_seen"]


def save_csv():
    """Reescreve o CSV inteiro com o estado atual de todas as aeronaves vistas."""
    with open(CSV_PATH, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        writer.writeheader()
        for icao, d in aircraft_data.items():
            writer.writerow({
                "icao": icao,
                "callsign": d.get("callsign", ""),
                "altitude": d.get("altitude", ""),
                "lat": round(d["lat"], 5) if "lat" in d else "",
                "lon": round(d["lon"], 5) if "lon" in d else "",
                "speed": d.get("speed", ""),
                "heading": d.get("heading", ""),
                "vrate": d.get("vrate", ""),
                "last_seen": d.get("last_seen", ""),
            })


class ADSBDecoder(RtlReader):
    def handle_messages(self, messages):
        for msg, ts in messages:
            # Só processa mensagens Extended Squitter (28 caracteres hex = 14 bytes)
            if len(msg) != 28:
                continue

            df = pms.df(msg)
            if df not in (17, 18):
                continue  # só nos interessa ADS-B (DF17/DF18)

            if pms.crc(msg) != 0:
                continue  # descarta mensagens com CRC inválido (ruído)

            icao = pms.icao(msg)
            if icao is None:
                continue

            entry = aircraft_data.setdefault(icao, {})
            entry["last_seen"] = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
            tc = pms.adsb.typecode(msg)

            # Identificação / callsign
            if 1 <= tc <= 4:
                entry["callsign"] = pms.adsb.callsign(msg).strip("_")

            # Posição (precisa de um frame par + um ímpar)
            elif 9 <= tc <= 18:
                oe = pms.adsb.oe_flag(msg)
                if oe == 0:
                    entry["even"], entry["even_ts"] = msg, ts
                else:
                    entry["odd"], entry["odd_ts"] = msg, ts

                alt = pms.adsb.altitude(msg)
                if alt is not None:
                    entry["altitude"] = alt

                if "even" in entry and "odd" in entry:
                    pos = pms.adsb.position(
                        entry["even"], entry["odd"],
                        entry["even_ts"], entry["odd_ts"],
                    )
                    if pos:
                        entry["lat"], entry["lon"] = pos

            # Velocidade / rumo / razão de subida
            elif tc == 19:
                velocity = pms.adsb.velocity(msg)
                if velocity:
                    spd, hdg, vr, _ = velocity
                    entry["speed"] = spd
                    entry["heading"] = hdg
                    entry["vrate"] = vr

            self.print_table()
            save_csv()

    def print_table(self):
        # limpa o terminal antes de redesenhar
        print("\033c", end="")
        print("Decodificador ADS-B — Ctrl+C para sair\n")
        print(f"Salvando em: {os.path.abspath(CSV_PATH)}\n")
        header = f"{'ICAO':<8}{'Callsign':<10}{'Alt(ft)':<9}{'Lat':<11}{'Lon':<11}{'Vel(kt)':<9}{'Rumo':<6}"
        print(header)
        print("-" * len(header))
        for icao, d in aircraft_data.items():
            print(
                f"{icao:<8}"
                f"{d.get('callsign', '---'):<10}"
                f"{d.get('altitude', '---'):<9}"
                f"{round(d['lat'], 4) if 'lat' in d else '---':<11}"
                f"{round(d['lon'], 4) if 'lon' in d else '---':<11}"
                f"{d.get('speed', '---'):<9}"
                f"{d.get('heading', '---'):<6}"
            )


def main():
    print("Iniciando... conectando ao RTL-SDR (frequência 1090 MHz)\n")
    try:
        decoder = ADSBDecoder()
        decoder.sdr.center_freq = 1090e6  # força 1090 MHz explicitamente
        decoder.sdr.sample_rate = 2e6
        decoder.sdr.gain = 49.6  # ganho máximo do R820T (em vez de automático)
        print(f"Frequência configurada: {decoder.sdr.center_freq / 1e6} MHz")
        print(f"Ganho: {decoder.sdr.gain}\n")
        save_csv()  # cria o arquivo (com cabeçalho, mesmo vazio) já no início
        print(f"CSV criado em: {os.path.abspath(CSV_PATH)}\n")
    except Exception as e:
        print(f"Erro ao conectar no RTL-SDR: {e}")
        sys.exit(1)

    try:
        decoder.run()
    except KeyboardInterrupt:
        print("\nEncerrado pelo usuário.")


if __name__ == "__main__":
    main()
