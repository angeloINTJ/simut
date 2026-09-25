#!/usr/bin/env python3
"""The Ângulo standard (ANGULO.md), where a machine can check it.

WHY THIS EXISTS
---------------
On 2026-09-24 the docs site, the landing page, the READMEs and the brand were
brought to the Ângulo standard that the device's web UI already followed. A
rule that lives only in prose does not last: simut-rx's copy of the guide
(§11) records two colour pairs that failed contrast for a whole version,
because nothing measured them. This is the part of the standard a machine can
measure. The rest is ANGULO.md §8, for a person, and AGENTS.md §7 says which
part is which.

WHAT IT CHECKS
--------------
  1. docs/assets/angulo.css is still the byte-for-byte copy of
     simut-rx/web/angulo.css: its sha256 is pinned below, so an edit by hand
     fails here, and a recopy updates the pin in the same change.
  2. The site's own CSS -- docs/assets/site.css and the <style> of
     docs/index.html -- names no colour (no #hex, rgb(), hsl()), draws no
     gradient, casts no shadow but --sombra-flutuante, rounds no corner but
     the three radius tokens, sets no margin, padding or gap off the 4 px grid,
     and uses no font size, line height or tracking outside the text styles.
  3. The brand files use token colours only, and no gradient or filter.
  4. No emoji plays an icon: not in the READMEs (outside the all-contributors
     block, which that bot writes), not on the site, not in any document that
     docs/README.md marks Living, nor in the root and tools guides.
  5. The README badges are flat-square: shields.io's flat style draws a
     gradient.

WHAT IT DOES NOT
----------------
Hierarchy, microcopy and whether every state is drawn are for a person
(ANGULO.md §8). The device's web UI (WebUI.h) is not checked here: it keeps
deviations paid in flash, listed in AGENTS.md §7. Snapshot documents are not
checked either -- a record that gets edited stops being evidence.

    python3 tools/check_angulo.py          # exit 1 on any finding

@project SIMUT
@license MIT License
"""
import glob
import hashlib
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANGULO_CSS = "docs/assets/angulo.css"
# sha256 of simut-rx/web/angulo.css as copied on 2026-09-24. Never edit the
# copy: recopy it from simut-rx and put the new hash here.
ANGULO_CSS_SHA256 = "f90637eda3a93dc112833fb24b582d2a2cd0e4680fcd54c84784141054488ff5"
FACE = ["docs/assets/fonts/angulo-display-600.woff2", "docs/assets/fonts/OFL-BricolageGrotesque.txt"]
SITE_CSS = ["docs/assets/site.css"]
LANDING = "docs/index.html"
BRAND = ["docs/images/logo-mark.svg", "docs/images/logo-wordmark.svg", "docs/images/logo-wordmark-dark.svg",
         "docs/images/logo-name.svg", "docs/images/powered-by-simut.svg", "docs/images/powered-by-simut-large.svg"]
READMES = ["README.md", "README.pt-BR.md", "README.es-ES.md"]
GUIDES = ["CONTRIBUTING.md", "CONTRIBUTING.pt-BR.md", "CONTRIBUTING.es-ES.md", "CODE_OF_CONDUCT.md",
          "CODE_OF_CONDUCT.pt-BR.md", "CODE_OF_CONDUCT.es-ES.md", "SECURITY.md", "AGENTS.md", "CLAUDE.md",
          "ANGULO.md", "docs/README.md", "tools/README.md", "tools/arduino_pico_overrides/README.md",
          "tools/PicoHand/MANUAL_CLAUDE_CODE.md", "tools/PicoHand/MANUAL_CLAUDE_CODE.pt-BR.md"]

