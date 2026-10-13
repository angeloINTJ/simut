#!/usr/bin/env python3
"""Testa o orçamento de PIO (tools/pio_budget.py): o que cabe nos blocos de cada chip,
quem disputa um pino e se a tabela de programas ainda é a que as fontes dizem.

Rodar: python3 tools/test_pio_budget.py

A tabela nasceu de uma contagem feita em 2026-10-12 (instruções e máquinas de estado de
cada programa que o boot carrega). Este teste a segura de três lados, sem compilar
firmware:
  - a colocação (`place`) contra os conjuntos de recursos de que o projeto já sabe a
    resposta, mais uma varredura das 32 combinações de chaves em cada um dos dois chips
    contra invariantes (nenhum bloco acima de 32 instruções ou 4 máquinas, a soma do que
    coube é a soma dos programas) e um controle negativo: a recusa vem do espaço que
    falta, não do nome. O rádio tenta os blocos como o pico-sdk os tenta, do mais alto
    ao mais baixo, e cai no seguinte quando o primeiro não tem lugar;
  - os pinos: `pin_conflicts`, o INT padrão de cada mostrador e a validação do
    `enc_int_pin`; e a tabela `PINS` contra as máscaras que o `src/simut_config.h` confere
    (SIMUT_PINS_DISPLAY, SIMUT_PINS_ENC, SIMUT_PINS_BUZZER), pedidas ao pré-processador do
    host para cada mostrador do `tools/features.toml`, com e sem buzzer;
  - as fontes: o `.length` de cada `.pio.h` e as instruções do `.pio` do rádio têm de
    ser o que a tabela diz. Onde a fonte não existe o teste a pula e avisa: o job `gates`
    do CI não tem as bibliotecas. O leitor das fontes é provado à parte, em arquivos
    sintéticos, porque é a única prova dele que o CI roda.

O número no início de cada rótulo diz de que grupo a falha é: 1 a 8, os conjuntos de
recursos; 2r, os programas de lista de blocos com a tabela mudada (a recusa, a ordem em que
tentam os blocos, o rádio que cai no seguinte); 9, os limites de todo resultado; 10, os
pinos e o INT; 11, as fontes reais; 12, o controle negativo; 13, o leitor das fontes e a
variável SIMUT_PIO_SOURCES; 14, a tabela de pinos contra o simut_config.h.

O grupo 14 precisa do `cpp` do host, o mesmo do test_pin_masks.py, de onde vêm o
`preprocess` e o `judge`; sem ele o teste falha, não pula: uma tabela de pinos que ninguém
conferiu não é uma tabela conferida.

Variáveis de ambiente (as fontes reais):
  SIMUT_LIBDEPS    <repo>/.pio/libdeps/pico_w_release
  ENC_PIO_HEADER   ~/Documentos/pico-enc28j60/PicoENC28J60/src/enc28j60-spi.pio.h
  CYW43_PIO        ~/.platformio/packages/framework-arduinopico/pico-sdk/src/rp2_common/
                   pico_cyw43_driver/cyw43_bus_pio_spi.pio
  SIMUT_PIO_SOURCES
                   vazio (o padrão): uma fonte que não existe é pulada, com um aviso.
                   `require`: pular vira falha para onewire, dht22, buzzer, cyw43 e o
                   WirePIO do BME280, as fontes que um job que compila o firmware tem (as
                   bibliotecas em .pio/libdeps e o pico-sdk do framework). O enc28j60
                   continua podendo faltar: a biblioteca ainda não é dependência de nenhuma
                   imagem. Qualquer outro valor reprova, para que um erro de digitação não
                   desligue o portão.
                   O grupo 13 prova as duas pontas rodando este mesmo arquivo como processo
                   filho, sem nenhuma fonte, e olhando o código de saída.
"""
import copy
import itertools
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import gen_features as gf  # noqa: E402
import pio_budget as pb  # noqa: E402
import test_pin_masks as tpm  # noqa: E402

casos = 0
falhas = []


def check(cond, msg):
    global casos
    casos += 1
    if not cond:
        falhas.append(msg)


def cfg_de(*chaves):
    """Uma config só com essas chaves ligadas; chave ausente vale desligada."""
    return {k: True for k in chaves}


resultados = []   # (rótulo, chip, resultado) de toda colocação, para o caso 9


def coloca(rotulo, chip, chaves, programas=None):
    r = pb.place(cfg_de(*chaves), chip, programs=programas)
    resultados.append((rotulo, chip, r))
    return r


def uso(r):
    return {b: (v["instr"], v["sm"]) for b, v in r["blocks"].items()}


def tabela_com(nome, **campos):
    """Uma cópia da tabela com o programa `nome` mudado; a original não se toca."""
    t = copy.deepcopy(pb.PROGRAMS)
    t[nome].update(campos)
    return t


def uma_frase(motivo):
    """O motivo de uma recusa é uma frase só: um ponto, no fim, e nenhuma quebra de linha."""
    return motivo.endswith(".") and motivo.count(".") == 1 and "\n" not in motivo


def levanta(fn, *args):
    """A mensagem do ValueError que fn(*args) levanta, ou None se não levantar."""
    try:
        fn(*args)
    except ValueError as e:
        return str(e)
    return None


TABELA_ORIGINAL = copy.deepcopy(pb.PROGRAMS)

# A tabela e a ordem do boot falam dos mesmos programas, e todo bloco que a tabela nomeia
# existe no maior chip.
check(sorted(n for n, _ in pb.ORDER) == sorted(pb.PROGRAMS),
      f"ORDER e PROGRAMS não falam dos mesmos programas: {pb.ORDER} / {sorted(pb.PROGRAMS)}")
nomeados = {b for p in pb.PROGRAMS.values() for b in (p["blocks"] or [])}
check(nomeados <= set(pb.CHIPS["rp2350"]), f"a tabela nomeia um bloco que nenhum chip tem: {nomeados}")

# ---------------------------------------------------------------------------------
# 1 a 8. Os conjuntos de recursos cuja resposta já se sabe.
# ---------------------------------------------------------------------------------
WIRE = ("sound_buzzer", "lan", "sensor_dht22")
RELEASE_PICO_W = ("sound_buzzer", "sensor_ds18b20", "sensor_dht22", "radio")
LAN_E_DOIS_SENSORES = ("sound_buzzer", "lan", "sensor_ds18b20", "sensor_dht22")

