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

TWO FAMILIES (2026-10-13)
-------------------------
The SIMUT Wire images (pico_wire, pico_wiretft) carry no radio: no cyw43-driver,
no CYW43439 firmware, no DHCP server, no BTstack, and they carry the ENC28J60
driver the others do not. The page and the panel of an image list the
components of its own family: a /* @IF wifi */ block of WebUI.h and an `#if
SIMUT_NET_LAN` of HelpLicenseEN.h. "all images" on them means all images of
that family, which the Wire's intro says and the radio's, written before there
was a second family, does not; its text is left as it was, so the seven radio
images keep their bytes. THIRD_PARTY_NOTICES.md lists both families, with the
images named against all nine.

THE ANGULO IMAGES (2026-10-14, rewritten 2026-10-30)
----------------------------------------------------
The Angulo touch panel is in two images: pico_wiretft_angulo (outside the nine, a bench image) and, since the C3 switch of
2026-10-30, pico2_w_release (one of the nine; the original panel on the Pico 2 W is the bench image pico2_w_original).
[bench_images] in the TOML names the images outside the nine, and a row names the bench images that carry it, by name: a bench
image no longer inherits the rows of the image it extends, because that was wrong for the original panel's FreeSans Bold faces,
which the Angulo images do not link (nm of the eleven firmware.elf, 2026-10-30).

A row marked `ui = "angulo"` (Bricolage Grotesque, the FreeSans and FreeMono faces, Lucide) is not in the family lists. The
/license page lists it in a block of its own, /* @IF ui_angulo */, which the build cuts from every image with the original
panel ([ui.original].web_omit in tools/features.toml, read by tools/gen_features.py), so the pages of the originals stay
byte for byte. The Angulo's License screen names it in the device's language (UA_TXT( ) in
src/ui/angulo/telas/MenusLicenca.cpp). That text is written by hand, so `--check` holds it to the rows: the name of every
marked row, and its holder and licence as "<holder>, <licence>.", must appear in it. A row's `page_images`, when it has one,
is the set the family lists label it with instead of `images`: the original panel's FreeSans Bold faces are in six images,
and the label the originals' pages have always carried ("the five images with the touch panel") is kept, one registered
deviation, so that those pages do not change.

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
if "--toml" in sys.argv[1:]:   # a copy of the table, for the controls of tools/test_gen_notices.py
    TOML = Path(sys.argv[sys.argv.index("--toml") + 1]).resolve()
LICDIR = ROOT / "LICENSES"
NOTICES = ROOT / "THIRD_PARTY_NOTICES.md"
WEBUI = ROOT / "WebUI.h"
FWH = ROOT / "src" / "HelpLicenseEN.h"
ANGULO_PANEL = ROOT / "src" / "ui" / "angulo" / "telas" / "MenusLicenca.cpp"   # the Angulo's License screen: hand-written, held to the rows
REPO = "https://github.com/angeloINTJ/simut"

PANEL = {"release", "test", "test_https", "asserts", "pico2_release", "wiretft"}
ENV = {"release": "pico_w_release", "test": "pico_w_test", "test_https": "pico_w_test_https",
       "asserts": "pico_w_asserts", "alpha": "pico_w_alpha", "air": "pico_w_air",
       "pico2_release": "pico2_w_release", "wire": "pico_wire", "wiretft": "pico_wiretft",
       "wiretft_angulo": "pico_wiretft_angulo", "pico2_original": "pico2_w_original"}
WIRED = ["wire", "wiretft"]
WORDS = {2: "two", 3: "three", 4: "four", 5: "five", 6: "six", 7: "seven", 8: "eight", 9: "nine"}

WEB_IF_RADIO, WEB_IF_WIRED, WEB_ENDIF = "/* @IF wifi */", "/* @IF !wifi */", "/* @ENDIF */"
WEB_IF_ANGULO = "/* @IF ui_angulo */"   # cut from every image with the original panel ([ui.original].web_omit)
CABEC_ANGULO = "Angulo touch panel: fonts and icons"

# The opening of the License screen, in the order the /license page shows it.
INTRO_KEYS = ["lic_sub", "lic_summary_title", "lic_summary", "lic_summary_note", "lic_legal_note"]

FW_INTRO_BEGIN = "/* BEGIN generated: licence opening — tools/gen_notices.py */"
FW_INTRO_END = "/* END generated: licence opening */"
FW_LIST_BEGIN = "/* BEGIN generated: third-party list — tools/gen_notices.py */"
FW_LIST_END = "/* END generated: third-party list */"


