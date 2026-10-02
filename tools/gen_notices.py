#!/usr/bin/env python3
"""Writes the third-party notices from tools/third_party.toml, in the three
places that state them, and the English opening of the panel's License screen.

WHY THIS EXISTS
---------------
On 2026-10-02 the panel's License screen and the /license web page were made
to say the same thing: the MIT text in its original English, after an opening
explanation in the language of the installed pack, and every piece of
third-party software at the end. Checking that list against the images
themselves (the symbols each firmware.elf keeps) found it had drifted:

  * six components that every image links were cited nowhere: TinyUSB,
    cyw43-driver, the CYW43439 radio firmware, MicroPython's DHCP server,
    LEAmDNS (in three images), and the toolchain's C and runtime libraries;
  * BTstack was described as non-commercial and alpha-only. It is in the Air
    image too, and the Pico SDK carries Raspberry Pi's licence for it
    (LICENSE.RP), which covers products built on the Pico W and Pico 2 W;
  * cyw43-driver's licence asks binary redistributions to reproduce its notice
    "in the documentation and/or other materials", and no copy did;
  * the panel's list had one entry fewer than the page by design, and the two
    lists disagreed on holders and licence names.

A list kept by hand in three files drifts. This script makes the TOML the only
place to edit, and `--check` (run by CI) fails when an output differs from what
the TOML says.

OUTPUTS
-------
  THIRD_PARTY_NOTICES.md   every component, with the file in LICENSES/ that
                           holds its licence text verbatim
  WebUI.h                  the <pre> under "Third-Party Notices" on /license
  src/HelpLicenseEN.h      the list at the end of the panel's License screen,
                           and the English opening of that screen, copied from
                           the /license page so both say the same words

USAGE
-----
    python3 tools/gen_notices.py            # rewrite the outputs
    python3 tools/gen_notices.py --check    # exit 1 if any output is stale

Project: SIMUT
License: MIT
"""
import html
import re
import sys
import textwrap
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOML = ROOT / "tools" / "third_party.toml"
LICDIR = ROOT / "LICENSES"
NOTICES = ROOT / "THIRD_PARTY_NOTICES.md"
WEBUI = ROOT / "WebUI.h"
FWH = ROOT / "src" / "HelpLicenseEN.h"
REPO = "https://github.com/angeloINTJ/simut"

PANEL = ["release", "test", "test_https", "asserts", "pico2_release"]
ENV = {"release": "pico_w_release", "test": "pico_w_test", "test_https": "pico_w_test_https",
       "asserts": "pico_w_asserts", "alpha": "pico_w_alpha", "air": "pico_w_air",
       "pico2_release": "pico2_w_release"}

# The opening of the License screen, in the order the /license page shows it.
INTRO_KEYS = ["lic_sub", "lic_summary_title", "lic_summary", "lic_summary_note", "lic_legal_note"]

FW_INTRO_BEGIN = "/* BEGIN generated: licence opening — tools/gen_notices.py */"
FW_INTRO_END = "/* END generated: licence opening */"
FW_LIST_BEGIN = "/* BEGIN generated: third-party list — tools/gen_notices.py */"
FW_LIST_END = "/* END generated: third-party list */"


def load():
    data = tomllib.loads(TOML.read_text(encoding="utf-8"))
    every = data["all_images"]
    for c in data["component"]:
        for f in ("name", "holder", "license", "images", "url", "what", "text"):
            if f not in c:
                sys.exit(f"gen_notices: {c.get('name', '?')} has no '{f}'")
        if c["images"] == "all":
            c["images"] = list(every)
        unknown = [i for i in c["images"] if i not in every]
        if unknown:
            sys.exit(f"gen_notices: {c['name']}: unknown image(s) {unknown}")
        for t in [c["text"]] + c.get("also", []):
            if not (LICDIR / t).is_file():
                sys.exit(f"gen_notices: {c['name']}: LICENSES/{t} does not exist")
    return data


def images_label(imgs, every):
    if set(imgs) == set(every):
        return "all images"
    if set(imgs) == set(PANEL):
        return "the five images with the touch panel"
    rest = [i for i in every if i not in imgs]
    if len(rest) == 1:
        return f"all images but {ENV[rest[0]]}"
    return ", ".join(ENV[i] for i in imgs)


def notices_md(data):
    every = data["all_images"]
    out = ["# Third-party notices", "",
           "SIMUT is distributed under the MIT License ([LICENSE](LICENSE)). Its firmware images",
           "also carry the third-party software below. For each one: who holds the copyright,",
           "the licence, which images carry it, and the file in [LICENSES/](LICENSES) that holds",
           "the licence text, reproduced verbatim as those licences ask of a binary distribution.",
           "",
           "The panel's License screen and the `/license` web page list the same components.",
           "This file is written by `tools/gen_notices.py` from `tools/third_party.toml`: edit",
           "those, run the script, and CI checks that the three lists still agree.",
           "",
           "| Component | Copyright holder | Licence | Images |",
           "|---|---|---|---|"]
    for c in data["component"]:
        lab = images_label(c["images"], every)
        out.append(f"| [{c['name']}](#{anchor(c['name'])}) | {c['holder']} | {c['license']} | {lab} |")
    for c in data["component"]:
        out += ["", f"## {c['name']}", "", c["what"], "",
                f"- Copyright holder: {c['holder']}",
                f"- Licence: {c['license']}, text in "
                + ", ".join(f"[LICENSES/{t}](LICENSES/{t})" for t in [c["text"]] + c.get("also", [])),
                f"- Images: {images_label(c['images'], every)}",
                f"- Source: <{c['url']}>"]
        if c.get("note"):
            out += ["", c["note"]]
    out += ["", "## Compiled, but not in any image", "",
            "Built along the way and dropped by the linker; nothing of them reaches the device.", ""]
    for k, v in data["not_linked"].items():
        out.append(f"- **{k}**: {v}.")
    return "\n".join(out) + "\n"


