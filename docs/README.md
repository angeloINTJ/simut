# docs/

Forty-two documents, and until now no way to tell which ones describe the firmware
you are holding and which ones describe a moment in its past. Both kinds are
worth keeping. Confusing them is what costs an afternoon.

Every entry below is marked:

- **Living** — kept in step with the firmware. If it disagrees with the code,
  the document is wrong and should be fixed.
- **Snapshot** — true of the version in its title, and deliberately not
  updated since. If it disagrees with the code, *the code moved*, and the
  document is still a correct record of where it moved from. The version it
  was written against is given; the firmware is at `SIMUT_VERSION` in
  `src/SystemDefs_Limits.h`.

A snapshot is not stale. A **living** document that has fallen behind is.

---

## Start here

| document | state | about |
|---|---|---|
| [MANUAL.md](MANUAL.md) · [pt-BR](MANUAL.pt-BR.md) | Living | The user manual: screens, web UI, CLI, telemetry, OTA. |
| [CLI-Manual.md](CLI-Manual.md) | Living | Every serial/Bluetooth command, for the `pico_w_test` profile. |
| [WIRING.md](WIRING.md) | Living | Pinout and bench wiring, including the PicoHand rig. |
| [RECOVERY.md](RECOVERY.md) | Living | Getting a Pico W back after a bad OTA. Read before you need it. |
| [OTA_USAGE.md](OTA_USAGE.md) | Living | How to stage and apply a firmware update. |
| [adding-a-new-sensor.md](adding-a-new-sensor.md) | Living | The checklist for a new sensor type, table by table. |

## Reference

Consulted while changing the code, and enforced by CI where a gate exists.

| document | state | about |
|---|---|---|
| [AUTHORIZATION.md](AUTHORIZATION.md) | Living | The 55-route authorization matrix. Gated by `tools/check_authz.py`. |
| [CONCURRENCY.md](CONCURRENCY.md) | Living | The concurrency invariants, including invariant 3 (`pico_w_asserts` arms its tripwire). |
| [GLOSSARY.md](GLOSSARY.md) · [pt-BR](GLOSSARY.pt-BR.md) · [es-ES](GLOSSARY.es-ES.md) | Living | Tag glossary. |
| [diretrizes_seguranca_vibecoding.md](diretrizes_seguranca_vibecoding.md) | Living | The six failure classes every new code path is audited against. |
| [images/README.md](images/README.md) · [images/screens/screens.md](images/screens/screens.md) | Living | How the screenshots and screen captures are produced. |

## Design and analysis

Investigations written while solving something. Each is a snapshot of its
subject at the version named — several describe behaviour that has since been
fixed, which is exactly what makes them useful when it comes back.

| document | version | about |
|---|---|---|
| [analysis/PLANO_DIVIDA_TECNICA.md](analysis/PLANO_DIVIDA_TECNICA.md) | **Living** | The debt still owed and the order to pay it in, with the dependencies that set that order. |
| [analysis/SIMUT_AIR_PLANO_FIX.md](analysis/SIMUT_AIR_PLANO_FIX.md) | v2.3.9-beta | The Air fix and optimisation plan (F-numbered findings). |
| [analysis/SIMUT_AIR_PLANO_ENERGIA.md](analysis/SIMUT_AIR_PLANO_ENERGIA.md) | 2026-09-07 | Energy budget: frequent readings, rare radio. |
| [analysis/SIMUT_AIR_ESBOCO.md](analysis/SIMUT_AIR_ESBOCO.md) | v2.3.9-beta | The original headless-build sketch. |
| [analysis/SIMUT_RX_INTEGRACAO_ESBOCO.md](analysis/SIMUT_RX_INTEGRACAO_ESBOCO.md) | v2.4.2-beta · simut-rx 1.1.0 | The fleet-management integration sketch: simut-rx reading status, configuring and OTA-updating many SIMUTs over the native API. |
| [analysis/SIMUT_TELEMETRIA_PLANO_CADENCIA.md](analysis/SIMUT_TELEMETRIA_PLANO_CADENCIA.md) | 2026-09-07 | Automatic telemetry cadence and batching, with measurements. |
| [analysis/ANALISE_TELEMETRIA_ALARMES.md](analysis/ANALISE_TELEMETRIA_ALARMES.md) | v2.3.2-stable | The second telemetry line (alarms). |
| [analysis/ANALISE_BURACO_HISTORICO_BANCADA_OTA.md](analysis/ANALISE_BURACO_HISTORICO_BANCADA_OTA.md) | v2.3.2-beta | The 00:00–00:22 history hole, and the `.wip` merge that prevents it. |
| [analysis/ANALISE_GRAFICOS_HISTORICO_WEB.md](analysis/ANALISE_GRAFICOS_HISTORICO_WEB.md) | v2.1.7-beta | History charts in the web UI, end to end. |
| [analysis/ANALISE_SISTEMA_HISTORICO.md](analysis/ANALISE_SISTEMA_HISTORICO.md) | 2026-08-14 | The history storage system. |
| [analysis/ANALISE_PIPELINE_SENSORES.md](analysis/ANALISE_PIPELINE_SENSORES.md) | 2026-08-14 | Sensor read and store pipeline, end to end. |
| [analysis/ANALISE_INSTABILIDADE_SENSORES.md](analysis/ANALISE_INSTABILIDADE_SENSORES.md) | v1.5.1 | BMP280 and friends: instability analysis. |
| [analysis/SIMUT-Plano-Estabilidade-Concorrencia.md](analysis/SIMUT-Plano-Estabilidade-Concorrencia.md) | 2026-08-14 | Concurrency stability plan — the source of the invariants. |
| [ANALISE_FLASH_RAM.md](ANALISE_FLASH_RAM.md) | v1.5.4-beta | Where the flash and RAM go. The §9 experiments still reproduce via `platformio_memstudy.ini`; the totals do not. |
| [PIO_ANALYSIS.md](PIO_ANALYSIS.md) | v1.5.0-beta | RP2040 PIO block compatibility across the drivers. |

