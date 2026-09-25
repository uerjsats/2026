#!/usr/bin/env python3
"""
Mede o tempo de captura de aeronaves via ADS-B (RTL-SDR + pyModeS), em lotes.

Para cada aeronave "completa" (callsign + posição + velocidade decodificados)
registra quanto tempo levou desde a aeronave anterior. A cada 5 aeronaves
(um "lote"), registra a duração total do lote. Repete até completar 50 lotes
(250 aeronaves no total) e salva tudo em dois CSVs:

    aircraft_timing.csv  -> uma linha por aeronave capturada
    batch_timing.csv     -> uma linha por lote de 5 aeronaves

Requisitos:
    pip install pyModeS pyrtlsdr --break-system-packages

Uso:
    python3 adsb_batch_timer.py
    (Ctrl+C para encerrar antes do fim; os CSVs ficam com o progresso parcial)
"""

import csv
import os
import sys
from datetime import datetime, timezone

import pyModeS as pms
from pyModeS.extra.rtlreader import RtlReader

# ---------------- Configurações ----------------
AIRCRAFT_PER_BATCH = 5
TOTAL_BATCHES = 50
TOTAL_AIRCRAFT = AIRCRAFT_PER_BATCH * TOTAL_BATCHES

AIRCRAFT_CSV = "aircraft_timing.csv"
BATCH_CSV = "batch_timing.csv"

AIRCRAFT_FIELDS = [
    "batch", "seq_in_batch", "global_seq", "icao", "callsign",
    "altitude", "lat", "lon", "speed", "heading",
    "seconds_since_previous_aircraft", "timestamp",
]
BATCH_FIELDS = ["batch", "start_time", "end_time", "duration_seconds", "aircraft_icaos"]


class BatchesFinished(Exception):
    """Levantada para sair do loop do RtlReader quando os 50 lotes terminam."""
    pass


