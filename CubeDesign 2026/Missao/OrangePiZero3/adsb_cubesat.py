#!/usr/bin/env python3
"""
adsb_cubesat.py — script ÚNICO de missão do Orange Pi Zero 3 (CubeDesign 2026).

Sobe sozinho com o Orange Pi (serviço systemd), abre a serial com o OBC
(Heltec WiFi LoRa 32 V3) e FICA ESPERANDO COMANDOS. O receptor ADS-B só liga
quando o OBC manda START.

Estados:  IDLE (esperando)  <->  RUNNING (decodificando ADS-B)

Orientação a objetos (tudo neste arquivo, só stdlib + pyserial):
    Config            constantes (única fonte de configuração)
    Protocolo         endereçamento das mensagens da serial  "2:COMANDO,args"
    Metricas          contadores da missão (HLR-ADS-08)
    BancoMissao       SQLite: 'autorizadas' (QRY) + log 'adsb' (RAM disk -> SD)
    EnlaceSerial      serial resiliente com thread de escrita
    FonteADSB (ABC)   fonte de dados ADS-B — herdada por:
        FonteDump1090   dump1090-fa/readsb (C, rápido) lido por SBS/TCP  [padrão]
        FontePyModeS    RTL-SDR + pyModeS puro Python                      [fallback]
        FonteSimulada   20 aeronaves falsas, sem antena (bancada)
    Missao            máquina de estados + comandos + escritor + flush

Por que dump1090 por padrão: a demodulação (I/Q -> PPM -> CRC -> CPR) é em C e
roda em ~1–3% de CPU; o Python só lê linhas de texto (SBS) já decodificadas.
O pyModeS faz tudo em Python/numpy e custa muito mais CPU e RAM.

Uso (normalmente pelo systemd):
    python3 adsb_cubesat.py                         # missão (fonte = auto)
    python3 adsb_cubesat.py --fonte sim --pasta-sd ~/adsb_teste
    python3 adsb_cubesat.py banco listar            # gerencia 'autorizadas'
    python3 adsb_cubesat.py frame "START,600"       # mostra a linha "2:START,600"
"""

import abc
import argparse
import collections
import logging
import logging.handlers
import math
import os
import queue
import random
import shutil
import signal
import socket
import sqlite3
import subprocess
import sys
import threading
import time

log = logging.getLogger("adsb")


# =============================================================================
# Configuração
# =============================================================================
class Config:
    """Única fonte de configuração. Argumentos de linha de comando sobrescrevem."""

    # --- Serial com o OBC (UART0 de debug do Orange Pi Zero 3) ---
    PORTA_SERIAL = "/dev/ttyS0"               # kernel 6.1
    PORTAS_ALTERNATIVAS = ["/dev/ttyAS0"]     # kernel 5.4
    BAUD = 115200                             # TODO: validar com o SoftwareSerial do Heltec
                                              # (o Controle de Atitude usa 9600 no mesmo OBC)
    ENDERECO_ORANGE = "2"                     # OBC manda "2:COMANDO"; o Orange responde "2:..."
    EXIGIR_CHECKSUM_TC = False                # True: TC sem "*XX" válido é ignorado
    CHECKSUM_TX = False                       # True: respostas saem com "*XX" no fim

    # --- Fonte ADS-B ---
    FONTE = "auto"                            # auto | dump1090 | pymodes | sim
    DUMP1090_BIN = None                       # None = procura dump1090-fa, dump1090, readsb
    DUMP1090_COMANDO = None                   # lista completa (só testes); sobrescreve tudo
    SBS_HOST = "127.0.0.1"
    SBS_PORTA = 30003
    SDR_FREQ_HZ = 1090e6
    SDR_SAMPLE_RATE = 2e6                     # só usado pelo pyModeS (dump1090 usa o dele)
    SDR_GAIN = 49.6                           # ganho máximo do R820T (validado)
    SDR_TIMEOUT_START_S = 20.0                # espera o dump1090 abrir o dongle e a porta SBS

    # --- Missão ---
    MISSAO_DURACAO_S = 600                    # 10 min; 0 = sem limite
    AC_INTERVALO_MIN_S = 2.0                  # mín. entre dois AC da mesma aeronave
    AERONAVE_TIMEOUT_S = 120
    PAR_IMPAR_MAX_S = 10.0                    # só pyModeS (CPR par+ímpar)

    # --- Filas ---
    FILA_REGISTROS_MAX = 2000
    TX_FILA_DADOS_MAX = 500
    DUMP_MAX = 200

    # --- Armazenamento ---
    PASTA_SD = "/var/lib/adsb"                # persistente: adsb.db, orange.log
    PASTA_RAM = "/dev/shm/adsb"               # tmpfs: buffer rápido da sessão
    PASTA_SD_FALLBACK = "~/adsb_dados"
    FLUSH_INTERVALO_S = 5.0                   # RAM -> SD

    # --- Consulta de autorizadas (QRY) ---
    TOLERANCIA_GRAUS = 0.01                   # ±0,01° (~1 km)

    ARQUIVO_TEMP_CPU = "/sys/class/thermal/thermal_zone0/temp"