# (rótulo, chip, chaves ligadas, cabe?, bloco do enc28j60, uso esperado {bloco: (instr, sm)})
# Numa recusa o uso é o dos blocos como estavam quando a colocação parou.
CASOS = [
    ("1 SIMUT Wire", "rp2040", WIRE, True, "pio0",
     {"pio0": (17, 1), "pio1": (21, 3)}),
    ("2 release do Pico W", "rp2040", RELEASE_PICO_W, True, None,
     {"pio0": (27, 1), "pio1": (27, 4)}),
    ("3 LAN, DS18B20 e DHT22", "rp2040", LAN_E_DOIS_SENSORES, False, None,
     {"pio0": (0, 0), "pio1": (4, 2)}),
    ("4 LAN e DS18B20", "rp2040", ("sound_buzzer", "lan", "sensor_ds18b20"), True, "pio1",
     {"pio0": (27, 1), "pio1": (21, 3)}),
    ("5 rádio, LAN, DS18B20 e DHT22", "rp2040", ("radio", "lan", "sensor_ds18b20", "sensor_dht22"),
     False, None, {"pio0": (0, 0), "pio1": (0, 0)}),
    ("6 rádio, buzzer, DHT22 e LAN", "rp2040", ("radio", "sound_buzzer", "sensor_dht22", "lan"),
     True, "pio0", {"pio0": (17, 1), "pio1": (27, 4)}),
    # O rádio não fica no pio1 como no RP2040: o pico-sdk tenta os blocos do mais alto ao mais
    # baixo (pio_claim_free_sm_and_add_program_for_gpio_range), e no RP2350 o mais alto é o pio2.
    ("7 release no RP2350", "rp2350", RELEASE_PICO_W, True, None,
     {"pio0": (27, 1), "pio1": (21, 3), "pio2": (6, 1)}),
    ("8 LAN, DS18B20 e DHT22 no RP2350", "rp2350", LAN_E_DOIS_SENSORES, True, "pio2",
     {"pio0": (27, 1), "pio1": (21, 3), "pio2": (17, 1)}),
    # O pio2 só entra quando é preciso: o mesmo conjunto do caso 1 fica no pio0.
    ("8b SIMUT Wire no RP2350", "rp2350", WIRE, True, "pio0",
     {"pio0": (17, 1), "pio1": (21, 3), "pio2": (0, 0)}),
]
por_rotulo = {}
for rotulo, chip, chaves, cabe, bloco_enc, usos in CASOS:
    r = coloca(rotulo, chip, chaves)
    por_rotulo[rotulo] = r
    check(r["ok"] is cabe, f"{rotulo}: ok deveria ser {cabe}, é {r['ok']} ({r['reason']})")
    check(r["enc_block"] == bloco_enc, f"{rotulo}: enc_block deveria ser {bloco_enc!r}, é {r['enc_block']!r}")
    check(uso(r) == usos, f"{rotulo}: uso dos blocos (instr, sm) deveria ser {usos}, é {uso(r)}")
    check((r["reason"] == "") is cabe, f"{rotulo}: reason só pode ser vazio quando cabe: {r['reason']!r}")

# A recusa diz o que faltou: quem foi recusado, e por bloco o espaço que havia e o programa
# seguinte que não caberia ali (pio0 deixa o 1-Wire sem lugar, pio1 deixa o DHT22).
motivo = por_rotulo["3 LAN, DS18B20 e DHT22"]["reason"]
for trecho in ("enc28j60 needs 17 instructions and 1 state machine and finds no block",
               "pio0 has 32 instructions and 4 state machines free, but then onewire would not fit",
               "pio1 has 28 instructions and 2 state machines free, but then dht22 would not fit"):
    check(trecho in motivo, f"3: o motivo deveria conter {trecho!r}: {motivo!r}")
motivo = por_rotulo["5 rádio, LAN, DS18B20 e DHT22"]["reason"]
for trecho in ("enc28j60", "pio0 has 32 instructions and 4 state machines free, but then onewire would not fit",
               "pio1 has 32 instructions and 4 state machines free, but then dht22 would not fit"):
    check(trecho in motivo, f"5: o motivo deveria conter {trecho!r}: {motivo!r}")

# ---------------------------------------------------------------------------------
# 12. Controle negativo do caso 3: a recusa é por espaço, não por nome.
# Com o enc28j60 em 15 instruções o conjunto do caso 3 ainda não cabe (pio0: 27 + 15 = 42;
# pio1: 4 + 15 + 17 = 36) e, sem o DHT22, cabe. A fronteira mostra a conta: o enc28j60 vai
# para o pio0 até 5 instruções (27 + 5 = 32, o limite em instruções) e para o pio1 até 11
# (4 + 11 + 17 = 32 instruções e 2 + 1 + 1 = 4 máquinas, os dois limites no ponto).
# ---------------------------------------------------------------------------------
com15 = tabela_com("enc28j60", instr=15)
r = coloca("12 enc28j60 com 15 instruções, conjunto do caso 3", "rp2040", LAN_E_DOIS_SENSORES, com15)
check(not r["ok"], f"12: com o enc28j60 em 15 instruções o conjunto do caso 3 ainda não cabe: {uso(r)}")
check("enc28j60" in r["reason"], f"12: a recusa deveria nomear o enc28j60: {r['reason']!r}")
r = coloca("12 enc28j60 com 15 instruções, sem o DHT22", "rp2040",
           ("sound_buzzer", "lan", "sensor_ds18b20"), com15)
check(r["ok"] and r["enc_block"] == "pio1" and uso(r) == {"pio0": (27, 1), "pio1": (19, 3)},
      f"12: sem o DHT22 o enc28j60 de 15 deveria caber no pio1 com 19 instruções: {r['ok']} {uso(r)}")
fronteira = {}
for n, cabe, bloco_enc in ((5, True, "pio0"), (6, True, "pio1"), (11, True, "pio1"), (12, False, None)):
    r = fronteira[n] = coloca(f"12 enc28j60 com {n} instruções", "rp2040", LAN_E_DOIS_SENSORES,
                              tabela_com("enc28j60", instr=n))
    check(r["ok"] is cabe and r["enc_block"] == bloco_enc,
          f"12: enc28j60 com {n} instruções deveria dar ok={cabe} em {bloco_enc}, deu "
          f"ok={r['ok']} em {r['enc_block']} {uso(r)}")
check(fronteira[5]["blocks"]["pio0"] == {"instr": 32, "sm": 2},
      f"12: com 5 instruções o pio0 deveria fechar em 32 instruções e 2 máquinas: {uso(fronteira[5])}")
check(fronteira[11]["blocks"]["pio1"] == {"instr": 32, "sm": 4},
      f"12: com 11 instruções o pio1 deveria fechar em 32 instruções e 4 máquinas: {uso(fronteira[11])}")

# A recusa de um programa de bloco fixo (regra 2) só se alcança com uma tabela mudada: com
# a de hoje, nenhuma combinação sem a LAN deixa de caber. Ela devolve os blocos como
# estavam e diz, de cada bloco que o programa podia usar, o que sobrava.
r = coloca("2r DHT22 de 30 instruções", "rp2040", ("sound_buzzer", "sensor_dht22"),
           tabela_com("dht22", instr=30))
check(not r["ok"] and uso(r) == {"pio0": (0, 0), "pio1": (4, 2)} and r["enc_block"] is None,
      f"2r: DHT22 de 30 instruções não cabe no pio1 com o buzzer: {r['ok']} {uso(r)}")
