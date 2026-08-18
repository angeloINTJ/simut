"""
PlatformIO pre-build script — regenerates WebUI_GZ.h from WebUI.h.

Compresses PROGMEM blocks with gzip level 9 and writes the header consumed
by WebManager_Core.cpp, plus the <PAGE>_SERVE macro that binds each route to
the partition its asset landed on.

The output depends on three things — the source, this script, and the env's
page layout — and the stamp on line 2 carries all three, so the "already up to
date" shortcut cannot hand one environment another one's header.

Project: SIMUT
License: MIT
"""

import os
import re
import gzip
import hashlib

try:
    import minify_html
    _HAS_MINIFY_HTML = True
except ImportError:
    _HAS_MINIFY_HTML = False

# gzip already compresses whitespace efficiently; minify_html kept OFF.
_HAS_MINIFY_HTML = False

try:
    Import("env")
    PROJECT_DIR = env.subst("$PROJECT_DIR")
    PIOENV      = env["PIOENV"]
    DIET_RAW    = env.GetProjectOption("custom_fs_pages", "")
except NameError:
    # Running standalone (not inside PlatformIO) — use CWD. No env means no
    # diet: the standalone path is what build_release_pio.sh calls, and a
    # release package carries the complete interface by definition.
    import sys

    PROJECT_DIR = os.getcwd()
    PIOENV      = ""
    DIET_RAW    = ""
INPUT_FILE  = os.path.join(PROJECT_DIR, "WebUI.h")
OUTPUT_FILE = os.path.join(PROJECT_DIR, "src", "WebUI_GZ.h")

# ── Where each page lives: firmware image, or LittleFS ──────────────────────
#
# A page moved to the filesystem is still gzipped — un-gzipping would make it
# ~5x BIGGER, not smaller — it just lives on the 1 MB filesystem partition
# instead of the app slot, and WebManager::serveProtectedFsPage streams it
# from there. The trade is a flash read per request instead of a PROGMEM
# pointer, plus a deploy step, in exchange for app-slot headroom.
#
# THE RULE, from 2026-08-17: a SHIPPING image carries the complete interface.
# Every page, in flash, no deploy step, no route that can answer "Page asset
# missing". The diet is declared PER ENVIRONMENT (custom_fs_pages in
# platformio.ini) and _resolve_diet below refuses it outright for the envs in
# SHIPPING_ENVS.
#
# This used to be one global constant, and that is what made the two images
# fight over one budget. pico_w_test carries the full serial CLI and had 152 B
# of real headroom in August 2026, so LICENSE_PAGE and then ALARMS_PAGE were
# moved out — out of BOTH images, because the constant could not tell them
# apart. The release paid for the test image's problem, and paid twice: the
# pages left the firmware, and data/web/*.gz became a deploy step that the zips
# did not carry (units built from the v2.2.5-beta zip had a broken /license).
#
# Measured on 2026-08-17, at e14170f, with arm-none-eabi-size:
#
#   release, 10 pages embedded + 2 on LittleFS ...... 30892 B free
#   release, all 12 embedded ....................... 18740 B free   <- linked
#   test,    all 12 embedded ....................... overflowed by 856 B
#   test,    all 12 + SIMUT_MDNS=0 ................. 14520 B free
#   test,    all 12 + SIMUT_MDNS=0 + strtol ........ 22044 B free   <- shipped
#
# So the release never needed the diet at all, and the test image ends up with
# twice the headroom it had while carrying MORE pages than before. The
# mechanism stays because the day instrumentation needs 30 KB again, it has to
# come out of the test image and nowhere else.
#
# PICKING A PAGE FOR A DIET (only ever for a test env): big AND rarely opened
# AND not needed to bring the device up. All three, or the cure is worse.
# CFG_PAGE was tried in July 2026 and broke the bootstrap — a freshly
# formatted device answered /config, the page you need to configure it, with
# "Page asset missing". FILE_PAGE is circular: it is how you upload the very
# file that is missing. HIST_PAGE is the biggest at 32 KB and the hottest page
# there is; moving it costs +12.5 ms per load (measured), because the mutex is
# taken once per KB, not once per request. LOGIN_PAGE, FORCE_CHPASS_PAGE,
# STYLE_CSS and LANG_JS are not in DIETABLE at all: the first two lock the
# device out if the file is missing, and the other two are loaded by eight
# pages each and have no FS route (they are served with their own cache
# headers, not through serveProtectedFsPage).
#
# DEPLOYING a diet page: the file must reach /web/ on the device or the route
# answers "Page asset missing". Upload it through the Files page or POST to
# /api/upload. NOT `uploadfs` — that reformats the partition and takes the
# history, logs and calib.csv with it.

