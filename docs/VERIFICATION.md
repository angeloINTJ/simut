# Verification on hardware

[English](VERIFICATION.md) | [Português](VERIFICATION.pt-BR.md) | [Español](VERIFICATION.es-ES.md)

What has been measured on real hardware, and on what bench. The [README](../README.md) has the summary.

**The bench:**
- a Pico W with the TFT panel and touch;
- a second Pico, the *PicoHand*, which works the target's RESET and BOOTSEL lines, times its awake/asleep line and fakes a charger (see [AGENTS.md](../AGENTS.md), in Portuguese);
- bench suites in `tools/` for the web API, the panel, telemetry, OTA, Wi-Fi outages and the Air cycle.

What has been measured on real hardware, latest first:

| Date | What | Result |
|---|---|---|
| 2026-10-02 | Release candidate (v2.9.0), signed by the CI | Web suite 87 passed, 0 failed on the test image; update from the published v2.8.0 over the air on release, Air and alpha: the configuration file identical byte for byte, every configuration value the web API reports unchanged, five sensors reading, the `.bkp` restored with 70 or 71 of 74 files identical (the others are the log and the history); the candidate over itself, checking its own signature; an unsigned image (8), the Air's (7) and v2.8.0 (8) refused; an upload cut at 400 kB and a reset, with and without the filesystem filled in between: configuration intact; 30 min without a restart |
| 2026-09-30 | Release candidate (v2.8.0) | Web suite 87 passed, 0 failed; update from the published v2.7.4 over the air: the configuration file identical byte for byte except the version, five sensors reading, the `.bkp` restored 67 of 72 files identical (the other five expected to differ); an upload cut at 400 kB, then a reset: configuration intact; 10 min without a restart; Air and alpha updated over the air from v2.7.4 with their configuration (the Air's own options back from the `.bkp`) |
| 2026-09-30 | An upload cut, then the filesystem filled (v2.8.0) | Same starting flash, upload cut at 400 kB, nothing changed, the filesystem filled to 100 % and emptied, reset: before #195 the device came back on factory defaults, after it with its configuration. The same cut on v2.7.3, without the filling: factory defaults (#192) |
| 2026-09-30 | Custom telemetry Content-Type (v2.8.0) | Collector on a PC: the header received matches the field for `application/x-ndjson`, `text/csv` and `application/json; charset=utf-8`; empty sends `application/json`; the JSON format ignores the field; `bad value`, `aplicação/json` and `json` are refused at save |
| 2026-09-26 | Release candidate (v2.7.4) | Web suite 87 passed, 0 failed; all five sensors across the three families; after a sensor scan the BMP280 kept reading for 90 s (before the fix it failed about 10 s after); a rehearsed commit of the sample interval answers `"reboot":false`; 10 min without a restart |
| 2026-09-26 | The alpha's 16×2 LCD (v2.7.4) | On an HD44780 wired in parallel: the boot screen with the version and its progress bar, the connected screen with the IP, then each sensor in turn with its slot and the Wi-Fi level |
| 2026-09-25 | Configuration page and login page (v2.7.3) | *Restart without saving*, on the `release` image and on the test build: offline 3.3 s after the click, back at 26.4 s, and a name edited but never saved did not survive the restart. The login page shows the version in both themes; 9 pages, 0 script errors |
| 2026-09-24 | Panel: PIN security and setup mode (v2.7.2) | The footer arrows stay on the screen (v2.7.1 closed it); an unsaved tap no longer changes the stored policy; Confirm shows the network, the key and 192.168.4.1 (v2.7.1 stayed on the confirmation, AP already up) |
| 2026-09-23 | Air clock across the sleep (v2.7.2) | Stamps within −0.085 … +0.030 s over 10 wakes (v2.7.1 lost 0.8 s per wake); the NTP correction fell from 9–10 s to 0.08 s |
| 2026-09-23 | Long telemetry queues on the Air (v2.7.2) | 0 invalid bodies; 13,681 of 13,682 records delivered awake, 13,670 of 13,671 hibernating (v2.7.1: 68 of 69 bodies were invalid JSON) |
| 2026-09-22 | v2.7.0 soak | 8.18 h, 0 reboots; the largest free heap block moved −42 B |
| 2026-09-22 | v2.7.0 over-the-air updates | 6 of 6 applied; 57 files restored, 0 records missing |
| 2026-09-22 | Setup access point (v2.7.1) | A client joins in 4.1 s, on `release` and on `alpha` with Bluetooth live, also with a randomised MAC. The automatic fallback opens after 6–7 min without a network (removed on 2026-10-01) |
| 2026-09-22 | V-09 fix | 10 of 10 verdicts, with positive controls |
| 2026-09-21 | Collector down for 3 h 58 min | 237 records queued, 0 reboots; drained in one round with 0 missing, plus 25 alarm-line records |
| 2026-09-21 | Web suites | 67/67 as admin, 87/87 as a restricted account; 500 flash-writing commits, 0 reboots |
| 2026-09-21 | Wi-Fi scan | 18 of 18, 0.94 s per sweep, also from inside the access point |
| 2026-09-20 | Panel accounts, PINs and policy | 32/32 |
| 2026-09-19 | Panel mirror | 613 → 213 ms per frame; pixel-exact against the framebuffer (0 of 76,800 differ) |
| 2026-09-11 | Power cut during an update | Only the ~25 s apply window leaves the device needing BOOTSEL |
| 2026-08-10 | History across resets | 10 of 10 hardware resets and 10 of 10 reboots lost 0 records |

Two things on the 16×2 LCD have not been on glass: the single-sensor layout, with its pending telemetry count (the bench has five sensors), and the access-point pages, which by the code the LCD does not reach.