check(uma_frase(r["reason"]), f"2r: a recusa deveria ser uma frase só: {r['reason']!r}")
check("dht22 needs 30 instructions and 1 state machine and no block" in r["reason"]
      and "pio1 has 28 instructions and 2 state machines free" in r["reason"],
      f"2r: o motivo deveria dizer o que o DHT22 pede e o que sobra no pio1: {r['reason']!r}")
r = coloca("2r DHT22 com 3 máquinas", "rp2040", ("sound_buzzer", "sensor_dht22"), tabela_com("dht22", sm=3))
check(not r["ok"] and "dht22 needs 17 instructions and 3 state machines" in r["reason"],
      f"2r: o DHT22 com 3 máquinas não cabe no pio1 (sobram 2): {r['ok']} {r['reason']!r}")
r = coloca("2r buzzer de 5 máquinas", "rp2040", ("sound_buzzer",), tabela_com("buzzer", sm=5))
check(not r["ok"] and "pio1 has 32 instructions and 4 state machines free; "
      "pio0 has 32 instructions and 4 state machines free" in r["reason"],
      f"2r: o motivo deveria listar os dois blocos do buzzer, na ordem em que ele os tenta: {r['reason']!r}")
r = coloca("2r buzzer prefere o pio0", "rp2040", ("sound_buzzer",),
           tabela_com("buzzer", blocks=["pio0", "pio1"]))
check(r["ok"] and uso(r) == {"pio0": (4, 2), "pio1": (0, 0)},
      f"2r: com a lista de blocos invertida o buzzer deveria ir para o pio0: {uso(r)}")
# Um bloco que o chip não tem não é erro, é bloco que não existe ali: o RP2040 pula o pio2 da
# lista, o RP2350 o usa, e um programa fixo só nele não tem onde ficar no RP2040.
r = coloca("2r DHT22 no pio2 ou no pio1, RP2040", "rp2040", ("sensor_dht22",),
           tabela_com("dht22", blocks=["pio2", "pio1"]))
check(r["ok"] and uso(r) == {"pio0": (0, 0), "pio1": (17, 1)},
      f"2r: o RP2040 não tem pio2, o DHT22 deveria ir para o pio1: {r['ok']} {uso(r)}")
r = coloca("2r DHT22 no pio2 ou no pio1, RP2350", "rp2350", ("sensor_dht22",),
           tabela_com("dht22", blocks=["pio2", "pio1"]))
check(r["ok"] and uso(r) == {"pio0": (0, 0), "pio1": (0, 0), "pio2": (17, 1)},
      f"2r: o RP2350 tem pio2, o DHT22 deveria ir para ele: {r['ok']} {uso(r)}")
r = coloca("2r DHT22 só no pio2, RP2040", "rp2040", ("sensor_dht22",), tabela_com("dht22", blocks=["pio2"]))
check(not r["ok"] and "none of the blocks it can use (pio2)" in r["reason"] and uma_frase(r["reason"]),
      f"2r: um programa só do pio2 não tem onde ficar no RP2040: {r['ok']} {r['reason']!r}")

# O rádio não escolhe o bloco: o pico-sdk o põe no primeiro, do mais alto ao mais baixo, com uma
# máquina livre e lugar para o programa (cyw43_spi_init chama
# pio_claim_free_sm_and_add_program_for_gpio_range). A lista dele é a dos blocos do chip de trás
# para a frente, e `place` deixa de fora o bloco que o chip não tem. Com a tabela de hoje e sem a
# LAN o pio1 sempre tem lugar para o rádio (o buzzer e o DHT22 deixam 11 instruções e uma
# máquina), então levá-lo ao pio0 ou à recusa pede uma tabela mudada.
check(pb.PROGRAMS["cyw43"]["blocks"] == sorted(pb.CHIPS["rp2350"], reverse=True),
      f"2r: o rádio deveria tentar os blocos do mais alto ao mais baixo, tenta {pb.PROGRAMS['cyw43']['blocks']}")
r = coloca("2r rádio sozinho, RP2040", "rp2040", ("radio",))
check(r["ok"] and uso(r) == {"pio0": (0, 0), "pio1": (6, 1)},
      f"2r: o rádio sozinho no RP2040 deveria ir para o pio1, o mais alto que o chip tem: {r['ok']} {uso(r)}")
r = coloca("2r rádio sozinho, RP2350", "rp2350", ("radio",))
check(r["ok"] and uso(r) == {"pio0": (0, 0), "pio1": (0, 0), "pio2": (6, 1)},
      f"2r: o rádio sozinho no RP2350 deveria ir para o pio2: {r['ok']} {uso(r)}")
# O pio1 sem lugar para o rádio, por instruções (o buzzer e o DHT22 deixam 11 e o rádio pede 12)
# ou por máquinas (as 4 tomadas): nos dois o rádio cai no pio0.
r = coloca("2r rádio de 12 instruções, pio1 com 11 livres", "rp2040", ("sound_buzzer", "sensor_dht22", "radio"),
           tabela_com("cyw43", instr=12))
check(r["ok"] and uso(r) == {"pio0": (12, 1), "pio1": (21, 3)},
      f"2r: sem lugar no pio1 por instruções o rádio deveria cair no pio0: {r['ok']} {uso(r)}")
r = coloca("2r rádio com o pio1 sem máquina livre", "rp2040", ("sound_buzzer", "sensor_dht22", "radio"),
           tabela_com("dht22", sm=2))
check(r["ok"] and uso(r) == {"pio0": (6, 1), "pio1": (21, 4)},
      f"2r: sem máquina livre no pio1 o rádio deveria cair no pio0: {r['ok']} {uso(r)}")
# Nenhum bloco com lugar: a release do RP2040 (o 1-Wire deixa 5 instruções no pio0, o buzzer e o
# DHT22 deixam 11 no pio1) com um rádio de 12 é recusada, e o motivo lista os blocos na ordem em
# que o rádio os tenta. O RP2350 tem o pio2 vazio, e nele o mesmo rádio cabe.
radio12 = tabela_com("cyw43", instr=12)
r = coloca("2r release do RP2040 com rádio de 12 instruções", "rp2040", RELEASE_PICO_W, radio12)
check(not r["ok"] and r["enc_block"] is None and uso(r) == {"pio0": (27, 1), "pio1": (21, 3)},
      f"2r: o rádio de 12 não cabe em nenhum bloco do RP2040, que ficam como estavam: {r['ok']} {uso(r)}")
check(r["reason"].startswith("cyw43 needs 12 instructions and 1 state machine and no block it can use has that much free")
      and "pio1 has 11 instructions and 1 state machine free; pio0 has 5 instructions and 3 state machines free" in r["reason"]
      and uma_frase(r["reason"]),
      f"2r: o motivo deveria nomear o rádio e listar o pio1 e depois o pio0 com o que sobra: {r['reason']!r}")
r = coloca("2r release do RP2350 com rádio de 12 instruções", "rp2350", RELEASE_PICO_W, radio12)
check(r["ok"] and uso(r) == {"pio0": (27, 1), "pio1": (21, 3), "pio2": (12, 1)},
      f"2r: o RP2350 tem o pio2 vazio para o rádio de 12: {r['ok']} {uso(r)}")