# =============================================================================
# Protocolo da serial
# =============================================================================
class Protocolo:
    """
    Endereçamento do OBC: cada subsistema tem um número. Mensagens ASCII, uma por linha:

        OBC -> Orange:   2:COMANDO[,arg,...]        (ex.: "2:START,600", "2:QRY,-22.88,-43.30")
        Orange -> OBC:   2:TIPO[,campo,...]         (ex.: "2:ACK,START,1", "2:QRES,NONE")

    Linhas que não começam com "2:" são de outros subsistemas (ou lixo de boot) e
    são ignoradas. Checksum opcional: "...*XX" (XOR dos caracteres depois de "2:",
    hex com 2 dígitos); validado se vier, exigido só com EXIGIR_CHECKSUM_TC.
    """

    @staticmethod
    def checksum(corpo):
        c = 0
        for b in corpo.encode("ascii", "ignore"):
            c ^= b
        return c

    @staticmethod
    def montar(tipo, *campos):
        """Linha completa '2:TIPO,campos\\n'. None vira campo vazio."""
        corpo = ",".join([tipo] + ["" if c is None else str(c) for c in campos])
        cs = f"*{Protocolo.checksum(corpo):02X}" if Config.CHECKSUM_TX else ""
        return f"{Config.ENDERECO_ORANGE}:{corpo}{cs}\n"

    @staticmethod
    def interpretar(linha, exigir_checksum=None):
        """(TIPO, [campos]) se a linha é para o Orange e válida; senão None."""
        if exigir_checksum is None:
            exigir_checksum = Config.EXIGIR_CHECKSUM_TC
        linha = linha.strip()
        prefixo = Config.ENDERECO_ORANGE + ":"
        if not linha.startswith(prefixo):
            return None                                     # outro subsistema ou lixo
        resto = linha[len(prefixo):].strip()
        if "*" in resto:
            corpo, _, cs = resto.rpartition("*")
            try:
                esperado = int(cs.strip(), 16)
            except ValueError:
                return None
            if Protocolo.checksum(corpo) != esperado:
                return None
        else:
            if exigir_checksum:
                return None
            corpo = resto
        partes = corpo.split(",")
        tipo = partes[0].strip().upper()
        if not tipo:
            return None
        return tipo, [p.strip() for p in partes[1:]]


# =============================================================================
# Métricas
# =============================================================================
class Metricas:
    """Contadores da missão (base para as métricas de sucesso, HLR-ADS-08)."""

    CAMPOS = ("rx", "df17_18", "crc_falha", "validas", "registros",
              "enviados", "descartados_tx", "descartados_fila")

    def __init__(self):
        self._lock = threading.Lock()
        self.zerar()

    def zerar(self):
        with self._lock:
            self.c = dict.fromkeys(self.CAMPOS, 0)
            self.aeronaves = set()
            self.lat_soma = 0.0
            self.lat_max = 0.0
            self.lat_n = 0

    def inc(self, campo, n=1):
        with self._lock:
            self.c[campo] += n

    def aeronave(self, icao):
        with self._lock:
            self.aeronaves.add(icao)

    def latencia(self, segundos):
        """Latência entre a decodificação e a escrita na serial."""
        with self._lock:
            self.lat_soma += segundos
            self.lat_n += 1
            self.lat_max = max(self.lat_max, segundos)

    def resumo(self):
        with self._lock:
            d = dict(self.c)
            d["aeronaves"] = len(self.aeronaves)
            d["lat_media_ms"] = round(1000 * self.lat_soma / self.lat_n, 1) if self.lat_n else 0.0
            d["lat_max_ms"] = round(1000 * self.lat_max, 1)
            return d


