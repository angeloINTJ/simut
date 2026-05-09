# Screenshots ainda têm linhas com defeito — TODO próxima sessão

User report 2026-05-08: screenshots TFT (mesmo via chunked + CRC32) continuam com:
1. **Linhas com defeito** — pixels corrompidos em algumas rows
2. **Mesma tela sempre** — `screen <NAME>` CLI retorna OK mas TFT não muda

## Análise das causas

### Linhas defeituosas (não é transit corruption)

CRC32 valida cada chunk perfeitamente. Logo a corrupção acontece **antes** do CRC compute:

- **`_displayRef->readRow(y, pixelRow, w)`** lê pixels do ILI9341 via SPI MPU read (RAMRD command)
- ILI9341 read protocol é notoriamente frágil: stale data quando pen-IRQ ativo, timing race com refresh interno, byte alignment issues
- `pause Core 1` por chunk (alpha18) ajuda mas não é suficiente — o chip TFT em si retorna dados inconsistentes

### `screen <NAME>` não muda display

Hipóteses:
- show*Screen() methods setam `_uiMode + _isDirty + _forceFullRedraw` mas Core 1 está pausado pela captura → não renderiza
- A captura subsequente lê o framebuffer ANTES de Core 1 ter chance de redesenhar
- 1.5s de wait entre `screen X` e capture pode ser insuficiente

## Soluções candidatas (próxima sessão)

### Para linhas defeituosas

**Opção A — Multi-sample readRow:**
```cpp
// Read same row 3x e pick majority/median per pixel
uint16_t row1[W], row2[W], row3[W];
_displayRef->readRow(y, row1, W);
_displayRef->readRow(y, row2, W);
_displayRef->readRow(y, row3, W);
// median per pixel
for (int x = 0; x < W; x++) {
    uint16_t a = row1[x], b = row2[x], c = row3[x];
    pixelRow[x] = (a == b) ? a : (b == c) ? b : c;  // majority
}
```
Trade-off: 3x slower per row.

**Opção B — Canvas-based capture (idealmente):**
Manter framebuffer 320×240 16-bit em SRAM (153KB) que Core 1 escreve em paralelo com TFT writes. Endpoint serve framebuffer (sem ler TFT). Custo RAM proibitivo (~58% do total).

**Opção C — Half-resolution canvas:**
Canvas 160×120 16-bit (38KB) atualizado com downsample. Endpoint serve em half-res. Compromisso bom: 14% RAM, sem TFT read issues.

**Opção D — Static screens via PROGMEM:**
Pra cada tela, ter uma versão "ideal" pré-renderizada em PROGMEM. Capture endpoint serve essa versão para a tela atual. Limitação: dados dinâmicos (sensor values, timestamps) não aparecem.

**Recomendação:** Opção A primeiro (mais simples, sem mudança arquitetural).

### Para `screen <NAME>` não mudar

**Opção A — Add explicit yield + force redraw:**
```cpp
case CMD_GOTO_SCREEN: {
    // ... existing show*Screen call ...
    _displayMgr->forceFullRedraw();  // novo método público
    delay(2000);  // give Core 1 time to render
    break;
}
```

**Opção B — Wait state in capture script:**
Após `screen X`, esperar 5s antes de capturar. Permite Core 1 completar redraw + idle.

**Opção C — Verify via diff:**
Capture antes + após `screen X`. Se MD5 idênticos → screen NAME não funcionou. Repetir comando.

## /data layout fix (RESOLVIDO em alpha18+)

User reportou que /data files iam pra `/` raiz. Fix em `tools/manual_capture/upload_data_to_lfs.py`:
- Server espera `uploadDir` como QUERY PARAM (não multipart `path`)
- URL agora: `?uploadDir=/history` etc
- Skip toda pasta `/web` + qualquer `.gz` (PROGMEM since v3.43.12)

## .gz files cleanup (RESOLVIDO)

`/web/*.gz` agora skipados pelo upload script. Se tivessem sido uploadeados antes, basta `factory reset` ou delete via /api/delete.

## Status alpha18

- /api/screenshot_chunk endpoint OK em testes (CRC valida)
- Mas pixels lidos do TFT continuam com defeitos esporádicos
- Próximo passo: implementar multi-sample readRow OU canvas-based capture

---

🚧 Pendente para alpha19+ próxima sessão.