# A conta do que vem depois do enc28j60 inclui o que os próprios programas de depois tiram
# uns dos outros. Sem a LAN, o DHT22 (só no pio1) e um rádio de 20 instruções cabem juntos: o
# rádio não cabe no pio1 com ele e cai no pio0. O enc28j60 desfaz esse arranjo: no pio0 deixa o
# rádio sem bloco (17 + 20 > 32), no pio1 deixa o DHT22 sem o dele. Quem recusa é o enc28j60 (a
# simulação) e não o rádio (a regra 2, que só pegaria depois de o enc28j60 já estar no lugar).
radio20 = tabela_com("cyw43", instr=20)
r = coloca("12 rádio de 20 instruções, sem a LAN", "rp2040", ("sensor_dht22", "radio"), radio20)
check(r["ok"] and uso(r) == {"pio0": (20, 1), "pio1": (17, 1)},
      f"12 sem a LAN o DHT22 e o rádio de 20 cabem juntos, o rádio no pio0: {r['ok']} {uso(r)}")
r = coloca("12 rádio de 20 instruções", "rp2040", ("lan", "sensor_dht22", "radio"), radio20)
check(not r["ok"] and r["reason"].startswith("enc28j60 needs")
      and "pio0 has 32 instructions and 4 state machines free, but then cyw43 would not fit" in r["reason"]
      and "pio1 has 32 instructions and 4 state machines free, but then dht22 would not fit" in r["reason"],
      f"12 o enc28j60 tira o lugar do rádio de 20 no pio0 e o do DHT22 no pio1, e quem recusa é ele: "
      f"{r['reason']!r}")
# Um enc28j60 maior que um bloco, ou com mais máquinas do que ele tem, não cabe em lugar nenhum.
for campo, valor in (("instr", 33), ("sm", 5)):
    r = coloca(f"12 enc28j60 com {campo} {valor}", "rp2350", ("lan",),
               tabela_com("enc28j60", **{campo: valor}))
    check(not r["ok"] and r["reason"].count("not enough for it") == 3,
          f"12 enc28j60 com {campo} {valor} não cabe em nenhum dos três blocos: {r['ok']} {r['reason']!r}")

# Chip desconhecido é recusa, não um chip sem blocos.
msg = levanta(pb.place, {}, "rp9999")
check(msg is not None and "rp9999" in msg, f"um chip desconhecido deveria levantar ValueError nomeando o chip: {msg!r}")

# ---------------------------------------------------------------------------------
# Varredura: as 32 combinações de chaves em cada um dos dois chips contra invariantes que não
# dependem de saber a resposta de cada uma.
# ---------------------------------------------------------------------------------
CHAVES = [k for _, k in pb.ORDER]
cabe_em = {}
for chip in pb.CHIPS:
    for bits in itertools.product((False, True), repeat=len(CHAVES)):
        ligadas = tuple(k for k, b in zip(CHAVES, bits) if b)
        rotulo = f"varredura {chip} {'+'.join(ligadas) or '(nada)'}"
        r = coloca(rotulo, chip, ligadas)
        cabe_em[(ligadas, chip)] = r["ok"]
        problemas = []
        if r["ok"]:
            if r["reason"] != "":
                problemas.append(f"cabe mas reason = {r['reason']!r}")
            pedido = [pb.PROGRAMS[n] for n, k in pb.ORDER if k in ligadas]
            soma = (sum(v["instr"] for v in r["blocks"].values()), sum(v["sm"] for v in r["blocks"].values()))
            esperado = (sum(p["instr"] for p in pedido), sum(p["sm"] for p in pedido))
            if soma != esperado:
                problemas.append(f"os blocos somam {soma} (instr, sm), os programas ligados somam {esperado}")
            if ("lan" in ligadas) != (r["enc_block"] is not None):
                problemas.append(f"enc_block = {r['enc_block']!r} com a LAN "
                                 f"{'ligada' if 'lan' in ligadas else 'desligada'}")
            if r["enc_block"] is not None and r["enc_block"] not in pb.CHIPS[chip]:
                problemas.append(f"enc_block {r['enc_block']!r} não é bloco do {chip}")
        else:
            if not uma_frase(r["reason"]):
                problemas.append(f"a recusa deveria ser uma frase só: {r['reason']!r}")
            if not any(r["reason"].startswith(n + " ") for n, k in pb.ORDER if k in ligadas):
                problemas.append(f"a recusa deveria começar pelo programa recusado, que está ligado: "
                                 f"{r['reason']!r}")
        check(not problemas, f"{rotulo}: " + "; ".join(problemas))
# O que cabe no RP2040 cabe no RP2350, que só tem um bloco a mais.
for (ligadas, chip), cabe in cabe_em.items():
    if chip == "rp2040" and cabe:
        check(cabe_em[(ligadas, "rp2350")], f"cabe no rp2040 e não no rp2350: {'+'.join(ligadas)}")

# ---------------------------------------------------------------------------------
# 9. Todo resultado de cima tem os blocos do seu chip e nenhum estoura.
# ---------------------------------------------------------------------------------
for rotulo, chip, r in resultados:
    check(sorted(r["blocks"]) == sorted(pb.CHIPS[chip]),
          f"9 {rotulo}: blocos {sorted(r['blocks'])}, o {chip} tem {sorted(pb.CHIPS[chip])}")
    estouro = [b for b, v in r["blocks"].items()
               if not (0 <= v["instr"] <= pb.BLOCK_INSTR and 0 <= v["sm"] <= pb.BLOCK_SM)]
    check(not estouro, f"9 {rotulo}: {estouro} fora de 0..{pb.BLOCK_INSTR} instruções ou "
                       f"0..{pb.BLOCK_SM} máquinas: {uso(r)}")
check(pb.PROGRAMS == TABELA_ORIGINAL, "colocar não pode mudar a tabela PROGRAMS (a cópia do controle vazou)")
check((pb.BLOCK_INSTR, pb.BLOCK_SM) == (32, 4), "um bloco de PIO tem 32 instruções e 4 máquinas de estado")


# ---------------------------------------------------------------------------------
# 10. Pinos.
# ---------------------------------------------------------------------------------
def pinos(display, lan=False, int_pin=None, buzzer=False):
    return {"display": display, "lan": lan, "enc_int_pin": int_pin, "sound_buzzer": buzzer}


def erro_de(fn, *args):
    """A exceção, de qualquer tipo, que fn(*args) levanta, ou None se não levantar. Um valor
    que não pode ser um pino tem de dar ValueError; um TypeError escapando não é isso."""
    try:
        fn(*args)
    except Exception as e:  # noqa: BLE001
        return e
    return None


