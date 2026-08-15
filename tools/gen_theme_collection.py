#!/usr/bin/env python3
"""Generates the curated .thm collection in themes/ and validates every
palette against the same contrast pairs the firmware audit uses.

Any pair below its minimum aborts the run listing the offender — a theme
never ships broken. Run after editing a palette here:

    python3 tools/gen_theme_collection.py
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "data" / "themes"

# ── the same checker the firmware palette audit uses ────────────────────────
def rt(c):
    r, g, b = c
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    r5, g6, b5 = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))

def lum(c):
    def ch(x):
        x /= 255.0
        return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4
    r, g, b = rt(c)
    return 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)

def cr(a, b):
    la, lb = lum(a), lum(b)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)

PAIRS = [
    ("textMain","bgMain",4.5), ("textMain","cardBg",4.5), ("titleText","cardBg",4.5),
    ("sensorName","cardBg",4.5), ("btnText","cardBg",4.5), ("btnTextActive","accentHigh",4.5),
    ("bgMain","accent",3.0), ("textSub","cardBg",2.5), ("textSub","bgMain",2.5),
    ("tempOk","cardBg",3.0), ("tempHot","cardBg",3.0), ("tempCold","cardBg",3.0),
    ("tempWarm","cardBg",3.0), ("humidity","cardBg",3.0), ("tempOk","bgMain",3.0),
    ("textOff","bgMain",1.6), ("accentHigh","bgMain",1.6), ("accent","bgMain",1.6),
    ("stampText","bgMain",3.0),
    ("alarmText","alarmBg",4.5), ("alarmTextDim","alarmBg",3.0),
    ("alarmText","cautionBg",4.5), ("alarmText","selBg",4.5),
    ("alarmTextDim","selBg",3.0), ("alarmBorder","bgMain",1.6),
    ("alarmBg","cardBg",1.3),
]

FIELDS = ["bgMain","cardBg","textMain","textSub","textOff","accent","accentHigh","barBg",
          "tempHot","tempWarm","tempOk","tempCold","humidity",
          "btnText","titleText","sensorName","btnTextActive",
          "alarmBg","alarmText","alarmTextDim","alarmBorder","cautionBg","selBg","stampText"]

STOCK_STATE = dict(alarmBg=(180,30,30), alarmText=(255,255,255), alarmTextDim=(220,200,200),
                   alarmBorder=(255,60,60), cautionBg=(180,90,0), selBg=(50,50,55))

# ── the collection ──────────────────────────────────────────────────────────
# 10 temas, todos com as 24 chaves. O dispositivo carrega no máximo 8
# (MAX_CUSTOM_THEMES) — escolha quais instalar.
#
# Par Unimed: paleta oficial da marca — verde #00995D, verde-escuro
# institucional #004E4C, lima #B1D34B, laranja auxiliar #F47920. O verde da
# marca é o tempOk (valor "tudo certo" = verde Unimed); o laranja vive no
# botão de silenciar (cautionBg) e no tempWarm; a seleção usa o verde-escuro
# institucional em vez do cinza genérico.
THEMES = {
 "unimed_claro": ("Unimed Claro", dict(
    bgMain=(243,248,245), cardBg=(255,255,255), textMain=(0,78,76), textSub=(85,100,92),
    textOff=(170,185,178), accent=(0,153,93), accentHigh=(0,122,75), barBg=(215,230,222),
    tempHot=(195,55,45), tempWarm=(185,90,0), tempOk=(0,153,93), tempCold=(0,115,160),
    humidity=(25,105,170), btnText=(85,100,92), titleText=(0,78,76),
    sensorName=(0,78,76), btnTextActive=(255,255,255),
    alarmBg=(180,30,30), alarmText=(255,255,255), alarmTextDim=(220,200,200),
    alarmBorder=(255,60,60), cautionBg=(175,84,8), selBg=(0,60,58),
    stampText=(130,98,10))),

 "unimed_escuro": ("Unimed Escuro", dict(
    bgMain=(10,30,28), cardBg=(18,46,42), textMain=(232,244,238), textSub=(150,180,165),
    textOff=(75,100,90), accent=(0,175,105), accentHigh=(60,210,140), barBg=(35,68,60),
    tempHot=(255,105,90), tempWarm=(255,150,50), tempOk=(177,211,75), tempCold=(95,190,235),
    humidity=(90,200,170), btnText=(150,180,165), titleText=(232,244,238),
    sensorName=(232,244,238), btnTextActive=(10,30,28),
    alarmBg=(180,30,30), alarmText=(255,255,255), alarmTextDim=(220,200,200),
    alarmBorder=(255,60,60), cautionBg=(175,84,8), selBg=(0,62,58),
    stampText=(200,180,90))),

 # Ubuntu: aubergine de terminal #300A24 + laranja #E95420. O botão de
 # silenciar NÃO usa o laranja stock — colidiria com o accent laranja do
 # tema na tela de alarme; vira âmbar mais amarelo p/ manter distinção.
 "ubuntu": ("Ubuntu", dict(
    bgMain=(48,10,36), cardBg=(74,21,55), textMain=(242,240,238), textSub=(190,175,185),
    textOff=(112,82,102), accent=(233,84,32), accentHigh=(255,125,70), barBg=(88,36,68),
    tempHot=(255,95,85), tempWarm=(255,170,60), tempOk=(130,215,115), tempCold=(110,185,240),
    humidity=(115,175,245), btnText=(190,175,185), titleText=(242,240,238),
    sensorName=(242,240,238), btnTextActive=(48,10,36),
    alarmBg=(180,30,30), alarmText=(255,255,255), alarmTextDim=(220,200,200),
    alarmBorder=(255,60,60), cautionBg=(150,110,0), selBg=(66,28,52),
    stampText=(214,184,120))),

 "nordic": ("Nordico", dict(
    bgMain=(28,33,42), cardBg=(41,48,61), textMain=(226,232,240), textSub=(154,165,182),
    textOff=(84,95,113), accent=(129,161,193), accentHigh=(136,192,208), barBg=(58,66,82),
    tempHot=(224,108,117), tempWarm=(235,180,100), tempOk=(140,200,130), tempCold=(120,180,230),
    humidity=(129,161,193), btnText=(154,165,182), titleText=(226,232,240),
    sensorName=(226,232,240), btnTextActive=(28,33,42),
    stampText=(200,180,110), **STOCK_STATE)),

 "quartzo": ("Quartzo Claro", dict(
    bgMain=(243,241,238), cardBg=(255,255,255), textMain=(45,41,38), textSub=(120,112,105),
    textOff=(186,180,174), accent=(150,90,60), accentHigh=(160,97,63), barBg=(224,220,215),
    tempHot=(180,50,40), tempWarm=(170,105,0), tempOk=(55,130,70), tempCold=(40,110,170),
    humidity=(60,110,160), btnText=(110,102,95), titleText=(45,41,38),
    sensorName=(45,41,38), btnTextActive=(255,255,255),
    stampText=(130,95,15), **STOCK_STATE)),

 "contraste": ("Alto Contraste", dict(
    bgMain=(0,0,0), cardBg=(20,20,20), textMain=(255,255,255), textSub=(210,210,210),
    textOff=(120,120,120), accent=(255,215,0), accentHigh=(255,235,60), barBg=(55,55,55),
    tempHot=(255,90,90), tempWarm=(255,200,60), tempOk=(90,255,90), tempCold=(90,200,255),
    humidity=(120,190,255), btnText=(230,230,230), titleText=(255,255,255),
    sensorName=(255,255,255), btnTextActive=(0,0,0),
    stampText=(255,235,150), **STOCK_STATE)),

 "noturno": ("Turno da Noite", dict(
    bgMain=(24,14,8), cardBg=(38,24,14), textMain=(255,196,130), textSub=(200,140,80),
    textOff=(110,72,40), accent=(230,120,30), accentHigh=(255,160,60), barBg=(58,38,22),
    tempHot=(255,110,80), tempWarm=(255,170,60), tempOk=(230,170,90), tempCold=(200,150,120),
    humidity=(220,150,90), btnText=(200,140,80), titleText=(255,196,130),
    sensorName=(255,196,130), btnTextActive=(24,14,8),
    stampText=(230,180,90), **STOCK_STATE)),

 "esmeralda": ("Esmeralda", dict(
    bgMain=(14,26,20), cardBg=(24,42,33), textMain=(222,240,230), textSub=(140,180,160),
    textOff=(70,100,85), accent=(60,180,130), accentHigh=(90,215,160), barBg=(40,62,50),
    tempHot=(240,110,90), tempWarm=(230,180,80), tempOk=(110,210,150), tempCold=(100,190,220),
    humidity=(90,190,170), btnText=(140,180,160), titleText=(222,240,230),
    sensorName=(222,240,230), btnTextActive=(14,26,20),
    stampText=(190,180,100), **STOCK_STATE)),

 "cafe": ("Cafe com Leite", dict(
    bgMain=(240,233,224), cardBg=(252,248,242), textMain=(60,45,35), textSub=(130,110,95),
    textOff=(190,175,160), accent=(120,80,50), accentHigh=(150,100,65), barBg=(220,210,198),
    tempHot=(175,55,40), tempWarm=(160,100,10), tempOk=(80,125,60), tempCold=(60,105,150),
    humidity=(90,105,150), btnText=(120,100,85), titleText=(60,45,35),
    sensorName=(60,45,35), btnTextActive=(252,248,242),
    stampText=(125,90,15), **STOCK_STATE)),

 "termico": ("Camera Termica", dict(
    bgMain=(16,10,32), cardBg=(30,20,52), textMain=(245,235,220), textSub=(180,150,190),
    textOff=(95,75,120), accent=(250,130,40), accentHigh=(255,170,60), barBg=(50,36,76),
    tempHot=(255,90,60), tempWarm=(255,170,60), tempOk=(255,210,90), tempCold=(90,140,255),
    humidity=(170,120,230), btnText=(180,150,190), titleText=(245,235,220),
    sensorName=(245,235,220), btnTextActive=(16,10,32),
    stampText=(220,190,110), **STOCK_STATE)),

 "grafite": ("Grafite Industrial", dict(
    bgMain=(24,26,28), cardBg=(38,41,44), textMain=(232,234,236), textSub=(160,166,172),
    textOff=(88,94,100), accent=(255,120,40), accentHigh=(255,150,70), barBg=(56,60,64),
    tempHot=(255,95,85), tempWarm=(255,180,70), tempOk=(120,210,130), tempCold=(110,185,240),
    humidity=(110,170,240), btnText=(160,166,172), titleText=(232,234,236),
    sensorName=(232,234,236), btnTextActive=(24,26,28),
    stampText=(200,180,100), **STOCK_STATE)),
}

# ── validate + emit ─────────────────────────────────────────────────────────
OUT.mkdir(exist_ok=True)
bad = 0
for code, (name, pal) in THEMES.items():
    missing = [f for f in FIELDS if f not in pal]
    if missing:
        raise SystemExit(f"{code}: faltam campos {missing}")
    for fg, bg, need in PAIRS:
        got = cr(pal[fg], pal[bg])
        if got < need:
            print(f"FALHA {code}: {fg} on {bg} {got:.2f} < {need}")
            bad += 1
    lines = [f"# SIMUT theme — gerado por tools/gen_theme_collection.py",
             f"@NAME {name}", f"@CODE {code}", "@COLORS"]
    for f in FIELDS:
        r, g, b = pal[f]
        lines.append(f"{f}=#{r:02X}{g:02X}{b:02X}")
    (OUT / f"{code}.thm").write_text("\n".join(lines) + "\n")

if bad:
    raise SystemExit(f"{bad} pares reprovados — corrija antes de publicar")
print(f"{len(THEMES)} temas validados e gravados em {OUT}/")
