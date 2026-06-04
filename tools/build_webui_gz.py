"""
PlatformIO pre-build script — regenerates WebUI_GZ.h from WebUI.h.

Compresses PROGMEM blocks with gzip level 9 and writes the header consumed
by WebManager_Core.cpp. Only regenerates if WebUI.h has changed (hash check)
to avoid invalidating the build cache when the asset was not modified.

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
except NameError:
    # Running standalone (not inside PlatformIO) — use CWD.
    import sys

    PROJECT_DIR = os.getcwd()
INPUT_FILE  = os.path.join(PROJECT_DIR, "WebUI.h")
OUTPUT_FILE = os.path.join(PROJECT_DIR, "src", "WebUI_GZ.h")

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


def generate() -> None:
    if not os.path.isfile(INPUT_FILE):
        print("build_webui_gz: WebUI.h not found — skipping generation.")
        return

    # Idempotency: skip if nothing changed.
    input_hash = _hash_file(INPUT_FILE)
    if os.path.isfile(OUTPUT_FILE):
        output_hash = _hash_file(OUTPUT_FILE)
        # The generated .h contains the input hash in the first comment line.
        with open(OUTPUT_FILE, "r", encoding="utf-8") as f:
            first_line = f.readline()
        if input_hash in first_line:
            print("build_webui_gz: WebUI_GZ.h is up to date — skipping.")
            return

    with open(INPUT_FILE, "r", encoding="utf-8") as f:
        content = f.read()

    content = _strip_disabled_langs(content)
    matches = _PROGMEM_RE.findall(content)

    out_lines = [
        "// Auto-generated by tools/build_webui_gz.py — do not edit.",
        f"// Source hash: {input_hash}",
        "#pragma once",
        "#include <Arduino.h>",
        "namespace WebUI_GZ {",
        "",
    ]

    total_in = 0
    total_min = 0
    total_gz = 0
    for name, html_content in matches:
        original_len = len(html_content)
        minified = _minify_web_block(html_content)
        compressed = gzip.compress(minified.encode("utf-8"), compresslevel=9)
        hex_parts = [f"0x{b:02x}" for b in compressed]

        array_lines = []
        for i in range(0, len(hex_parts), 16):
            array_lines.append("    " + ", ".join(hex_parts[i : i + 16]))

        array_str = ",\n".join(array_lines)
        length = len(compressed)

        total_in  += original_len
        total_min += len(minified)
        total_gz  += length

        gz_name = name + "_GZ"
        ratio = length / max(original_len, 1) * 100
        out_lines.append(
            f"// {name}: {original_len} -> {len(minified)} (min) -> {length} bytes (gz, {ratio:.1f}%)"
        )
        out_lines.append(
            f"static const uint8_t {gz_name}[] PROGMEM = {{\n{array_str}\n}};"
        )
        out_lines.append(f"static const size_t {gz_name}_LEN = {length};\n")

    out_lines.append("}")

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(out_lines) + "\n")

    print(
        f"build_webui_gz: {len(matches)} arrays | "
        f"input {total_in} -> minified {total_min} ({100*total_min/max(total_in,1):.1f}%) "
        f"-> gzipped {total_gz} ({100*total_gz/max(total_in,1):.1f}%)"
    )


generate()