# Envs that a user's device runs. These carry the whole interface, always.
SHIPPING_ENVS = {"pico_w_release", "pico_w_alpha", "pico_w_asserts"}

# The only pages a diet may name, and the file each becomes on the device.
# The set is exactly the routes that go through serveProtectedPage, which is
# the one server path that has a filesystem twin.
DIETABLE = {
    "DASH_PAGE":    "dash.html.gz",
    "HIST_PAGE":    "history.html.gz",
    "CFG_PAGE":     "config.html.gz",
    "NET_PAGE":     "network.html.gz",
    "USR_PAGE":     "users.html.gz",
    "FILE_PAGE":    "files.html.gz",
    "ALARMS_PAGE":  "alarms.html.gz",
    "LICENSE_PAGE": "license.html.gz",
}

FS_OUT_DIR = os.path.join(PROJECT_DIR, "data", "web")


def _resolve_diet() -> dict:
    """Read custom_fs_pages for this env, and refuse it where it must not be.

    Failing the build is the point. The invariant "a shipping image carries
    the complete interface" is worth exactly as much as the thing that
    enforces it, and the last enforcement was a comment.
    """
    names = [n for n in re.split(r"[,\s]+", DIET_RAW) if n]
    if not names:
        return {}
    if PIOENV in SHIPPING_ENVS:
        raise SystemExit(
            f"build_webui_gz: {PIOENV} e um ambiente de producao e declarou "
            f"custom_fs_pages = {', '.join(names)}.\n"
            f"Imagem de producao carrega a interface INTEIRA no firmware: sem "
            f"passo de implantacao e sem rota que responda 'Page asset "
            f"missing'. A dieta existe so para os ambientes instrumentados."
        )
    unknown = [n for n in names if n not in DIETABLE]
    if unknown:
        raise SystemExit(
            f"build_webui_gz: custom_fs_pages nomeia {', '.join(unknown)}, que "
            f"nao pode sair do firmware.\n"
            f"Elegiveis: {', '.join(sorted(DIETABLE))}.\n"
            f"As demais ou trancam o aparelho se o arquivo faltar (login, "
            f"force_chpass) ou nao tem rota de filesystem (style.css, lang.js)."
        )
    return {n: DIETABLE[n] for n in names}


FS_PAGES = _resolve_diet()

# The layout is part of the build identity, not just the source hash: two envs
# generate different headers from the same WebUI.h, and without this the
# idempotency check below would hand the second env the first one's header.
LAYOUT_TAG = ",".join(sorted(FS_PAGES)) or "all-embedded"

# Languages enabled in firmware.
ENABLED_LANGS = {"en", "pt"}

_LANG_BEGIN = re.compile(
    r'^\s*(?://|<!--)\s*@LANG_BEGIN:(\w+)\s*(?:-->)?\s*$'
)
_LANG_END = re.compile(
    r'^\s*(?://|<!--)\s*@LANG_END:(\w+)\s*(?:-->)?\s*$'
)

# Regex para strings PROGMEM inline no WebUI.h
_PROGMEM_RE = re.compile(
    r'static const char (\w+)\[\] PROGMEM = R"raw\((.*?)\)raw";',
    re.DOTALL,
)


def _strip_disabled_langs(content: str) -> str:
    """Remove @LANG_BEGIN/@LANG_END blocks for disabled languages."""
    out = []
    skip_depth = 0
    for line in content.splitlines(keepends=True):
        mb = _LANG_BEGIN.match(line)
        me = _LANG_END.match(line)
        if mb:
            if mb.group(1) not in ENABLED_LANGS:
                skip_depth += 1
            continue
        if me:
            if skip_depth > 0:
                skip_depth -= 1
            continue
        if skip_depth > 0:
            continue
        out.append(line)
    return "".join(out)


def _hash_file(path: str) -> str:
    """Fast SHA256 hash to detect file changes."""
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


# Conservative minification applied BEFORE gzip. Reduces ~10-20% before
# compression, saving a few KB per page post-gzip (gzip already handles
# whitespace but comments and structure still inflate).
# Strategy: tokenize string literals first to avoid touching them, then
# strip comments and collapse whitespace outside strings.
_STR_PLACEHOLDER = "\x00STR{}\x00"
_STRING_RE = re.compile(
    r"""(`(?:\\.|[^`\\])*`        # template literal
        |"(?:\\.|[^"\\])*"        # double-quoted
        |'(?:\\.|[^'\\])*')       # single-quoted
    """,
    re.VERBOSE,
)


