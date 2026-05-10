# arduino_pico_overrides — slim build do framework arduino-pico

> **Save: 26 KB BSS** (RAM) por ajustar config de lwIP + BTstack pré-compiladas.
> Combinado com Tier 1.2 do firmware (screenshot heap-alloc, +15 KB), total ~41 KB.

## Por que existe

`liblwip.a` e `liblwip-bt.a` vêm pré-compilados em `framework-arduinopico/lib/rp2040/`.
Os buffers (PBUF pool, BTstack profile storage) são alocados como variáveis estáticas
DENTRO desses `.o` já compilados — não dá pra mudar via header local nem build flag do
projeto. Só rebuildando a framework com configs customizadas.

## Conteúdo

```
arduino_pico_overrides/
├── README.md                  ← este arquivo
├── patch.sh                   ← aplica overrides (idempotente)
├── restore.sh                 ← reverte para originais
├── originals/                 ← backup dos arquivos virgens (criado pelo patch.sh)
│   ├── lwipopts.h
│   ├── btstack_config.h
│   ├── liblwip.a
│   └── liblwip-bt.a
└── patched_headers/           ← headers SIMUT-tunados
    ├── lwipopts.h
    └── btstack_config.h
```

## Mudanças aplicadas

### `lwipopts.h`

| Setting | Original | Patched | Razão |
|---|---|---|---|
| `PBUF_POOL_SIZE` | 24 | **12** | SIMUT tem 1-2 conexões TCP simultâneas; 12 envelopes pré-alocados sobram |
| `MEMP_NUM_TCP_PCB` | 5 | **3** | Web server + telemetria HTTP + MQTT = 3 |
| `MEMP_NUM_UDP_PCB` | 7 | **2** | NTP + DNS + DHCP = 3 (margem com 2 ainda OK porque PCBs podem multiplexar) |

### `btstack_config.h`

| Setting | Original | Patched | Razão |
|---|---|---|---|
| `MAX_NR_HCI_CONNECTIONS` | 2 | **1** | SerialBT é client único (CLI auth) |
| `MAX_NR_AVDTP_CONNECTIONS` | 1 | **0** | A/V transport não usado |
| `MAX_NR_AVDTP_STREAM_ENDPOINTS` | 1 | **0** | idem |
| `MAX_NR_AVRCP_CONNECTIONS` | 2 | **0** | Controle de player não usado |
| `MAX_NR_HFP_CONNECTIONS` | 1 | **0** | Hands-free não usado |
| `MAX_NR_HID_HOST_CONNECTIONS` | 1 | **0** | HID não usado |
| `MAX_NR_HIDS_CLIENTS` | 1 | **0** | HID services não usado |
| `MAX_NR_BNEP_CHANNELS` | 1 | **0** | IP-over-Bluetooth não usado |
| `MAX_NR_BNEP_SERVICES` | 1 | **0** | idem |

## Uso

```bash
# Aplicar overrides (primeira vez OU após update do arduino-pico via PIO)
bash tools/arduino_pico_overrides/patch.sh

# Reverter (debug ou comparação)
bash tools/arduino_pico_overrides/restore.sh
```

`patch.sh` é idempotente — pode rodar quantas vezes quiser. `originals/` é preservado
após o primeiro patch.

## Validação

| Métrica | Sem patch | Com patch | Save |
|---|---|---|---|
| RAM SIMUT v4.2.0 | 49.6% (129,900 B) | **33.7% (88,328 B)** | -41 KB |
| Flash | 98.7% | 98.7% | ~0 |
| `memp_memory_PBUF_POOL_base` | 36,771 B | 18,387 B | -18 KB |
| `hci_connection_storage` | 7,400 B | 3,700 B | -3.7 KB |
| AVRCP/HFP/HIDS/AVDTP storage | ~2.5 KB | 0 B | -2.5 KB |
| `WebManager::handleApiScreenshotChunk::payload` | 15,360 B (BSS) | 0 B (heap on demand) | -15 KB |

## Quando isso quebra

- `pio update` ou `pio pkg update framework-arduino-pico` → framework reinstalado, override perde.
  **Reaplique:** `bash tools/arduino_pico_overrides/patch.sh`.
- Versão NOVA do arduino-pico mexer em estrutura interna (sources renomeados, configs).
  **Sintoma:** patch.sh dá erro no cmake. **Resolução:** atualizar `patched_headers/` para
  bater com a nova versão (rever diff vs `originals/` após reinstall).

## Limites desse approach

- Não é portável para outros projetos sem o arduino-pico patched.
- Build local depende de cmake + arm-none-eabi-gcc (ambos vêm com PIO).
- Re-aplicar patch leva ~1-2 min (cmake config + make compile + auto-copy).

## Alternativa upstream

Se a Arduino-Pico Foundation aceitar PR adicionando `#ifndef` guards em `lwipopts.h` e
`btstack_config.h`, este patch fica obsoleto — bastará `-D PBUF_POOL_SIZE=12` em
`build_flags` do platformio.ini, sem rebuild de framework.
