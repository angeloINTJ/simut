#!/usr/bin/env python3
"""Testa o orçamento de PIO (tools/pio_budget.py): o que cabe nos blocos de cada chip,
quem disputa um pino e se a tabela de programas ainda é a que as fontes dizem.

Rodar: python3 tools/test_pio_budget.py

A tabela nasceu de uma contagem feita em 2026-10-12 (instruções e máquinas de estado de
cada programa que o boot carrega). Este teste a segura de três lados, sem compilar
firmware:
  - a colocação (`place`) contra os conjuntos de recursos de que o projeto já sabe a
    resposta, mais uma varredura das 32 combinações dos dois chips contra invariantes
    (nenhum bloco acima de 32 instruções ou 4 máquinas, a soma do que coube é a soma dos
    programas) e um controle negativo: a recusa vem do espaço que falta, não do nome;
  - os pinos (`pin_conflicts`);
  - as fontes: o `.length` de cada `.pio.h` e as instruções do `.pio` do rádio têm de
    ser o que a tabela diz. Onde a fonte não existe o teste a pula e avisa: o job `gates`
    do CI não tem as bibliotecas. O leitor das fontes é provado à parte, em arquivos
    sintéticos, porque é a única prova dele que o CI roda.

O número no início de cada rótulo diz de que grupo a falha é: 1 a 8, os conjuntos de recursos;
2r, a recusa de um programa de bloco fixo; 9, os limites de todo resultado; 10, os pinos;
11, as fontes reais; 12, o controle negativo; 13, o leitor das fontes.

Variáveis de ambiente (as fontes reais):
  SIMUT_LIBDEPS    <repo>/.pio/libdeps/pico_w_release
  ENC_PIO_HEADER   ~/Documentos/pico-enc28j60/PicoENC28J60/src/enc28j60-spi.pio.h
  CYW43_PIO        ~/.platformio/packages/framework-arduinopico/pico-sdk/src/rp2_common/
                   pico_cyw43_driver/cyw43_bus_pio_spi.pio
"""
import copy
import itertools
import os
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import pio_budget as pb  # noqa: E402

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
    ("7 release no RP2350", "rp2350", RELEASE_PICO_W, True, None,
     {"pio0": (27, 1), "pio1": (27, 4), "pio2": (0, 0)}),
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

# A conta do que vem depois do enc28j60 inclui o que os próprios programas de depois tiram
# uns dos outros: com o rádio em 20 instruções o DHT22 e o rádio não cabem juntos no pio1,
# cada um cabe sozinho, e quem recusa é o enc28j60 (a simulação) e não o rádio (a regra 2,
# que só pegaria depois de o enc28j60 já estar no lugar).
r = coloca("12 rádio de 20 instruções", "rp2040", ("lan", "sensor_dht22", "radio"),
           tabela_com("cyw43", instr=20))
check(not r["ok"] and r["reason"].startswith("enc28j60 needs") and "but then cyw43 would not fit" in r["reason"],
      f"12 o DHT22 e o rádio de 20 não cabem juntos no pio1, e quem recusa é o enc28j60: {r['reason']!r}")
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
# Varredura: as 32 combinações de chaves nos dois chips contra invariantes que não dependem
# de saber a resposta de cada uma.
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


CONFLITOS = [
    ("alpha_parallel + LAN com INT 21", pinos("alpha_parallel", True, 21), [(21, "display:alpha_parallel", "lan")]),
    ("alpha_parallel + LAN com INT 28", pinos("alpha_parallel", True, 28), []),
    ("tft + LAN com INT 21", pinos("tft", True, 21), []),
    ("tft + LAN com INT 28", pinos("tft", True, 28), [(28, "display:tft", "lan")]),
    ("alpha_i2c + LAN com INT 22 + buzzer", pinos("alpha_i2c", True, 22, True), [(22, "buzzer", "lan")]),
    ("alpha_i2c + LAN com INT 22, sem buzzer", pinos("alpha_i2c", True, 22, False), []),
    ("LAN desligada não reivindica o INT 21", pinos("alpha_parallel", False, 21), []),
    ("nenhum mostrador + LAN com INT 22 + buzzer", pinos("none", True, 22, True), [(22, "buzzer", "lan")]),
]
for rotulo, cfg, esperado in CONFLITOS:
    got = pb.pin_conflicts(cfg)
    check(got == esperado, f"10 {rotulo}: deveria dar {esperado}, deu {got}")

# Sem pino de INT (None ou -1) a LAN reivindica só os quatro dela, com qualquer mostrador;
# com a LAN desligada não reivindica nada. A leitura é pelo dono, porque um pino -1 ou None
# reivindicado a mais não colidiria com ninguém e passaria despercebido pelas colisões.
for display in pb.DISPLAYS:
    for sem_int in (None, -1):
        dono = pb.pin_owners(pinos(display, True, sem_int))
        check(dono["lan"] == set(pb.PINS["lan"]),
              f"10 {display} + LAN com INT {sem_int!r}: a LAN deveria reivindicar só {sorted(pb.PINS['lan'])}, "
              f"reivindica {dono['lan']}")
        check(pb.pin_conflicts(pinos(display, True, sem_int)) == [],
              f"10 {display} + LAN sem pino de INT ({sem_int!r}) não colide com ninguém")
    check("lan" not in pb.pin_owners(pinos(display, False, 21)),
          f"10 {display}: com a LAN desligada ela não reivindica nada")
check(0 in pb.pin_owners(pinos("none", True, 0))["lan"], "10 o GP0 é um pino de INT válido (0 não é 'sem pino')")
check(pb.pin_owners({"lan": True})["lan"] == set(pb.PINS["lan"]),
      "10 sem enc_int_pin na config, a LAN reivindica só os quatro")
check(levanta(pb.pin_conflicts, {"display": "lcd"}) is not None,
      "10 um mostrador desconhecido deveria levantar ValueError")

# Os pares saem em ordem alfabética e a lista, em ordem de pino. Com a tabela mudada para
# um buzzer em dois pinos (21 e 14), cada um colide com um dono: o 14 com a LAN e o 21 com o
# mostrador. O par do mostrador vem primeiro na varredura dos donos e o do 14 tem de vir antes.
PINS_ORIGINAIS = pb.PINS
try:
    pb.PINS = dict(PINS_ORIGINAIS, buzzer=(21, 14))
    got = pb.pin_conflicts(pinos("alpha_parallel", True, None, True))
finally:
    pb.PINS = PINS_ORIGINAIS
check(got == [(14, "buzzer", "lan"), (21, "buzzer", "display:alpha_parallel")],
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
lidos = pb.program_lengths(libdeps, enc_header, cyw43_pio)
for nome, caminho in fontes.items():
    if not os.path.exists(caminho):
        print(f"pulado: {caminho} não existe")
        check(nome not in lidos, f"11 {nome}: {caminho} não existe e mesmo assim foi lido")
        continue
    check(lidos.get(nome) == pb.PROGRAMS[nome]["instr"],
          f"11 {nome}: {caminho} diz {lidos.get(nome)} instruções, a tabela diz {pb.PROGRAMS[nome]['instr']}")

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

# ---------------------------------------------------------------------------------
if falhas:
    print("test_pio_budget: FALHOU")
    for msg in falhas:
        print("  " + msg)
    sys.exit(1)
print(f"test_pio_budget: {casos} casos.")
