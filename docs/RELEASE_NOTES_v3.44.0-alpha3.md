# SIMUT v3.44.0-alpha3 — Pre-release / Alpha

**Status:** EXPERIMENTAL — pré-release com mudanças funcionais novas. Não usar em produção sem leitura completa abaixo.

**Data:** 2026-05-07
**Branch:** feature/ota-self-flash
**Baseado em:** v3.43.21 + UI safety (alpha1) + F-OTA-RAM (alpha3)

---

## Highlights

### 🎯 F-OTA-RAM RESOLVIDO (-33 KiB de BSS)

`validation.cpp` removeu o gzip decompress dry-run inteiramente. SIMUT só sobe firmware RAW (.bin) desde v3.43.3 — gzip path era código morto que mantinha `uzlib` + 33 KiB de BSS estática (`g_validate_ctx`) linkados permanentemente.

**Resultado em build:**
- **RAM: 56.8% → 43.7%** (-13.1pp = -34 324 B)
- **Flash: 98.7% → 98.5%** (-2 440 B liberados pra futuras features)

Trade-off: se user upar `.bin.gz` por engano, validation falha em boot2_crc (gzip header não bate com layout RP2040 boot2 → response v=6=BOOT2_BAD). Mensagem clara, sem corrupção.

### 🛡 UI de segurança pré-OTA (de alpha1)

O botão **Firmware** em `/files` agora exige:

1. **Tela de aviso (modal)** com texto explicando:
   - OTA reformata a LittleFS
   - Configs (login/WiFi/sensores) preservadas via snapshot
   - history/themes/calib/logs **serão APAGADOS**
   - **~24% dos casos** exigem recovery manual via picotool ou power cycle

2. **Backup obrigatório** — `/api/backup` baixado automaticamente, com verificação de integridade:
   - Browser parseia o header `BKP1` do `.bkp` baixado
   - Compara `payload_size` e `payload_crc32` do arquivo com response headers HTTP `X-Backup-PSize` / `X-Backup-PCrc` (novos em v3.44)
   - Se mismatch → operação ABORTADA (download corrompido)
   - `.bkp` é forçosamente salvo no disco do usuário

3. **Confirmação final** com sumário antes do apply.

4. **Validação server-side** mantida — `/api/restore?op=stage` valida boot2 CRC-32/MPEG-2, range de tamanho. Falhas retornam código numérico (`v=6=BOOT2_BAD`, etc.).

### Endpoint `/api/backup` mudado

Adicionados response headers (sem mudança no payload):
- `X-Backup-PSize: <bytes>` — tamanho do payload
- `X-Backup-PCrc: <uint32>` — CRC32 EDB88320 do payload

Clientes existentes (curl, scripts) continuam funcionando.

---

## Validação em hardware

### Loop20 contra v3.43.21 (linha de base)

| | Count | % |
|---|---|---|
| **PASS** | 13 / 20 | 65% raw, **76% válidos** |
| FAIL real (brick) | 4 / 20 | 20% |
| FAIL falso (recovery script hung) | 3 / 20 | 15% |

Log: `docs/test_reports/f9_loop20_20260507-013912.log`.

### Por que estes números aplicam à alpha3

A mudança F-OTA-RAM toca exclusivamente `validation.cpp` (path de upload-time validation). **NÃO altera o OTA core** (orchestrator, applier, staging, snapshot). UI safety toca apenas browser-side e adiciona 2 response headers em `/api/backup` — também não afeta o caminho destrutivo.

Estatísticamente, alpha3 deve ter pass rate ~ idêntico a v3.43.21 (76%). Validação loop20 dedicada à alpha3 fica **pendente** — device em estado pós-investigação fix #4 (ver abaixo) precisa power cycle físico.

### Tentativa de Fix #4 (REVERTIDA)

Em alpha2, tentei power-cycle do CYW43 chip via WL_REG_ON (GPIO 23) LOW por 100ms antes do `watchdog_reboot`. Hipótese: state acumulado no CYW43 entre OTAs causa os 24% de bricks residuais.

**Resultado:** REGRESSÃO. Loop20 alpha2 (parcial, 7 iters) mostrou **3 PASS / 4 FAIL = 43% pass rate** (P(>=4 bricks em 7 iters | rate=24%) ≈ 6%, estatisticamente significativo).

Fix #4 foi REVERTIDO em alpha3. Investigação continua em `docs/INVESTIGATION_BOOTLOOP.md` Achado #4.

---

## Bricks residuais — KNOWN ISSUE 🔴

**~24%** dos OTA applies entram em bootloop pós-apply. Recovery requer:

- BOOTSEL via `picotool load -x firmware.uf2`; OU
- Power cycle físico (USB unplug/replug)

Causa raiz não identificada após 4 tentativas de fix. Permanece bloqueador para release final v4.0.0.

Hipóteses ranqueadas (ver `docs/INVESTIGATION_BOOTLOOP.md`):

1. ~~CYW43 power-cycle via WL_REG_ON~~ — REVERTIDO (regrediu)
2. LittleFS metadata accumulation
3. BTstack TLV state
4. USB CDC enumeration drift
5. Timing edge no PSM reset

---

## Como usar

1. Flash via USB BOOTSEL: `picotool load -x firmware.uf2` (recomendado).
2. Login web em `https://<IP>/login`.
3. Para fazer OTA via web:
   - Ir em `/files` → botão **Firmware** (💻)
   - Ler aviso, confirmar
   - Selecionar `.bin` (gerado via `pio run -e pico_w_release`)
   - Browser baixa backup automaticamente + verifica integridade
   - Confirmar apply
4. **Tenha backup local antes.** Se brickar (~24% chance), recovery via BOOTSEL + picotool.

---

## Limitações conhecidas

- **Bricks residuais ~24%** (descrito acima) — bloqueia v4.0.0.
- **Boot2 CRC client-side não foi incluído na UI** — server valida em `/api/restore?op=stage` (response `v=6` se falhar).
- **Firmware version não detectada client-side** — server pode comparar via `SIMUT_VERSION` se necessário (postergado).

---

## Arquivos relevantes

- Firmware UF2: `firmware-v3.44.0-alpha3.uf2` (anexo do release)
- Source: branch `feature/ota-self-flash` @ commit `0b39b9f`
- Plano: `STABILITY_PLAN.md` (F-OTA-BOOTLOOP, F-OTA-RAM)
- Investigação bricks: `docs/INVESTIGATION_BOOTLOOP.md`
- Logs de teste linha-de-base: `docs/test_reports/f9_loop20_20260507-013912.log`
- Logs de teste alpha2 (regressão fix #4): `docs/test_reports/f9_loop20_20260507-070253.log`

---

## Histórico de versões nesta linha

- **v3.44.0-alpha1** (commit 7c854df) — UI safety pré-OTA implementada
- **v3.44.0-alpha2** (commit ab5f3f4) — adicionou fix #4 CYW43 power-cycle (REGREDIU para 43% pass rate)
- **v3.44.0-alpha3** (commit 0b39b9f, este release) — fix #4 REVERTIDO; mantém F-OTA-RAM + UI safety