# Pictographs and dingbats, each with its presentation selector if it has one
# (a warning sign is two code points and one finding), or the selector alone.
# Arrows are left out on purpose, and so is U+25B6: the ASCII diagrams in the
# PicoHand manual draw their arrowheads with it, in text presentation.
EMOJI = re.compile("[\U0001F000-\U0001FAFF☀-➿⬀-⯿⏩-⏺"
                   "⌚⌛ℹ‼⁉Ⓜ]️?|️")
BOT_START, BOT_END = "<!-- ALL-CONTRIBUTORS-LIST:START", "<!-- ALL-CONTRIBUTORS-LIST:END"

# The text styles of ANGULO.md §3.2 (and the 15 px of its buttons).
FONT_PX = {13, 15, 16, 18, 24, 34}
LINE_PX = {16, 18, 20, 24, 30, 38}
FONT_EM = {".8125em"}                      # 13 on 16: the dado size, relative
TRACKING = {"-0.02em", "-0.01em", "-0.005em", "0"}
RADII = {"0", "var(--raio-controle)", "var(--raio-cartao)", "var(--raio-total)"}
SPACING_PROP = re.compile(r"^(margin|padding|gap|row-gap|column-gap)(-(top|right|bottom|left|block|inline)(-(start|end))?)?$")
SPACING_VAL = re.compile(r"^(0|auto|-?[12]px|var\(--espaco-[1-7]\))$")    # 1-2 px is a line, not a distance

findings = []


def rel(path):
    return os.path.relpath(path, ROOT)


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return f.read()


def report(path, text, pos, msg):
    findings.append(f"{path}:{text.count(chr(10), 0, pos) + 1}: {msg}")


def blank_comments(css):
    """Comments out, positions kept, so a finding still names its line."""
    return re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group()), css, flags=re.S)


def check_tokens():
    data = open(os.path.join(ROOT, ANGULO_CSS), "rb").read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != ANGULO_CSS_SHA256:
        findings.append(f"{ANGULO_CSS}:1: sha256 {digest[:16]}... is not the pinned copy of simut-rx/web/angulo.css -- "
                        "recopy it from simut-rx and update ANGULO_CSS_SHA256 in tools/check_angulo.py")
    for f in FACE:
        if not os.path.exists(os.path.join(ROOT, f)):
            findings.append(f"{f}:1: missing -- the display face and its OFL licence travel together")
    return {h.lower() for h in re.findall(r"--[a-z0-9-]+:\s*(#[0-9a-fA-F]{6})", data.decode("utf-8"))}


def check_css(path, css, offset=0, text=None):
    text = text if text is not None else css
    clean = blank_comments(css)
    for block in re.finditer(r"\{([^{}]*)\}", clean):
        for decl in re.finditer(r"([a-z-]+)\s*:\s*([^;]+)", block.group(1)):
            prop, value = decl.group(1), decl.group(2).strip()
            where = offset + block.start(1) + decl.start()
            say = lambda msg: report(path, text, where, f"{prop}: {value} -- {msg}")
            if re.search(r"#[0-9a-fA-F]{3,8}\b|\brgba?\(|\bhsla?\(", value):
                say("a literal colour; use a token (ANGULO.md §8)")
            if "gradient(" in value:
                say("a gradient; only data charts get one (rule 1)")
            if prop == "box-shadow" and value not in ("none", "var(--sombra-flutuante)"):
                say("a shadow that is not --sombra-flutuante; cards never have one (rule 3)")
            if prop == "border-radius" and any(v not in RADII for v in value.split()):
                say("a radius off the three tokens (rule 4)")
            if SPACING_PROP.match(prop):
                parts = re.sub(r"calc\((?:[^()]|var\([^)]*\))*\)", lambda m: "0" if all(
                    t in ("+", "-", "*", "/") or re.fullmatch(r"var\(--espaco-[1-7]\)|\d+", t)
                    for t in re.findall(r"var\([^)]*\)|[^\s()]+", m.group()[5:-1])) else m.group(), value).split()
                if any(not SPACING_VAL.match(p) for p in parts):
                    say("a distance off the 4 px grid; use an --espaco token (rule 5)")
            if prop in ("font", "font-size"):
                for size, _, line in re.findall(r"(\d+)px(/(\d+)px)?", value):
                    if int(size) not in FONT_PX or (line and int(line) not in LINE_PX):
                        say("a font size or line height outside the text styles (§3.2)")
                for em in re.findall(r"(?<![\w.])(\.\d+em|\d+\.?\d*em)", value):
                    if em not in FONT_EM:
                        say("a font size outside the text styles (§3.2)")
            if prop == "letter-spacing" and value not in TRACKING:
                say("tracking outside the text styles (§3.2)")