# =============================================================================
# Banco de dados (SQLite)
# =============================================================================
class BancoMissao:
    """
    Arquivo persistente <PASTA_SD>/adsb.db (cartão SD) com:
      autorizadas  aeronaves NÃO clandestinas, preenchidas à mão — única tabela do QRY
      adsb         log dos ADS-B decodificados
    Registros novos entram primeiro num banco em RAM disk e vão para o SD em
    lote, por flush() (RF07). O SD usa WAL + synchronous=FULL (sobrevive a queda
    de energia).
    """

    SCHEMA = """
    CREATE TABLE IF NOT EXISTS autorizadas (
        icao24 TEXT PRIMARY KEY, callsign TEXT, lat REAL NOT NULL, lon REAL NOT NULL,
        alt_ft INTEGER, gs_kt INTEGER, track_deg INTEGER, vrate_fpm INTEGER, squawk TEXT);
    CREATE TABLE IF NOT EXISTS adsb (
        id INTEGER PRIMARY KEY AUTOINCREMENT, sessao INTEGER, seq INTEGER, ts REAL,
        ts_mono REAL, icao24 TEXT NOT NULL, callsign TEXT, lat REAL, lon REAL,
        alt_ft INTEGER, gs_kt INTEGER, track_deg INTEGER, vrate_fpm INTEGER);
    CREATE TABLE IF NOT EXISTS meta (chave TEXT PRIMARY KEY, valor TEXT);
    """
    # Aeronaves não clandestinas conhecidas (mesmo formato do ADSBRecord do ESP32).
    AUTORIZADAS_INICIAIS = [
        ("7788FF", "TAP0803", -22.8800, -43.3000, 38000, 460, 180, 600, "5511"),
        ("33CC99", "UAL1890", -23.0000, -43.1000, 32000, 430, 315, -300, "1200"),
    ]
    COLUNAS_AUTORIZADAS = ("icao24", "callsign", "lat", "lon", "alt_ft",
                           "gs_kt", "track_deg", "vrate_fpm", "squawk")
    COLUNAS_ADSB = ("sessao", "seq", "ts", "ts_mono", "icao24", "callsign",
                    "lat", "lon", "alt_ft", "gs_kt", "track_deg", "vrate_fpm")
    _INSERT_ADSB = ("INSERT INTO adsb ({}) VALUES ({})".format(
        ",".join(COLUNAS_ADSB), ",".join("?" * len(COLUNAS_ADSB))))

    @classmethod
    def abrir_sd(cls, pasta_sd):
        """Abre (criando se preciso) o banco persistente."""
        os.makedirs(pasta_sd, exist_ok=True)
        con = sqlite3.connect(os.path.join(pasta_sd, "adsb.db"), timeout=10,
                              check_same_thread=False)
        con.row_factory = sqlite3.Row
        con.execute("PRAGMA journal_mode=WAL")
        con.execute("PRAGMA synchronous=FULL")
        con.executescript(cls.SCHEMA)
        # Semeia as autorizadas só na criação (não ressuscita o que a equipe apagar).
        if con.execute("SELECT valor FROM meta WHERE chave='seed'").fetchone() is None:
            con.executemany("INSERT OR IGNORE INTO autorizadas VALUES (?,?,?,?,?,?,?,?,?)",
                            cls.AUTORIZADAS_INICIAIS)
            con.execute("INSERT INTO meta VALUES ('seed','1')")
            con.commit()
        return con

    def __init__(self, pasta_sd, pasta_ram):
        self._lock = threading.Lock()
        self._sd = self.abrir_sd(pasta_sd)
        os.makedirs(pasta_ram, exist_ok=True)
        live = os.path.join(pasta_ram, "live.db")
        for sufixo in ("", "-wal", "-shm", "-journal"):     # RAM começa limpa a cada boot
            try:
                os.remove(live + sufixo)
            except FileNotFoundError:
                pass
        self._live = sqlite3.connect(live, check_same_thread=False)
        self._live.row_factory = sqlite3.Row
        self._live.execute("PRAGMA journal_mode=MEMORY")
        self._live.execute("PRAGMA synchronous=OFF")
        self._live.executescript(self.SCHEMA)
        self._cursor = 0                                    # último id do live já copiado

    def nova_sessao(self):
        """Número da sessão (sem RTC, data não é confiável)."""
        with self._lock:
            l = self._sd.execute("SELECT valor FROM meta WHERE chave='sessao'").fetchone()
            n = int(l["valor"]) + 1 if l else 1
            self._sd.execute("INSERT OR REPLACE INTO meta VALUES ('sessao', ?)", (str(n),))
            self._sd.commit()
            return n

    def inserir_lote(self, registros):
        if not registros:
            return
        linhas = [tuple(r.get(c) for c in self.COLUNAS_ADSB) for r in registros]
        with self._lock:
            self._live.executemany(self._INSERT_ADSB, linhas)
            self._live.commit()

    def flush(self):
        """RAM disk -> SD numa transação. Retorna nº de linhas copiadas."""
        with self._lock:
            novos = self._live.execute(
                "SELECT id,{} FROM adsb WHERE id > ? ORDER BY id".format(",".join(self.COLUNAS_ADSB)),
                (self._cursor,)).fetchall()
            if not novos:
                return 0
            self._sd.executemany(self._INSERT_ADSB,
                                 [tuple(l[c] for c in self.COLUNAS_ADSB) for l in novos])
            self._sd.commit()
            self._cursor = novos[-1]["id"]
            return len(novos)

    def ultimos(self, n):
        """Últimos n ADS-B (mais antigo primeiro), já após flush()."""
        self.flush()
        with self._lock:
            l = self._sd.execute("SELECT * FROM adsb ORDER BY id DESC LIMIT ?", (int(n),)).fetchall()
        return [dict(x) for x in reversed(l)]

    def consultar_autorizada(self, lat, lon, icao=None, tolerancia=None):
        """Aeronave autorizada mais próxima de (lat, lon) dentro da tolerância; dict ou None."""
        tol = Config.TOLERANCIA_GRAUS if tolerancia is None else tolerancia
        sql = "SELECT * FROM autorizadas WHERE abs(lat-?)<=? AND abs(lon-?)<=?"
        args = [lat, tol, lon, tol]
        if icao:
            sql += " AND upper(icao24)=?"
            args.append(icao.upper())
        sql += " ORDER BY (lat-?)*(lat-?)+(lon-?)*(lon-?) LIMIT 1"
        args += [lat, lat, lon, lon]
        with self._lock:
            l = self._sd.execute(sql, args).fetchone()
        return dict(l) if l else None

    def fechar(self):
        try:
            self.flush()
        finally:
            with self._lock:
                self._live.close()
                self._sd.close()

    # ---- CLI: python3 adsb_cubesat.py banco listar|adicionar|remover|ultimos ----
    @classmethod
    def cli(cls, argv):
        ap = argparse.ArgumentParser(prog="adsb_cubesat.py banco",
                                     description="Gerencia a tabela 'autorizadas' (pode com o serviço rodando)")
        ap.add_argument("--pasta-sd", default=os.path.expanduser(Config.PASTA_SD))
        sub = ap.add_subparsers(dest="cmd", required=True)
        sub.add_parser("listar", help="lista as aeronaves autorizadas")
        a = sub.add_parser("adicionar", help="adiciona/atualiza uma aeronave autorizada")
        for nome, tipo in (("icao24", str), ("callsign", str), ("lat", float), ("lon", float),
                           ("alt_ft", int), ("gs_kt", int), ("track_deg", int),
                           ("vrate_fpm", int), ("squawk", str)):
            a.add_argument(nome, type=tipo)
        r = sub.add_parser("remover", help="remove uma aeronave autorizada")
        r.add_argument("icao24")
        u = sub.add_parser("ultimos", help="últimos ADS-B decodificados")
        u.add_argument("n", type=int, nargs="?", default=20)
        args = ap.parse_args(argv)

        con = cls.abrir_sd(args.pasta_sd)
        if args.cmd == "listar":
            for l in con.execute("SELECT * FROM autorizadas ORDER BY icao24"):
                print(" | ".join(str(l[c]) for c in cls.COLUNAS_AUTORIZADAS))
        elif args.cmd == "adicionar":
            v = (args.icao24.upper(), args.callsign, args.lat, args.lon, args.alt_ft,
                 args.gs_kt, args.track_deg, args.vrate_fpm, args.squawk)
            con.execute("INSERT OR REPLACE INTO autorizadas VALUES (?,?,?,?,?,?,?,?,?)", v)
            con.commit()
            print("OK:", v)
        elif args.cmd == "remover":
            n = con.execute("DELETE FROM autorizadas WHERE upper(icao24)=?",
                            (args.icao24.upper(),)).rowcount
            con.commit()
            print(f"{n} removida(s)")
        else:
            for l in reversed(con.execute("SELECT * FROM adsb ORDER BY id DESC LIMIT ?",
                                          (args.n,)).fetchall()):
                print(" | ".join(str(l[c]) for c in cls.COLUNAS_ADSB))
        con.close()