### HistoryV5

Three documents, written in sequence rather than as alternatives: the
instructions came first, the implementation notes record what was actually
built, and the amendments are what revision 2.0 changed afterwards.

| document | version | about |
|---|---|---|
| [HistoryV5_Instrucoes_Implementacao.md](HistoryV5_Instrucoes_Implementacao.md) | 2026-08-21 | The specification handed to the implementation. |
| [HistoryV5_Implementacao.md](HistoryV5_Implementacao.md) | 2.0.1-alpha | What was built, and why it differs. |
| [HistoryV5_Emendas_Rev2.md](HistoryV5_Emendas_Rev2.md) | Rev 2.0 | Amendments E1–E10. |

### V4 ecosystem

| document | version | about |
|---|---|---|
| [patches/SIMUT-Diagnostico-V4-Consumidores.md](patches/SIMUT-Diagnostico-V4-Consumidores.md) | 2026-08-14 | Every consumer of the V4 record format. |
| [patches/RELATORIO-patch-v4.md](patches/RELATORIO-patch-v4.md) | 2026-08-21 | The V4 correction patch and its result. |

## Campaigns

Dated test campaigns. Each folder is closed: it records what was run, what
broke, and what the numbers were on that day. They are never updated — a
campaign that gets edited afterwards stops being evidence.

| campaign | date | contents |
|---|---|---|
| beta sweep | 2026-08-10 | [DEFEITOS](beta-sweep-2026-08-10/DEFEITOS.md) — promotion-to-beta sweep. |
| network storm | 2026-08-10 | [RELATORIO](netstorm-campaign-2026-08-10/RELATORIO.md) · [DEFEITOS](netstorm-campaign-2026-08-10/DEFEITOS.md) |
| telemetry | 2026-08-02 | [RELATORIO](telemetry-campaign-2026-08-02/RELATORIO.md) · [METODOLOGIA](telemetry-campaign-2026-08-02/METODOLOGIA.md) · [DEFEITOS](telemetry-campaign-2026-08-02/DEFEITOS.md) |
| security audit (externa) | 2026-08-29 | [IMPLEMENTACAO](security-audit/IMPLEMENTACAO.md) — ACH-01..08, como cada achado foi fechado. ⚠️ ACH-05 e ACH-08 constam como "adiado"/"sem ação" e foram feitos depois, na linha de setembro. |
| security audit 2026-09-07 | 2026-09-07 | [PLANO_CORRECAO](security-audit/PLANO_CORRECAO_2026-09-07.md) · [IMPLEMENTACAO](security-audit/IMPLEMENTACAO_2026-09-07.md) — V-01..V-08 e O-1..O-3, com a tabela do que foi e do que não foi ao ferro. |

---

## Language

The user-facing documents exist in English, Portuguese and Spanish; the
analyses and campaigns are Portuguese only, because that is the language they
were investigated in and a translation of a record is not the record.
Translating the four user-facing guides that lack Spanish is tracked as
[issue #52](https://github.com/angeloINTJ/simut/issues/52).

## Adding a document

Add its row here in the same change, and mark it **Living** or **Snapshot**.
If it is a snapshot, put the version it is true of in its own title — every
snapshot above does, which is how this table was assembled and how the next
reader will know without opening it.
