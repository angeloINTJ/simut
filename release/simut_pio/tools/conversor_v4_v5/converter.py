#!/usr/bin/env python3
"""Batch .sim4 (V4) -> .h5 (V5) history converter, packaged to run on a phone.

Drop the V4 files into V4/ and run this file. The converted V5 files land in
V5/. No arguments and no dependencies beyond the standard library, because
Pydroid runs a script by pressing play — there is nowhere to type a flag.

Two things this file exists to get right:

  * Paths resolve against __file__, never the working directory. Pydroid starts
    scripts with the CWD pointing somewhere else entirely, so a relative "V4"
    would find nothing and the run would report "no files" on a full folder.

  * Inputs are recognised by their magic bytes, not by their extension.
    Transferring files through a phone routinely renames them (20260731.sim4
    arrives as 20260731.sim4.txt, or loses the extension outright), and a
    converter that filtered on ".sim4" would skip the whole folder in silence.

The conversion itself is not implemented here. It calls history_v5.py, which
is the normative reference for the format — the same module the firmware codec
is tested against, byte for byte. Keep the two copies in sync; this one is a
copy of tools/history_v5.py.
"""

import os
import struct
import sys
import traceback

BASE = os.path.dirname(os.path.abspath(__file__))
V4_DIR = os.path.join(BASE, 'V4')
V5_DIR = os.path.join(BASE, 'V5')

sys.path.insert(0, BASE)

try:
    import history_v5 as h5
except ImportError:
    print('ERRO: history_v5.py nao foi encontrado ao lado deste arquivo.')
    print('      A pasta precisa conter os dois: converter.py e history_v5.py.')
    raise SystemExit(1)

V4_MAGIC = b'SIM4'


def human(n):
    """Bytes as a short string — phone screens are narrow."""
    return f'{n / 1024:.1f} KB' if n >= 1024 else f'{n} B'


def looks_like_v4(path):
    """True when the file starts with the V4 magic, whatever it is named."""
    try:
        with open(path, 'rb') as fh:
            return fh.read(4) == V4_MAGIC
    except OSError:
        return False


def output_name(filename):
    """20260731.sim4 -> 20260731.h5, and cope with names a phone mangled.

    Strips a trailing .txt/.bin the file manager may have bolted on, then the
    real extension, so 20260731.sim4.txt still comes out as 20260731.h5.
    """
    stem = filename
    for _ in range(2):
        root, ext = os.path.splitext(stem)
        if ext.lower() in ('.sim4', '.txt', '.bin', '.dat'):
            stem = root
        else:
            break
    return (stem or filename) + '.h5'


def convert_one(src_path, dst_path):
    """Convert a single file. Returns (records, channels, in_bytes, out_bytes).

    Raises on anything that makes the output untrustworthy — the caller counts
    it as a failure and moves to the next file.
    """
    with open(src_path, 'rb') as fh:
        blob = fh.read()

    v4, records = h5.read_v4_records(blob)
    if not records:
        raise ValueError('nenhum registro pode ser lido deste arquivo')

    schema = h5.v4_schema_to_v5(v4)
    series = [
        (epoch, [h5.H5_NAN if v is None else max(-32767, min(32767, v))
                 for v in values])
        for epoch, values in records
    ]
    count, size = h5._write_series(dst_path, schema, series, 60)

    # Decode the file that was just written and count what comes back. Without
    # this the run would report success on a file nothing can read, which is
    # the one failure mode a converter must never have.
    with open(dst_path, 'rb') as fh:
        written = fh.read()
    errors = []
    back = sum(1 for _ in h5.read_series(written, 60,
                                         lambda off, msg: errors.append((off, msg))))
    if errors:
        raise ValueError(f'a saida nao decodifica limpa: {errors[0][1]}')
    if back != count:
        raise ValueError(f'a saida tem {back} registros, esperados {count}')

    return count, len(schema), len(blob), size


def main():
    print()
    print('=== Conversor de historico SIMUT: V4 (.sim4) -> V5 (.h5) ===')
    print()

    if not os.path.isdir(V4_DIR):
        os.makedirs(V4_DIR, exist_ok=True)
        print(f'Criei a pasta V4 em:\n  {V4_DIR}')
        print('\nColoque os arquivos .sim4 la dentro e rode de novo.')
        return 0

    os.makedirs(V5_DIR, exist_ok=True)

    entries = sorted(os.listdir(V4_DIR))
    inputs, skipped = [], []
    for name in entries:
        path = os.path.join(V4_DIR, name)
        if not os.path.isfile(path):
            continue
        if name.startswith('.') or name.upper().startswith('LEIA-ME'):
            continue
        if looks_like_v4(path):
            inputs.append(name)
        else:
            skipped.append(name)

    if not inputs:
        print(f'Nenhum arquivo V4 encontrado em:\n  {V4_DIR}')
        if skipped:
            print(f'\n{len(skipped)} arquivo(s) foram ignorados por nao comecarem')
            print('com a marca "SIM4" — provavelmente nao sao historico V4:')
            for name in skipped[:10]:
                print(f'  - {name}')
            if len(skipped) > 10:
                print(f'  ... e mais {len(skipped) - 10}')
        return 0

    print(f'Entrada: {V4_DIR}')
    print(f'Saida:   {V5_DIR}')
    print(f'{len(inputs)} arquivo(s) V4 para converter.')
    print()

    ok = failed = 0
    tot_in = tot_out = tot_rec = 0

    for name in inputs:
        src = os.path.join(V4_DIR, name)
        dst = os.path.join(V5_DIR, output_name(name))
        try:
            rec, ch, nin, nout = convert_one(src, dst)
        except Exception as exc:                      # noqa: BLE001 — report and continue
            failed += 1
            # struct.error prints as a bare "error: unpack requires a buffer
            # of 9 bytes", which tells a phone user nothing. Every way a file
            # can be short or scrambled arrives here, so say that instead.
            if isinstance(exc, (struct.error, IndexError, EOFError)):
                reason = 'arquivo truncado ou corrompido'
            else:
                reason = f'{exc}'
            print(f'  FALHOU  {name}')
            print(f'          {reason}')
            # A half-written output is worse than none: it would decode as a
            # short day and nothing downstream would flag it.
            if os.path.exists(dst):
                try:
                    os.remove(dst)
                except OSError:
                    pass
            continue

        ok += 1
        tot_in += nin
        tot_out += nout
        tot_rec += rec
        ratio = nin / nout if nout else 0
        print(f'  ok      {name}')
        print(f'          -> {os.path.basename(dst)}  '
              f'{rec} registros, {ch} canais  '
              f'{human(nin)} -> {human(nout)}  ({ratio:.2f}x)')

    print()
    print('--- Resumo ---')
    print(f'Convertidos: {ok}')
    if failed:
        print(f'Falharam:    {failed}')
    if skipped:
        print(f'Ignorados:   {len(skipped)} (nao sao V4)')
    if ok:
        ratio = tot_in / tot_out if tot_out else 0
        print(f'Registros:   {tot_rec}')
        print(f'Tamanho:     {human(tot_in)} -> {human(tot_out)}  ({ratio:.2f}x menor)')
        print(f'\nOs arquivos .h5 estao em:\n  {V5_DIR}')
    print()
    return 1 if failed else 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except SystemExit:
        raise
    except Exception:                                 # noqa: BLE001
        # Pydroid closes on an unhandled traceback before it can be read.
        print('\nERRO inesperado:\n')
        traceback.print_exc()
        print('\n(anote a mensagem acima)')
        raise SystemExit(1)