# =============================================================================
# Enlace serial com o OBC
# =============================================================================
class EnlaceSerial:
    """
    Serial resiliente: reabre sozinha se a porta cair e escreve por thread
    própria (a decodificação nunca espera a serial). Respostas a comandos têm
    prioridade sobre os AC; a fila de AC é limitada e descarta os mais antigos.
    """

    @staticmethod
    def _abrir_pyserial(porta, baud):
        import serial
        return serial.Serial(porta, baud, timeout=1)

    def __init__(self, portas, baud, metricas, fabrica=None):
        self._portas = portas
        self._baud = baud
        self._metricas = metricas
        self._fabrica = fabrica or self._abrir_pyserial
        self._ser = None
        self._ultima_tentativa = 0.0
        self._m = threading.Lock()
        self._ctrl = collections.deque()
        self._dados = collections.deque()
        self._tem = threading.Event()
        threading.Thread(target=self._loop_tx, name="serial-tx", daemon=True).start()

    def _abrir(self):
        if time.monotonic() - self._ultima_tentativa < 2.0:
            return False
        self._ultima_tentativa = time.monotonic()
        for porta in self._portas:
            try:
                self._ser = self._fabrica(porta, self._baud)
                log.info("serial aberta: %s @ %d", porta, self._baud)
                return True
            except Exception as e:                          # noqa: BLE001
                log.warning("não abriu %s: %s", porta, e)
        return False

    def _fechar(self):
        ser, self._ser = self._ser, None
        if ser is not None:
            try:
                ser.close()
            except Exception:                               # noqa: BLE001
                pass

    def ler_linha(self):
        """Uma linha recebida ('' se estourou o timeout ou a porta está fora)."""
        if self._ser is None and not self._abrir():
            time.sleep(0.5)
            return ""
        try:
            return self._ser.readline().decode("ascii", errors="ignore")
        except Exception as e:                              # noqa: BLE001
            log.error("erro de leitura na serial: %s", e)
            self._fechar()
            return ""

    def enviar(self, tipo, *campos):
        """Mensagem de controle (ACK/NAK/EVT/STATUS/QRES): nunca descartada."""
        with self._m:
            self._ctrl.append((Protocolo.montar(tipo, *campos), None))
        self._tem.set()

    def enviar_dado(self, linha, t_rx_mono):
        """Registro AC: se a fila encher, descarta o mais antigo."""
        with self._m:
            if len(self._dados) >= Config.TX_FILA_DADOS_MAX:
                self._dados.popleft()
                self._metricas.inc("descartados_tx")
            self._dados.append((linha, t_rx_mono))
        self._tem.set()

    def _loop_tx(self):
        while True:
            self._tem.wait()
            with self._m:
                if self._ctrl:
                    item = self._ctrl.popleft()
                elif self._dados:
                    item = self._dados.popleft()
                else:
                    item = None
                if not self._ctrl and not self._dados:
                    self._tem.clear()
            if item is None or self._ser is None:
                continue
            linha, t_rx = item
            try:
                self._ser.write(linha.encode("ascii"))
                self._ser.flush()
                if t_rx is not None:
                    self._metricas.inc("enviados")
                    self._metricas.latencia(time.monotonic() - t_rx)
            except Exception as e:                          # noqa: BLE001
                log.error("erro de escrita na serial: %s", e)
                self._fechar()


# =============================================================================
# Fontes de dados ADS-B  (classe base + 3 implementações)
# =============================================================================
class FonteADSB(abc.ABC):
    """
    Base: guarda o estado acumulado por aeronave e decide quando emitir um
    registro. Subclasses só implementam _executar() (a thread da fonte), lendo
    sua origem e chamando _atualizar(icao, campos, posicao_nova).

    ao_registro(dict): icao24, lat, lon, alt_ft, gs_kt, track_deg, vrate_fpm,
    callsign, ts (época, com o ajuste do TIME), ts_mono, t_rx_mono.
    """

    nome = "base"

    def __init__(self, ao_registro, metricas, agora=time.time, ao_evento=None):
        self.ao_registro = ao_registro
        self.metricas = metricas
        self.agora = agora
        self.ao_evento = ao_evento or (lambda *a: None)
        self._parar = threading.Event()
        self._thread = None
        self._estado = {}

    # ---- ciclo de vida ----
    def iniciar(self):
        self._parar.clear()
        self._thread = threading.Thread(target=self._executar, name=f"fonte-{self.nome}", daemon=True)
        self._thread.start()

    def parar(self, timeout=8.0):
        self._parar.set()
        self._interromper()
        if self._thread is not None:
            self._thread.join(timeout)

    def _interromper(self):
        """Gancho: acorda a fonte bloqueada (opcional)."""

    @abc.abstractmethod
    def _executar(self):
        """Corpo da thread da fonte; deve terminar quando self._parar for setado."""

    # ---- lógica comum ----
    def _atualizar(self, icao, campos, posicao_nova):
        a = self._estado.setdefault(icao, {"ultimo_ac": 0.0})
        a["visto"] = time.monotonic()
        for k, v in campos.items():
            if v is not None:
                a[k] = v
        if posicao_nova:
            self._emitir(icao, a)
        if len(self._estado) >= 64:
            self._limpar_antigas()

    def _emitir(self, icao, a):
        """Emite registro se tiver os 5 parâmetros e respeitar o intervalo mínimo."""
        if not all(k in a for k in ("lat", "lon", "alt_ft", "gs_kt")):
            return
        t = time.monotonic()
        if t - a["ultimo_ac"] < Config.AC_INTERVALO_MIN_S:
            return
        a["ultimo_ac"] = t
        self.metricas.aeronave(icao)
        self.metricas.inc("registros")
        trk, vr = a.get("track_deg"), a.get("vrate_fpm")
        self.ao_registro({
            "icao24": icao.upper(),
            "lat": round(a["lat"], 5),
            "lon": round(a["lon"], 5),
            "alt_ft": int(round(a["alt_ft"])),
            "gs_kt": int(round(a["gs_kt"])),
            "track_deg": int(round(trk)) if trk is not None else None,
            "vrate_fpm": int(round(vr)) if vr is not None else None,
            "callsign": a.get("callsign"),
            "ts": self.agora(),
            "ts_mono": t,
            "t_rx_mono": t,
        })

    def _limpar_antigas(self):
        agora = time.monotonic()
        for icao in [i for i, a in self._estado.items()
                     if agora - a["visto"] > Config.AERONAVE_TIMEOUT_S]:
            del self._estado[icao]


