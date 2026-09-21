# tools/

127 scripts. This file exists because until 2026-09-08 there was no way
to tell a live bench tool from a leftover, and one of them —
`compressor.py` — had been superseded for three months while still looking
usable: it regenerated `WebUI_GZ.h` into the repository root, where nothing
reads it, instead of `src/`, where the build does. It was deleted in the same
change that created this page.

**The categories below say how a script is reached, not how important it is.**
A tool nobody calls automatically can still be the right tool; what it cannot
be is self-evident, which is what the last column is for.

If you add a script here, add its line. If you find one whose description is
wrong, the description is the bug.

---

## Called by CI (13)

Invoked from `.github/workflows/build.yml` — or, for the release manifest, from
`release-ota.yml` when a tag is pushed. Breaking one of these fails a pull request
or a release.

| script | what it does | last touched |
|---|---|---|
| `arduino_pico_overrides/patch.sh` | patch.sh — aplica overrides SIMUT no framework arduino-pico do PlatformIO. | 2026-08-20 |
| `build_release.sh` | build_release.sh — Generate Arduino IDE-compatible release zips for simut_tft and simut_alpha. | 2026-08-16 |
| `check_authz.py` | check_authz.py — the authorization matrix, pinned so it cannot silently rot. | 2026-08-19 |
| `release_manifest.py` | The manifest a fleet manager downloads before an OTA: version, per-image size and sha256. Written by `release-ota.yml` next to the assets. | 2026-09-13 |
| `check_flash_budget.py` | Fails the build when a firmware image grows past its budget in tools/flash_budget.json. | 2026-09-08 |
| `check_fsguard.py` | check_fsguard.py — the /config filesystem guards, pinned so they cannot silently rot. | 2026-08-29 |
| `fsguard.py` | LittleFS guard for OTA benches: backup and restore with day-file merging. | 2026-08-21 |
| `gen_logcodes.py` | Single source of truth for the SIMUT log-code tables. | 2026-07-26 |
| `h5_day_merge.py` | Merge V5 history day files (same day, same schema) into one file. | 2026-08-21 |
| `run_cppcheck.sh` | run_cppcheck.sh — static-analysis gate over src/ (issue #35). | 2026-08-18 |
| `run_fuzz.sh` | run_fuzz.sh — libFuzzer gate over the web-API input validators (issue #44). | 2026-08-19 |
| `scan_secrets.sh` | scan_secrets.sh — release gate: refuse to ship when a secret is tracked in Git. | 2026-08-16 |
| `test_h5_day_merge.py` | Testa o mesclador de arquivos-dia V5 (tools/h5_day_merge.py). | 2026-08-21 |

## Called by the build (11)

Invoked from `platformio.ini`, mostly as `extra_scripts`. These run on every
`pio run`, so they are the gates you meet first — and the ones that fail your
local build before CI ever sees it.

| script | what it does | last touched |
|---|---|---|
| `build_favicon_header.py` | PlatformIO pre-build script — regenerates src/Favicon.{h,cpp} from data/favicon.ico. | 2026-07-27 |
| `build_webui_gz.py` | PlatformIO pre-build script — regenerates WebUI_GZ.h from WebUI.h. Running it **by hand** needs the interpreter that has zopfli, which is PlatformIO's pipx venv (`~/.local/share/pipx/venvs/platformio/bin/python`) — not the system `python3` and not `~/.platformio/penv/bin/python`. Either of those falls back to gzip -9 and every page grows ~2,888 B in total, with no error. | 2026-09-20 |
| `check_channels.py` | Build guard: keep channel knowledge inside the channel table. | 2026-07-30 |
| `check_cli_help.py` | PlatformIO pre-build guard — every usable CLI command must be documented. | 2026-09-07 |
| `check_flash_probe.py` | PlatformIO post-build guard — FlashIrqProbe wrappers must live in SRAM. | 2026-07-23 |
| `check_lang_packs.py` | Fails the build when a .lng @DICT does not have exactly TR_KEYS_COUNT lines. | 2026-08-19 |
| `check_logcodes.py` | PlatformIO pre-build guard — the five log-code tables must agree. | 2026-07-26 |
| `pico_test_suite.py` | Pico W v1.5.2 — Test suite final de estabilidade. | 2026-08-22 |
| `save_storm.py` | Save-storm validation — protocol item #2 of docs/analysis/SIMUT-Plano-Estabilidade-Concorrencia.md. | 2026-08-22 |
| `sensor_soak.py` | Sensor soak — protocol item #4 of docs/analysis/SIMUT-Plano-Estabilidade-Concorrencia.md. | 2026-08-14 |
| `web_test_suite.py` | End-to-end test suite for the SIMUT web interface. | 2026-08-17 |

## Referenced elsewhere (50)

Named by a document, a workflow, or another script — a recipe in `docs/` or a
step in a bench procedure. Run by hand, but with instructions somewhere.

| script | what it does | last touched |
|---|---|---|
| `PicoHand/pico_hand.sh` | Bash wrapper for the "robotic hand" (pico_hand firmware). | 2026-09-06 |
| `air_test_suite.py` | SIMUT Air — hardware-in-the-loop test suite (serial CLI + web + PicoHand). | 2026-09-09 |
| `alarm_mqtt_test.py` | SIMUT v21 — Teste em hardware da 2ª linha de telemetria sobre MQTT com | 2026-08-23 |
| `arduino_pico_overrides/restore.sh` | restore.sh — reverte overrides SIMUT do framework arduino-pico. | 2026-08-20 |
| `build_lang_pack.py` | F-LANGPACK Etapa 3 — gera data/lang/language_pt-BR.lng. | 2026-08-10 |
| `build_release_pio.sh` | build_release_pio.sh — Generate PlatformIO / VS Code compatible release zip. | 2026-08-29 |
| `check_air_consistency.py` | Static consistency gate for the SIMUT Air build (no hardware, no PlatformIO). | 2026-09-06 |
| `check_history_v5_parity.py` | Bit-exact parity gate between the firmware V5 codec and the reference. | 2026-08-22 |
| `cleanup_comments.py` | Clean version history references and Portuguese markers from source files. | 2026-06-06 |
| `commit_bool_cases.py` | Boolean round-trip cases for POST /api/commit_all, run against the real device. | 2026-08-18 |
| `configure_and_test.py` | Configure WiFi and test Pico stability after lockout fixes. | 2026-08-16 |
| `gen_logo.py` | Generate the SIMUT logo (mark + wordmark) as self-contained SVG. | 2026-08-18 |
| `gen_synth_history.py` | Generate a varied synthetic /history for bench work. | 2026-08-15 |
| `gen_theme_collection.py` | Generates the curated .thm collection in themes/ and validates every | 2026-08-15 |
| `ha_discovery_test.py` | r"""§5.18 Interoperabilidade — Home Assistant MQTT Discovery ao vivo | 2026-08-23 |
| `heap_gate_interaction.py` | r"""§5.7 Interações de recurso (§4.4 do plano) — gate de heap da telemetria | 2026-08-23 |
| `history_v5.py` | HistoryV5 — reference implementation of the SIMUT compressed history format. | 2026-08-01 |
| `repro_post_slow.py` | repro_post_slow.py — §5.9: corpo lento (POST) dropado sem reboot (15.000 ms). | 2026-08-22 |
| `repro_sensorswitch.py` | repro_sensorswitch.py — §5.9: trocar schema de sensor sob carga não trava. | 2026-08-22 |
| `repro_slowloris.py` | repro_slowloris.py — §5.9: GET lento dropado sem reboot (parse 3.000 ms). | 2026-08-22 |
| `screen_mapper.py` | Drive the SIMUT touch UI from the host and capture every screen it has. | 2026-07-27 |
| `subset_font.py` | subset_font.py — gera versao subsetted de um header GFXfont (Adafruit_GFX). | 2026-06-03 |
| `telemetry_bench/bench.py` | Bench library for the telemetry test campaign. | 2026-08-02 |
| `telemetry_bench/cadence_cleanup.py` | Put the bench back the way it was after phase_cadence.py. | 2026-09-07 |
| `telemetry_bench/cadence_report.py` | Turn results/phase_cadence.json into the tables the energy plan quotes. | 2026-09-07 |
| `telemetry_bench/campaign.py` | Telemetry test campaign driver. | 2026-08-10 |
| `telemetry_bench/enumerate_history.py` | Enumerate /history by date instead of trusting /api/ls. | 2026-08-02 |
| `telemetry_bench/phase_cadence.py` | Phase E — what the cadence and the batch size actually cost, on the Air build. | 2026-09-07 |
| `telemetry_bench/phase_drain.py` | Phase B — drain as much history as the telemetry path will give up. | 2026-08-02 |
| `telemetry_bench/phase_drain_full.py` | Phase B2 — drain the WHOLE archive, past the firmware's 30-day floor. | 2026-08-02 |
| `telemetry_bench/phase_minbatch.py` | Does the radio stay off until there is enough to say? | 2026-09-07 |
| `telemetry_bench/phase_mqtt.py` | Phases A2/C2 — MQTT and MQTTS: throughput, and survival against a broken broker. | 2026-08-22 |
| `telemetry_bench/phase_mqtt_oversize.py` | Targeted test — an MQTT payload bigger than the client buffer can ever be. | 2026-08-02 |
| `telemetry_bench/phase_payload.py` | Phase D — payload builders and value integrity. | 2026-08-02 |
| `telemetry_bench/phase_survive.py` | Phase C — survival against broken servers. | 2026-08-02 |
| `telemetry_bench/rescore.py` | Re-score the data-loss check, counting what the FAULT server received. | 2026-08-02 |
| `telemetry_bench/revalidate.py` | Re-run, against the fixed firmware, every test that failed or exposed a defect. | 2026-08-02 |
| `telemetry_bench/serial_probe.py` | One measured window with the serial console captured alongside. | 2026-09-07 |
| `telemetry_bench/server_http.py` | Instrumented HTTP/HTTPS telemetry sink with fault injection. | 2026-09-07 |
| `telemetry_bench/server_mqtt.py` | Instrumented MQTT 3.1.1 broker with fault injection (plain and TLS). | 2026-08-02 |
| `telemetry_bench/soak24.py` | Soak de 24 h com a configuração real do usuário. | 2026-08-02 |
| `telemetry_bench/soak_a6.py` | r"""Acceptance A6 — long soak on the image that actually ships. | 2026-08-10 |
| `telemetry_bench/storm_net.py` | storm_net.py — the combined network storm. | 2026-08-10 |
| `telemetry_bench/storm_report.py` | Turn a storm_net.py run into the tables the report needs. | 2026-08-10 |
| `telemetry_bench/synth_upload.py` | Put a directory of synthetic days on the device, and check them first. | 2026-09-07 |
| `test_lang_gate.py` | Positive controls for check_lang_packs.py — the split-ceiling gates. | 2026-08-19 |
| `test_webui_minify.py` | Testa o minificador do build_webui_gz.py contra as armadilhas conhecidas. | 2026-08-18 |
| `theme-editor/gen_presets.py` | Regenerates presets.js from src/Themes.cpp — the single source of truth. | 2026-08-15 |
| `theme-editor/server.py` | SIMUT Theme Editor — launcher. | 2026-06-03 |
| `validate_top_pin_alarm.py` | SIMUT v21 — Validação visual da correção do painel superior fixado (pin). | 2026-08-23 |

## Standalone (53)

Nothing in the repository names these. That is a statement about
discoverability, **not** a verdict: several are ordinary bench and recovery
tools that only ever get typed at a prompt (`factory_reset.py`,
`fix_lockout.py`, `history_watch.py`), and their being here is exactly why the
list was worth writing down.

Treat this section as the queue of things to confirm, retire, or reference
from a document. Anything genuinely dead should leave the tree, as
`compressor.py` did — `git log` keeps it.

| script | what it does | last touched |
|---|---|---|
| `rig_reset_admin.py` | Resets the bench device's admin password over the emergency console and writes it into ~/.simut-bench.env (mode 600, never printed). Verifies it AFTER the reboot, which is the distinction finding F27 turned on. | 2026-09-08 |
| `bt_auth_test.py` | Bluetooth surface of the CLI against the real device: the lockout survives a dropped link (V-01a), and the discovery window closes without taking connectability with it (V-01b). | 2026-09-08 |
| `bt_reconnect_probe.py` | Bluetooth reconnect-auth probe: authenticate, drop the link, reconnect, and confirm the new client gets a password prompt rather than the previous session. Regression for the `_authenticated`-across-disconnect bypass measured and fixed 2026-09-10. Reuses `bt_auth_test.py`'s helpers. | 2026-09-10 |
| `air_soak.py` | Hands-off soak of the Air hibernation cycle, passive by construction: raw USB presence for every wake/sleep edge, read-only console for the `[AIR]` lines, and an HTTP collector counting what the device uploads. Never writes to the device. Found F28 (a sleep that never wakes) on 2026-09-10. | 2026-09-11 |
| `air_soak_snapshot.py` | Snapshot of the Air bench in M0 — status, today's full history (sealed + open block), log count, telemetry target — as JSON. Run at the start and end of a soak and diff the two. Read-only. | 2026-09-11 |
| `air_soak_recover.py` | Recovers a stalled Air bench in the right order: RESET, read the flash log (which survives it), snapshot the deltas against the baseline, restore the telemetry target. Use AFTER any in-situ measurement — the reset destroys the stuck state. | 2026-09-11 |
| `install_tls_cert.py` | Installs the HTTPS pair on a device in service through `POST /api/tls`: logs in, sends the two PEM blocks, prints what the device answered, and with `--reboot` restarts it so the next boot picks it up. The device refuses a pair whose key does not belong to the certificate, so a mistake is a 400 here instead of an HTTPS that does not come up later. Before this route there was no way at all (#133). | 2026-09-18 |
| `ota_test.py` | Drives one OTA on the bench: login, stage (`/api/restore?op=stage&commit=1`), apply. Does NOT verify the version — the caller reads it back, because the only proof of an installed update is the new version reporting itself. ⚠️ Stage the image on an alternate port: the router kills long port-80 flows, and any stage borrows the whole LittleFS region. | 2026-09-11 |
| `wifi_outage_test.py` | The access point that vanishes, on the bench: the host's USB Wi-Fi adapter is the AP, the device is held in M0 and pointed at it, and the AP goes away for 60 s, then 9 min (into dormancy), then hidden. Judged from the device's own log (blind joins, NET_DORMANT_MODE), not only from pings. | 2026-09-09 |
| `json_escape_cases.py` | Three strings a permitted user could store that each broke a different JSON response for everyone (V-04). An A/B, not a checklist: against firmware from before the fix, cases 1 and 2 must break. | 2026-09-08 |
| `air_telemetry_server.py` | SIMUT Air telemetry test server. | 2026-09-05 |
| `alarm_hw_test.py` | SIMUT v21 — Suíte de testes em hardware da 2ª linha de telemetria (alarmes). | 2026-08-23 |
| `alarm_collector.py` | Coletor da linha de alarmes como processo próprio: cada POST vira uma linha JSON no `--log`. Fica no ar entre rodadas — o registro postado sem ninguém ouvindo é um retry que a bancada nunca vê. | 2026-09-19 |
| `panel_users_hw_test.py` | v25 — identidade no painel no rig: dirige as telas por `/api/touch`, captura cada uma, lê o sorteio E A GEOMETRIA do teclado por `/api/keypad` e o log binário por `/api/logs`, e lê o coletor. Prep/joao/admin/maria/web/**identity**/cleanup; 32 checks. O passo `identity` (v25) prova no ferro que a assinatura de um registro de auditoria **sobrevive ao apagamento da conta**: a conta age, é apagada, outra toma o MESMO slot, e o que chega ao coletor ainda nomeia quem agiu. **v25: `Rig.pick_user( )` escolhe a conta ANTES do PIN** e `Rig.pin( )` toca onde a grade lida disser — a geometria deixou de ser constante porque o teclado virou política (4, 6, 12 ou 18 cartões). ⚠️ `touch sim` pela CLI perde toques (fila de 2 na janela de prioridade do toque). ⚠️ Ler o sorteio pela serial custa 1,2 s por dígito contra 0,01 s pela rota web. | 2026-09-20 |
| `panel_fulltable_test.py` | Enche todos os slots de conta livres e dirige o painel com todas: acesso, uma ação de configuração por conta, o log de eventos e a linha de alarme. **v25: inverteu o que mede.** Até a v24 media quantas entradas legítimas eram recusadas por caber em duas contas (15% com 4 dígitos e 25 contas, 19/09); agora a conta é escolhida antes do PIN, a árvore é resolvida contra UM digest e o teste AFIRMA que o código 311 fica em zero com a tabela cheia — junto da taxa de acerto na 1ª tentativa, porque uma bancada que parou de chegar ao teclado também relataria zero colisões. Lê sorteio e log por HTTP; pela serial a rodada não termina. | 2026-09-20 |
| `pin_policy_matrix.py` | O que cada botão da política de PIN (alfabeto, teclado, comprimento) vale em segurança, em resistência a quem olha por cima do ombro e em Core 0 bloqueado. Calibrado pelos 360 ms medidos de 8 toques × 3 glifos (9.840 nós, 36,6 µs/nó). `--v24` imprime o que a v25 mudou, número a número. Puro cálculo: não fala com o rig. | 2026-09-20 |
| `build_lang_pack_es.py` | F-LANGPACK — gera data/lang/language_es-ES.lng a partir do PT-BR. | 2026-08-16 |
| `capture_web_shots.py` | capture_web_shots.py — recapture the web UI screenshots used by the README | 2026-08-22 |
| `webui_preview.py` | Pré-visualiza a interface web do `WebUI.h` sem gravar o firmware: serve as páginas, o `/lang.js` e o `/style.css` do arquivo em edição (relido a cada requisição) e encaminha a API para um SIMUT real, com a sessão feita pelo próprio proxy. `?theme=light&lang=pt&run=toggleDrawer()` na URL semeiam o que a captura precisa. Uma instância só: cada login ocupa um dos três slots de sessão do aparelho. | 2026-09-16 |
| `export_csv_bench.py` | r"""§5.19 Performance — export CSV de 3 dias (PLANO-VALIDACAO-v2.3.2-stable.md). | 2026-08-23 |
| `a11y_keyboard_tests.js` | §5.16 of PLANO-VALIDACAO v2.3.2: the web UI driven by keyboard only, in a real browser (Playwright). | 2026-08-23 |
| `browser_tests.js` | §5.5/§5.7 of PLANO-VALIDACAO v2.3.2: the web UI in a real browser — login, every page, the history graph. | 2026-08-22 |
| `https_tests.js` | The same browser checks over HTTPS, against a release image that serves one TLS client at a time. | 2026-08-22 |
| `offline_dns_tests.js` | §5.17: the web UI renders with DNS cut — proof that no page depends on anything outside the device. | 2026-08-23 |
| `soak_nav.js` | §5.7: an open-read-click soak of at least 30 min over HTTPS, watching for a page that stops answering. | 2026-08-22 |
| `test_config_page.js` | Exercises the shipped /config loader against its three failure paths (no session, truncated JSON, device gone). | 2026-07-23 |
| `theme-editor/index.html` | Browser app for authoring `.thm` theme files — `index.html` + `app.js` + `presets.js` + `sha256.js`; open the file, no server needed. | 2026-06-03 |
| `factory_reset.py` | Factory reset Pico and configure WiFi. | 2026-07-23 |
| `factory_reset_manual_wifi.py` | r"""§5.6 E2E — Config de fábrica → WiFi com reconfiguração manual | 2026-08-23 |
| `flash_compose.py` | Where the flash goes, from a linker map: attributes every section to its archive or object and computes the merged string pool once instead of trusting the map (docs/analysis/DIETA_FLASH.md). | 2026-09-18 |
| `fix_lockout.py` | Fix DisplayManager.cpp - reduce lockout timeout and add Core 1 restart. | 2026-07-23 |
| `gen_gfx_font.py` | Porte fiel do fontconvert.c (Adafruit GFX) para Python/freetype-py. | 2026-08-12 |
| `history_reset_cycles.py` | r"""Abrupt-reset cycles against the V5 history, driven by the PicoHand. | 2026-08-10 |
| `history_watch.py` | r"""Watch the V5 history cadence over serial, using only the release CLI. | 2026-08-10 |
| `history_web_cases.py` | r"""The two history-loss cases that need a web session, not just serial. | 2026-08-10 |
| `inline_tokens_check.py` | Portão das cópias dos tokens de cor: o bloco dos dois temas servido no `/lang.js` tem que ser idêntico às cópias inline de `/login` e `/force_chpass` (que não carregam o lang.js), e nenhuma página autenticada pode voltar a carregar uma cópia. | 2026-09-16 |
| `repro_lockout.py` | repro_lockout.py — §5.9: lockout de login — backoff exponencial + teto. | 2026-08-22 |
| `repro_restore_gate.py` | repro_restore_gate.py — §5.9/§5.21: restore sem auth → 100 recusas, 0 arquivo. | 2026-08-22 |
| `rig_validate_history_clock.py` | Hardware validation for the provisional-clock and out-of-order fixes. | 2026-08-15 |
| `screen_safe_area.py` | r"""§5.14 Display/TFT — área segura (PLANO-VALIDACAO-v2.3.2-stable.md). | 2026-08-23 |
| `syslog_live_check.py` | r"""§5.18 Interoperabilidade — Syslog RFC 5424 ao vivo (bônus). | 2026-08-23 |
| `telemetry_bench/mini_mqtt_broker.py` | mini_mqtt_broker.py — broker MQTT cru que imprime payloads (investigação). | 2026-08-22 |
| `telemetry_bench/phase_perf.py` | Phase A — telemetry throughput and latency, per transport and batch size. | 2026-08-02 |
| `telemetry_bench/run_rest.sh` | Runs the remaining phases back to back. Only one may hold the serial port at | 2026-08-02 |
| `telemetry_bench/run_rest2.sh` | Second leg: the two runs that need the MQTT phases out of the way first. | 2026-08-02 |
| `telemetry_bench/run_rest3.sh` | Third leg: true history inventory, once nothing else is loading the device. | 2026-08-02 |
| `telemetry_bench/summarize.py` | Turn the campaign's JSON output into the tables that go in the report. | 2026-08-02 |
| `telemetry_bench/verify_huge1mb.py` | verify_huge1mb.py — reproduz o fault huge1mb isolado, 2 corridas. | 2026-08-22 |
| `telemetry_bench/verify_mq_rst.py` | verify_mq_rst.py — mq_rst isolado: perda real ou ruído do heurístico? | 2026-08-22 |
| `test_bmx280/flash.sh` | Build and upload the BMx280 test sketch | 2026-08-14 |
| `test_fable5.py` | Test Claude Fable 5 via raw HTTPS (no SDK dependency). | 2026-07-23 |
| `test_stability.py` | Stability test - check if lockout and WDT fixes work. | 2026-08-16 |
| `test_webui_graph_order.py` | Regression test for the graph reader's handling of out-of-order blocks. | 2026-08-15 |
| `theme_audit.py` | Coherence audit for SIMUT themes. | 2026-08-15 |
