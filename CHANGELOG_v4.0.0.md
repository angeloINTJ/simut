# SIMUT v4.0.0 — Release Notes

**Release date:** 2026-05-09
**Branch:** `feature/ota-self-flash` → merging into `main`
**Tag:** `v4.0.0`

---

## 🎯 Headline

**F-OTA totalmente fechada.** SIMUT v4.0.0 é a primeira release pública com Over-The-Air firmware update funcional, validado em hardware com **20/20 PASS no loop20 (0% brick rate, 0 recoveries necessários)**.

---

## Mudanças desde v3.43.21 (97 commits)

### 🚀 Nova capacidade: OTA self-flash (F-OTA Fases 1–10)

| Fase | Capability | Tag inicial |
|---|---|---|
| F-OTA Fase 1 | Backup `.bkp` da LFS atrelado ao chip_id | v3.38.0 |
| F-OTA Fase 2 | Restore via state machine streaming + `/api/restore` | v3.39.0 |
| F-OTA Fase 3 | (Removida em v3.44.0-alpha2 — gzip path morto) | v3.40.0 |
| F-OTA Fase 4 | Layout flash investigado + staging primitives | v3.41.0 |
| F-OTA Fase 5 | Upload .bin pra staging via `/api/restore?op=stage` | v3.42.0 |
| F-OTA Fase 6 | Pré-validação dry-run (raw-only desde alpha3) | v3.43.0 |
| F-OTA Fase 7a/7b | Apply destrutivo (erase + program app slot via SRAM applier) | v3.43.10 |
| F-OTA Fase 8 | (Integrado nas demais fases) | — |
| F-OTA Fase 9 | Snapshot de `/config/system.bin` preservado através do apply | v3.43.16 |
| F-OTA Fase 10 / UI safety | Modal warn + backup auto + manifest match + chunked validation | v3.44.0-alpha1 |

### 🐛 F-OTA-BOOTLOOP — 3 root causes corrigidas

Bug residual que travava boot pós-OTA mesmo com applier funcionando. Investigação intensiva 2026-05-07 → 2026-05-09 (35+ alphas) achou e fixou:

1. **alpha27** — `README.md` write reentrante em `StorageManager::begin` (deadlock LFS dentro de `enterFlashSafeMode`).
2. **alpha31** — `mkdir` reentrante em `StorageManager::begin` (mesmo padrão; removido o wrap `enterFlashSafeMode` dos mkdirs porque LFS já protege).
3. **alpha35** — `multicore_lockout` IRQ-based usado por `flash_safe_execute` do LFS travava em boot pós-OTA porque Core 1 entrava em estado onde IRQ inter-core não responde. **Fix:** deferir `_displayMgr->startCore1()` de step 3 (logo após `displayMgr->begin`) para step 6.5 (após `StorageManager::begin`). Com Core 1 INACTIVE durante mountFS+mkdirs+snapshot+loadConfig, `flash_safe_execute` usa single-core path (só `disable_interrupts` local) — sem multicore lockout = sem hang.

**Validação HW 2026-05-09** (`tools/test_f9_loop20.sh` com break-on-fail):
- **20/20 PASS, 0 FAIL, 0 recoveries necessários**
- Tempo médio 75 s/iter, total 25 min
- Boot post-OTA capturado via UART markers (PicoHand bridge GP8/GP9): step 14 SYS READY @ 18.5 s
- Brick rate alpha31 ~24% → v3.45.0 **0%**

Log completo: `docs/test_reports/f9_loop20_20260509-134813.log`.

### 🧹 F-OTA-RAM — resolvido + cleanup

`g_validate_ctx` (33 KiB BSS, uzlib LZ77 dict 32 KiB + state) bloqueava release público — RAM apertada (~11 KiB de heap_lb).

- **v3.44.0-alpha2/alpha3**: gzip dry-run REMOVIDO de `validation.cpp` (SIMUT só recebe firmware RAW desde v3.43.3, gzip path era código morto). Linker dead-strip eliminou uzlib + g_validate_ctx. RAM 56.8% → 49.6%.
- **v3.45.1**: cleanup do source — deletados `lib/uzlib/` (10 arquivos), `src/ota/decompressor.{h,cpp}`, `test/test_decompressor/`, `[env:native_decompressor]` do `platformio.ini`. RAM/Flash inalterados (confirma dead-strip prévio).

### 🛠 Infra de debug adicionada

- **PicoHand v3** (`tools/PicoHand/`): mão robótica em outro Pico W aciona BOOTSEL/RUN do alvo via GPIO + open-drain emulado. Comandos serial: `PING`, `BOOTSEL`, `RESET`, `HOLD/RELEASE`, `STATUS`, `PINOUT`, `SELF_BOOTSEL`. Wrapper bash + manual escrito explicitamente para o agente. Sem isso, automação OTA seria inviável.
- **UART debug bridge GP8/GP9**: SIMUT emite boot logs via UART1 (HW UART, não USB CDC) em paralelo com `Serial.print`. PicoHand recebe e repassa pra USB CDC do host com prefix `[S]`. Quando F-USB-CDC-DEAD acontece pós-OTA, USB CDC do SIMUT fica mute mas UART1 continua transmitindo até o ponto exato do hang.
- **Single-char markers boot** (`@!#*$%&` early + `0..E` no `StorageManager::begin` + `D/C/R` em alpha35) — diagnóstico bit-level do que travou.
- **Scratch[5] magic** infra pra boot-conditional code (post-OTA detection sem flag em flash).
- **`pico_blink_echo`** test firmware mínimo de revival (memória `feedback_pico_revival.md`: "Pico volta à vida com firmware bem básico" mesmo após bricks duros).