class ADSBBatchTimer(RtlReader):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        self.aircraft_data = {}     # icao -> últimos campos decodificados
        self.seen_icaos = set()     # icaos já contabilizados na medição

        self.current_batch = 1
        self.batch_aircraft = []           # icaos já contados no lote atual
        self.batch_start_dt = datetime.now(timezone.utc)
        self.last_aircraft_dt = self.batch_start_dt

        self.global_count = 0
        self.aircraft_elapsed_list = []    # p/ estatísticas finais
        self.batch_duration_list = []      # p/ estatísticas finais

        self._init_csv(AIRCRAFT_CSV, AIRCRAFT_FIELDS)
        self._init_csv(BATCH_CSV, BATCH_FIELDS)

    # ---------------- CSV helpers ----------------
    @staticmethod
    def _init_csv(path, fields):
        with open(path, "w", newline="") as f:
            csv.DictWriter(f, fieldnames=fields).writeheader()

    @staticmethod
    def _append_csv(path, fields, row):
        with open(path, "a", newline="") as f:
            csv.DictWriter(f, fieldnames=fields).writerow(row)

    # ---------------- Decodificação ----------------
    def handle_messages(self, messages):
        for msg, ts in messages:
            if len(msg) != 28:
                continue

            df = pms.df(msg)
            if df not in (17, 18):
                continue

            if pms.crc(msg) != 0:
                continue

            icao = pms.icao(msg)
            if icao is None or icao in self.seen_icaos:
                continue  # aeronave inválida ou já contabilizada nesta medição

            entry = self.aircraft_data.setdefault(icao, {})
            tc = pms.adsb.typecode(msg)

            if 1 <= tc <= 4:
                entry["callsign"] = pms.adsb.callsign(msg).strip("_")

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

            elif tc == 19:
                velocity = pms.adsb.velocity(msg)
                if velocity:
                    spd, hdg, vr, _ = velocity
                    entry["speed"] = spd
                    entry["heading"] = hdg
                    entry["vrate"] = vr

            # Considera a aeronave "capturada" quando já temos callsign,
            # posição e velocidade — ajuste _is_complete() se quiser um
            # critério mais simples (ex: só posição).
            if self._is_complete(entry):
                self._register_aircraft(icao, entry)

                if len(self.batch_aircraft) == AIRCRAFT_PER_BATCH:
                    self._close_batch()

                    if self.current_batch > TOTAL_BATCHES:
                        raise BatchesFinished()

    @staticmethod
    def _is_complete(entry):
        required = ("callsign", "lat", "lon", "speed", "heading")
        return all(k in entry for k in required)

    # ---------------- Registro de tempos ----------------
    def _register_aircraft(self, icao, entry):
        now = datetime.now(timezone.utc)
        elapsed = (now - self.last_aircraft_dt).total_seconds()
        self.last_aircraft_dt = now
        self.global_count += 1

        self.seen_icaos.add(icao)
        self.batch_aircraft.append(icao)
        self.aircraft_elapsed_list.append(elapsed)

        row = {
            "batch": self.current_batch,
            "seq_in_batch": len(self.batch_aircraft),
            "global_seq": self.global_count,
            "icao": icao,
            "callsign": entry.get("callsign", ""),
            "altitude": entry.get("altitude", ""),
            "lat": round(entry["lat"], 5),
            "lon": round(entry["lon"], 5),
            "speed": entry.get("speed", ""),
            "heading": entry.get("heading", ""),
            "seconds_since_previous_aircraft": round(elapsed, 3),
            "timestamp": now.strftime("%Y-%m-%d %H:%M:%S"),
        }
        self._append_csv(AIRCRAFT_CSV, AIRCRAFT_FIELDS, row)

        print(
            f"[Lote {self.current_batch}/{TOTAL_BATCHES}] "
            f"Aeronave {len(self.batch_aircraft)}/{AIRCRAFT_PER_BATCH} "
            f"(total {self.global_count}/{TOTAL_AIRCRAFT}) "
            f"| ICAO {icao} | Callsign {entry.get('callsign', '---')} "
            f"| +{elapsed:.2f}s desde a anterior"
        )

    def _close_batch(self):
        end_dt = datetime.now(timezone.utc)
        duration = (end_dt - self.batch_start_dt).total_seconds()
        self.batch_duration_list.append(duration)

        row = {
            "batch": self.current_batch,
            "start_time": self.batch_start_dt.strftime("%Y-%m-%d %H:%M:%S"),
            "end_time": end_dt.strftime("%Y-%m-%d %H:%M:%S"),
            "duration_seconds": round(duration, 3),
            "aircraft_icaos": ";".join(self.batch_aircraft),
        }
        self._append_csv(BATCH_CSV, BATCH_FIELDS, row)

        print(f"\n>>> Lote {self.current_batch}/{TOTAL_BATCHES} concluído em "
              f"{duration:.2f}s  ({', '.join(self.batch_aircraft)})\n")

        self.current_batch += 1
        self.batch_aircraft = []
        self.batch_start_dt = datetime.now(timezone.utc)

    # ---------------- Estatísticas finais ----------------
    def print_summary(self):
        n_air = len(self.aircraft_elapsed_list)
        n_bat = len(self.batch_duration_list)

        print("\n" + "=" * 50)
        print("RESUMO")
        print("=" * 50)
        print(f"Aeronaves capturadas: {n_air}/{TOTAL_AIRCRAFT}")
        print(f"Lotes concluídos:     {n_bat}/{TOTAL_BATCHES}")

        if n_air:
            print(
                f"Tempo por aeronave -> média: {sum(self.aircraft_elapsed_list)/n_air:.2f}s "
                f"| min: {min(self.aircraft_elapsed_list):.2f}s "
                f"| max: {max(self.aircraft_elapsed_list):.2f}s"
            )
        if n_bat:
            print(
                f"Tempo por lote     -> média: {sum(self.batch_duration_list)/n_bat:.2f}s "
                f"| min: {min(self.batch_duration_list):.2f}s "
                f"| max: {max(self.batch_duration_list):.2f}s"
            )
        print(f"\nCSVs salvos em:\n  {os.path.abspath(AIRCRAFT_CSV)}\n  {os.path.abspath(BATCH_CSV)}")


def main():
    print("Iniciando... conectando ao RTL-SDR (frequência 1090 MHz)\n")
    try:
        decoder = ADSBBatchTimer()
        decoder.sdr.center_freq = 1090e6
        decoder.sdr.sample_rate = 2e6
        decoder.sdr.gain = 49.6
        print(f"Frequência configurada: {decoder.sdr.center_freq / 1e6} MHz")
        print(f"Ganho: {decoder.sdr.gain}")
        print(f"Meta: {TOTAL_BATCHES} lotes de {AIRCRAFT_PER_BATCH} aeronaves "
              f"({TOTAL_AIRCRAFT} aeronaves no total)\n")
    except Exception as e:
        print(f"Erro ao conectar no RTL-SDR: {e}")
        sys.exit(1)

    try:
        while True:
            try:
                decoder.run()
                break
            except ValueError as e:
                # Bug conhecido do pyModeS: buffer sem pulsos detectados
                # (sinal fraco/momento sem tráfego). Não é erro fatal,
                # apenas reinicia a leitura do SDR.
                print(f"Aviso: buffer vazio ({e}) — reiniciando leitura...")
                continue
    except BatchesFinished:
        print("\nMeta de lotes atingida!")
    except KeyboardInterrupt:
        print("\nEncerrado pelo usuário.")
    finally:
        decoder.print_summary()


if __name__ == "__main__":
    main()