CONFLITOS = [
    ("alpha_paralelo + LAN com INT 21", pinos("alpha_paralelo", True, 21), [(21, "display:alpha_paralelo", "lan")]),
    ("alpha_paralelo + LAN com INT 28", pinos("alpha_paralelo", True, 28), []),
    ("tft + LAN com INT 21", pinos("tft", True, 21), []),
    ("tft + LAN com INT 28", pinos("tft", True, 28), [(28, "display:tft", "lan")]),
    ("alpha_paralelo + LAN com INT 22 + buzzer", pinos("alpha_paralelo", True, 22, True), [(22, "buzzer", "lan")]),
    ("alpha_paralelo + LAN com INT 22, sem buzzer", pinos("alpha_paralelo", True, 22, False), []),
    ("LAN desligada não reivindica o INT 21", pinos("alpha_paralelo", False, 21), []),
    ("nenhum mostrador + LAN com INT 22 + buzzer", pinos("nenhum", True, 22, True), [(22, "buzzer", "lan")]),
    # O INT que o simut_config.h escolhe quando a build não escolhe (GP11, com qualquer mostrador)
    # não colide com o mostrador nem com o buzzer de fábrica.
    ("tft + LAN + buzzer, INT pelo padrão", pinos("tft", True, None, True), []),
    ("alpha_paralelo + LAN + buzzer, INT pelo padrão", pinos("alpha_paralelo", True, None, True), []),
]
for rotulo, cfg, esperado in CONFLITOS:
    got = pb.pin_conflicts(cfg)
    check(got == esperado, f"10 {rotulo}: deveria dar {esperado}, deu {got}")

# Com INT -1 ("sem pino": o driver consulta o módulo) a LAN reivindica só os quatro dela, com
# qualquer mostrador; com a LAN desligada não reivindica nada. A leitura é pelo dono, porque um
# pino -1 reivindicado a mais não colidiria com ninguém e passaria despercebido pelas colisões.
for display in pb.DISPLAYS:
    dono = pb.pin_owners(pinos(display, True, -1))
    check(dono["lan"] == set(pb.PINS["lan"]),
          f"10 {display} + LAN com INT -1: a LAN deveria reivindicar só {sorted(pb.PINS['lan'])}, "
          f"reivindica {dono['lan']}")
    check(pb.pin_conflicts(pinos(display, True, -1)) == [],
          f"10 {display} + LAN sem pino de INT (-1) não colide com ninguém")
    check("lan" not in pb.pin_owners(pinos(display, False, 21)),
          f"10 {display}: com a LAN desligada ela não reivindica nada")
check(0 in pb.pin_owners(pinos("nenhum", True, 0))["lan"], "10 o GP0 é um pino de INT válido (0 não é 'sem pino')")
check(29 in pb.pin_owners(pinos("nenhum", True, 29))["lan"], "10 o GP29 é o último pino de INT válido")

# Sem enc_int_pin (a chave ausente, ou None) o INT é o que o simut_config.h escolhe: o GP11, com
# qualquer mostrador e sem mostrador (2026-10-12; até então seguia o mostrador).
for display, padrao in (("tft", 11), ("alpha_paralelo", 11), ("nenhum", 11)):
    quer = set(pb.PINS["lan"]) | {padrao}
    for como, cfg in (("com enc_int_pin None", pinos(display, True, None)),
                      ("sem a chave enc_int_pin", {"display": display, "lan": True})):
        dono = pb.pin_owners(cfg)
        check(dono["lan"] == quer,
              f"10 {display} + LAN {como}: o INT padrão é {padrao!r}, a LAN deveria reivindicar "
              f"{sorted(quer)} e reivindica {sorted(dono['lan'])}")
check(pb.pin_owners({"lan": True})["lan"] == set(pb.PINS["lan"]) | {11},
      "10 sem mostrador nem enc_int_pin na config, a LAN reivindica os quatro e o INT padrão (GP11)")

# O que não pode ser um pino é recusa, com o valor no texto: só None, -1 ou um int de 0 a 29. Um
# bool não conta (True seria o GP1 e False o GP0), e a LAN desligada não livra o valor da conta.
for valor in ("21", True, False, 30, -2, 21.0, [21]):
    for lan in (True, False):
        e = erro_de(pb.pin_owners, pinos("tft", lan, valor))
        check(type(e) is ValueError and repr(valor) in str(e),
              f"10 enc_int_pin={valor!r} com a LAN {'ligada' if lan else 'desligada'} deveria levantar "
              f"ValueError com o valor no texto: {e!r}")

check(levanta(pb.pin_conflicts, {"display": "lcd"}) is not None,
      "10 um mostrador desconhecido deveria levantar ValueError")
# Os ids são os do manifesto (grupo 14); os que o módulo usava antes deixaram de valer.
for velho in ("alpha_i2c", "alpha_parallel", "none"):
    msg = levanta(pb.pin_conflicts, {"display": velho})
    check(msg is not None and velho in msg,
          f"10 o mostrador {velho!r} não é um id do manifesto: deveria levantar ValueError nomeando-o: {msg!r}")

# Os pares saem em ordem alfabética e a lista, em ordem de pino. Com a tabela mudada para
# um buzzer em dois pinos (21 e 14), cada um colide com um dono: o 14 com a LAN e o 21 com o
# mostrador. O par do mostrador vem primeiro na varredura dos donos e o do 14 tem de vir antes.
PINS_ORIGINAIS = pb.PINS
try:
    pb.PINS = dict(PINS_ORIGINAIS, buzzer=(21, 14))
    got = pb.pin_conflicts(pinos("alpha_paralelo", True, None, True))
finally:
    pb.PINS = PINS_ORIGINAIS
check(got == [(14, "buzzer", "lan"), (21, "buzzer", "display:alpha_paralelo")],
      f"10 três donos: deveria dar as colisões do 14 e do 21, nessa ordem, deu {got}")

# ---------------------------------------------------------------------------------
# 11. As fontes reais: cada tamanho da tabela é o que a fonte do programa diz.
# ---------------------------------------------------------------------------------
libdeps = os.path.expanduser(os.environ.get("SIMUT_LIBDEPS")
                             or os.path.join(ROOT, ".pio", "libdeps", "pico_w_release"))
enc_header = os.path.expanduser(os.environ.get("ENC_PIO_HEADER")
                                or "~/Documentos/pico-enc28j60/PicoENC28J60/src/enc28j60-spi.pio.h")
cyw43_pio = os.path.expanduser(os.environ.get("CYW43_PIO")
                               or "~/.platformio/packages/framework-arduinopico/pico-sdk/src/"
                                  "rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.pio")
fontes = {nome: os.path.join(libdeps, rel) for nome, rel in pb.LIBDEPS_FILES.items()}
fontes["enc28j60"] = enc_header
fontes["cyw43"] = cyw43_pio
check(sorted(fontes) == sorted(pb.PROGRAMS), f"11 há programa sem fonte (ou fonte sem programa): {sorted(fontes)}")
# SIMUT_PIO_SOURCES=require: o job que compila o firmware tem as fontes, e uma que sumiu (uma pasta
# de libdeps que mudou de lugar) não pode virar um "pulado" que ninguém lê. O enc28j60 fica de
# fora: a biblioteca dele ainda não é dependência de nenhuma imagem. Um valor que não seja
# "require" nem vazio também reprova, porque um erro de digitação desligaria o portão sem aviso.
modo_fontes = os.environ.get("SIMUT_PIO_SOURCES", "")
check(modo_fontes in ("", "require"),
      f"11 SIMUT_PIO_SOURCES={modo_fontes!r}: os valores são vazio (a fonte que falta é pulada) e "
      f"'require' (a fonte que falta é falha)")
