# F-RESTORE — Bugs do `/api/restore?op=apply` em v4.0.0

> **Status:** descobertos em 2026-05-09/10 durante prep do teste de loop100 com config real preservada.
> **Bloqueia:** teste de estabilidade backup→OTA→restore→verify (pausado até fix).
> **Severidade global:** 🔴 — caminho user-facing (restaurar backup) tem dataloss observada.

---

## Contexto

Usuário tinha SIMUT configurado com tema custom, calibração, language pack pt-BR e 19 dias de
histórico. O fluxo natural após uma OTA é: usuário baixa backup, faz update, restaura backup pra
preservar config + histórico. Esse fluxo **não funciona em v4.0.0**.

## Reprodução

1. Device com 32 arquivos no LFS (canonical 455 KB com calib, config, favicon, history, lang, themes).
2. `GET /api/backup` → salva canonical.bkp 455 KB ✅.
3. `POST /api/restore?op=validate` com canonical → 200 OK, `fsm:0` ✅.
4. `POST /api/restore?op=apply` com canonical →
   - **Bug #1:** Cliente recebe `ConnectionResetError(104, 'Connection reset by peer')` em ~13s
     (vs esperado: 200 OK).
   - **Bug #2:** Device NÃO reseta automaticamente. Sem RESET manual via PicoHand:
     LFS fica com **3 arquivos** (1.1 KB) em vez dos 32 esperados.
   - **Bug #3:** Mesmo COM RESET manual via PicoHand imediatamente após o apply:
     LFS fica com **26 arquivos** (364 KB). Faltam 6 arquivos do **final do payload**:
     `lang/language_pt-BR.lng`, `themes/unimed_dark.thm`,
     `history/{20260429,20260502,20260503,20260504,20260509}.bin`.
     Padrão consistente: WDT bate antes de processar a tail.

## Comparação com `/api/upload` arquivo-por-arquivo

Workaround atual de recovery: enviar cada arquivo via `/api/upload?uploadDir=<dir>`.
- 8/8 uploads testados retornaram 200 OK em <1s cada.
- **Sem ConnReset, sem reboot, sem dataloss.**
- Conclusão: o problema é específico ao path `/api/restore?op=apply`.

## Code paths suspeitos

### Bug #1+#3: WDT mid-write em `restore_session_feed`

`src/ota/restore.cpp:223-228`:
```cpp
if (s.mode == RestoreMode::APPLY) {
    /* Escreve direto no path final (sem rename). Ver commit_all_tmps. */
    ensure_parent_dirs(s.cur_path);
    if (s.cur_file) s.cur_file.close();
    s.cur_file = LittleFS.open(s.cur_path, "w");  // ← TRUNCA arquivo existente
    if (!s.cur_file) { fail(s, BackupStatus::IO_ERROR); return; }
```

Hipótese: `LittleFS.open(path, "w")` em LFS fragmentado triggera GC interno que bloqueia
por segundos. Repetido 32×, acumula tempo > WDT (8 s). Mesma classe de problema descrito
em `STABILITY_PLAN.md` U15 (`writeCompactToFlash` sem feeds).

`WebManager_Ota.cpp:153-164` chama `feedWatchdog()` por chunk, mas a chamada pra
`restore_session_feed` em si pode demorar > 8 s sem retorno se vários `LittleFS.open("w")`
acontecerem dentro de um mesmo chunk.

### Bug #2: Sem auto-reboot após `op=apply`

`WebManager_Ota.cpp:209-311` `handleApiRestoreFinish()`:
```cpp
void WebManager::handleApiRestoreFinish() {
    ...
    bool fs_mod = false;
    {
        RenderGuard rg(is_apply ? _displayRef : nullptr);
        ota::restore_session_finish(_restoreSession, &fs_mod);
    }
    LOG_CODE(...);
    emit_restore_json(_server, _restoreSession, fs_mod);
    // ← NÃO chama LogManager::safeReboot() apesar de fs_mod==true
}
```

Comparação: `WebManager_Commit.cpp:542` chama `LogManager::instance().safeReboot()` após
commit-all (também escreve config). Restore deveria fazer o mesmo quando `fs_mod==true`.

## Fixes sugeridos (ordem de prioridade)

### Fix #1 (alta prioridade): WDT durante restore

Opções:
- **A.** Inserir `feedWatchdog()` ANTES de cada `LittleFS.open("w")` em `on_path_complete`.
- **B.** Voltar pra strategy v1 (`.restore_tmp` + rename atômico) — comentário em restore.cpp:105
  explicitamente removeu isso por causa de 16 KiB de flash. Em v4.0.0 (RAM 49.6%, Flash 98.7%),
  pode não caber. Re-avaliar.
- **C.** Adicionar `HeavyTaskGuard` no apply path (igual `staging_test`), que pausa Core 1 e
  alimenta WDT em background.

### Fix #2 (média): Auto-reboot após apply

`handleApiRestoreFinish` quando `is_apply && fs_mod && status==OK`:
1. Enviar resposta JSON.
2. Aguardar ~500 ms (cliente recebe).
3. `LogManager::safeReboot()`.

Mesmo padrão de `WebManager_Commit.cpp:534-542`.

### Fix #3 (baixa): Documentar comportamento atual

Enquanto Fix #1 + #2 não chegam: web UI deveria avisar usuário que após restore precisa
RESET manual + risco de dataloss. Atualmente só vê o ConnReset sem orientação.

## Impacto no teste de estabilidade (`tools/test_f9_loop100_real.sh`)

Test runner já criado (untracked, não commitado). Comparator in-memory funciona
(`tools/bkp_compare.py`). Pausados até firmware ter restore confiável:

- Sem fix → roda 100 iters mostrando ~100% MISMATCH (esperado, dado os bugs).
- Com fix #1 + #2 → roda 100 iters mostrando integridade real.

## Estado de evidência

Artefatos da sessão 2026-05-09/10:
- `/home/angelo/Documentos/simut_real_loop_20260509-233527/canonical.bkp` (455 KB, 32 arquivos)
- `post_revival_*.bkp` (estado pós test_real iter1 falho — 7 arquivos)
- `post_restore_0003.bkp` (após restore SEM RESET — 3 arquivos)
- `post_restore_reset_*.bkp` (após restore + RESET imediato — 26 arquivos)
- `post_upload_*.bkp` (após /api/upload manual dos 8 faltantes — 33 arquivos, 29 críticos OK)