class FonteDump1090(FonteADSB):
    """
    dump1090-fa / dump1090 / readsb em subprocesso (C: RTL-SDR, demodulação PPM,
    CRC e CPR), lido pela porta SBS (BaseStation, TCP em 127.0.0.1). O Python só
    interpreta linhas CSV:  MSG,<tipo>,,,<icao>,,,,,,<callsign>,<alt>,<gs>,<trk>,<lat>,<lon>,<vrate>,...
    Cada aeronave é carimbada aqui (RF04) quando a linha chega.
    """

    nome = "dump1090"
    CANDIDATOS = ("dump1090-fa", "dump1090", "readsb")

    def __init__(self, *a, **kw):
        super().__init__(*a, **kw)
        self._proc = None
        self._log_err = os.devnull

    @classmethod
    def localizar_binario(cls):
        if Config.DUMP1090_COMANDO:
            return Config.DUMP1090_COMANDO[0]
        if Config.DUMP1090_BIN:
            return shutil.which(Config.DUMP1090_BIN) or (
                Config.DUMP1090_BIN if os.path.exists(Config.DUMP1090_BIN) else None)
        for nome in cls.CANDIDATOS:
            p = shutil.which(nome) or (f"/usr/local/bin/{nome}"
                                       if os.path.exists(f"/usr/local/bin/{nome}") else None)
            if p:
                return p
        return None

    @classmethod
    def _comando(cls, binario):
        if Config.DUMP1090_COMANDO:
            return list(Config.DUMP1090_COMANDO)
        cmd = [binario]
        if os.path.basename(binario).startswith("readsb"):
            cmd += ["--device-type", "rtlsdr"]
        cmd += ["--gain", str(Config.SDR_GAIN), "--freq", str(int(Config.SDR_FREQ_HZ)),
                "--net", "--net-sbs-port", str(Config.SBS_PORTA),
                "--net-bind-address", Config.SBS_HOST, "--quiet"]
        return cmd

    # ---- subprocesso ----
    def _lancar(self):
        binario = self.localizar_binario()
        if binario is None:
            raise RuntimeError("dump1090 não encontrado (rode instalacao/instalar_ubuntu.py)")
        cmd = self._comando(binario)
        log.info("iniciando: %s", " ".join(cmd))
        os.makedirs(Config.PASTA_RAM, exist_ok=True)
        self._log_err = os.path.join(Config.PASTA_RAM, "dump1090.log")
        with open(self._log_err, "wb") as err:
            return subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=err)

    def _cauda_erro(self):
        try:
            with open(self._log_err, "rb") as f:
                return f.read()[-300:].decode("utf8", "ignore").strip().replace("\n", " | ")
        except OSError:
            return ""

    @staticmethod
    def _matar(proc):
        if proc is None or proc.poll() is not None:
            return
        proc.terminate()
        try:
            proc.wait(3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    def _conectar(self, proc):
        """Espera a porta SBS abrir (o dump1090 leva 1–2 s para abrir o dongle)."""
        limite = time.monotonic() + Config.SDR_TIMEOUT_START_S
        while not self._parar.is_set():
            if proc.poll() is not None:
                raise RuntimeError(f"dump1090 saiu ({proc.returncode}): {self._cauda_erro()}")
            try:
                return socket.create_connection((Config.SBS_HOST, Config.SBS_PORTA), timeout=1)
            except OSError:
                if time.monotonic() > limite:
                    raise RuntimeError("porta SBS não abriu: " + self._cauda_erro())
                self._parar.wait(0.3)
        return None

    # ---- thread ----
    def _executar(self):
        espera = 1.0
        while not self._parar.is_set():
            proc = sock = None
            try:
                proc = self._proc = self._lancar()
                sock = self._conectar(proc)
                if sock is None:
                    break
                self.ao_evento("SDR_OK")
                espera = 1.0
                self._ler(sock, proc)
            except Exception as e:                          # noqa: BLE001
                log.error("fonte dump1090: %s", e)
                self.ao_evento("SDR_ERR", str(e).replace(",", ";")[:60])
            finally:
                if sock is not None:
                    sock.close()
                self._matar(proc)
                self._proc = None
            if not self._parar.is_set():
                self._parar.wait(espera)
                espera = min(espera * 2, 10.0)

    def _ler(self, sock, proc):
        sock.settimeout(1.0)
        buf = b""
        while not self._parar.is_set():
            try:
                dados = sock.recv(65536)
            except socket.timeout:
                dados = None
            if dados == b"":
                raise RuntimeError("conexão SBS fechada")
            if dados:
                buf += dados
                *linhas, buf = buf.split(b"\n")
                for l in linhas:
                    self._processar_linha(l.decode("ascii", "ignore"))
            if proc.poll() is not None:
                raise RuntimeError(f"dump1090 saiu ({proc.returncode}): {self._cauda_erro()}")

    def _processar_linha(self, linha):
        p = linha.rstrip("\r").split(",")
        if len(p) < 17 or p[0] != "MSG":
            return
        icao = p[4].strip().upper()
        if not icao:
            return
        self.metricas.inc("rx")                 # com dump1090 só chegam mensagens já validadas
        self.metricas.inc("validas")
        if p[1] in ("1", "2", "3", "4"):
            self.metricas.inc("df17_18")

        def num(i):
            try:
                return float(p[i])
            except (ValueError, IndexError):
                return None

        lat, lon = num(14), num(15)
        if lat is not None and lon is not None and not (-90 <= lat <= 90 and -180 <= lon <= 180):
            lat = lon = None
        self._atualizar(icao, {
            "callsign": p[10].strip() or None,
            "alt_ft": num(11), "gs_kt": num(12), "track_deg": num(13),
            "lat": lat, "lon": lon, "vrate_fpm": num(16),
        }, posicao_nova=(p[1] == "3" and lat is not None and lon is not None))


class FontePyModeS(FonteADSB):
    """
    Fallback em Python puro: RtlReader do pyModeS 2.x (demodula I/Q com numpy).
    Bem mais pesado em CPU/RAM que o dump1090 — use só se ele não estiver instalado.
    """

    nome = "pymodes"

    def __init__(self, *a, **kw):
        super().__init__(*a, **kw)
        self._leitor = None
        self._cpr = {}

    def _interromper(self):
        leitor = self._leitor
        if leitor is not None:
            try:
                leitor.sdr.cancel_read_async()              # faz leitor.run() retornar
            except Exception as e:                          # noqa: BLE001
                log.warning("cancel_read_async falhou: %s", e)

    def _criar_leitor(self):
        import pyModeS as pms
        from pyModeS.extra.rtlreader import RtlReader

        fonte = self

        class Leitor(RtlReader):
            def handle_messages(self, messages):
                fonte._processar(pms, messages)

        leitor = Leitor()
        leitor.sdr.center_freq = Config.SDR_FREQ_HZ
        leitor.sdr.sample_rate = Config.SDR_SAMPLE_RATE
        leitor.sdr.gain = Config.SDR_GAIN
        return leitor

    def _executar(self):
        espera = 1.0
        while not self._parar.is_set():
            leitor = None
            try:
                leitor = self._leitor = self._criar_leitor()
                self.ao_evento("SDR_OK")
                espera = 1.0
                while not self._parar.is_set():
                    try:
                        leitor.run()                        # bloqueia até cancel_read_async()
                    except ValueError as e:
                        log.debug("buffer sem pulsos (%s) — reiniciando leitura", e)
            except Exception as e:                          # noqa: BLE001
                log.error("fonte pyModeS: %s", e)
                self.ao_evento("SDR_ERR", str(e).replace(",", ";")[:60])
                self._parar.wait(espera)
                espera = min(espera * 2, 10.0)
            finally:
                self._leitor = None
                if leitor is not None:
                    try:
                        leitor.sdr.close()
                    except Exception:                       # noqa: BLE001
                        pass

    def _processar(self, pms, messages):
        """Roda na thread do SDR: só decodifica e enfileira."""
        for msg, ts in messages:
            self.metricas.inc("rx")
            try:
                if len(msg) != 28 or pms.df(msg) not in (17, 18):
                    continue
                self.metricas.inc("df17_18")
                if pms.crc(msg) != 0:
                    self.metricas.inc("crc_falha")
                    continue
                icao = pms.icao(msg)
                if not icao:
                    continue
                self.metricas.inc("validas")
                self._tratar(pms, msg, ts, icao)
            except Exception as e:                          # noqa: BLE001
                log.debug("mensagem descartada (%s): %s", msg, e)

    def _tratar(self, pms, msg, ts, icao):
        c = self._cpr.setdefault(icao, {})
        tc = pms.adsb.typecode(msg)
        campos, nova = {}, False
        if 1 <= tc <= 4:
            campos["callsign"] = pms.adsb.callsign(msg).strip("_")
        elif 9 <= tc <= 18:
            if pms.adsb.oe_flag(msg) == 0:
                c["even"], c["even_ts"] = msg, ts
            else:
                c["odd"], c["odd_ts"] = msg, ts
            campos["alt_ft"] = pms.adsb.altitude(msg)
            if ("even" in c and "odd" in c
                    and abs(c["even_ts"] - c["odd_ts"]) <= Config.PAR_IMPAR_MAX_S):
                pos = pms.adsb.position(c["even"], c["odd"], c["even_ts"], c["odd_ts"])
                if pos and -90 <= pos[0] <= 90 and -180 <= pos[1] <= 180:
                    campos["lat"], campos["lon"] = pos
                    nova = True
        elif tc == 19:
            v = pms.adsb.velocity(msg)
            if v:
                campos["gs_kt"], campos["track_deg"], campos["vrate_fpm"], _ = v
        self._atualizar(icao, campos, nova)


class FonteSimulada(FonteADSB):
    """20 aeronaves sintéticas em linha reta — teste de bancada sem antena."""

    nome = "sim"

    def _executar(self, n_aeronaves=20):
        rnd = random.Random(2026)
        avioes = [{
            "icao": f"5{i:02X}A{i:02X}", "callsign": f"SIM{i:04d}",
            "lat": -22.9 + rnd.uniform(-0.4, 0.4), "lon": -43.2 + rnd.uniform(-0.4, 0.4),
            "alt_ft": rnd.choice([28000, 32000, 35000, 38000, 41000]),
            "gs_kt": rnd.randint(380, 480), "track_deg": rnd.randint(0, 359), "vrate_fpm": 0,
        } for i in range(n_aeronaves)]
        self.ao_evento("SDR_OK")
        dt = 0.5
        while not self._parar.wait(dt):
            for a in avioes:
                passo = a["gs_kt"] / 3600.0 / 60.0 * dt         # kt -> graus/s (aprox.)
                a["lat"] += passo * math.cos(math.radians(a["track_deg"]))
                a["lon"] += passo * math.sin(math.radians(a["track_deg"]))
                self.metricas.inc("rx", 3)
                self.metricas.inc("df17_18", 3)
                self.metricas.inc("validas", 3)
                self._atualizar(a["icao"], {k: a[k] for k in (
                    "callsign", "lat", "lon", "alt_ft", "gs_kt", "track_deg", "vrate_fpm")}, True)


def criar_fonte(nome, *args, **kwargs):
    """Fábrica: 'auto' usa dump1090 se estiver instalado, senão pyModeS."""
    if nome == "auto":
        nome = "dump1090" if FonteDump1090.localizar_binario() else "pymodes"
        log.info("fonte ADS-B (auto): %s", nome)
    classes = {c.nome: c for c in (FonteDump1090, FontePyModeS, FonteSimulada)}
    if nome not in classes:
        raise ValueError(f"fonte desconhecida: {nome} (use auto|dump1090|pymodes|sim)")
    return classes[nome](*args, **kwargs)


# =============================================================================
# Missão: máquina de estados + comandos
# =============================================================================
def linha_ac(r):
    return Protocolo.montar("AC", r["seq"], f"{r['ts']:.3f}", r["icao24"],
                            f"{r['lat']:.5f}", f"{r['lon']:.5f}", r["alt_ft"], r["gs_kt"])


def temperatura_cpu():
    try:
        with open(Config.ARQUIVO_TEMP_CPU) as f:
            return round(int(f.read().strip()) / 1000.0, 1)
    except Exception:                                       # noqa: BLE001
        return ""


class Missao:
    def __init__(self, enlace, banco, metricas, fonte="auto"):
        self.enlace = enlace
        self.banco = banco
        self.metricas = metricas
        self.fonte_nome = fonte
        self.estado = "IDLE"
        self.modo = 0               # 0 = grava e transmite AC; 1 = só grava (DUMP depois)
        self.sessao = 0
        self.seq = 0
        self._t0 = time.monotonic()
        self._offset = 0.0          # ajuste de relógio definido pelo OBC (TIME)
        self._lock = threading.RLock()
        self._parar = threading.Event()
        self._threads = []
        self._fila = queue.Queue(maxsize=Config.FILA_REGISTROS_MAX)
        self._fonte = None

    def agora(self):
        return time.time() + self._offset

    # ---- ponte fonte -> escritor ----
    def _ao_registro(self, r):
        try:
            self._fila.put_nowait(r)
        except queue.Full:
            self.metricas.inc("descartados_fila")

    def _ao_evento(self, nome, *campos):
        self.enlace.enviar("EVT", nome, *campos)

    # ---- ciclo da missão ----
    def iniciar(self, duracao=None):
        with self._lock:
            if self.estado == "RUNNING":
                return False
            duracao = Config.MISSAO_DURACAO_S if duracao is None else duracao
            self.metricas.zerar()
            self.sessao = self.banco.nova_sessao()
            self.seq = 0
            self._parar.clear()
            self._fonte = criar_fonte(self.fonte_nome, self._ao_registro, self.metricas,
                                      agora=self.agora, ao_evento=self._ao_evento)
            self._threads = [
                threading.Thread(target=self._escritor, name="escritor", daemon=True),
                threading.Thread(target=self._flusher, name="flush", daemon=True),
            ]
            if duracao > 0:
                self._threads.append(threading.Thread(
                    target=self._temporizador, args=(duracao,), name="timer", daemon=True))
            for t in self._threads:
                t.start()
            self._fonte.iniciar()
            self.estado = "RUNNING"
            log.info("missão INICIADA (sessão %d, fonte %s, duração %ss)",
                     self.sessao, self._fonte.nome, duracao or "livre")
            return True

    def parar(self, motivo="STOP"):
        with self._lock:
            if self.estado != "RUNNING":
                return False
            self._parar.set()
            self._fonte.parar()
            atual = threading.current_thread()
            for t in self._threads:
                if t is not atual:
                    t.join(timeout=10)
            try:
                self.banco.flush()
            except Exception:                               # noqa: BLE001
                log.exception("flush final falhou")
            self.estado = "IDLE"
            log.info("missão ENCERRADA (%s): %s", motivo, self.metricas.resumo())
            return True

    def _temporizador(self, duracao):
        if not self._parar.wait(duracao) and self.parar("DONE"):
            self.enlace.enviar("EVT", "DONE", self.sessao)

    def _escritor(self):
        """Grava no RAM disk e (modo 0) manda AC pela serial."""
        while True:
            try:
                lote = [self._fila.get(timeout=0.5)]
            except queue.Empty:
                if self._parar.is_set():
                    break
                continue
            while len(lote) < 50:
                try:
                    lote.append(self._fila.get_nowait())
                except queue.Empty:
                    break
            for r in lote:
                self.seq += 1
                r["seq"], r["sessao"] = self.seq, self.sessao
            try:
                self.banco.inserir_lote(lote)
            except Exception:                               # noqa: BLE001
                log.exception("falha ao gravar no banco")
            if self.modo == 0:
                for r in lote:
                    self.enlace.enviar_dado(linha_ac(r), r["t_rx_mono"])

    def _flusher(self):
        while not self._parar.wait(Config.FLUSH_INTERVALO_S):
            try:
                self.banco.flush()
            except Exception:                               # noqa: BLE001
                log.exception("flush RAM->SD falhou")

    # ---- comandos (TC) ----
    def tratar_linha(self, linha):
        msg = Protocolo.interpretar(linha, Config.EXIGIR_CHECKSUM_TC)
        if msg is None:
            if linha.strip():
                log.debug("linha ignorada (lixo/checksum): %r", linha.strip()[:80])
            return
        tipo, campos = msg
        log.info("TC %s %s", tipo, campos)
        try:
            self._despachar(tipo, campos)
        except Exception:                                   # noqa: BLE001
            log.exception("erro tratando %s", tipo)
            self.enlace.enviar("NAK", tipo, "ERRO_INTERNO")

    def _despachar(self, tipo, c):
        e = self.enlace
        if tipo == "PING":
            e.enviar("ACK", "PING")
        elif tipo == "START":
            if self.iniciar(float(c[0]) if c and c[0] else None):
                e.enviar("ACK", "START", self.sessao)
            else:
                e.enviar("NAK", "START", "JA_EM_EXECUCAO")
        elif tipo == "STOP":
            if self.parar("STOP"):
                e.enviar("ACK", "STOP")
            else:
                e.enviar("NAK", "STOP", "OCIOSO")
        elif tipo == "STATUS":
            m = self.metricas.resumo()
            e.enviar("STATUS", self.estado, self.modo, int(time.monotonic() - self._t0),
                     temperatura_cpu(), m["rx"], m["validas"], m["aeronaves"], m["enviados"],
                     m["lat_media_ms"], m["lat_max_ms"])
        elif tipo == "TIME":
            self._offset = float(c[0]) - time.time()
            e.enviar("ACK", "TIME")
        elif tipo == "MODE":
            if c and c[0] in ("0", "1"):
                self.modo = int(c[0])
                e.enviar("ACK", "MODE", self.modo)
            else:
                e.enviar("NAK", "MODE", "ARGUMENTO")
        elif tipo == "DUMP":
            n = min(int(c[0]) if c and c[0] else Config.DUMP_MAX, Config.DUMP_MAX)
            regs = self.banco.ultimos(n)
            for r in regs:
                e.enviar_dado(linha_ac(r), None)
            e.enviar("ACK", "DUMP", len(regs))
        elif tipo == "QRY":
            self._consultar(c)
        else:
            e.enviar("NAK", tipo, "DESCONHECIDO")

    def _consultar(self, c):
        """QRY,<lat>,<lon>[,<icao>]: a aeronave consta em 'autorizadas'? (FOUND = não clandestina)"""
        try:
            lat, lon = float(c[0]), float(c[1])
        except (IndexError, ValueError):
            self.enlace.enviar("NAK", "QRY", "ARGUMENTO")
            return
        r = self.banco.consultar_autorizada(lat, lon, c[2] if len(c) > 2 and c[2] else None)
        if r is None:
            self.enlace.enviar("QRES", "NONE")
        else:
            self.enlace.enviar("QRES", "FOUND", r["icao24"], r["callsign"], r["lat"], r["lon"],
                               r["alt_ft"], r["gs_kt"], r["track_deg"], r["vrate_fpm"], r["squawk"])


# =============================================================================
# main
# =============================================================================
def _escolher_pasta(pasta):
    pasta = os.path.expanduser(pasta)
    try:
        os.makedirs(pasta, exist_ok=True)
        if os.access(pasta, os.W_OK):
            return pasta
    except OSError:
        pass
    alt = os.path.expanduser(Config.PASTA_SD_FALLBACK)
    os.makedirs(alt, exist_ok=True)
    print(f"AVISO: {pasta} não é gravável, usando {alt}", file=sys.stderr)
    return alt


def _configurar_log(pasta_sd):
    fmt = logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s")
    raiz = logging.getLogger()
    raiz.setLevel(logging.INFO)
    h = logging.StreamHandler()                             # journald, sob o systemd
    h.setFormatter(fmt)
    raiz.addHandler(h)
    try:
        arq = logging.handlers.RotatingFileHandler(
            os.path.join(pasta_sd, "orange.log"), maxBytes=1_000_000, backupCount=3)
        arq.setFormatter(fmt)
        raiz.addHandler(arq)
    except OSError:
        pass


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv and argv[0] == "banco":
        return BancoMissao.cli(argv[1:])
    if argv and argv[0] == "frame":
        print(Protocolo.montar(*argv[1].split(",")), end="")
        return

    ap = argparse.ArgumentParser(description="Missão ADS-B — Orange Pi Zero 3")
    ap.add_argument("--porta", help=f"serial (padrão {Config.PORTA_SERIAL})")
    ap.add_argument("--baud", type=int, default=Config.BAUD)
    ap.add_argument("--fonte", default=Config.FONTE, choices=("auto", "dump1090", "pymodes", "sim"))
    ap.add_argument("--sim", action="store_true", help="atalho para --fonte sim")
    ap.add_argument("--pasta-sd", default=Config.PASTA_SD)
    ap.add_argument("--pasta-ram", default=Config.PASTA_RAM)
    ap.add_argument("--exigir-checksum", action="store_true", help="ignora TC sem '*XX' válido")
    ap.add_argument("--checksum-tx", action="store_true", help="respostas com '*XX' no fim")
    ap.add_argument("--endereco", default=Config.ENDERECO_ORANGE, help="endereço do Orange no OBC")
    args = ap.parse_args(argv)

    Config.EXIGIR_CHECKSUM_TC = args.exigir_checksum or Config.EXIGIR_CHECKSUM_TC
    Config.CHECKSUM_TX = args.checksum_tx or Config.CHECKSUM_TX
    Config.ENDERECO_ORANGE = args.endereco
    Config.PASTA_RAM = os.path.expanduser(args.pasta_ram)
    pasta_sd = _escolher_pasta(args.pasta_sd)
    _configurar_log(pasta_sd)
    portas = [args.porta] if args.porta else [Config.PORTA_SERIAL] + Config.PORTAS_ALTERNATIVAS

    metricas = Metricas()
    banco = BancoMissao(pasta_sd, Config.PASTA_RAM)
    enlace = EnlaceSerial(portas, args.baud, metricas)
    missao = Missao(enlace, banco, metricas, fonte="sim" if args.sim else args.fonte)

    if hasattr(signal, "SIGTERM"):                          # systemd stop -> encerra limpo
        signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(KeyboardInterrupt()))
    log.info("Orange Pi pronto — IDLE, aguardando comandos (fonte=%s)", missao.fonte_nome)
    enlace.enviar("EVT", "BOOT")
    try:
        while True:
            missao.tratar_linha(enlace.ler_linha())
    except KeyboardInterrupt:
        log.info("encerrando")
    finally:
        missao.parar("SAIDA")
        banco.fechar()


if __name__ == "__main__":
    main()