exigidas = set(fontes) - {"enc28j60"} if modo_fontes == "require" else set()
lidos = pb.program_lengths(libdeps, enc_header, cyw43_pio)
for nome, caminho in fontes.items():
    if not os.path.exists(caminho):
        if nome in exigidas:
            check(False, f"11 {nome}: {caminho} não existe, e SIMUT_PIO_SOURCES=require não deixa pular")
        else:
            print(f"pulado: {caminho} não existe")
        check(nome not in lidos, f"11 {nome}: {caminho} não existe e mesmo assim foi lido")
        continue
    check(lidos.get(nome) == pb.PROGRAMS[nome]["instr"],
          f"11 {nome}: {caminho} diz {lidos.get(nome)} instruções, a tabela diz {pb.PROGRAMS[nome]['instr']}")

# O WirePIO do BME280 fica fora da tabela, e o motivo é o tamanho dele, um bloco inteiro (ver o
# docstring do pio_budget): esse número também é conferido contra a fonte, com as mesmas regras.
wirepio = os.path.join(libdeps, pb.WIREPIO_FILE)
texto_wirepio = pb._read(wirepio)
if texto_wirepio is None:
    if modo_fontes == "require":
        check(False, f"11 wirepio: {wirepio} não existe, e SIMUT_PIO_SOURCES=require não deixa pular")
    else:
        print(f"pulado: {wirepio} não existe")
else:
    n_wirepio = pb._length_in(texto_wirepio, wirepio)
    check(n_wirepio == pb.WIREPIO_INSTR,
          f"11 wirepio: {wirepio} diz {n_wirepio} instruções, o pio_budget diz {pb.WIREPIO_INSTR}")

# ---------------------------------------------------------------------------------
# 13. O leitor das fontes, em arquivos sintéticos: é a única prova dele que o CI roda.
# ---------------------------------------------------------------------------------
PIO_INICIO = (
    "; cabeçalho\n"
    ".program outro\n"                                   # não é o programa: 2 instruções que não contam
    ".side_set 1\n"
    "lp: out pins, 1   side 0\n"
    "    jmp x-- lp    side 1\n"
    "public fim_outro:\n"
    "\n"
    ".program spi_gap01_sample0\n"
    ".side_set 1\n"
    "\n"
    "; comentário sozinho\n"
    "lp: out pins, 1             side 0   ; comentário no fim\n"
    "    jmp x-- lp              side 1\n"
    "public lp1_end:\n"                                  # rótulo sozinho
    "    set pindirs, 0          side 0\n"
    "    nop                     side 1\n"
    "    nop                     side 0\n"
    ".wrap_target\n"                                     # diretiva
    "lp2:\n"
    "    in pins, 1              side 0\n"
    "public meio: jmp y-- lp2    side 1\n"              # rótulo e instrução na mesma linha
    ".wrap\n"
    "public end:\n"
)
PIO_FINS = {
    "bloco C (%)": "\n% c-sdk {\nstatic inline void f(void) { a; b; }\n%}\n.program depois\n    nop\n",
    "outro .program": "\n.program depois\n    nop\n    nop\n",
    "fim do arquivo": "",
}


def grava(base, rel, texto):
    caminho = os.path.join(base, rel)
    os.makedirs(os.path.dirname(caminho), exist_ok=True)
    with open(caminho, "w", encoding="utf-8") as fh:
        fh.write(texto)
    return caminho


# Os arquivos das três bibliotecas como o PlatformIO as põe em .pio/libdeps/<ambiente>/,
# escritos por extenso e não tirados de LIBDEPS_FILES: se o módulo trocasse dois caminhos,
# este teste tem de perceber mesmo onde as bibliotecas não existem.
NA_LIBDEPS = {
    "onewire": "OneWirePIO_RP2040/src/onewire_protocol.pio.h",
    "dht22": "DHT22PIO_RP2040/src/dht22_protocol.pio.h",
    "buzzer": "BuzzerPIO_RP2040/src/BuzzerPIO_RP2040.cpp",
}
with tempfile.TemporaryDirectory() as tmp:
    lib = os.path.join(tmp, "libdeps")
    grava(lib, NA_LIBDEPS["onewire"],
          "static const struct pio_program p = {\n    .length       = 27,\n    .origin = -1,\n};\n")
    grava(lib, NA_LIBDEPS["dht22"], "    .length = 18,\n")
    grava(lib, NA_LIBDEPS["buzzer"], "const pio_program_t p = {.length=4, .origin=-1};\n")
    enc = grava(tmp, "enc.pio.h", "    .length = 19,\n")
    for fim, texto in PIO_FINS.items():
        pio = grava(tmp, "radio.pio", PIO_INICIO + texto)
        esperado = {"onewire": 27, "dht22": 18, "buzzer": 4, "enc28j60": 19, "cyw43": 7}
        got = pb.program_lengths(lib, enc, pio)
        check(got == esperado, f"13 programa seguido de {fim}: deveria dar {esperado}, deu {got}")
    # Arquivo que não existe fica de fora; os outros continuam lidos.
    got = pb.program_lengths(lib, os.path.join(tmp, "nao_existe.h"), pio)
    check("enc28j60" not in got and got.get("cyw43") == 7 and got.get("onewire") == 27,
          f"13 arquivo ausente deveria ficar de fora e só ele: {got}")
    got = pb.program_lengths(os.path.join(tmp, "sem_libdeps"), enc, os.path.join(tmp, "nao_existe.pio"))
    check(got == {"enc28j60": 19}, f"13 sem libdeps e sem o .pio sobra só o ENC28J60: {got}")
    # Arquivo que existe e não tem o padrão é recusa, não ausência.
    for i, (rotulo, rel, texto) in enumerate((("onewire sem .length", NA_LIBDEPS["onewire"], "nada aqui\n"),
                                              ("dht22 com dois .length", NA_LIBDEPS["dht22"],
                                               ".length = 17,\n.length = 9,\n"))):
        lib_ruim = os.path.join(tmp, f"ruim{i}")
        grava(lib_ruim, rel, texto)
        msg = levanta(pb.program_lengths, lib_ruim,
                      os.path.join(tmp, "nao_existe.h"), os.path.join(tmp, "nao_existe.pio"))
        check(msg is not None and os.path.basename(rel) in msg,
              f"13 {rotulo}: deveria levantar ValueError apontando {os.path.basename(rel)}: {msg!r}")
    pio_sem = grava(tmp, "sem_programa.pio", PIO_INICIO.replace("spi_gap01_sample0", "outro_nome"))
    msg = levanta(pb.program_lengths, lib, enc, pio_sem)
    check(msg is not None and "no `.program spi_gap01_sample0`" in msg,
          f"13 .pio sem o .program spi_gap01_sample0: deveria dizer que ele não está lá: {msg!r}")
    pio_vazio = grava(tmp, "vazio.pio", ".program spi_gap01_sample0\n.side_set 1\n; só comentário\n")
    msg = levanta(pb.program_lengths, lib, enc, pio_vazio)
    check(msg is not None and "has no instruction" in msg,
          f"13 .program sem instrução: deveria dizer que está vazio: {msg!r}")
    enc_sem = grava(tmp, "enc_sem.h", "nada aqui\n")
    check(levanta(pb.program_lengths, lib, enc_sem, pio) is not None,
          "13 cabeçalho do ENC28J60 sem .length: deveria levantar ValueError")


