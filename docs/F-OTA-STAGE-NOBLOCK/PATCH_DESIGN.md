# F-OTA-STAGE-NOBLOCK — Fix design (BLOCKED on flash budget)

## Bug
`/api/restore?op=stage` em `UPLOAD_FILE_START` chama `stage_session_begin`
que internamente apaga 1 MiB de staging síncrono (~13 s). Durante esse tempo
o WebServer callback não retorna, kernel buffer TCP não é drenado, cliente
recebe `RemoteDisconnected`, server registra `s/early` ao tentar responder.

Workaround atual: PicoHand BOOTSEL + `picotool load -x firmware.uf2`.

## Fix design (testado, funciona logicamente)
Erase on-demand sector-by-sector em `flush_page` (cada chunk recebido
apaga só o setor que vai escrever, ~50 ms). Snapshot commit movido para
`stage_session_end`. Bitmap rastreia setores já apagados.

**Arquivos**:
- `src/ota/firmware_stage.cpp`: `ensure_sector_erased()` helper +
  `staging_session_begin_lite()` em vez de `staging_session_begin()`
  (sem erase upfront) + snapshot commit em `stage_session_end`.
- `src/ota/staging.cpp`: nova função `staging_session_begin_lite()`
  (só `enterFlashSafeMode` + `LittleFS.end()`).
- `src/ota/staging.h`: declaração de `staging_session_begin_lite`.
- Bitmap (32 bytes) + snapshot_len como statics em `firmware_stage.cpp`
  (evita modificar `firmware_stage.h` → previne recompile cascade
  via `WebManager.h`).

## Bloqueio
Build com fix overflow flash em **3212 bytes** mesmo com `+152 bytes`
de mudança em `.text` confirmado via `arm-none-eabi-size`. Causa
não-óbvia (provável reorganização de seções pelo `--gc-sections` do GCC
LD ao link final).

## Próxima fase
F-FLASH-DIET deve liberar 5+ KB pra cobrir o fix + margem:
- Mover CSS inline das pages (DASH, HIST, CFG) para `style.css` compartilhado
- HIST_PAGE = 18.8 KB gz é maior consumidor (Chart.js setup + CSS)
- EXT-002: remover `CMD_DBG_SENSOR_HISTORY_ALL` debug command (~1.5 KB)
- Reativar `minify_html` no `tools/build_webui_gz.py` (+1.4 KB)

## Patch
Ver `firmware_stage.cpp.patch` neste diretório.