def load():
    data = tomllib.loads(TOML.read_text(encoding="utf-8"))
    every = data["all_images"]
    bench = data.setdefault("bench_images", {})
    for b, v in bench.items():
        if b in every or b not in ENV or v.get("extends") not in every or "what" not in v or "page" not in v:
            sys.exit(f"gen_notices: [bench_images.{b}] must be an image outside all_images, "
                     f"known to ENV, that extends one of them, with a 'what' and a 'page'")
    for c in data["component"]:
        for f in ("name", "holder", "license", "images", "url", "what", "text"):
            if f not in c:
                sys.exit(f"gen_notices: {c.get('name', '?')} has no '{f}'")
        if c["images"] == "all":
            c["images"] = list(every) + list(bench)
        unknown = [i for i in c["images"] if i not in every and i not in bench]
        if unknown:
            sys.exit(f"gen_notices: {c['name']}: unknown image(s) {unknown}")
        if c.get("ui", "angulo") != "angulo":
            sys.exit(f"gen_notices: {c['name']}: ui = {c['ui']!r}, the only panel with rows of its own is \"angulo\"")
        if not set(c.get("page_images", [])) <= set(every):
            sys.exit(f"gen_notices: {c['name']}: page_images names an image outside all_images")
        for t in [c["text"]] + c.get("also", []):
            if not (LICDIR / t).is_file():
                sys.exit(f"gen_notices: {c['name']}: LICENSES/{t} does not exist")
    return data


def images_label(imgs, every):
    """Which of the images in `every` carry a component, in words."""
    imgs = [i for i in imgs if i in every]
    if set(imgs) == set(every):
        return "all images"
    panel = PANEL & set(every)
    if len(panel) > 1 and set(imgs) == panel:
        return f"the {WORDS[len(panel)]} images with the touch panel"
    rest = [i for i in every if i not in imgs]
    if len(rest) == 1:
        return f"all images but {ENV[rest[0]]}"
    if len(rest) == 2:
        return f"all images but {ENV[rest[0]]} and {ENV[rest[1]]}"
    return ", ".join(ENV[i] for i in imgs)


def notices_images(c, data):
    """The `Images` of THIRD_PARTY_NOTICES.md: the label over the nine, then the bench images the
    row names (since 2026-10-30 by name only: a bench image inherits nothing from the image it extends).
    The page and the panel never call this: they label over their family alone."""
    every = data["all_images"]
    nine = [i for i in c["images"] if i in every]
    bench = [ENV[b] for b in data["bench_images"] if b in c["images"]]
    parts = ([images_label(nine, every)] if nine else []) + ([", ".join(bench)] if bench else [])
    return ", and ".join(parts)


def families(data):
    """(radio images, wired images): the two lists a page or a panel shows."""
    every = data["all_images"]
    return [i for i in every if i not in WIRED], [i for i in every if i in WIRED]


def of_family(data, family):
    """The components at least one image of `family` carries, in TOML order. The rows marked `ui` are not in
    a family's list: they have a block of their own on the page (web_angulo) and a text on the Angulo's panel."""
    return [c for c in data["component"] if not c.get("ui") and set(c["images"]) & set(family)]


def angulo_rows(data):
    """The rows of the Angulo panel's fonts and icons, in TOML order."""
    return [c for c in data["component"] if c.get("ui") == "angulo"]


def angulo_panel_problems(data, source):
    """What the Angulo's License screen leaves out of the `ui = "angulo"` rows. Its text is written by hand
    (UA_TXT( ) in MenusLicenca.cpp), so the table cannot write it; it can hold it: the name of each row, and
    its holder and licence as "<holder>, <licence>." (the holder up to its parenthesis), must be there."""
    out = []
    for c in angulo_rows(data):
        pair = f"{c['holder'].split(' (')[0]}, {c['license']}."
        for what, s in (("name", c["name"]), ("holder and licence", pair)):
            if s not in source:
                out.append(f"{c['name']}: the {what} {s!r} is not in {ANGULO_PANEL.relative_to(ROOT)}")
    return out


