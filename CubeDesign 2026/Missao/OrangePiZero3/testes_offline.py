#!/usr/bin/env python3
"""
testes_offline.py — testa o adsb_cubesat.py SEM RTL-SDR e SEM serial física.
Usa uma porta serial falsa, um "dump1090" falso (servidor SBS local) e a fonte
simulada. Roda em qualquer PC e no Orange Pi:

    python3 testes_offline.py
"""

import os
import socket
import sys
import tempfile
import time

import adsb_cubesat as ac
from adsb_cubesat import Config, Protocolo

PORTA_SBS_TESTE = 30993


# ----------------------------------------------------------------------------
# "dump1090" falso: servidor SBS que emite 20 aeronaves (roda como subprocesso)
# ----------------------------------------------------------------------------
def dump1090_falso(porta):
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", porta))
    srv.listen(1)
    cli, _ = srv.accept()
    passo = 0
    try:
        while True:
            for i in range(20):
                icao = f"A{i:05X}"
                lat, lon = -22.9 + 0.001 * passo + i * 0.01, -43.2 + 0.001 * passo
                cli.sendall((f"MSG,1,1,1,{icao},1,2026/01/01,00:00:00.000,2026/01/01,00:00:00.000,"
                             f"TST{i:04d},,,,,,,,0,0,0,0\r\n"
                             f"MSG,4,1,1,{icao},1,2026/01/01,00:00:00.000,2026/01/01,00:00:00.000,"
                             f",,450,90,,,0,,0,0,0,0\r\n"
                             f"MSG,3,1,1,{icao},1,2026/01/01,00:00:00.000,2026/01/01,00:00:00.000,"
                             f",35000,,,{lat:.5f},{lon:.5f},,,0,0,0,0\r\n").encode())
            passo += 1
            time.sleep(0.2)
    except OSError:
        pass


class PortaFalsa:
    def __init__(self):
        self.escritas = []

    def readline(self):
        time.sleep(0.05)
        return b""

    def write(self, dados):
        self.escritas.append(dados.decode())

    def flush(self):
        pass

    def close(self):
        pass


def saidas(porta, tipo=None):
    out = []
    for l in porta.escritas:
        t, c = Protocolo.interpretar(l)           # toda saída tem de ter checksum válido
        if tipo is None or t == tipo:
            out.append((t, c))
    return out