### 📚 Outras correções importantes

- **Backup restore reboot mid-transfer (alpha20)**: `handleApiRestoreUploadData` agora envolve `restore_session_feed` em `RenderGuard` quando `mode==APPLY`. Sem isso, Core 1 (SPI ILI9341) corria paralelo a `flash_program_page` (Core 0) → lockout deadlock → WDT reboot mid-upload. Validado HW 10/10 stress (backup 770 KB).
- **Screenshot chunked com CRC32 (alpha16-19)**: `/api/screenshot_chunk?n=N` retorna 1 chunk de 16 rows (15360 bytes) + header binário 12 bytes (chunk_idx, payload_size, CRC32 EDB88320). Reduz corrupção silenciosa do `/api/screenshot` full. Multi-sample readRow 3x + majority vote per pixel reduz defects em ~95%.
- **Touch sim CLI (alpha14-15)**: comando `touch sim X Y` + `screen <NAME>` pra captura automatizada de screenshots TFT bypass `handleTouch` gates.
- **CMD `screen` direto via show*Screen()** (alpha15): muda telas TFT bypass touch (`dashboard`, `settings`, `themes`, `lang`, `password`, `license`, `status`, `touchcal`, `touchsens`, `sounds`, `alarms`, `displayoffset`, `graph`).
- **UI safety pré-OTA (alpha1)**: modal warn + backup auto-download com integridade verificada via headers HTTP `X-Backup-PSize`/`X-Backup-PCrc` (cliente parseia header BKP1 e compara com headers anunciados; abort se mismatch).
- **Pastas custom visíveis no /files (alpha21)**: `handleApiLs` root listing emite TODAS as dirs encontradas (não só hardcoded sysDirs); `handleApiMkdir` cria placeholder pra persistir pasta vazia (LittleFS perde dirs sem entries).
- **Versão SIMUT_VERSION** validada no upload (alpha1): regex parser cliente bloqueia upload de não-SIMUT.

### 🔒 Mudanças em segurança

- **Threat surface OTA documentada** (`SECURITY.md` §8.1, §9, §9.1): qualquer credencial admin comprometida = poder flashar firmware arbitrário remotamente. Mitigações enumeradas: rate-limit + lockout exponencial; `mustChangePin`; admin pwd random em factory; rede isolada recomendada. UF2/BIN não assinados ainda — operador valida origem.
- **Procedimento de update via OTA** (`SECURITY.md` §9.1): passo-a-passo explícito.
- **Rollback**: snapshot preserva config; reflash USB layout-compat preserva FS.

---

## Migrations / Breaking changes

- **Nenhuma migração de schema** desde v3.43.21. Schema da config continua em v15 (último bump em F15.2.a).
- **`bin.gz` upload **não funciona mais**: SIMUT só recebe `.bin` RAW desde v3.43.3. Cliente deve descomprimir antes do upload.
- **`uzlib`** removido do source (ainda no histórico git para quem quiser referência).

## Known limitations

- **UF2/BIN não são assinados criptograficamente**. Qualquer admin pode flashar firmware arbitrário — confiar na origem do binário é responsabilidade do operador.
- **OTA apply é IRREVERSÍVEL** sem `.bkp` baixado pré-apply. UI já força backup automático mas operador pode cancelar.
- **TFT sem boot status entre step 3 e step 6.5** (~7s) — trade-off aceito do fix #11. Operador vê apenas splash inicial até `_displayMgr->setBootStatusKey(TR_BOOT_LOAD_THEME_LANG)`.
- **F-OTA-RAM source removido — re-habilitar gzip exigirá re-vendoring de uzlib + heap on-demand**. Não há plano imediato.

## Compatibilidade

- **arduino-pico**: 4.7.x (testado em 4.7.6)
- **PlatformIO**: 6.x
- **RP2040**: Pico W (`pico_w_release` env)

## Validação

- **Loop20 OTA**: `tools/test_f9_loop20.sh` — 20/20 PASS em 2026-05-09
- **Backup restore stress**: 10/10 APPLY OK em alpha20 (FS 85.5% cheia, backup 770 KB)
- **Boot post-OTA UART trace**: `docs/test_reports/alpha35_boot_20260509-134654.log`
- **Build**: RAM 49.6% (130 KB livres), Flash 98.7% (margem ~13 KB)

## Créditos

- Auditoria técnica + implementação: Ângelo Moisés Alves + Claude Code (Opus 4.7, 1M context)
- F-OTA fases 1–10: branch `feature/ota-self-flash`, ~97 commits 2026-05-01..2026-05-09
- Histórico completo: `STABILITY_PLAN.md` § 5

---

🤖 Generated with [Claude Code](https://claude.com/claude-code)