def _minify_web_block(src: str) -> str:
    """Minify mixed HTML+CSS+JS preserving string literals.

    If the `minify_html` library (Rust-based, aggressive but correct) is
    available, use it. Otherwise fall back to a conservative regex minifier.

    Conservative: doesn't touch pre/textarea/code blocks and preserves
    anything between quotes/backticks.
    """
    if _HAS_MINIFY_HTML:
        # minify_html applies HTML + JS + CSS minification with syntax
        # awareness. Conservative flags:
        #   keep_closing_tags: True — keeps </p> </li> etc (compat)
        #   keep_html_and_head_opening_tags: True — keeps <html><head>
        #   allow_removing_spaces_between_attributes: True — aggressive OK
        #   minify_css/js: True — minify embedded blocks
        try:
            return minify_html.minify(
                src,
                minify_css=True,   # whitespace + shorthand only, preserves hex/numeric values
                minify_js=False,   # kept OFF: template literals may break with mangling
                keep_closing_tags=True,
                keep_html_and_head_opening_tags=True,
                allow_removing_spaces_between_attributes=True,
                remove_bangs=True,
                remove_processing_instructions=True,
            )
        except Exception as e:
            print(f"build_webui_gz: minify_html failed ({e}), using regex fallback.")

    # Fallback regex (without minify_html installed)
    # 1. Save strings
    saved = []

    def _save(m):
        saved.append(m.group(0))
        return _STR_PLACEHOLDER.format(len(saved) - 1)

    src = _STRING_RE.sub(_save, src)

    # 2. HTML comments
    src = re.sub(r"<!--.*?-->", "", src, flags=re.DOTALL)
    # 3. CSS/JS block comments
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.DOTALL)
    # 4. JS line comments — only inside <script>...</script> to avoid
    #    confusing URLs (`http://`) in HTML href/src attributes.
    def _strip_js_lines(m):
        body = m.group(2)
        body = re.sub(r"^[ \t]*//[^\n]*$", "", body, flags=re.MULTILINE)
        return m.group(1) + body + m.group(3)

    src = re.sub(
        r"(<script\b[^>]*>)(.*?)(</script>)",
        _strip_js_lines,
        src,
        flags=re.DOTALL,
    )

    # 5. Collapse whitespace
    src = re.sub(r"[ \t]+", " ", src)
    src = re.sub(r" *\n+ *", "\n", src)
    src = re.sub(r"\n{2,}", "\n", src)

    # 6. Tag adjacency: `> <` -> `><` (only when both are HTML tags;
    #    conservative — only between literal `>` and `<` without other
    #    chars between).
    src = re.sub(r">[ \t]+<", "><", src)
    src = re.sub(r">\n+<", "><", src)

    # 7. Restore strings
    def _restore(m):
        idx = int(m.group(1))
        return saved[idx]

    src = re.sub(r"\x00STR(\d+)\x00", _restore, src)

    return src


# /style.css e /lang.js sao servidos com Cache-Control: public, max-age=604800
# (WebManager_Util.cpp). Sem versionar a URL, gravar um firmware novo NAO troca
# o que o navegador ja tem: ele nem pergunta durante 7 dias, e a interface
# atualizada so aparecia limpando os dados do site na mao. Carimbar a URL com um
# tag derivado do hash do WebUI.h mantem o cache longo (o Pico e lento e reservir
# esses arquivos a cada navegacao custa) e mesmo assim invalida sozinho quando os
# assets mudam. O servidor casa a rota pelo caminho e ignora a query — verificado
# contra o dispositivo antes de adotar isto.
_ASSET_URLS = ("/style.css", "/lang.js")


def _stamp_assets(src: str, tag: str) -> str:
    """Carimba ?v=<tag> nas URLs dos assets compartilhados."""
    for url in _ASSET_URLS:
        src = src.replace('"%s"' % url, '"%s?v=%s"' % (url, tag))
    return src


# Real ratios across the ten pages sit between 61% and 97%. A page that comes
# out far below that has not been minified, it has been eaten.
MIN_RETAINED_FRACTION = 0.40