def check_site():
    for path in SITE_CSS:
        check_css(path, read(path))
    page = read(LANDING)
    for m in re.finditer(r"<style>(.*?)</style>", page, re.S):
        check_css(LANDING, m.group(1), offset=m.start(1), text=page)
    for m in re.finditer(r'\sstyle="([^"]*)"', page):
        if re.search(r"(margin|padding|gap)[^:]*:\s*[0-9.]+(rem|px|em)", m.group(1)):
            report(LANDING, page, m.start(), f'inline style="{m.group(1)}" -- a distance off the grid; use a class')


def check_brand(token_colours):
    for path in BRAND:
        svg = read(path)
        for m in re.finditer(r'(?:fill|stroke|stop-color|flood-color)="(#[0-9a-fA-F]{3,8})"', svg):
            if m.group(1).lower() not in token_colours:
                report(path, svg, m.start(), f"{m.group(1)} is not a token colour")
        for m in re.finditer(r"<(linearGradient|radialGradient|filter)\b", svg):
            report(path, svg, m.start(), f"<{m.group(1)}>: no gradient, no shadow (rules 1 and 3)")


def living_docs():
    """Every .md that docs/README.md marks Living -- the index is the source."""
    out = []
    for line in read("docs/README.md").splitlines():
        if re.search(r"\|\s*\**Living\**\s*\|", line):
            for target in re.findall(r"\]\(([^)#]+)\)", line):
                path = os.path.normpath(os.path.join("docs", target))
                if path.endswith(".md") and os.path.exists(os.path.join(ROOT, path)):
                    out.append(path)
    return out


def check_emoji():
    files = READMES + GUIDES + living_docs() + [LANDING]
    files += [rel(p) for p in glob.glob(os.path.join(ROOT, "docs", "_manual", "*.md"))]
    files += [rel(p) for p in glob.glob(os.path.join(ROOT, "docs", "_layouts", "*.html"))]
    files += [rel(p) for p in glob.glob(os.path.join(ROOT, "docs", "_includes", "*.html"))]
    for path in sorted(set(files)):
        text = read(path)
        skip = (text.find(BOT_START), text.find(BOT_END)) if BOT_START in text else (-1, -1)
        for m in EMOJI.finditer(text):
            if skip[0] <= m.start() < skip[1]:
                continue
            report(path, text, m.start(), f"U+{ord(m.group()[0]):04X}: an emoji in the place of an icon or a "
                                          "word (rule 6) -- say the state in words")
    return len(set(files))


def check_badges():
    for path in READMES:
        text = read(path)
        for m in re.finditer(r"https://img\.shields\.io/[^)\s]+", text):
            if "style=flat-square" not in m.group():
                report(path, text, m.start(), "a badge without style=flat-square -- the flat style draws a gradient")


if __name__ == "__main__":
    colours = check_tokens()
    check_site()
    check_brand(colours)
    n = check_emoji()
    check_badges()
    for f in findings:
        print(f)
    if findings:
        print(f"\nÂngulo: {len(findings)} finding(s). The rules are ANGULO.md §2; how they apply here, AGENTS.md §7.")
        sys.exit(1)
    print(f"Ângulo: tokens pinned, site CSS clean, brand on tokens, {n} documents without emoji, badges flat-square.")
