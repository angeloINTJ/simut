#!/usr/bin/env python3
"""Testa o mesclador de arquivos-dia V5 (tools/h5_day_merge.py).

Rodar: python3 tools/test_h5_day_merge.py   (o CI roda como gate)

Cada caso corresponde a um jeito de perder ou duplicar dado na operacao que
motivou a ferramenta — devolver o dia corrente ao device depois de uma OTA
reformatar o LittleFS (o buraco de 21/08 00:00-00:22, ver
docs/analysis/ANALISE_BURACO_HISTORICO_BANCADA_OTA.md). As invariantes:
chunks copiados byte a byte (CRC intacto), saida cronologica, dedup exato
(logo idempotente), e recusa explicita — nunca escolha silenciosa — para
schema divergente, conflito ambiguo e chunk ilegivel.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import history_v5 as h5                      # noqa: E402
from h5_day_merge import MergeError, merge_blobs   # noqa: E402

DAY0 = 1787281200            # 21/08/2026 00:00:00 -03
SCHEMA = [h5.ChannelDesc(10, h5.H5_KIND_TEMP_C, -1),
          h5.ChannelDesc(18, h5.H5_KIND_HUM_PCT, 0)]

FAILS = []


def check(name, cond, detail=''):
    tag = 'ok  ' if cond else 'FAIL'
    print(f'[{tag}] {name}' + (f' — {detail}' if detail and not cond else ''))
    if not cond:
        FAILS.append(name)


def values(i):
    return [250 + (i % 7), 60 + (i % 5)]


def day_file(start_min, count, step_s=60, schema=SCHEMA):
    w = h5.FileWriter(schema)
    for i in range(count):
        w.append(DAY0 + start_min * 60 + i * step_s, values(start_min + i))
    return w.close()


def records(blob):
    return [(ep, vals) for _s, ep, vals in read_series_strict(blob)]


def read_series_strict(blob):
    errs = []
    out = list(h5.read_series(blob, on_error=lambda o, m: errs.append((o, m))))
    assert not errs, f'arquivo ilegivel no teste: {errs}'
    return out


def blocks_t0(blob):
    return [e.header.t0 for e in h5.scan(blob) if e.kind == 'data']


# ---------------------------------------------------------------------------
# 1. Mesclagem disjunta — o caso real: backup 00:00-00:16 + device 00:22-...
A = day_file(0, 17)          # 00:00..00:16
B = day_file(22, 98)         # 00:22..01:59
M, st = merge_blobs([B, A])  # ordem de entrada proposital: device primeiro
check('disjunto: uniao exata das series',
      records(M) == sorted(records(A) + records(B)))
check('disjunto: blocos em ordem cronologica',
      blocks_t0(M) == sorted(blocks_t0(M)))
check('disjunto: um unico SCHEMA, na frente',
      [e.kind for e in h5.scan(M)].count('schema') == 1
      and next(h5.scan(M)).kind == 'schema')

# 2. Dedup exato: mesclar um arquivo com ele mesmo devolve o proprio arquivo
M2, st2 = merge_blobs([A, A])
check('dedup: merge(A,A) == A byte a byte', M2 == A)
check('dedup: duplicatas contadas', st2.duplicates == len(blocks_t0(A)))

# 3. Idempotencia: remesclar nao muda um byte
M3, _ = merge_blobs([M, B])
check('idempotencia: merge(merge(B,A),B) == merge(B,A)', M3 == M)

# 4. Identidade fatiar-e-mesclar: partir um dia em dois e mesclar reconstroi
#    o original byte a byte (chunks verbatim — CRC nunca recalculado)
F = day_file(0, 150)         # 150 registros -> 2 blocos cheios + 1 parcial
entries = list(h5.scan(F))
schema_raw = F[entries[0].offset:entries[0].offset + entries[0].size]
data = [F[e.offset:e.offset + e.size] for e in entries if e.kind == 'data']
half = len(data) // 2 or 1
F1 = schema_raw + b''.join(data[:half])
F2 = schema_raw + b''.join(data[half:])
MF, _ = merge_blobs([F2, F1])
check('fatiar-e-mesclar: reconstroi o original byte a byte', MF == F)

# 5. Schema divergente: recusa explicita
A2 = day_file(0, 5, schema=[h5.ChannelDesc(99, h5.H5_KIND_TEMP_C, -1),
                            h5.ChannelDesc(18, h5.H5_KIND_HUM_PCT, 0)])
try:
    merge_blobs([A, A2])
    check('schema divergente: recusado', False)
except MergeError:
    check('schema divergente: recusado', True)

# 6. Conflito (mesmo t0, conteudo diferente): default recusa; keep-longer
#    fica com o bloco de mais registros; empate nunca e decidido
C1 = day_file(30, 1)
C3 = day_file(30, 3)
try:
    merge_blobs([C1, C3])
    check('conflito: default recusa', False)
except MergeError:
    check('conflito: default recusa', True)
MC, stc = merge_blobs([C1, C3], on_conflict='keep-longer')
check('conflito keep-longer: fica o mais longo',
      len(records(MC)) == 3 and stc.conflicts_resolved == 1)
C3b = day_file(30, 3, step_s=120)     # mesmo t0, mesmo count, valores outros
try:
    merge_blobs([C3, C3b], on_conflict='keep-longer')
    check('conflito empatado: recusa mesmo com keep-longer', False)
except MergeError:
    check('conflito empatado: recusa mesmo com keep-longer', True)

# 7. Chunk com CRC ruim: default recusa; --skip-bad descarta so o ilegivel
BAD = bytearray(B)
BAD[-1] ^= 0xFF              # corrompe o payload do ultimo bloco
BAD = bytes(BAD)
try:
    merge_blobs([A, BAD])
    check('CRC ruim: default recusa', False)
except MergeError:
    check('CRC ruim: default recusa', True)
MS, sts = merge_blobs([A, BAD], skip_bad=True)
check('CRC ruim + skip-bad: descarta so o bloco ilegivel',
      sts.bad_chunks == 1
      and len(blocks_t0(MS)) == len(blocks_t0(A)) + len(blocks_t0(B)) - 1)

# 8. Entrada vazia / nao-V5: recusa (download quebrado nao vira "dia vazio")
for label, blob in (('vazia', b''), ('lixo', b'\x00' * 64)):
    try:
        merge_blobs([blob])
        check(f'entrada {label}: recusada', False)
    except MergeError:
        check(f'entrada {label}: recusada', True)

# 9. Arquivo com dois SCHEMAs (troca no meio do dia): fora de escopo, recusa
w = h5.FileWriter(SCHEMA)
w.append(DAY0, values(0))
w.new_schema([h5.ChannelDesc(10, h5.H5_KIND_TEMP_C, -1)])
w.append(DAY0 + 60, [250])
MULTI = w.close()
try:
    merge_blobs([MULTI])
    check('multi-schema: recusado', False)
except MergeError:
    check('multi-schema: recusado', True)

# 10. Blocos intercalados entre arquivos: a saida ordena globalmente
X = day_file(30, 1)                          # so 00:30
Y_w = h5.FileWriter(SCHEMA)
Y_w.append(DAY0, values(0))                  # 00:00
Y_w2 = h5.FileWriter(SCHEMA)
Y_w2.append(DAY0 + 3600, values(60))         # 01:00
Y = Y_w.close()
Z = Y_w2.close()
MI, _ = merge_blobs([X, Z, Y])
check('intercalado: ordenacao global por t0',
      blocks_t0(MI) == sorted(blocks_t0(MI)) and len(blocks_t0(MI)) == 3)

# 11. .wip: validacao do chunk nu e absorcao no arquivo do dia — o carreador
#     dos registros ainda nao selados (foi a maior fatia do buraco de 21/08)
from h5_day_merge import wip_info                  # noqa: E402

wip_raw = data[-1]                                 # um DATA chunk nu, valido
hdr = wip_info(wip_raw)
check('.wip valido: header lido', hdr.count > 0 and hdr.t0 >= DAY0)
for label, bad in (('SCHEMA no lugar de DATA', schema_raw),
                   ('lixo', b'\x12' * 40),
                   ('chunk + rabo', wip_raw + b'\x00')):
    try:
        wip_info(bad)
        check(f'.wip invalido ({label}): recusado', False)
    except MergeError:
        check(f'.wip invalido ({label}): recusado', True)
MW, _ = merge_blobs([F1, schema_raw + wip_raw])    # absorcao = merge normal
check('.wip absorvido: registros do bloco nao-selado entram no dia',
      records(MW) == sorted(records(F1) + records(schema_raw + wip_raw)))

# 12. CLI: recusa mesclar dias DIFERENTES pelo nome (20260820.h5+20260821.h5
#     viraria um arquivo cujos blocos caem fora da janela do dia na semeadura)
import tempfile                                    # noqa: E402

import h5_day_merge                                # noqa: E402

with tempfile.TemporaryDirectory() as td:
    pa, pb = os.path.join(td, '20260820.h5'), os.path.join(td, '20260821.h5')
    open(pa, 'wb').write(A)
    open(pb, 'wb').write(B)
    out = os.path.join(td, 'out.h5')
    check('CLI: dias diferentes recusados (exit 2)',
          h5_day_merge.main([out, pa, pb]) == 2 and not os.path.exists(out))
    check('CLI: --force-cross-day sobrepoe',
          h5_day_merge.main([out, pa, pb, '--force-cross-day', '--quiet']) == 0)
    pb2 = os.path.join(td, 'dev_20260820', '20260820.h5')
    os.makedirs(os.path.dirname(pb2))
    open(pb2, 'wb').write(B)
    check('CLI: mesmo dia em dirs diferentes passa (exit 0)',
          h5_day_merge.main([out, pa, pb2, '--quiet']) == 0)

# ---------------------------------------------------------------------------
print()
if FAILS:
    print(f'{len(FAILS)} caso(s) FALHARAM: {FAILS}')
    sys.exit(1)
print('todos os casos passaram')