def notices_md(data):
    every = data["all_images"]
    out = ["# Third-party notices", "",
           "SIMUT is distributed under the MIT License ([LICENSE](LICENSE)). Its firmware images",
           "also carry the third-party software below. For each one: who holds the copyright,",
           "the licence, which images carry it, and the file in [LICENSES/](LICENSES) that holds",
           "the licence text, reproduced verbatim as those licences ask of a binary distribution.",
           "",
           "The panel's License screen and the `/license` web page of an image list the same",
           "components: those of its family, the seven images with the radio or the two SIMUT",
           "Wire images (`pico_wire`, `pico_wiretft`). This file is written by",
           "`tools/gen_notices.py` from `tools/third_party.toml`: edit those, run the script,",
           "and CI checks that the lists still agree.",
           ""]
    names = []
    for c in angulo_rows(data):
        names += [f"`{ENV[i]}`" for i in c["images"] if f"`{ENV[i]}`" not in names]
    para = ("The images with the Angulo touch panel (" + ", ".join(names) + ") list their own fonts and icons "
            "as well: on `/license` in a block that the build cuts from every image with the original panel, "
            "and on the Angulo's License screen in the device's language.")
    out += textwrap.wrap(para, 86, break_on_hyphens=False, break_long_words=False) + [""]
    for b, v in data["bench_images"].items():
        para = (f"`{ENV[b]}` is outside the nine: {v['what']}. This file lists it against the rows that "
                f"name it. {v['page']}")
        out += textwrap.wrap(para, 86, break_on_hyphens=False, break_long_words=False) + [""]
    out += ["| Component | Copyright holder | Licence | Images |",
            "|---|---|---|---|"]
    for c in data["component"]:
        lab = notices_images(c, data)
        out.append(f"| [{c['name']}](#{anchor(c['name'])}) | {c['holder']} | {c['license']} | {lab} |")
    for c in data["component"]:
        out += ["", f"## {c['name']}", "", c["what"], "",
                f"- Copyright holder: {c['holder']}",
                f"- Licence: {c['license']}, text in "
                + ", ".join(f"[LICENSES/{t}](LICENSES/{t})" for t in [c["text"]] + c.get("also", [])),
                f"- Images: {notices_images(c, data)}",
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


WEB_W, WEB_LAB = 80, 15
WEB_BAR = "=" * WEB_W
WEB_END = "END OF THIRD-PARTY NOTICES"


def web_escape(text):
    return html.escape(text, quote=True).replace("&#x27;", "'")


def web_fields(c, images):
    """The labelled lines of one item of the /license list, and the blank line after it."""
    fields = [("Description:", c["what"]), ("Holder:", c["holder"]),
              ("License:", c["license"]), ("Images:", images), ("URL:", c["url"])]
    if c.get("note"):
        fields.append(("Note:", c["note"]))
    out = []
    for k, v in fields:
        wrapped = textwrap.wrap(v, WEB_W - 3 - WEB_LAB, break_on_hyphens=False, break_long_words=False)
        out.append(f"   {k:<{WEB_LAB - 1}} {wrapped[0]}")
        out += [" " * (3 + WEB_LAB) + x for x in wrapped[1:]]
    return out + [""]


def web_parts(data, family):
    """(list, trailer) of one family's block, escaped for the page. The list ends with the blank line after its
    last item; the trailer is the closing bar and the END line. The page keeps the trailer outside the family
    blocks so that the Angulo block (web_angulo) can sit between the two: for an image without that block the
    build's cut gives list + trailer, which is what the one block was, so the originals' pages stay byte for
    byte (S10c C4, 2026-10-30)."""
    every = family
    lines = [WEB_BAR, "SIMUT \u2014 Sistema Integrado de Monitoramento Universal e Telemetria",
             "Third-Party Software Notices", WEB_BAR, ""]
    if set(family) == set(WIRED):
        carry = ("Its SIMUT Wire images, " + " and ".join(ENV[i] for i in family)
                 + ", also carry the third-party software listed here, and \"all images\" "
                 "below means those two.")
    else:
        carry = "Its firmware images also carry the third-party software listed here."
    intro = (f"SIMUT is distributed under the MIT License above. {carry} The licence text of "
             "each one is reproduced "
             f"in full in THIRD_PARTY_NOTICES.md and the LICENSES directory of the source "
             f"repository, published with every release: {REPO}")
    lines += textwrap.wrap(intro, WEB_W) + [""]
    for n, c in enumerate(of_family(data, family), 1):
        lines += [WEB_BAR, f"{n}. {c['name']}", WEB_BAR]
        lines += web_fields(c, images_label(c.get("page_images", c["images"]), every))
    return web_escape("\n".join(lines) + "\n"), web_escape(WEB_BAR + "\n\n" + WEB_END)


def web_block(data, family):
    """The whole block of one family, as the page had it before the Angulo block existed."""
    return "".join(web_parts(data, family))


def web_angulo(data):
    """The Angulo block: the rows marked `ui = "angulo"`, lettered (their number in a family list would depend
    on the family), after the family's list and before the closing bar."""
    rows = angulo_rows(data)
    names = []
    for c in rows:
        names += [ENV[i] for i in c["images"] if ENV[i] not in names]
    intro = ("The images with the Angulo touch panel (" + ", ".join(names) + ") draw with the fonts and icons "
             "below, besides the software listed above. The licence text of each one is reproduced in full in "
             f"THIRD_PARTY_NOTICES.md and the LICENSES directory of the source repository: {REPO}")
    lines = [WEB_BAR, CABEC_ANGULO, WEB_BAR] + textwrap.wrap(intro, WEB_W) + [""]
    for n, c in enumerate(rows):
        lines += [WEB_BAR, f"{chr(ord('A') + n)}. {c['name']}", WEB_BAR]
        lines += web_fields(c, ", ".join(ENV[i] for i in c["images"]))
    return web_escape("\n".join(lines) + "\n")


def fw_list(data, family):
    out = ["--- Third-party software ---", ""]
    for c in of_family(data, family):
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
    """The <pre> of /license holds one list per family, the Angulo block and the closing bar. Each @ENDIF
    closes the blank line after the last item of its block and the trailer follows on the next line, so the
    cut (tools/build_webui_gz.py) leaves <pre>list + trailer</pre> for an image with the original panel,
    byte for byte what the one-block page was."""
    radio, wired = families(data)
    i = webui.index('data-i18n="lic_notice"')
    a = webui.index("<pre>", i) + len("<pre>")
    b = webui.index("</pre>", a)
    items_r, trailer = web_parts(data, radio)
    items_w, trailer_w = web_parts(data, wired)
    assert trailer == trailer_w
    pre = (f"{WEB_IF_RADIO}\n{items_r}{WEB_ENDIF}\n"
           f"{WEB_IF_WIRED}\n{items_w}{WEB_ENDIF}\n"
           f"{WEB_IF_ANGULO}\n{web_angulo(data)}{WEB_ENDIF}\n{trailer}")
    return webui[:a] + pre + webui[b:]


def new_fwh(fwh, webui, data):
    radio, wired = families(data)
    fwh = replace_between(fwh, FW_INTRO_BEGIN, FW_INTRO_END, fw_intro(web_intro(webui)), "the opening")
    body = (FW_LIST_BEGIN + "\n#if SIMUT_NET_LAN\nR\"raw(" + fw_list(data, wired) + ")raw\"\n"
            "#else\nR\"raw(" + fw_list(data, radio) + ")raw\"\n#endif\n" + FW_LIST_END)
    return replace_between(fwh, FW_LIST_BEGIN, FW_LIST_END, body, "the list")


def main():
    check = "--check" in sys.argv[1:]
    data = load()
    webui = WEBUI.read_text(encoding="utf-8")
    fwh = FWH.read_text(encoding="utf-8")
    want = {NOTICES: notices_md(data), WEBUI: new_webui(webui, data), FWH: new_fwh(fwh, webui, data)}
    problems = angulo_panel_problems(data, ANGULO_PANEL.read_text(encoding="utf-8"))
    stale = []
    for path, text in want.items():
        cur = path.read_text(encoding="utf-8") if path.exists() else None
        if cur != text:
            stale.append(path.relative_to(ROOT))
            if not check:
                path.write_text(text, encoding="utf-8")
    for p in problems:
        print("[notices] FAIL: the Angulo's License screen disagrees with tools/third_party.toml: " + p, file=sys.stderr)
    if check:
        if stale:
            print("[notices] FAIL: out of date with tools/third_party.toml or the /license page: "
                  + ", ".join(map(str, stale)) + " — run python3 tools/gen_notices.py",
                  file=sys.stderr)
        if stale or problems:
            return 1
        on_lists = len({c["name"] for fam in families(data) for c in of_family(data, fam)})
        print(f"[notices] OK: {on_lists} components, the same in the three lists; "
              f"{len(angulo_rows(data))} more for the Angulo images (the Angulo block of /license, the Angulo's "
              "License screen, THIRD_PARTY_NOTICES.md)")
        return 0
    print("[notices] wrote " + (", ".join(map(str, stale)) if stale else "nothing (all current)"))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