def main():
    Config.AC_INTERVALO_MIN_S = 0.2
    Config.FLUSH_INTERVALO_S = 0.3

    # ---- protocolo ----
    linha = Protocolo.montar("QRY", -22.88, -43.3, "7788FF")
    assert linha == "2:QRY,-22.88,-43.3,7788FF\n", linha
    assert Protocolo.interpretar(linha) == ("QRY", ["-22.88", "-43.3", "7788FF"])
    assert Protocolo.interpretar("2: start ,600\r\n") == ("START", ["600"])      # espaços/caixa
    assert Protocolo.interpretar("1:PING") is None                             # outro subsistema
    assert Protocolo.interpretar("lixo\x00boot 2:PING") is None                # não começa com 2:
    assert Protocolo.interpretar("PING") is None
    assert Protocolo.interpretar("2:PING*00", exigir_checksum=False) is None   # checksum errado
    cs = f"{Protocolo.checksum('PING'):02X}"
    assert Protocolo.interpretar(f"2:PING*{cs}") == ("PING", [])
    assert Protocolo.interpretar("2:PING", exigir_checksum=True) is None       # exigido e ausente
    Config.CHECKSUM_TX = True
    assert Protocolo.montar("PING") == f"2:PING*{cs}\n"
    Config.CHECKSUM_TX = False
    print("protocolo OK")

    with tempfile.TemporaryDirectory() as tmp:
        Config.PASTA_RAM = os.path.join(tmp, "ram")
        banco = ac.BancoMissao(os.path.join(tmp, "sd"), Config.PASTA_RAM)

        # ---- banco: QRY ----
        assert banco.consultar_autorizada(-22.8800, -43.3000)["icao24"] == "7788FF"
        assert banco.consultar_autorizada(-23.0001, -43.1001)["icao24"] == "33CC99"
        assert banco.consultar_autorizada(-22.9068, -43.1729) is None            # A1B2C3: clandestina
        assert banco.consultar_autorizada(-22.8800, -43.3000, "33CC99") is None  # ICAO não bate
        assert banco.consultar_autorizada(-22.8800, -43.3000, "7788ff") is not None
        print("banco/QRY OK")

        # ---- parser SBS isolado ----
        regs = []
        metricas = ac.Metricas()
        fonte = ac.FonteDump1090(regs.append, metricas)
        fonte._processar_linha("MSG,1,1,1,ABC123,1,d,t,d,t,GOL1234,,,,,,,,0,0,0,0")
        fonte._processar_linha("MSG,4,1,1,ABC123,1,d,t,d,t,,,455,181,,,-64,,0,0,0,0")
        fonte._processar_linha("MSG,3,1,1,ABC123,1,d,t,d,t,,36000,,,-22.88123,-43.29876,,,0,0,0,0")
        fonte._processar_linha("lixo sem formato")
        fonte._processar_linha("MSG,3,1,1,ZZZ999,1,d,t,d,t,,36000,,,,,,,0,0,0,0")     # sem posição
        assert len(regs) == 1, regs
        r = regs[0]
        assert (r["icao24"], r["callsign"], r["alt_ft"], r["gs_kt"], r["track_deg"], r["vrate_fpm"]) == \
               ("ABC123", "GOL1234", 36000, 455, 181, -64), r
        assert r["lat"] == -22.88123 and r["lon"] == -43.29876
        print("parser SBS OK")

        # ---- serial falsa + Missao ----
        def montar_missao(fonte_nome):
            m = ac.Metricas()
            porta = PortaFalsa()
            enlace = ac.EnlaceSerial(["fake"], 115200, m, fabrica=lambda p, b: porta)
            enlace.ler_linha()                                                    # abre a porta
            return ac.Missao(enlace, banco, m, fonte=fonte_nome), porta

        missao, porta = montar_missao("sim")

        def tc(tipo, *campos):
            missao.tratar_linha(Protocolo.montar(tipo, *campos))
            time.sleep(0.15)

        tc("PING")
        tc("QRY", -22.88, -43.30)
        tc("QRY", -22.9068, -43.1729)
        tc("QRY", "abc", "x")
        tc("FOO")
        missao.tratar_linha("1:START,5")                                          # outro subsistema: ignorado
        missao.tratar_linha("START,5")                                            # sem endereço: ignorado
        assert missao.estado == "IDLE"
        assert saidas(porta, "ACK") == [("ACK", ["PING"])]
        q = saidas(porta, "QRES")
        assert q[0][1][:3] == ["FOUND", "7788FF", "TAP0803"] and q[1] == ("QRES", ["NONE"]), q
        assert ("NAK", ["QRY", "ARGUMENTO"]) in saidas(porta, "NAK")
        assert ("NAK", ["FOO", "DESCONHECIDO"]) in saidas(porta, "NAK")
        print("comandos em IDLE OK (QRY funciona sem o SDR)")

        tc("TIME", 1_800_000_000)
        assert abs(missao.agora() - 1_800_000_000) < 2
        tc("MODE", 1)
        tc("MODE", 0)
        tc("START", 3)
        time.sleep(1.5)
        tc("START")                                                               # já rodando
        tc("STATUS")
        assert missao.estado == "RUNNING"
        acs = saidas(porta, "AC")
        assert len(acs) > 20, len(acs)
        seq, ts, icao, lat, lon, alt, vel = acs[0][1]
        assert abs(float(ts) - 1_800_000_000) < 30 and len(icao) == 6
        status = saidas(porta, "STATUS")[-1][1]
        assert status[0] == "RUNNING" and int(status[6]) == 20, status           # 20 aeronaves
        print(f"RUNNING (fonte sim) OK: {len(acs)} AC, STATUS={status}")

        time.sleep(2.5)                                                           # estoura os 3 s
        assert missao.estado == "IDLE"
        assert any(t == "EVT" and c[0] == "DONE" for t, c in saidas(porta))
        n_antes = len(saidas(porta, "AC"))
        tc("DUMP", 5)
        assert len(saidas(porta, "AC")) == n_antes + 5
        assert ("ACK", ["DUMP", "5"]) in saidas(porta, "ACK")
        sd_rows = banco._sd.execute("SELECT count(*) FROM adsb").fetchone()[0]
        assert sd_rows >= len(acs), (sd_rows, len(acs))                           # RAM -> SD persistiu
        print(f"fim por tempo + DUMP + flush RAM->SD OK ({sd_rows} linhas no SD)")

        tc("START", 0)
        time.sleep(0.8)
        tc("MODE", 1)
        antes = len(saidas(porta, "AC"))
        time.sleep(1.0)
        assert len(saidas(porta, "AC")) <= antes + 3                              # modo 1: só grava
        tc("STOP")
        assert missao.estado == "IDLE"
        assert ("ACK", ["STOP"]) in saidas(porta, "ACK")
        print("MODE 1 / STOP OK")

        # ---- fonte dump1090 de ponta a ponta (subprocesso + socket SBS) ----
        Config.SBS_PORTA = PORTA_SBS_TESTE
        Config.DUMP1090_COMANDO = [sys.executable, os.path.abspath(__file__),
                                   "--dump1090-falso", str(PORTA_SBS_TESTE)]
        missao2, porta2 = montar_missao("dump1090")
        missao2.tratar_linha(Protocolo.montar("START", 0))
        time.sleep(3.0)
        missao2.tratar_linha(Protocolo.montar("STATUS"))
        time.sleep(0.2)
        acs2 = saidas(porta2, "AC")
        st = saidas(porta2, "STATUS")[-1][1]
        assert any(t == "EVT" and c == ["SDR_OK"] for t, c in saidas(porta2)), saidas(porta2)[:5]
        assert len(acs2) >= 20 and int(st[6]) == 20, (len(acs2), st)
        assert missao2.parar("teste")
        assert missao2._fonte._proc is None                                       # subprocesso morto
        print(f"fonte dump1090 (SBS/TCP + subprocesso) OK: {len(acs2)} AC, 20 aeronaves")

        banco.fechar()

    print("\nTODOS OS TESTES OFFLINE PASSARAM")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--dump1090-falso":
        dump1090_falso(int(sys.argv[2]))
    else:
        main()
