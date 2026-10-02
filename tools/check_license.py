#!/usr/bin/env python3
"""The licence says the same thing in every place that states it.

WHY THIS EXISTS
---------------
SIMUT carries the MIT text in three places — the LICENSE file, the /license
web page inside WebUI.h, and the firmware string in src/HelpLicenseEN.h that
the panel's License screen draws — plus two release scripts that are supposed
to ship the file alongside the source. Until 2026-10-02 each .lng pack carried
a fourth, translated, in @LICENSE; the panel now shows the original English
text in every language, as the web page does, and the section is gone.

On 2026-09-21 a sweep found two things that had been true for a long time and
that nothing could have caught:

  * the /license web page said "Copyright (c) 2025" while every other copy said
    2026. The page is the one a user actually reads;
  * tools/build_release.sh built the Arduino IDE zips — a copy of the whole
    source tree — without putting LICENSE in them. The MIT text itself asks for
    the notice in "all copies or substantial portions of the Software", so that
    zip was the one artefact that did not carry it. build_release_pio.sh always
    did.

Neither is a compile error, a test failure or a lint. They are the kind of
thing a person finds by reading, once, years late.

WHAT IT CHECKS
--------------
  1. every copyright line names the same year and the same holder;
  2. the MIT body on the web page and in the firmware string matches the
     LICENSE file word for word: the two screens show the same text;
  3. no language pack carries a @LICENSE section again — nothing reads it,
     and it cost every device ~1.95 KB of each pack;
  4. both release scripts copy LICENSE into what they package.

The holder is compared without accents on purpose: the firmware string is drawn
on the TFT with a CP437 font where "Ângelo Moisés" is unrelated symbols, and
HelpLicenseEN.h says so at the flag. The gate should not push anyone into
"fixing" that.

@project SIMUT
@license MIT License
"""
import html
import re
import sys
import unicodedata
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
COPY_RE = re.compile(
    r"(?:Copyright|Direitos Autorais|Derechos de Autor)\s*(?:\(c\)|©)\s*(\d{4})\s+(.+)")


def fold(name: str) -> str:
    """Accent-insensitive, whitespace-insensitive holder name."""
    n = unicodedata.normalize("NFKD", name)
    return " ".join("".join(c for c in n if not unicodedata.combining(c)).split())


def body(text: str) -> str:
    """The MIT paragraphs as one whitespace-normalised string, no header line."""
    i = text.index("Permission is hereby granted")
    j = text.index("SOFTWARE.", i) + len("SOFTWARE.")
    return " ".join(text[i:j].split())


def find(path: str, text: str):
    m = COPY_RE.search(text)
    if not m:
        return None
    return (int(m.group(1)), fold(m.group(2).strip()), path)


def main() -> int:
    problems, seen = [], []

    lic = (ROOT / "LICENSE").read_text(encoding="utf-8")
    seen.append(find("LICENSE", lic))

    web_src = (ROOT / "WebUI.h").read_text(encoding="utf-8")
    i = web_src.index("<pre>MIT License")
    web = html.unescape(web_src[i + len("<pre>"):web_src.index("</pre>", i)])
    seen.append(find("WebUI.h (/license)", web))

    fw = (ROOT / "src" / "HelpLicenseEN.h").read_text(encoding="utf-8")
    full = fw[fw.index("#else", fw.index("SIMUT_LICENSE_STUB")):]
    seen.append(find("src/HelpLicenseEN.h", full))

    for pack in sorted((ROOT / "data" / "lang").glob("*.lng")):
        if re.search(r"^@LICENSE\b", pack.read_text(encoding="utf-8"), re.M):
            problems.append(f"  data/lang/{pack.name} traz @LICENSE: o painel "
                            "nao le mais essa secao, mostra a string do firmware")

    missing = [s for s in seen if s is None]
    seen = [s for s in seen if s]
    if missing or len(seen) < 3:
        problems.append("  nao achei a linha de copyright em alguma das copias")

    years = {s[0] for s in seen}
    if len(years) > 1:
        problems.append("  anos divergentes: " + ", ".join(
            f"{p} diz {y}" for y, _, p in sorted(seen)))
    holders = {s[1] for s in seen}
    if len(holders) > 1:
        problems.append("  titulares divergentes: " + ", ".join(
            f"{p} diz {h!r}" for _, h, p in sorted(seen, key=lambda s: s[2])))

    if body(lic) != body(web):
        problems.append("  o texto MIT da pagina /license nao bate com o LICENSE")
    if body(lic) != body(full):
        problems.append("  o texto MIT da string do firmware (tela Licenca do "
                        "painel) nao bate com o LICENSE")

    for script in ("build_release.sh", "build_release_pio.sh"):
        t = (ROOT / "tools" / script).read_text(encoding="utf-8")
        if not re.search(r"^\s*cp\s+LICENSE\b", t, re.M):
            problems.append(
                f"  tools/{script} empacota o codigo sem copiar o LICENSE")

    if problems:
        print("LICENSE GATE: divergencia.")
        print("\n".join(problems))
        return 1

    y = years.pop()
    print(f"LICENSE GATE: clean ({len(seen)} copias, (c) {y} {seen[0][1]}; "
          f"os dois scripts de release levam o arquivo)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