def _assert_not_gutted(name: str, original: int, minified: int) -> None:
    """Fail the build when minification silently drops most of a page.

    The syntax check above cannot see this: what survives is valid JavaScript,
    just missing the half of the page that mattered. ALARMS_PAGE shipped once
    at 9% of its source — every handler gone, the page still parsing — because
    a JS comment contained a double quote and an apostrophe, which is the same
    trigger that cost a release before.

    A ratio is a blunt instrument, but the failure it catches is not subtle.
    """
    if original <= 0:
        return
    retained = minified / original
    if retained >= MIN_RETAINED_FRACTION:
        return
    raise SystemExit(
        f"build_webui_gz: {name} ENCOLHEU DEMAIS na minificacao — "
        f"{original} -> {minified} B ({retained:.0%} do original, "
        f"minimo {MIN_RETAINED_FRACTION:.0%}).\n"
        f"Causa comum: aspas ou apostrofo dentro de comentario JS no WebUI.h. "
        f"O `node --check` nao detecta: o que sobra continua sintaticamente valido."
    )


def _syntax_check_scripts(name: str, html: str) -> None:
    """Valida a sintaxe dos <script> inline do HTML minificado via `node --check`.

    O minificador preserva strings ANTES de remover comentarios: aspas dentro
    de um comentario JS confundem essa ordem e ja produziram paginas com 13KB
    de JS devorado (pagina /history quebrada em producao). Falha o BUILD alto
    e claro em vez de embarcar JS invalido. No-op se node nao estiver no PATH.
    """
    import re as _re
    import shutil
    import subprocess
    import tempfile

    node = shutil.which("node") or shutil.which("nodejs")
    if not node:
        return
    for i, script in enumerate(_re.findall(r"<script>(.*?)</script>", html, _re.DOTALL)):
        if not script.strip():
            continue
        with tempfile.NamedTemporaryFile("w", suffix=".js", delete=False) as tf:
            tf.write(script)
            path = tf.name
        try:
            r = subprocess.run([node, "--check", path], capture_output=True, text=True)
            if r.returncode != 0:
                raise SystemExit(
                    f"build_webui_gz: SINTAXE JS INVALIDA apos minificacao em "
                    f"{name} <script>[{i}] — build abortado.\n"
                    f"Causa comum: aspas dentro de comentarios JS no WebUI.h.\n"
                    f"{r.stderr.strip()[:400]}"
                )
        finally:
            os.unlink(path)


def _prune_stale_fs_pages() -> None:
    """Delete the data/web outputs this layout does not produce.

    A page brought back into the firmware leaves its .gz behind, and a leftover
    is worse than clutter: it still looks like a deploy artifact, so the next
    reader takes it as evidence that the route is served from the filesystem.
    Only names this script itself can emit are ever removed — nothing else in
    data/web is touched.
    """
    if not os.path.isdir(FS_OUT_DIR):
        return
    for fn in DIETABLE.values():
        if fn in FS_PAGES.values():
            continue
        path = os.path.join(FS_OUT_DIR, fn)
        if os.path.isfile(path):
            os.remove(path)
            print(
                f"build_webui_gz: removed stale data/web/{fn} — that page is in "
                f"the firmware image now."
            )


