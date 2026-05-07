# SIMUT v3.44.0-alpha1 — Pre-release / Alpha

**Status:** EXPERIMENTAL — não usar em produção sem leitura completa abaixo.

**Data:** 2026-05-07
**Branch:** feature/ota-self-flash
**Baseado em:** v3.43.21 (F-OTA-BOOTLOOP fix #3 — TRIGGER-only watchdog)

---

## O que tem nesta alpha

### Novo: UI de segurança pré-OTA

O botão **Firmware** em `/files` agora exige:

1. **Tela de aviso (modal)** — texto explica:
   - OTA reformata a LittleFS
   - Configs (login/WiFi/sensores) são preservadas via snapshot
   - history/themes/calib/logs **serão APAGADOS**
   - **~24% dos casos** exigem recovery manual via picotool ou power cycle (bricks residuais não resolvidos — ver abaixo)

2. **Backup obrigatório** — antes de aceitar o `.bin` do firmware:
   - Browser baixa `/api/backup` automaticamente
   - Browser parseia o header `BKP1` do arquivo
   - Browser compara `payload_size` e `payload_crc32` do arquivo com headers HTTP `X-Backup-PSize` / `X-Backup-PCrc` (novos)
   - Se mismatch → operação ABORTADA (download corrompido)
   - Se OK → `.bkp` é forçosamente salvo no disco do usuário

3. **Confirmação final** após backup — informa tamanho do backup salvo e do firmware antes do apply.

4. **Validação server-side do firmware** (já existente) — `/api/restore?op=stage` valida boot2 CRC-32/MPEG-2, range de tamanho, formato. Se falhar (`v=6=BOOT2_BAD`, `v=4|5=size out of range`), browser mostra o código.

### Endpoint mudado: `GET /api/backup`

Adicionados response headers (sem mudança no payload):
- `X-Backup-PSize: <bytes>` — tamanho do payload (sem header)
- `X-Backup-PCrc: <uint32>` — CRC32 EDB88320 do payload

Os clientes existentes (script, curl) continuam funcionando — headers extras são ignorados.

---

## Estatísticas de teste (v3.43.21 — base desta alpha)

20-cycle test (loop20) em hardware:

| | Count | % |
|---|---|---|
| **PASS** | 13 / 20 | 65% raw, **76% válidos** |
| FAIL real (brick) | 4 / 20 | 20% |
| FAIL falso (recovery script hung) | 3 / 20 | 15% |
| Recovery picotool | 6× | — |
| Tempo total | 228 min | — |
| Média/iter | 684s | — |

**Padrão:** bricks ocorreram após blocos de 4-7 PASS consecutivos (iter 5, 13, 15, 18). Sugere acúmulo de estado entre OTAs — não é fix #3 falhando isolado.

Log completo: `docs/test_reports/f9_loop20_20260507-013912.log`.

---

## Bricks residuais — KNOWN ISSUE 🔴

**Em ~24% dos casos** (4 brick reais em 17 tentativas válidas), o device entra em bootloop após `apply` que requer recovery manual:

- BOOTSEL via `picotool` + flash do baseline; OU
- Power cycle físico (desligar/religar 3V3)

**Causa raiz não resolvida.** Fix #3 (watchdog TRIGGER-only) corrigiu o caso 100% reprodutível ("toda iter brick"), mas há um residual entre iters consecutivas que sugere acúmulo de estado:
- snapshot offset / LFS interaction?
- CYW43 module residual state?
- HW watchdog em scenarios edge?

**Investigação continua.** Quando resolvido, será **SIMUT v4.0.0**.

Documentação detalhada de fixes anteriores em `docs/INVESTIGATION_BOOTLOOP.md`.

---

## Como usar

1. Flash via USB BOOTSEL: `picotool load -x firmware.uf2` (recomendado).
2. Login web em `https://<IP>/login`.
3. Para fazer OTA via web:
   - Ir em `/files` → botão **Firmware** (💻)
   - Ler aviso, confirmar
   - Selecionar `.bin` (gerado via `pio run -e pico_w_release`)
   - Browser baixa backup automaticamente
   - Confirmar apply
4. **Tenha backup local antes.** Se brickar, recovery via BOOTSEL + picotool.

---

## Limitações conhecidas

- **F-OTA-RAM** — `uzlib` ocupa ~33 KiB de BSS RAM (decompressor gunzip). Mantido como bloqueador para release público estável. Otimização postergada para após bricks resolvidos.
- **Bricks residuais ~24%** (descrito acima).
- **Boot2 CRC client-side não foi incluído** — cabia mas estouraria flash budget. Server valida em `/api/restore?op=stage` (response `v=6` se falhar).
- **Firmware version não detectada client-side** — server pode comparar via `SIMUT_VERSION` se necessário (postergado).

---

## Arquivos relevantes

- Firmware UF2: `firmware.uf2` (anexo do release)
- Source: branch `feature/ota-self-flash`
- Commits: ver tag `v3.44.0-alpha1`
- Plano: `STABILITY_PLAN.md`
- Investigação bricks: `docs/INVESTIGATION_BOOTLOOP.md`
- Logs de teste: `docs/test_reports/f9_loop20_20260507-013912.log`