# SIMUT_PIO_SOURCES de ponta a ponta: este arquivo rodando como processo filho, sem nenhuma fonte
# (todo caminho aponta para o que não existe), e o código de saída dele. O padrão pula e passa;
# `require` reprova nomeando as cinco fontes que um job de firmware tem (as quatro da tabela e o
# WirePIO do BME280), sem nomear o enc28j60
# (que pode faltar); um valor desconhecido reprova. O filho não gera filhos (SIMUT_PIO_FILHO).
if not os.environ.get("SIMUT_PIO_FILHO"):
    with tempfile.TemporaryDirectory() as tmp:
        sem_fontes = {"SIMUT_LIBDEPS": os.path.join(tmp, "libdeps"),
                      "ENC_PIO_HEADER": os.path.join(tmp, "enc.pio.h"),
                      "CYW43_PIO": os.path.join(tmp, "radio.pio"),
                      "SIMUT_PIO_FILHO": "1"}

        def filho(modo):
            r = subprocess.run([sys.executable, os.path.abspath(__file__)], capture_output=True, text=True,
                               env=dict(os.environ, SIMUT_PIO_SOURCES=modo, **sem_fontes))
            return r.returncode, r.stdout + r.stderr

        code, saida = filho("")
        check(code == 0 and saida.count("pulado:") == len(pb.PROGRAMS) + 1,
              f"13 SIMUT_PIO_SOURCES vazio, sem nenhuma fonte: deveria passar pulando as "
              f"{len(pb.PROGRAMS)}; saiu {code}: {saida!r}")
        code, saida = filho("require")
        nomeadas = {n for n in pb.PROGRAMS if re.search(rf"^  11 {n}: .*SIMUT_PIO_SOURCES=require", saida, re.M)}
        check(code == 1 and nomeadas == set(pb.PROGRAMS) - {"enc28j60"} and saida.count("pulado:") == 1
              and re.search(r"^  11 wirepio: .*SIMUT_PIO_SOURCES=require", saida, re.M),
              f"13 SIMUT_PIO_SOURCES=require, sem nenhuma fonte: deveria reprovar nomeando todas menos o "
              f"enc28j60, que só é pulado; saiu {code} e nomeou {sorted(nomeadas)}: {saida!r}")
        code, saida = filho("requer")
        check(code == 1 and "SIMUT_PIO_SOURCES='requer'" in saida,
              f"13 SIMUT_PIO_SOURCES=requer (erro de digitação): deveria reprovar dizendo o valor; "
              f"saiu {code}: {saida!r}")

# ---------------------------------------------------------------------------------
# 14. A tabela de pinos contra o simut_config.h, pelo pré-processador do host.
# O cabeçalho confere máscaras de pinos (SIMUT_PINS_DISPLAY, SIMUT_PINS_ENC e SIMUT_PINS_BUZZER, só
# com SIMUT_NET_LAN=1) e as desdefine no fim da seção 10b. Este grupo as lê de uma cópia do
# cabeçalho sem os #undef, com o `cpp` do test_pin_masks.py, e exige que sejam os GPIOs que o
# pio_budget dá para cada mostrador do manifesto, com e sem buzzer e com o INT padrão.
# ---------------------------------------------------------------------------------
M = gf.load_manifest()
check(set(pb.DISPLAYS) == set(M["display"]),
      f"14 os mostradores do módulo {sorted(pb.DISPLAYS)} não são os [display.*] do features.toml "
      f"{sorted(M['display'])}")
check(set(pb.PINS) == set(M["display"]) | {"lan", "buzzer"},
      f"14 PINS deveria ter um mostrador por [display.*] do features.toml, a LAN e o buzzer: {sorted(pb.PINS)}")
check(type(pb.ENC_INT_DEFAULT) is int,
      f"14 ENC_INT_DEFAULT deveria ser um pino só, o mesmo com todo mostrador: {pb.ENC_INT_DEFAULT!r}")

SONDA_MASCARAS = (
    '#include "simut_config.h"\n'
    "#define SONDA_STR_(x) #x\n"
    "#define SONDA_STR(x) SONDA_STR_(x)\n"
    'PROBE_ENV SONDA_STR(SIMUT_PINS_DISPLAY) "@" SONDA_STR(SIMUT_PINS_ENC) "@" SONDA_STR(SIMUT_PINS_BUZZER)\n'
)
NOME_NA_OTA = {"tft": "wiretft", "alpha_paralelo": "wire"}   # o que test_pin_masks.py fixa para a LAN
MASCARAS = ("display", "enc", "buzzer")
TERMO = re.compile(r"1ull\s*<<\s*(\d+)")


def pinos_da_mascara(expansao):
    """Os GPIOs de uma máscara como o pré-processador a expandiu: só `1ull << N` ligados por `|`
    (ou `0ull`) são aceitos, e qualquer outra forma levanta ValueError em vez de ser lida errado."""
    if re.sub(r"[\s()|]", "", TERMO.sub("", expansao)) not in ("", "0ull"):
        raise ValueError(f"máscara de forma inesperada: {expansao!r}")
    return {int(n) for n in TERMO.findall(expansao)}


def le_mascaras(flags, inc_dirs, sonda):
    """(código de saída, textos dos #error, {máscara: GPIOs}) do cabeçalho com estas flags; as
    máscaras são None quando a sonda não as devolveu ou elas têm forma inesperada."""
    code, texto, erros = tpm.preprocess(flags, sonda, inc_dirs)
    erros = [e.split("error: ", 1)[-1] for e in erros]
    partes = texto.split("@")
    try:
        return code, erros, dict(zip(MASCARAS, map(pinos_da_mascara, partes))) if len(partes) == 3 else None
    except ValueError as e:
        return code, erros, str(e)


montagens = 0
if shutil.which("cpp") is None:
    check(False, "14 não há `cpp` no host: o grupo lê as máscaras do simut_config.h pelo pré-processador "
                 "(instale o gcc)")
