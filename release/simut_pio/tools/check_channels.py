#!/usr/bin/env python3
"""Build guard: keep channel knowledge inside the channel table.

A table only stays the single source of truth if nothing is allowed to keep a
private copy. That is not hypothetical here — histV4ChannelPrefix() already
mapped channels to 't'/'u'/'p', the calibration code hardcoded the same letters
anyway, and the reader-side whitelist that copy grew ("prefix != 't' && != 'u'")
silently refused every pressure row ever written. The offset persisted
correctly and was applied to nothing.

WHAT FAILS THE BUILD
  A literal channel letter in a calibration or history code path. Those must
  come from channelInfo(ch).letter, which is the invariant layers 1 and 2
  established and the one whose violation cost a release.

WHAT IS ONLY REPORTED
  Fields named per quantity (tempMin, humMax, hasHum, ...). Those are the
  alarm-threshold and display layers, still pending the SensorRecord schema
  bump — layer 3. Counting them here keeps the backlog visible instead of
  letting it look finished. Run with --debt to list them.

Usage:  python3 tools/check_channels.py [--debt] [--list]
Exit:   0 clean, 1 a hardcoded letter escaped the table.
"""
import os
import re
import sys

# Runs both as a PlatformIO pre: script and from a shell. Under SCons the script
# is exec'd, so `__file__` does not exist and the project root has to come from
# the injected env instead.
try:
    Import("env")  # noqa: F821 — provided by SCons
    _SCONS = True
    ROOT = env.subst("$PROJECT_DIR")  # noqa: F821
except NameError:
    _SCONS = False
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

TABLE = os.path.join('src', 'sensors', 'SensorChannelTable.h')

# The vocabulary and the table itself obviously get to name channels.
TABLE_FILES = {
    TABLE,
    os.path.join('src', 'sensors', 'SensorChannels.h'),
    os.path.join('src', 'sensors', 'SensorPresets.h'),
}

# Layer 3 backlog: each of these still parses or emits a channel letter by hand.
# Listing them is a promise to convert, not an exemption — remove an entry as
# soon as it reads the table.
LETTER_DEBT = {
    # {tN}/{uN}/{pN} telemetry placeholders. Generalizing them means the
    # template syntax itself becomes table-driven, which changes user-visible
    # templates, so it belongs with the schema bump rather than beside it.
    os.path.join('src', 'TelemetryManager.cpp'),
}

# Named-per-quantity fields. Not a failure: these are SensorRecord's alarm
# thresholds (tempMin/humMax), the TFT layer, and the legacy JSON keys the web
# API still ships for one release so a cached page keeps working.
LEGACY_NAMES = re.compile(
    r'\b(?:temp|hum|press|lux)(?:Read|Offset|Min|Max)\b'
    r'|\bref(?:Temp|Hum|Press|Lux)\b'
    r'|\bhas(?:Hum|Press|Lux)\b'
)

CHANNEL_LETTER = re.compile(r"'([tupl])'")
# Only in files that actually deal with calibration rows or V4 measurement keys.
# Elsewhere a quoted 't' is far more likely to be parsing something unrelated.
CONTEXT = re.compile(r'getCalibrationByHwId|calib\.csv|histV4|CalibChange', re.I)
# ...and only on a line that is itself about channel identity. Without this the
# 't' emitted by a JSON escaper (dst[di++] = 't') reads as a channel letter just
# because the same file happens to mention calib.csv somewhere else.
LINE_CONTEXT = re.compile(r'idStr|prefix|pref\b|letter|channel|Channel|calib|Calib')

SKIP_DIRS = {'release', '.pio', 'test_bmx280', 'node_modules', '.git'}


def sources():
    for dirpath, dirnames, filenames in os.walk(os.path.join(ROOT, 'src')):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for fn in sorted(filenames):
            if fn.endswith(('.cpp', '.h')):
                full = os.path.join(dirpath, fn)
                yield os.path.relpath(full, ROOT), full


def strip_comments(text):
    """Blank out comments in place.

    Replaces each comment with spaces rather than deleting it, keeping every
    newline and byte offset — otherwise the reported line numbers drift by
    however much commentary preceded the hit, which sends the reader to the
    wrong place in a file that is mostly commentary.
    """
    def blank(m):
        return ''.join(c if c == '\n' else ' ' for c in m.group(0))
    text = re.sub(r'/\*.*?\*/', blank, text, flags=re.S)
    return re.sub(r'//[^\n]*', blank, text)


def scan():
    failures, debt = [], []
    for rel, full in sources():
        if rel in TABLE_FILES:
            continue
        with open(full, encoding='utf-8', errors='replace') as fh:
            body = strip_comments(fh.read())

        for m in LEGACY_NAMES.finditer(body):
            debt.append((rel, body[:m.start()].count('\n') + 1, m.group(0)))

        if rel in LETTER_DEBT or not CONTEXT.search(body):
            continue
        lines = body.split('\n')
        for m in CHANNEL_LETTER.finditer(body):
            n = body[:m.start()].count('\n')
            if not LINE_CONTEXT.search(lines[n]):
                continue
            failures.append((rel, n + 1, m.group(0)))
    return failures, debt


def main():
    if '--list' in sys.argv:
        print(f'tabela: {TABLE}')
        print('debito de letra (converter para channelInfo(ch).letter):')
        for f in sorted(LETTER_DEBT):
            print(f'  {f}')
        return 0

    failures, debt = scan()

    if '--debt' in sys.argv:
        by_file = {}
        for rel, line, name in debt:
            by_file.setdefault(rel, []).append((line, name))
        print(f'[check_channels] {len(debt)} campos nomeados por grandeza '
              f'em {len(by_file)} arquivos (camada 3 — bump de schema do SensorRecord):')
        for rel in sorted(by_file):
            names = sorted({n for _, n in by_file[rel]})
            print(f'  {rel}: {len(by_file[rel])}x  {", ".join(names)}')

    if failures:
        print('[check_channels] letra de canal literal fora da tabela:')
        for rel, line, lit in failures:
            print(f'  {rel}:{line}: {lit} — usar channelInfo(ch).letter')
        print(f'\n{len(failures)} violacao(oes). Adicione a grandeza em {TABLE}.')
        return 1

    print(f'[check_channels] OK — nenhuma letra de canal hardcoded '
          f'({len(debt)} campos por grandeza pendentes da camada 3)')
    return 0


if _SCONS:
    if main() != 0:
        sys.exit(1)
elif __name__ == '__main__':
    sys.exit(main())