def generate() -> None:
    if not os.path.isfile(INPUT_FILE):
        print("build_webui_gz: WebUI.h not found — skipping generation.")
        return

    # Idempotency: skip if nothing changed.
    #
    # The stamp covers all three things the output depends on: the source, the
    # generator itself, and the layout. The generator half matters because the
    # skip only became real today — the check read line 1 while the hash was
    # written on line 2, so it never once matched and every build regenerated.
    # Turning it on without hashing this file would mean editing the minifier
    # and getting yesterday's pages.
    input_hash = _hash_file(INPUT_FILE)
    gen_hash = _hash_file(os.path.join(PROJECT_DIR, "tools", "build_webui_gz.py"))
    stamp = f"{input_hash} gen={gen_hash[:12]} layout={LAYOUT_TAG}"
    if os.path.isfile(OUTPUT_FILE):
        # The generated .h carries the input hash AND the layout in the first
        # comment line. The layout half is not cosmetic: `pio run -e pico_w_test
        # && pio run -e pico_w_release` compiles the same WebUI.h into two
        # different headers, and matching on the hash alone would hand the
        # second env whatever the first one left behind — a release image with
        # pages missing, or a test image over the ceiling, from a build that
        # printed "up to date".
        with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
            head = f.readline() + f.readline()
        # The FS_PAGES outputs are not covered by the header hash: deleting one
        # would otherwise be invisible here and the build would quietly ship a
        # firmware whose /config has no page to serve.
        fs_ok = all(
            os.path.isfile(os.path.join(FS_OUT_DIR, fn)) for fn in FS_PAGES.values()
        )
        if stamp in head and fs_ok:
            print(
                f"build_webui_gz: WebUI_GZ.h is up to date (layout={LAYOUT_TAG}) "
                f"— skipping."
            )
            return

    with open(INPUT_FILE, "r", encoding="utf-8") as f:
        content = f.read()

    content = _strip_disabled_langs(content)
    matches = _PROGMEM_RE.findall(content)

    out_lines = [
        "// Auto-generated by tools/build_webui_gz.py — do not edit.",
        f"// Source hash: {stamp}",
        f"// Env: {PIOENV or '(standalone)'}",
        "#pragma once",
        "#include <Arduino.h>",
        "namespace WebUI_GZ {",
        "",
    ]

    # One SERVE macro per dietable page, emitted next to the asset it serves.
    # The handler in WebManager_Auth.cpp is then one line that reads the same
    # whichever partition the page landed on. Before this, moving a page meant
    # editing the diet here AND the handler there, two files that could
    # disagree in silence: the header would stop emitting the array and the
    # build would die, or worse, the handler kept streaming a filesystem path
    # for a page that was back in flash.
    serve_macros = []

    total_in = 0
    total_min = 0
    total_gz = 0
    asset_tag = input_hash[:8]
    for name, html_content in matches:
        original_len = len(html_content)
        minified = _minify_web_block(_stamp_assets(html_content, asset_tag))
        _syntax_check_scripts(name, minified)
        _assert_not_gutted(name, original_len, len(minified))
        # mtime=0, or the gzip header carries the build clock and firmware.bin
        # differs on every build from identical sources. That makes a released
        # image impossible to reproduce, and makes "is this the binary we
        # tested?" unanswerable by hash — which is exactly the question a
        # firmware release has to answer.
        compressed = gzip.compress(minified.encode("utf-8"), compresslevel=9, mtime=0)
        hex_parts = [f"0x{b:02x}" for b in compressed]

        array_lines = []
        for i in range(0, len(hex_parts), 16):
            array_lines.append("    " + ", ".join(hex_parts[i : i + 16]))

        array_str = ",\n".join(array_lines)
        length = len(compressed)

        total_in  += original_len
        total_min += len(minified)
        total_gz  += length

        ratio = length / max(original_len, 1) * 100

        if name in FS_PAGES:
            os.makedirs(FS_OUT_DIR, exist_ok=True)
            fs_path = os.path.join(FS_OUT_DIR, FS_PAGES[name])
            with open(fs_path, "wb") as fo:
                fo.write(compressed)
            serve_macros.append(
                f"// {name}: {length} bytes gz on LittleFS — NOT in this image.\n"
                f"#define {name}_SERVE(perm) "
                f'serveProtectedFsPage((perm), "/web/{FS_PAGES[name]}")'
            )
            print(
                f"build_webui_gz: {name} -> data/web/{FS_PAGES[name]} "
                f"({length} bytes gz) — served from LittleFS, not linked."
            )
            continue

        gz_name = name + "_GZ"
        out_lines.append(
            f"// {name}: {original_len} -> {len(minified)} (min) -> {length} bytes (gz, {ratio:.1f}%)"
        )
        out_lines.append(
            f"static const uint8_t {gz_name}[] PROGMEM = {{\n{array_str}\n}};"
        )
        out_lines.append(f"static const size_t {gz_name}_LEN = {length};\n")
        if name in DIETABLE:
            serve_macros.append(
                f"#define {name}_SERVE(perm) serveProtectedPage((perm), "
                f"WebUI_GZ::{gz_name}, WebUI_GZ::{gz_name}_LEN)"
            )

    out_lines.append("}")
    out_lines.append("")
    out_lines.append("// Route bindings — see the SERVE macro note in build_webui_gz.py.")
    out_lines.extend(serve_macros)

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(out_lines) + "\n")

    _prune_stale_fs_pages()

    print(
        f"build_webui_gz: {len(matches)} arrays | layout={LAYOUT_TAG} | "
        f"input {total_in} -> minified {total_min} ({100*total_min/max(total_in,1):.1f}%) "
        f"-> gzipped {total_gz} ({100*total_gz/max(total_in,1):.1f}%)"
    )


generate()