else:
    with tempfile.TemporaryDirectory() as tmp:
        with open(os.path.join(tpm.SRC, "simut_config.h"), encoding="utf-8") as fh:
            linhas = fh.read().split("\n")
        desdefinidas = sorted(ln.split()[1] for ln in linhas if ln.startswith("#undef SIMUT_PINS_"))
        check(desdefinidas == ["SIMUT_PINS_BUZZER", "SIMUT_PINS_DISPLAY", "SIMUT_PINS_ENC", "SIMUT_PINS_ENC_SPI"],
              f"14 o cabeçalho deveria desdefinir as quatro máscaras no fim da seção 10b, desdefine {desdefinidas}")
        copia = os.path.dirname(grava(os.path.join(tmp, "copia"), "simut_config.h",
                                      "\n".join(ln for ln in linhas if not ln.startswith("#undef SIMUT_PINS_"))))
        padrao = grava(tmp, "padrao.c", tpm.PROBE)
        sonda = grava(tmp, "mascaras.c", SONDA_MASCARAS)

        for disp, definicao in M["display"].items():
            for buzzer in (True, False):
                montagens += 1
                rotulo = f"{disp} + LAN, {'com' if buzzer else 'sem'} buzzer"
                flags = [tpm.TARGET] + tpm.LAN + [f"-DSIMUT_SOUND_BUZZER={int(buzzer)}"] + list(definicao["flags"])
                # O que o cabeçalho de verdade diz da montagem: aceita (e com que nome), ou, para a
                # LAN sem mostrador, o #error que a recusa por falta de nome de variante. Esse
                # #error é o esperado, e as máscaras dele vêm do ramo que antecede o erro.
                quer = ("erro", tpm.ERR_NO_NAME) if disp == "nenhum" else ("ok", NOME_NA_OTA[disp])
                why = tpm.judge(rotulo, flags, quer, padrao, [tpm.SRC])
                check(why is None, f"14 {rotulo}: o cabeçalho deveria dar {quer}: {why}")
                # A cópia sem os #undef diz o mesmo que o cabeçalho de verdade, e dela saem as máscaras.
                code, erros, got = le_mascaras(flags, [copia, tpm.SRC], sonda)
                code_real, _nome, erros_real = tpm.preprocess(flags, padrao, [tpm.SRC])
                check((code, erros) == (code_real, [e.split("error: ", 1)[-1] for e in erros_real]),
                      f"14 {rotulo}: a cópia do cabeçalho sem os #undef disse {code} {erros}, o cabeçalho "
                      f"de verdade disse {code_real} {erros_real}")
                if not isinstance(got, dict):
                    check(False, f"14 {rotulo}: a sonda não devolveu as três máscaras: {got!r}")
                    continue
                dono = pb.pin_owners({"display": disp, "lan": True, "sound_buzzer": buzzer})
                modulo = {"display": dono["display:" + disp], "enc": dono["lan"], "buzzer": dono.get("buzzer", set())}
                for m in MASCARAS:
                    check(got[m] == modulo[m],
                          f"14 {rotulo}: SIMUT_PINS_{m.upper()} é {sorted(got[m])} no cabeçalho e "
                          f"{sorted(modulo[m])} no pio_budget")
                if disp != "nenhum":
                    check(pb.pin_conflicts({"display": disp, "lan": True, "sound_buzzer": buzzer}) == [],
                          f"14 {rotulo}: o cabeçalho aceita a montagem e o pio_budget acusa pino repetido")

        # O leitor enxerga o cabeçalho, não a tabela do módulo (por isso o esperado aqui é escrito por
        # extenso): um pino movido por -D anda com a máscara.
        lcd = [tpm.TARGET] + tpm.LAN + list(M["display"]["alpha_paralelo"]["flags"])
        for rotulo, extra, m, quer in (("buzzer no GP5", "-DBUZZER_PIN=5", "buzzer", {5}),
                                       ("INT no GP27", "-DSIMUT_ENC_INT_PIN=27", "enc", {12, 13, 14, 15, 27})):
            code, erros, got = le_mascaras(lcd + [extra], [copia, tpm.SRC], sonda)
            check(code == 0 and isinstance(got, dict) and got[m] == quer,
                  f"14 leitor com {rotulo} (alpha_paralelo + LAN): a máscara {m} deveria ser {sorted(quer)}, "
                  f"deu {got!r} (saída {code}, {erros})")

# ---------------------------------------------------------------------------------
# 15. O bloco do ENC28J60 no cabeçalho é o que o place( ) escolhe (2026-10-13). O módulo sobe
# antes dos sensores (AppManager_Boot.cpp), e o place( ) o põe no primeiro bloco que deixa
# lugar para o que vem depois; SIMUT_ENC_PIO no simut_config.h tem de dizer o mesmo, ou o
# driver pede um bloco e o orçamento conta outro. Lido pelo pré-processador do host, em cada
# combinação das chaves com PIO que o RP2040 aceita.
# ---------------------------------------------------------------------------------
SONDA_ENC_PIO = (
    '#include "simut_config.h"\n'
    "#define SONDA_STR_(x) #x\n"
    "#define SONDA_STR(x) SONDA_STR_(x)\n"
    "PROBE_ENV SONDA_STR(SIMUT_ENC_PIO)\n"
)
if shutil.which("cpp") is None:
    check(False, "15 sem o cpp do host para ler SIMUT_ENC_PIO")
else:
    with tempfile.TemporaryDirectory() as tmp:
        sonda = grava(tmp, "enc_pio.c", SONDA_ENC_PIO)
        lidas = 0
        for ds18, dht, buz in itertools.product((False, True), repeat=3):
            cfg = {"display": "alpha_paralelo", "lan": True, "sensor_ds18b20": ds18,
                   "sensor_dht22": dht, "sound_buzzer": buz}
            r = pb.place(cfg, "rp2040")
            if not r["ok"]:
                continue
            quer = r["enc_block"][len("pio"):]
            flags = ([tpm.TARGET] + tpm.LAN + list(M["display"]["alpha_paralelo"]["flags"])
                     + [f"-DSIMUT_SENSOR_DS18B20={int(ds18)}", f"-DSIMUT_SENSOR_DHT22={int(dht)}",
                        f"-DSIMUT_SOUND_BUZZER={int(buz)}"])
            code, nome, erros = tpm.preprocess(flags, sonda, [tpm.SRC])
            lidas += 1
            check(code == 0 and nome == quer,
                  f"15 DS18B20={int(ds18)} DHT22={int(dht)} buzzer={int(buz)}: o place( ) põe o ENC28J60 no "
                  f"{r['enc_block']}, e o cabeçalho diz SIMUT_ENC_PIO={nome!r} (saída {code}, {erros})")
        check(lidas == 6, f"15 deveriam ser 6 combinações aceitas no RP2040 (DS18B20 com DHT22 não cabe), foram {lidas}")

# ---------------------------------------------------------------------------------
if falhas:
    print("test_pio_budget: FALHOU")
    for msg in falhas:
        print("  " + msg)
    sys.exit(1)
print(f"test_pio_budget: {casos} casos ({len(CASOS)} conjuntos de resposta conhecida, "
      f"{2 ** len(CHAVES)} combinações de chaves por chip, {montagens} montagens de pinos lidas do cabeçalho).")
