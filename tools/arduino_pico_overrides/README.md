# arduino_pico_overrides — slim build do framework arduino-pico

> **Save: ~18 KB RAM** (`PBUF_POOL_SIZE` 24→12 em `lwipopts.h`).
> Combinado com Tier 1.2 do firmware (screenshot heap-alloc, +15 KB), **total ~33 KB**.

## Por que existe

Para reduzir o footprint de RAM do PBUF pool do lwIP, que ocupa 36 KB com config default
do arduino-pico (24 envelopes × ~1530 B). SIMUT tem 1-2 conexões TCP simultâneas — 12
envelopes sobram com folga.

## Descoberta importante (v1.0.0)

PIO compila **a maior parte do lwIP do source** em cada build do projeto, NÃO da
`liblwip.a` precompilada. Os `.o` ficam cacheados em
`.pio/build/<env>/FrameworkArduino/lwip/src/`.

**Implicação:** patches em `lwipopts.h` propagam para a próxima build SIMUT após
invalidação de cache. **NÃO é necessário** rebuildar `liblwip.a` — versão antiga deste
script tentava (caminho errado, levou ~10 min e não dava ganho extra).

**BTstack ainda é precompilada** em `liblwip-bt.a` — patches em `btstack_config.h`
exigiriam rebuild da framework. Mas tentei (v1.0.0, descartado em v1.0.0):
**alterar BTstack quebra cyw43 RSSI sampling** (chip CYW43439 é compartilhado entre
WiFi e BT no Pico W; reduzir HCI ou desabilitar perfis afeta WiFi management).

Por isso v1.0.0 só patcheia `lwipopts.h`, deixa BTstack virgin.

## Conteúdo

```
arduino_pico_overrides/
├── README.md                  ← este arquivo
├── patch.sh                   ← aplica overrides (idempotente)
├── restore.sh                 ← reverte para originais
├── originals/                 ← backup virgin (.gitignored, criado pelo patch.sh)
└── patched_headers/
    └── lwipopts.h             ← header SIMUT-tunado
```

## Mudanças aplicadas

### `lwipopts.h`

| Setting | Original | Patched | Razão |
|---|---|---|---|
| `PBUF_POOL_SIZE` | 24 | **12** | SIMUT tem 1-2 conexões TCP simultâneas; 12 envelopes pré-alocados sobram |

`MEMP_NUM_TCP_PCB`, `MEMP_NUM_UDP_PCB` permanecem nos defaults (5 e 7) —
reduzir UDP_PCB **quebra mDNS** (DHCP+DNS+NTP+mDNS responder = 4 PCBs mínimo).

### `btstack_config.h`

**Não modificado** — alterações em BTstack quebram RSSI sampling no Pico W.

## Uso

```bash
# Aplicar overrides (primeira vez OU após update do arduino-pico via PIO)
bash tools/arduino_pico_overrides/patch.sh

# Reverter (debug ou comparação)
bash tools/arduino_pico_overrides/restore.sh
```

`patch.sh` é idempotente — pode rodar quantas vezes quiser. `originals/` é preservado
após o primeiro patch.

## Validação HW (2026-05-10)

| Métrica | Sem patch | Com patch | Save |
|---|---|---|---|
| RAM SIMUT v1.0.0 | 49.6% (129,900 B) | **36.7% (96,156 B)** | -33 KB |
| Flash | 98.7% | 98.8% | ~0 |
| `memp_memory_PBUF_POOL_base` | 36,771 B | 18,387 B | -18 KB |
| `WebManager::handleApiScreenshotChunk::payload` | 15,360 B (BSS) | 0 B (heap on demand) | -15 KB |
| mDNS responder (`simut.local:5353`) | ✅ funciona | ✅ **funciona** | — |
| RSSI display | ✅ -35 dBm | ✅ **-35 dBm** | — |
| Telemetria HTTP | ✅ | ✅ | — |
| Backup/Restore via API | ✅ (29/29 críticos) | ✅ (29/29 críticos) | — |

## O que NÃO foi feito (e por quê)

| Tentativa | Resultado | Status |
|---|---|---|
| `MEMP_NUM_UDP_PCB` 7→2 | Quebrou mDNS responder | ❌ revertido em v1.0.0 |
| `MEMP_NUM_TCP_PCB` 5→3 | Sem efeito mensurável (PCBs são pequenos) | ❌ não vale a complexidade |
| BTstack profiles 0 (AVRCP/HFP/HIDS/AVDTP) | Quebrou RSSI sampling no display | ❌ revertido em v1.0.0 |
| `MAX_NR_HCI_CONNECTIONS` 2→1 | Quebrou RSSI sampling no display | ❌ revertido em v1.0.0 |

## Quando isso quebra

- `pio update` ou `pio pkg update framework-arduino-pico` → framework reinstalado, override perde.
  **Reaplique:** `bash tools/arduino_pico_overrides/patch.sh`.

## Limites desse approach

- Não é portável para outros projetos sem o arduino-pico patched.
- Re-aplicar patch leva **<1 segundo** (só copia header + invalida cache).
- Próxima build PIO recompila lwIP source (~30s) na primeira vez após patch.

## Alternativa upstream

Se a Arduino-Pico Foundation aceitar PR adicionando `#ifndef` guards em
`lwipopts.h`, este patch fica obsoleto — bastará `-D PBUF_POOL_SIZE=12` em
`build_flags` do platformio.ini, sem patch local de framework.