def anchor(name):
    a = name.lower()
    a = re.sub(r"[^\w\- ]", "", a)
    return a.replace(" ", "-")


def web_block(data):
    every = data["all_images"]
    w = 80
    bar = "=" * w
    lines = [bar, "SIMUT \u2014 Sistema Integrado de Monitoramento Universal e Telemetria",
             "Third-Party Software Notices", bar, ""]
    intro = ("SIMUT is distributed under the MIT License above. Its firmware images also carry "
             "the third-party software listed here. The licence text of each one is reproduced "
             f"in full in THIRD_PARTY_NOTICES.md and the LICENSES directory of the source "
             f"repository, published with every release: {REPO}")
    lines += textwrap.wrap(intro, w) + [""]
    lab = 15
    for n, c in enumerate(data["component"], 1):
        lines += [bar, f"{n}. {c['name']}", bar]
        fields = [("Description:", c["what"]), ("Holder:", c["holder"]),
                  ("License:", c["license"]), ("Images:", images_label(c["images"], every)),
                  ("URL:", c["url"])]
        if c.get("note"):
            fields.append(("Note:", c["note"]))
        for k, v in fields:
            wrapped = textwrap.wrap(v, w - 3 - lab, break_on_hyphens=False, break_long_words=False)
            lines.append(f"   {k:<{lab - 1}} {wrapped[0]}")
            lines += [" " * (3 + lab) + x for x in wrapped[1:]]
        lines.append("")
    lines += [bar, "", "END OF THIRD-PARTY NOTICES"]
    return html.escape("\n".join(lines), quote=True).replace("&#x27;", "'")


def fw_list(data):
    out = ["--- Third-party software ---", ""]
    for c in data["component"]:
        out += [c["name"], f"  {c['holder']} - {c['license']}"]
    out += ["", "Licence texts: THIRD_PARTY_NOTICES.md and",
            "LICENSES/ in the source repository."]
    return "\n".join(out) + "\n"


def web_intro(webui):
    """The five English strings of the /license page that open the screen."""
    page = webui[webui.index("static const char LICENSE_PAGE[]"):]
    out = []
    for k in INTRO_KEYS:
        m = re.search(r'data-i18n="' + k + r'"[^>]*>(.*?)</', page, re.S)
        if not m:
            sys.exit(f"gen_notices: WebUI.h /license has no data-i18n=\"{k}\"")
        out.append(html.unescape(m.group(1)))
    return out


def c_string(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def fw_intro(texts):
    names = ["LIC_OPEN_SUB", "LIC_OPEN_TITLE", "LIC_OPEN_SUMMARY", "LIC_OPEN_NOTE", "LIC_OPEN_LEGAL"]
    out = [FW_INTRO_BEGIN,
           "/* The English text of the /license page, which the screen shows when no",
           " * language pack is loaded. The page itself is the source: edit WebUI.h. */"]
    for n, t in zip(names, texts):
        out.append(f"static const char {n}[] PROGMEM = {c_string(t)};")
    out.append("static const char* const LICENSE_OPENING_EN[] = {")
    out.append("\t" + ", ".join(names) + ",")
    out.append("};")
    out.append("static const char* const LICENSE_OPENING_KEYS[] = {")
    out.append("\t" + ", ".join(f'"{k}"' for k in INTRO_KEYS) + ",")
    out.append("};")
    out.append(FW_INTRO_END)
    return "\n".join(out)


def replace_between(text, begin, end, inner, what):
    a = text.find(begin)
    b = text.find(end)
    if a < 0 or b < 0 or b < a:
        sys.exit(f"gen_notices: markers for {what} not found")
    return text[:a] + inner + text[b + len(end):]


def new_webui(webui, data):
    i = webui.index('data-i18n="lic_notice"')
    a = webui.index("<pre>", i) + len("<pre>")
    b = webui.index("</pre>", a)
    return webui[:a] + web_block(data) + webui[b:]


def new_fwh(fwh, webui, data):
    fwh = replace_between(fwh, FW_INTRO_BEGIN, FW_INTRO_END, fw_intro(web_intro(webui)), "the opening")
    body = (FW_LIST_BEGIN + "\nR\"raw(" + fw_list(data) + ")raw\"\n" + FW_LIST_END)
    return replace_between(fwh, FW_LIST_BEGIN, FW_LIST_END, body, "the list")


def main():
    check = "--check" in sys.argv[1:]
    data = load()
    webui = WEBUI.read_text(encoding="utf-8")
    fwh = FWH.read_text(encoding="utf-8")
    want = {NOTICES: notices_md(data), WEBUI: new_webui(webui, data), FWH: new_fwh(fwh, webui, data)}
    stale = []
    for path, text in want.items():
        cur = path.read_text(encoding="utf-8") if path.exists() else None
        if cur != text:
            stale.append(path.relative_to(ROOT))
            if not check:
                path.write_text(text, encoding="utf-8")
    if check:
        if stale:
            print("[notices] FAIL: out of date with tools/third_party.toml or the /license page: "
                  + ", ".join(map(str, stale)) + " — run python3 tools/gen_notices.py",
                  file=sys.stderr)
            return 1
        print(f"[notices] OK: {len(data['component'])} components, the same in the three lists")
        return 0
    print("[notices] wrote " + (", ".join(map(str, stale)) if stale else "nothing (all current)"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
