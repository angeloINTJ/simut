# Changelog

**English** | [Português](CHANGELOG.pt-BR.md)

All notable changes to SIMUT firmware.

## v1.6.3-beta (2026-07-30)

Saving a sensor calibration could fail permanently, and for two of the three
sensor families it had been doing nothing at all since v1.6.2-beta. Pressure,
which the sensor and history paths have carried for months, finally reaches the
calibration panel and the history extremes.

> Updates over the air work from v1.6.2-beta onward, so this one can be applied
> that way. Validated across 20 consecutive applies, below.

### A calibration save could fail forever

`calib.csv` carries a VERSION line, and the commit renames the temporary file
over the real one only when the new version beats the stored one. The version
came from `getEpoch( )`, which never fails visibly: with NTP down it falls back
to the virtual RTC, and failing that to `SIMUT_BUILD_EPOCH`, a compile-time
constant. Both are behind the real epoch that a previous, synced save wrote.

So on a device whose clock was not synced, every calibration save produced a
version *lower* than the one on disk, the comparison failed, and the commit
deleted `calib.tmp` and answered HTTP 500. The reported symptom — "calib.tmp is
created but the .csv is never replaced" — is exactly that.

The failure absorbs: once the stored version passes the clock, no calibration
can ever be saved again, because every subsequent attempt loses the same
comparison. With the build-epoch fallback that meant months. Version stamps are
monotonic now, so a save always moves forward whether or not the clock does.

The guard that should have caught this was dead. `/api/calib` refuses to run
when `isTimeSynced( )` is false, but that function is `getEpoch( ) > 1600000000`
and `getEpoch( )` never returns anything smaller — the check could not fire.

### Calibrating a DHT22 or a BMP280 silently did nothing

Sensors without a 1-Wire ROM are keyed in `calib.csv` by the board serial, with
the measurement letter and the sensor's hwId in the id column. Rewriting the
file split the key off the front of each line but left the id column as
`<id>,<offset>,<name>`, which never compared equal to a bare id. Every
board-serial row therefore missed its own update, was copied through untouched,
and the new value was appended at the end instead.

Readers stop at the first match, which is the stale row at the top. The offset
was written correctly and never read, and the file grew by one row per sensor
per save. Introduced in 4cff8ca, so it affects v1.6.2-beta only. Files already
carrying duplicates collapse back to one row per sensor on their next save.

### An interrupted upload left a file nothing would collect

The file-upload handler had no `UPLOAD_FILE_ABORTED` branch, so a connection
dropped mid-transfer left the `File` handle open and the partial file on flash.
For `calib.csv` that meant an orphan `/calib.tmp`, and the commit that would
have resolved it only ever ran from the two web handlers — never at boot.

Both halves are closed: the abort path discards the partial file, and boot
collects a stranded `calib.tmp`. Recovery refuses a truncated one rather than
promoting half a calibration over a good file, since a reset can land in the
middle of the write.

### Pressure reaches calibration and the history extremes

A BMP280 reports temperature and pressure and no humidity. The calibration API
and the history statistics were built around temperature and humidity, so its
pressure had nowhere to appear: no field in `/api/calib`, no reference input on
`/config`, and no MIN/MAX badge on `/history` even though the chart drew the
series. The offset was not applied to readings either, so it would have been
write-only had the rest existed.

### One table for what a measurement is

Fixing the above meant editing five layers that each kept a private copy of what
a channel is — the V4 prefix, bit-width and scale switches, the codec's
signedness test, the calibration reader's letter whitelist, the row writer, and
a per-driver restatement of every channel's unit and icon. That is why pressure
support had to be added in five places and still did not work: one of the copies
was a whitelist that refused the letter.

`sensors/SensorChannelTable.h` now holds one row per quantity, binding it to its
storage identity and to a display preset from `SensorPresets.h` — a catalogue of
80 units that had been in the tree, unreferenced, since it was written. Adding a
quantity is one row plus one bit in the driver's channel mask.

The wire formats follow. `/api/calib` reports `channels[]` and accepts
`refs{}`; `/api/history_multi` reports `extremes{}`; the pages iterate instead
of naming a field per quantity. The fixed keys ship alongside for one release so
a cached page keeps working.

`tools/check_channels.py` fails the build if a channel letter appears outside
the table, and reports what has not been generalized yet. Alarm thresholds are
in that backlog: `SensorRecord` still has fixed temperature and humidity limits,
so **there are still no pressure alarms** — that needs a stored-config schema
change and is not in this release.

### Validated: 20 consecutive over-the-air updates

Each cycle shipped an image carrying a version marker no other image had, so
"it applied" is a version read back from the running firmware rather than an
inference from an HTTP code or from elapsed time — every layer of an OTA reports
success whether or not anything was replaced.

| step | n=20 |
|---|---|
| upload + stage (962,476 B) | 29.2 s |
| apply → web reachable again | 47.5 s (45.6–49.8) |
| full cycle | 83.2 s (81.3–85.6) |

All 20 applies were confirmed by marker. No soft panic, no `APP_CORE1_DEAD`, no
watchdog reset across the 20 boots; heap free ended at 55,452 B, unchanged
within noise from where it started.

### Also

- `SIMUT_BUILD_EPOCH` was stamped 2025-09-20 and commented as 2026-07-21. It is
  the fallback clock for a device that has never reached NTP, and the further
  behind it sits the worse the version regression above behaved.

## v1.6.2-beta (2026-07-27)

The headline is not a feature: **OTA has never applied an update**, on any
published version, and this is the release where it does.

> **Flash this one over USB.** Everyone on v1.6.1-beta or earlier is running the
> broken applier, so there is no over-the-air path to the version that fixes
> over-the-air updates. From this build onward, OTA works — measured across 21
> consecutive updates, below.

### OTA reported success at every step and never replaced the firmware

The applier's watchdog feed was a reboot.

Feeding the RP2040 watchdog means reloading LOAD at offset 0x04.
`applier_wdt_feed()` instead wrote bit 31 of CTRL at offset 0x00 — the TRIGGER
bit, which forces an immediate reset. `WATCHDOG_CTRL_OFFSET` is 0x00, so it was
writing the same bit to the same address as `applier_reboot()`. The first call,
right after the sector-0 program in step (1a), reset the chip before a single
sector was erased or copied.

What that produces on the bench is indistinguishable from a successful apply
that changed nothing: the app slot keeps the old firmware, the reset reason is a
forced watchdog, metadata is left in APPLYING, and LittleFS is gone — destroyed
not by the applier but by the upload, since staging shares the partition with
it. Stage, apply and reboot all report success. Nothing compared the app slot
against what was staged, so the version simply did not change.

This is unchanged since v1.4.4-beta, and the same code is in v1.0.0.

Two more bugs sat behind it, on lines the applier never reached:

- **`memcpy` lives in the app slot**, which step (1b) erases. The first copy in
  step (2) would have executed erased flash. Replaced with an SRAM word copy;
  volatile pointers keep GCC from recognising the loop and calling `memcpy`
  again. Verified by disassembly: every branch target in `ota_applier_run` now
  resolves to SRAM.
- **`WATCHDOG_SCRATCH4_OFFSET` was 0x18, which is SCRATCH3.** `applier_reboot()`
  was clearing the trace register the boot autopsy reads as `sc3` and leaving
  the bootrom's watchdog magic untouched. Corrected to 0x1C.

The post-apply boot now CRCs the app slot against the metadata and logs the
verdict. The applier computes this too, but it runs from SRAM with interrupts
off and cannot report anything, so it discarded the result — which is why three
separate bugs survived this long. The check belongs where there is logging, and
the metadata is still on flash at that point.

`/api/ota/apply` failures also reach the user now: the firmware page checked
none of the three responses and swallowed its own exceptions.

### The post-apply check compared a CRC against the wrong length

Staging reported the padded size in both metadata size fields, so the pair
(size, CRC) never described the same bytes. `stage_session_end` pads the last
256 B page with 0xFF and `bytes_written` counts that padding — correctly, since
it is what the applier has to copy — while the CRC covers only the bytes that
arrived. Verifying the CRC of 957,460 bytes against the CRC of 957,696 fails on
a byte-perfect copy.

The session now tracks `bytes_received` separately and reports it as the
uncompressed size, giving the two fields the meanings the struct already
documented. `/api/restore`'s `dsize` and `dcrc` describe the same range as well.

That alone would not help an update staged by an older build, which is every
update to this version: the padded length is all its metadata carries. So the
post-apply check accepts any length within the final page.

### Validated: 21 consecutive over-the-air updates

Measured on the bench (Pico W, `pico_w_release`), 21 stage+apply cycles back to
back. Each cycle staged an image carrying a distinct version string, so "it
applied" is read back from the device rather than inferred from an HTTP status:

| Stage | Time |
|---|---|
| Upload + stage (957,500 B) | 29.2 s ± 0.07 (32.1 KiB/s) |
| `/api/ota/apply` → 202 | 0.1 s |
| Applier window (erase + program) | 25.1 s ± 0.10 |
| Reboot → image verified | 9.4 s ± 0.06 |
| **Web interface unreachable** | **48.4 s** |

21 of 21 applied. The verified length came back as exactly 957,500 B every time,
the config snapshot survived every reformat with Wi-Fi rejoining unattended,
free heap moved 24 B across the whole run, and not one boot produced a soft
panic — under the heaviest flash load the firmware has.

The download is the slow part, and the web interface is unreachable for roughly
50 seconds. Two thirds of that is the applier; the rest is Wi-Fi re-associating.

### A white screen after setting the display offset

Two independent writers were racing the display. The touch-calibration auto-set
block called `saveConfiguration()`, which writes flash, about 190 lines after
`startCore1()` — and boot defers Core 1 precisely so flash work can take the
single-core path. `setDisplayOffset()` also repainted the margins
unconditionally, so Core 0 drew to the TFT while Core 1 rendered.

The symptom was a blank screen on the next boot after adjusting the offset,
which read like corrupted settings but was a torn write.

### The log's uptime column always read zero

`CompactLogRecord` stored uptime as `millis() / 3600000` in a `uint16_t`. Any
device that reboots more than once an hour writes 0 into every record it ever
makes, which on a bench board is every record. The column was not missing an
implementation — it had one, at a resolution that rounded the entire useful
range to zero.

Uptime is now seconds across 24 bits, reusing a `reserved` byte that was written
as 0 and read by nobody, so the record stays 12 bytes. `setUptimeSec` saturates
rather than wraps, because a truncated large number would read as a small
plausible one. The serial dump, the `/api/logs` decoder and the CSV export
follow, and that column changes from `uptime_hr` to `uptime_sec`.

**Old `.blog` files decode differently.** There is no version marker in the
format, so a record written before this reads its old hours field as seconds —
in practice 0, which is what that field already contained.

Flash 944,600 -> 945,464 B (+864).

## v1.6.1-beta (2026-07-27)

Single fix, shipped on its own because the symptom is silent and the trigger is
an ordinary maintenance action.

### Replacing a language pack broke every translation until reboot

`/api/lang` streams the `@WEBDICT` block straight off flash using a byte range
the parser records **once, at boot**. Upload a new pack through `/files` and
those numbers still describe the previous file: the handler seeks to a stale
offset and sends a stale length, so the response ends in the middle of a string.

Invalid JSON makes the browser's `JSON.parse` throw, and that drops the **whole**
dictionary — all ~400 keys fall back to English, not just the ones that changed.
Nothing is logged; the interface simply switches language. Measured on the
bench: a pack 143 B larger than the resident one produced 15,868 B of truncated
body.

The range is now scanned from the file on each request instead of trusted from
boot. Rescanning rather than reloading the pack is deliberate — a reload costs a
~28 KB transient allocation and rewrites the strings Core 1 is reading off the
display, while this endpoint never touches the resident dictionary and only
needs the range. One pass over ~28 KB of flash, on an endpoint the client caches
for five minutes.

Verified against the real failure: a pack with the block shifted +105 B, and
`/api/lang` stayed valid at 404 keys with no reboot.

**Who should update.** Anyone who uploads or replaces a `.lng` through `/files`.
If you have never done that, v1.6.0-beta behaves identically — the stale range
is only wrong once the file underneath it changes.

Flash 944,408 -> 944,600 B (+192). No other change.

## v1.6.0-beta (2026-07-27)

Universal-model release. Three special cases were standing in for general
rules, and each of them was visible to a user as a bug rather than as a design
choice: a slot that could not be freed, a sensor whose pressure never appeared,
and a history layer carrying two formats where one is written.

Minor bump rather than patch: `SensorFormat` changed shape, `TYPE_BMP280`
exists, and the factory default no longer provisions any slot.

> **Still under test.** Verified on the bench against real hardware (2 DS18B20,
> 1 DHT22, 1 BMP280) but without a long soak.

### Slot 10 stopped being "the ambient sensor"

Eight places treated one slot as special: `/api/calib` emitted an extra
`ambient` object hardwired to `cfg.sensors[10]`; `/alarms` accepted `idx == -1`
as an alias for it; `/api/config` published its hwId as `ambHwId`; the telemetry
tokens `{tAMB}`/`{uAMB}`/`{pAMB}` resolved their key through it; the history
graph defaulted to sensor 10 and grafted the record's `ambientHum` onto that
slot alone; and `loadDefaults` pre-activated it as a DHT22 named `AMB` on GP10.

The last one is what a user hits. The `/config` pin picker greys out every GPIO
owned by an active slot, so a phantom sensor wired to nothing made **GP10
unassignable**, and a factory reset put it back. **All 16 slots now come up
empty and claim no GPIO.**

Three defects surfaced inside that work:

- **No calibration offset ever reached a running sensor.**
  `loadAndCalibrateSensors` applied the offsets and *then* called
  `initRuntimeSensors`, which rebuilds the vector with every offset back at 0.
- **`/api/calib` indexed its per-slot arrays by GPIO and read them by slot
  number.** Those agree only while every slot sits on the GPIO of its own
  number — the factory layout, and nothing else.
- **One humidity calibration per board.** Offsets for ROM-less parts were a
  single device-wide row pair, found by "first line starting with `t`/`u`" and
  applied to "the first DHT22 in the runtime list". A board with two DHT22s
  could calibrate exactly one, and which one depended on slot order. Rows are
  now tagged with the sensor's own hwId; **the `calib.csv` format is
  unchanged**.

### A BMP280 is not a BME280

`sensorHasChannel()` was `channel < valueCount` — channels had to be a
contiguous prefix of the enum. A BMP280 measures **temperature and pressure and
no humidity**: `{CH_TEMP, CH_PRESS}` with a hole at `CH_HUM`, which a count
cannot express. So both parts shared `TYPE_BME280`, which declared humidity and
was *displayed* as "BMP280" — whichever chip you owned, the firmware was wrong
about one of them.

- `SensorFormat` carries a **channel mask**; `values[]` is indexed by channel.
- **`TYPE_BMP280`** is a distinct type, appended so no stored value shifts.
- **The chip ID decides.** `initRuntimeSensors` adopts what the part reports
  (0x60 = BME280, 0x58 = BMP280) and persists it, so an existing slot corrects
  itself on the next boot without anyone having to know which chip they soldered.
- The phantom humidity is gone from the V4 schema, `/api/calib`, `/api/alarms`,
  `/api/config` and the history chart. **Pressure gained a series and an axis**
  on `/history` — it sits near 1000 hPa and would flatten °C and %RH if it
  shared either.
- The type catalogue moved from a `t <= TYPE_BME280` range to an explicit list.
  That range excluded every type added after it, and only avoided
  `TYPE_UNKNOWN_ACTIVITY` by accident of enum order.

### History: v2/v3 removed, V4 is the only format

`HistoryCodec` is deleted — 441 lines, plus five `.bin` readers, its 653-line
test suite and the v1→v2 converter. **The legacy writer had had no callers for
several releases**: an entire delta codec kept alive to serve nobody.

Two readers could not simply be deleted, because they read *only* the legacy
format: the `.simx` export bundle and the telemetry pending count. Both were
rewritten over V4 — otherwise the export would have gone silently empty and the
dashboard counter would have read zero forever. `getLastRecordedTimestamp`,
which seeds the virtual RTC at boot, had the same problem.

`BinaryHistoryRecord` loses `ambientTemp`/`ambientHum`. Nothing had written them
since V4 landed; they survived only as the first two fields of the v2/v3 layout.
The struct is now what its name never said: an **in-RAM carrier**, not a file
format.

> **Migration.** `.sim4` files are untouched and keep working. Any `.bin`
> history still on a device becomes unreadable by this firmware — convert it
> first with `tools/history_v2_to_v4.py`, which is kept for exactly this.

### Silent failures made loud

- **A failed PIO claim disabled a whole sensor family without a word.** Both
  drivers dropped the return value of `begin()`, and `DHTBus` consults its own
  `_isInitialized` only in the destructor — so `requestReading` went on driving
  state machine 0 of `pio1`, which this firmware never owned and which is shared
  with the CYW43 radio on a Pico W. Symptom: every read of that type times out,
  on every pin, with nothing in the log. Both drivers now refuse to touch the
  PIO when init failed, and `SensorManager::begin` logs which block was full.
- **The `STH` prefix hijacked user-chosen IDs.** The auto-ID regenerated any
  hwId starting with those three letters — a marker from an older scheme that
  the current generator never emits, so the clause could only ever hit an ID a
  person had typed. Set `STH0001`, reboot, get `DHT2202` back. Empty is now the
  only trigger, and `commit_all` refuses a blank hwId on an active slot instead
  of letting the next boot refill it.
- **The default telemetry template published nothing but a timestamp.** It was
  `{"ts":{TS},"tAmb":{tAMB},"hAmb":{uAMB}}`, and both AMB tokens read record
  columns nothing had written since V4.

### Filesystem manual, and a favicon that stops disappearing

- **`/README.txt`** is written by the firmware at boot: a map of every directory
  and file, what belongs where, and the traps (`uploadfs` reformats the
  partition; the V4 schema freezes when the day's file is created). It cannot be
  deleted from `/files` — the row has no checkbox and `/api/delete` answers 403.
- **`/themes` and `/web` are created at boot** and each system folder carries a
  one-line note. That note is load-bearing: LittleFS drops a directory with no
  entries from the parent listing, so an empty `/themes` did not exist as far as
  the file manager was concerned — **and a folder you cannot see is a folder you
  cannot upload a theme into**.
- **The favicon moved back into the firmware image.** It went to LittleFS when
  real flash headroom was 660 B; that is no longer the constraint, and the
  filesystem copy vanished on every `system format`. The generator that was
  supposed to produce it read a directory that does not exist and wrote outside
  `build_src_filter` — it could never have worked, and nothing called it. It is
  a pre-build hook now, with a hash check.

### Web UI

- **The dashboard never said "synchronized".** The line under the pending
  counter was static markup with `data-i18n`, written once at load and never
  revisited — it read "waiting" forever, including at zero. `/api/status` gained
  `tel` so the four states are distinguishable; without it, `pending == 0` means
  both "nothing left to send" and "nothing is ever sent".
- **The IP was deleted on mobile**, not fitted: the 640 px breakpoint had
  `.status-pill span { display: none }`. It stays and truncates.
- **CSV export was broken for everyone.** The browser-side reader tested
  `recordSize !== 28` against a firmware emitting 74.
- **History min/max are shown only for a single selected sensor** — the server
  measures the extremes across all of them, so with a mixed set the strip
  reported the coldest reading of whichever probe happened to be coldest.
- **A series with no numeric point is no longer drawn**, which is what removed
  the phantom humidity line from the BMP280.
- **`/files` buttons are uniform**, sized from the longest label across the three
  language packs, and file names are download links — reading a file used to
  require ticking its checkbox, which left the protected README unopenable.

### Numbers

| | v1.5.6-beta | v1.6.0-beta |
|---|---|---|
| Flash (`pico_w_release`) | 939,096 B | 944,408 B |
| Real headroom | 93,348 B | 89,092 B |
| RAM (`.bss` + `.data`) | 120,492 B | 122,540 B |
| Native tests | 141 | 119 |

The favicon accounts for 11,047 B of the flash delta and the v2/v3 removal
returns 8,888 B. RAM grows by one `HistV4State` in `getLastRecordedTimestamp` —
V4 records are delta-encoded, so there is no seeking to the end of a file, and
the RP2040 stack is ~4 KB. The test count drops because the 653-line v2/v3 codec
suite went with the codec.

### What has not been verified

- No long soak. The R1 class (Core 1 heartbeat race under heavy flash load)
  is unchanged and still open.
- The BME280 path is untested against real hardware — the bench has a BMP280.
  The split is symmetric, but only one side has been exercised.
- `pico_w_alpha` does not link (`DisplayManager::showTouchSensitivity`
  undefined). Pre-existing, unrelated, and confirmed against v1.5.6-beta.

## v1.5.6-beta (2026-07-26)

Web-first release. Every setting already had a web equivalent, and the serial
CLI was carrying a second, untested copy of all of it: 42 of its 55 commands
duplicated a page that already worked. The release image now ships **9
commands** — the ones that matter when the web is what is broken — and the
duplication is gone, along with **44,516 B of flash**.

A translation review came first and is what surfaced the duplication. It also
found that the English fallbacks in the web UI were Portuguese, so an
English-speaking user read "Salvar e Reiniciar" on the top bar.

> **Still under test.** This is the largest structural change since 1.0.0 and it
> has not had a long soak. See *What has not been verified* at the end.

### The CLI shipped 55 commands the web already answered

- **`SIMUT_CLI_FULL`** (`SystemDefs_Cli.h`, default 1) selects the surface.
  `pico_w_release` sets 0 and keeps `show net status`, `show system info`,
  `show system log`, `debug on|off`, `system admin reset`, `system format`,
  `system factory`, `reload` and `help`. Single prompt, no Cisco mode tree.
- The four CLI files went from **56,361 to 13,904 B of text**. Flash
  983,180 → 938,664 B (94.1% → 89.9%). The five web actions added to replace
  what was cut cost ~3.7 KB back, hence 44,516 B net.
- **`[env:pico_w_test]`** builds the full CLI and exists for the suites under
  `tools/`, which drive the device over serial with `enable`,
  `configure terminal`, `write memory`, `user add/del/perm` and `touch sim`.
  `web_test_suite.py` bootstraps its throwaway account that way because it
  cannot authenticate yet. **Flash the test image before a suite run.**
  It links at exactly the pre-change byte count, which is the evidence that the
  full profile was not disturbed.
- Removing the CLI also removed the **282 hardcoded `isPt()` string pairs**,
  which is what made a device running the Spanish pack answer in Portuguese.

### Five operations that had no web equivalent

`POST /api/action?op=` — one route with a selector rather than five routes, for
the reason `/api/restore` documents. `sensor_scan` / `scan_results` (arm and
poll; the scan is a state machine the main loop steps, so the handler never
blocks), `sensor_accept`, `sensor_wipe`, `tel_sync`, `tel_reset`.

They bypass the Save-and-Restart staging buffer deliberately: each reads or
writes hardware state at this instant, so deferring them would apply them
against a different reality.

### Translations

- **es-ES was machine-generated from pt-BR and never reviewed.** Raw Portuguese
  in the display dictionary (`SALVAR`, `PULAR`, `Umid Min/Max`, `SIM`/`NÃO`,
  `%UR`), in ~25 web strings, and in roughly 60 of the 115 log codes.
- Two entries were a **rendering** bug: `unaccent()` maps ASCII, the 0xC3 block
  and six 0xC2 symbols, so the `¡` in `¡Calibración Completada!` reached the TFT
  as a literal `?`. `@DICT`, `@HELP` and `@LICENSE` are now checked for it.
- **14 `window.t(key, fallback)` calls passed Portuguese as the English
  fallback** — which is exactly what an English user sees. Two `TRL()` literals
  were Portuguese sentences in the C++ source.
- Coverage: pt-BR was missing the 15 `sens_rebind_*` keys; es-ES was missing 75
  web keys plus `@HELP` and `@LICENSE` entirely, so its CLI fell back to
  English. 44 dead keys removed from both. Packs now agree with the firmware on
  all 109 display strings, 119 log codes, 81 log translations and 403 web keys.
- `*.lng` was marked `binary` in `.gitattributes` while being plain UTF-8, so
  no translation had ever appeared in a diff. Now text.

### Fixes found along the way

- **`AppManager_Loop.cpp` filtered `CMD_UNKNOWN` before `executeCommand` at both
  dispatch points**, which made the "unknown command" branch dead code — a typo
  returned silently to the prompt, and always had. Harmless while nearly
  everything parsed; not harmless when 46 commands now parse to `CMD_UNKNOWN`
  and silence reads as a hung device.
- `/api/action` validated the slot before the op, so a typo in the op name came
  back as `{"error":"slot"}`.
- The "Page asset missing" message named `config.html.gz` literally, which
  stopped being true when `/config` moved back into the firmware. It reads the
  path from its argument now.
- `pico_test_suite.py` could not connect to a board that had been up for a
  while: `_connect` waited passively for a prompt, but opening with DTR does not
  reset this board and the firmware only prints a prompt in reply to input.
- Test 11 logged in as the factory `viewer` account, which cannot be recreated
  once deleted (`user add` takes plaintext and derives the hash). It brings its
  own account now.

### Verified

Both environments build. 136/136 native tests across four suites. On hardware:
**11/11** `pico_test_suite.py`, **81/0/5** `web_test_suite.py` including the
CLI-bootstrapped account, **8/8** on the new web actions. The emergency console
was exercised directly — the nine survivors answer, cut commands return the
message naming where the setting went, prompt stays `SIMUT>`. The Spanish pack
was loaded on the device and `help` rendered through `unaccent()` with no `?`.

### What has not been verified

- ~~`tel_reset` was never run on hardware.~~ **Verified after publication.**
  Against the bench test endpoint: HTTP 200, then 21 uploads in ~3 min carrying
  62,707 B — against 6 uploads and 3,678 B for the whole prior uptime — with
  **0 failures and 0 retries**, sensor reads still error-free and no reboot.
  That is the backlog re-sending exactly as documented. All five actions are now
  exercised.
- **No long soak on this build.** Previous releases carried multi-hour storm
  runs; this one has minutes.
- The **Core 1 heartbeat race** under heavy flash load (`APP_CORE1_DEAD` →
  soft panic) is still open and unrelated to this release.
- The es-ES pack is newly complete and has had little real use.

## v1.5.5-beta (2026-07-26)

Headroom release, and what it bought. A flash and RAM study of 1.5.4-beta found
that **the Bluetooth stack was linked into every image and nothing ever called
it** — 64,732 B of flash and 16,416 B of RAM for a subsystem that
`build_src_filter` excluded and `SIMUT_BLUETOOTH=0` reduced to empty stubs. Real
headroom went from 4,740 B to 69,472 B, and two features that had been rejected
as unaffordable were built with the space: `/config` back inside the firmware,
and a history rewrite that keeps the day instead of discarding it.

The study is published in full at `docs/ANALISE_FLASH_RAM.md` — measured, not
estimated, including the experiments that turned out to save nothing.

### The Bluetooth that was never there

- **`-DPIO_FRAMEWORK_ARDUINO_ENABLE_BLUETOOTH` selected the `liblwip-bt.a` variant and the *combined* WiFi+BT radio blob**, while `BluetoothManager.cpp` was excluded from every shipping environment. Removing it: flash 1,039,740 → 975,008 B, static RAM 131,436 → 115,020 B, heap 130,704 → 147,120 B. On hardware the number that matters is not free heap but the largest contiguous block, **11,483 → 35,776 B** — the one BearSSL asks for, and the reason `setBufferSizes(4096, 512)` had to exist at all.
- `lib_ignore = SerialBT` is mandatory alongside it: the PlatformIO LDF walks the `#include <SerialBT.h>` inside `#if SIMUT_BLUETOOTH` even with the branch off. Environments that declare their own `lib_ignore` replace the inherited list rather than extend it, so `pico_w_alpha` repeats the entry.
- `pico_w_debug`, documented as overflowing the app slot by ~69 KB, now overflows by **16,576 B**. Still short, but within reach.

### A sensor added today is recorded today

- **A `.sim4` freezes its schema in the header and matches values by hwId**, so a slot added or renamed after the day's file exists had no column to write into. The record was still appended, that channel just stayed at the NaN sentinel, and nothing in the log said so. The only remedy was `sensor reschema confirm`, which recreates the file and throws the day away.
- **`migrateV4Schema` keeps the day.** It rewrites the file against a schema built from the current slots, carries every column that still exists record by record, and fills the new ones with the NaN sentinel back to 00:00. Sequence: quiesce, verify the source (repairing a torn tail first), write a temporary `.mig`, re-decode source and replacement **in lockstep comparing every carried column**, and only then remove the original and rename. The original is untouched until the replacement has been read back from flash and compared.
- **It streams rather than buffering the file, and that is the decision that matters.** Measured with the production codec, a full day at the 1-minute minimum interval is 9.7 KB at 9 measurements but **42.8 KB at 48** and 55.9 KB at the format ceiling — against ~47 KB of free heap and a largest block of ~36 KB. Buffering would pass on a five-sensor bench and fail on a full deployment, which is exactly when the function matters. Streaming costs a constant 5.6 KB regardless of the day, the sensor count or the interval.
- Values are carried **verbatim** when `(bitWidth, scale)` match on both sides — the normal case. Only a genuine width or scale change goes through float, because a raw integer means nothing without the def it was packed against.
- Reachable from a button in the slot editor of `/config`, which is where you are standing when you notice the problem. The client blocks it while edits are staged: migrating reads the slots from flash, so running it on uncommitted changes would freeze the old schema again and spend the day for nothing.
- `POST /api/history_rebind` migrates by default; `?force=1` selects the old destructive path, and the page only offers it when migration fails on an unreadable source.

### `/config` no longer needs to be uploaded by hand

- **The page lived on LittleFS via `FS_PAGES`**, from when the image had 660 bytes of headroom. That carried a bootstrap trap: on a freshly formatted or freshly built device the file is not there, and `/config` — the page you need to configure the device — answered *"Page asset missing"* until someone uploaded `config.html.gz` through `/files`.
- Back in the firmware for **11,544 B**, not the 12,152 B of the array: `serveProtectedFsPage` had `/config` as its only caller, so the helper and its error page are gc-sectioned out with it. The mechanism stays in place, unused, for whenever headroom gets tight again.
- **Updating from 1.5.4-beta leaves an orphan `/web/config.html.gz` on the device**, holding ~12 KB of LittleFS. Delete it from the `/files` page or with `POST /api/delete?file=/web/config.html.gz`. Not `uploadfs`, which reformats the partition and takes `/history` with it.

### Findings recorded but not yet acted on

- **The PlatformIO flash percentage is not the headroom.** It omits the `.ota` section (10,228 B) and the `.text`→`.rodata` alignment padding: "98.3% used" was really 4,740 bytes of slack. `docs/ANALISE_FLASH_RAM.md` gives the only measurement that holds.
- **The documented mDNS knob has never worked.** `NetworkManager` tests `#ifdef SIMUT_MDNS` on a symbol `simut_config.h` always defines, so `-DSIMUT_MDNS=0` produces a byte-identical image with all 236 `MDNSResponder` symbols still linked. Its comment claims ~196 KB; the measured cost is 15,036 B.
- **Heap fragmentation is established at boot, not by traffic.** 32 KB free but 11.4 KB contiguous, and it did not move across 9.6 MB of traffic and 679 requests. The candidates are the two `GFXcanvas16` allocations (40 KB of heap) and the language-pack excision, which peaks at ~42 KB and leaves a 28 KB hole.
- **`-DNDEBUG` is worth 6,600 B** and one remaining `sscanf` is worth 7,532 B; both are measured and neither is applied here.
- **The `lwipopts.h` patch that saves 18 KB of `.bss` lives outside the build tree**, so a clean clone silently builds without it.

### Verified

- 136/136 native tests across four suites, including 7 new cases for the migration: column added, column dropped, reorder matched by hwId, NaN across a width change, scale conversion, codec rewind, header length.
- Migration on hardware against 32 real records: adding a sixth sensor gave `meas` 9→10 with **32/32 records and 240/240 values identical**; removing it gave 10→9 with 32/32 and 240/240 again.
- Load on the published image: **1,343 HTTP requests across two runs, one failure**. The device counted it as a client disconnect (`desconexao 1`), not a fault of its own — heap stayed flat, PBUF reported 0 allocation failures and uptime was continuous through both runs. The second run was 691 requests with 0 errors. Heap 41,268–41,572 B, largest block never below 30,075 B, PBUF peak 7/12, 0 sensor read errors.
- The heap figures are lower than 1.5.4-beta's because the migration costs 5.7 KB of `.bss`. Against 1.5.4-beta as shipped it is still a large gain: free heap 32,220 → 41,572 B and largest contiguous block **11,483 → 30,075 B**.

## v1.5.4-beta (2026-07-26)

Web interface release. The interface was usable on a desktop and hostile on a
phone, and the reason turned out to be structural rather than cosmetic: **not a
single breakpoint in the codebase targeted anything below 600 px**, so every
phone in existence fell entirely below the smallest one that existed. Whole
pages were the desktop layout squeezed, and four of them were not merely ugly
but inoperable.

Nothing here cost app flash. `.rodata` is page-aligned at 4096 bytes and the
whole set fit inside the existing padding — headroom measured 4,740 B before and
after.

### Pages that could not be operated on a phone

- **The first-run password screen was 432 px wide on a 360 px screen** — `width:350px` plus `padding:40px` with no `border-box`. It overflowed every mainstream phone in portrait, and because the body centred it with flex, half the overflow landed on the left, where there is no scrolling back past the origin. The login box had the same defect at 382 px. Both are now fluid, and vertical centring moved from `align-items` to `margin:auto`, which collapses instead of pushing content off the top.
- **The save button left the screen** — the topbar had a hard `height:48px`, no wrapping, and ~475 px of content. `#commit-btn` was the first thing pushed out; on `/config`, `/network` and `/users` it is the *only* way to persist a form, the in-form button having been removed in its favour. On phones it now docks as a fixed bar at the bottom of the viewport.
- **The sensor table dragged the whole page sideways** — six columns, 168 px of pure cell padding in a 228 px column, and no ancestor with `overflow-x`. The real cause sat one level up: `.main-content` is a grid item, and a grid item's default `min-width:auto` refuses to shrink below its content, so the table stretched the column to 824 px on a 360 px screen and the card's own `overflow-x` was never consulted. Fixed with `min-width:0` on grid children.
- **Sound rows needed 428 px of viewport** — 278 px of non-negotiable width (a 140 px melody select in a `flex-shrink:0` group, the test button, a 44 px toggle, gaps and padding) inside 272 px, with no `flex-wrap` to let anything drop. The label now takes the first line and the controls share the second. The volume sliders needed `min-width:0` as well: `flex:1` leaves `min-width:auto`, and a range input's automatic minimum is its intrinsic ~129 px, so it refused to shrink.

### The mobile scale, applied once

`/style.css` is served as a single gzipped blob that every page links, so rules
placed there cost flash once and reach all ten pages. That is where the phone
breakpoint lives: container and card padding cut from 20/24 px to 12/16 px, a
44 px floor on buttons and drawer items, `100dvh` on the drawer, and a viewport
clamp on the custom dropdown menu.

- **The drawer's footer was unreachable** — `height:100%` resolves against the large viewport, so License, language and Logout sat under the browser chrome, and `nav { flex:1 }` absorbed all free space so `overflow-y` never produced a scrollbar to reach them.
- **Eight pages redeclared `toggleDrawer()`** — identical copies of the shared function, and since a page's inline `<script>` runs after `/lang.js`, each one shadowed it. Any improvement to the shared version was dead code. The duplicates are gone; there is now exactly one in the firmware.
- **The network toast covered the topbar** — full width at `top:0` with `z-index:9999` against the topbar's 50. During a persistent error the hamburger — the only navigation on a phone — was hidden for as long as the toast stayed.
- **GPIO ownership was tooltip-only** — `title="slot N"` does not exist on touch, so the only way to learn which slot owned GP7 was to open all sixteen. The slot number is now printed on the pin, and the pins sit in an even grid instead of pills sized by their own text.

### Cache

- **A firmware update did not reach the browser** — `/style.css` and `/lang.js` are served with `Cache-Control: public, max-age=604800`. Seven days, and no way to invalidate: flashing new firmware changed nothing the browser would ask for again. The build now stamps `?v=<hash>` on those URLs, derived from the hash of `WebUI.h` that the packer already computed, so the long cache survives and breaks itself exactly when the assets change.

### Login screen

- **The wordmark is vector, not text** — the brand rendered in whatever the system stack supplied, so it changed shape between Safari, Chrome and Android. Five glyphs traced from Liberation Sans Bold into static SVG paths: identical everywhere, no font to load, and 638 B gzipped against ~1,130 B for the equivalent subsetted WOFF2 in base64. No font file is embedded; see notice 13 in the third-party notices.
- The mark is larger, carries the interface accent, and gained the expansion of the acronym as a subtitle. That subtitle is deliberately not translated: SIMUT is an acronym of the Portuguese phrase, and translating it would break the correspondence with the letters.

### Fixed

- **The alarm-limit fields left the sensor dialog** — they duplicated `/alarms`, which edits the same four keys and additionally couples `min < max`. The staging payload still carries all four: its `num(id, d)` helper returns the stored value when the element is absent, so nothing is zeroed. Note that `/api/alarms` only returns active sensors, so limits are now set after activating a slot.
- **The history log filter styled its checkboxes as text fields** — `.log-header input` is an element selector and matched `#chkInf`, `#chkWrn` and `#chkErr`; below 600 px the media query gave them `width:100%`, turning three checkboxes into full-width black bars. The page's only breakpoint was making it worse.
- **The pending-changes notice said "at the top"** — true on a desktop, wrong on a phone since the save button moved to the bottom. Now position-neutral, in both language packs.

## v1.5.3-beta (2026-07-25)

Stability and telemetry release. Most of it comes from chasing reboots to their
actual cause rather than to the first plausible one — several entries below
record a hypothesis that measurement killed, because those are the ones most
likely to be re-proposed.

### Core-1 lifecycle and reboots (class R1)

- **Core 1 was being hard-reset while healthy** — `getHeartbeat()` guarded on `_isPausedForFlash`, a flag declared, cleared in five places, read there, and **never set true**. Every millisecond of flash lockout read as staleness; past 10 s the watchdog killed a working core, wedging Core 0 and breaking in-flight HTTP responses.
- **Flash writes without a Core-1 pause** — `writeHistoryEntryFlashV4` programmed flash while Core 1 fetched from XIP, hanging the QSPI arbiter. Fixed with a refcounted `Core1FlashPause` RAII guard.
- **The crash autopsy printed a constant** — `scratch[3]` and `scratch[5]` were both destroyed within the first instants of `setup()`, so every reboot classified as a HW watchdog stall in `C0=[BOOT]`. Two sessions were spent reading a forensic channel that returned the same answer whatever had happened. Now snapshotted before anything can overwrite it.
- **The watchdog window was never 15 s** — the RP2040 load register caps at 8.388 s, so every `WdtWindow` asking for more got exactly the default. The class stays, its comment no longer lies, and long operations are sized by *feeding* the watchdog.
- **Core-1 lifecycle visible in `show metrics`** — phase markers, per-phase worst stalls, QSPI latency, lockout accounting.

### Telemetry

- **TLS handshakes could wedge Core 0 forever** — `_wait_for_handshake()` upstream has no overall deadline: `_run_until()` restarts its own timer on every call, so `setTLSConnectTimeout()` bounds one iteration and never the handshake. Against a peer that accepts TCP without completing the handshake — a wrong port was enough — Core 0 spun there permanently. Patched in `tools/arduino_pico_overrides`, which now also feeds the watchdog inside the bounded loop.
- **BearSSL asked for 16 KB contiguous and the heap had 11.3** — `setBufferSizes(4096, 512)` drops the receive buffer to what actually fits. Measured at the moment of the attempt: 31,900 B free, 11,370 B contiguous. Freeing memory does not help when the heap is fragmented; the block is what matters.
- **`TelemetryGuard` removed, not repaired** — it claimed to feed the watchdog during blocking network calls via a 2 s timer. Measured: the timer ticks correctly right up to `http.POST()` and stops the instant it blocks. It never worked in any build. Repairing it would have been worse — a guard that fed through a wedged handshake converts a recoverable reboot into a permanent freeze.
- **Templates rejected `{u..}` and `{p..}`** — `{pAMB}` compared 7 bytes against a 6-character token, so it resolved only at the very end of a template. Per-slot pressure `{p0}`..`{p15}` added, resolving against the slot that actually reports it, so the rewritten key matches the V4 history key for that channel.
- **Live Preview matched the firmware** — the editor knew only single-digit `{t0}`..`{t9}`, so every `{u..}`, `{p..}` and `{t10}`..`{t15}` was echoed literally and a working template looked broken. `/api/config` now exposes per-slot `hum`/`press` so the preview resolves channels the way the firmware does.

### History (V4)

- **Records were written with a timestamp and no data, and reported as success** — a mid-day sensor identity change stops every value from being recorded: the schema lives in the `.sim4` header and values match by `hwId`, while `ensureV4Schema` restores that header from the existing file instead of rebuilding it. `writeHistoryEntryV4` succeeded regardless, so the log kept saying "History record saved" once a minute. An empty row is worse than a gap because it looks like data. Now refused, with `APP_HIST_SCHEMA_MISMATCH` (code 515) warning once.
- **`sensor reschema confirm`** — new privileged command that rebinds the day's history to the slots as currently configured. Destructive: it recreates today's file, so the day's earlier records are lost.
- **Codec fixes** — post-failure refill, transactional two-pass decode, midnight rollover, and a `-0.01 °C` value colliding with the NaN sentinel.
- **Chart streaming ran Core 1 dry** — large ranges decimated tens of thousands of records with no watchdog feed between emissions.

### Memory

- **The language pack held 14 KB of heap for the browser's benefit** — the `.lng` loader mallocs the whole file and never frees it; `@WEBDICT` is half of it and no firmware path reads it. Now excised from the buffer and streamed from LittleFS on demand. Measured 14,052 B recovered against 14,124 B predicted; dashboard RAM went 81% → 70%.
- **`/config` moved to the filesystem** — the app slot had 660 bytes left. Serving the page gzipped from LittleFS took real headroom back to 8,852 B.

### Web and UI

- **Sensors configurable from `/config`** — the dashboard goes back to being status only.
- **`/api/logs` sent unguarded, and two handlers self-deadlocked** on the read lock.
- **Top-panel graph asked for sensor -1**, found nothing, and rebooted the device.
- **Full redraw painted 90% of its pixels twice** — 254 ms → 126 ms.
- **Touch failures now say why** instead of blanking the screen.

### i18n

- **pt-BR pack completed** — every sensor key and 35 log messages were missing.

### Known limitation

The Core-1 heartbeat race under heavy flash load (class R1, `APP_CORE1_DEAD` → soft panic) is **not** closed. It is rare and orthogonal to everything above, and it is the remaining stability gap.

## v1.5.1-beta (2026-07-19)

### AP Mode Fix — Touch Hold at Boot

- **XPT2046 SPI wake-up removed** — The manual SPI transaction (`0x90`) at boot was putting the XPT2046 into power-down mode with PENIRQ disabled (PD0=0). The pipelined data bytes inherited PD0=0, keeping PENIRQ permanently disabled and deadlocking AP-mode-via-touch-hold. The XPT2046 touch-detect circuit is always active from power-up — no SPI initialization is needed. Fixes: AP mode now activates correctly when holding touch at boot.

### Calibration Persistence Fixes

- **Calibration changes now persist through reboot** — `commit_all` reboot path correctly saves calibration data. Previously lost on watchdog-triggered reboot.
- **Skip calib.csv rewrite when `nChanges==0`** — Avoids unnecessary flash writes when no calibration data has changed.
- **Fast calib save for non-ROM sensors** — No quiet mode hang when saving calibration for sensors without ROM identifiers.
- **Calibration hwId/name changes now instant** — Changes take effect in 0.4s instead of requiring a full sensor reload.

### Dashboard & UI Fixes

- **Top-panel slot-0 persistence** — Slot 0 now correctly persists in the top panel after display offset or theme changes.
- **Auto-switch bottom panel** — When the top panel slot changes, the bottom panel now auto-switches to the next available slot.

### Arduino IDE Release Packages

- **`tools/build_release.sh`** — Automated script to generate Arduino IDE-compatible `.zip` releases for both `simut_tft` (ILI9341) and `simut_alpha` (HD44780) variants.
- **Flattened file structure** — All source files at sketch root; `ota/`, `display/`, `sensors/` subdirectory includes rewritten to flat paths.
- **Both variants compile with arduino-cli** — TFT: 911.888 bytes (87%), Alpha: 819.636 bytes (78%) on RP2040 Pico W with 1 MB filesystem.

### OTA Update Files

- **Firmware binaries** — `release/simut_v1.5.1-beta.bin` (OTA update) and `release/simut_v1.5.1-beta.uf2` (USB mass-storage flash).

## v1.5.0-beta (2026-07-19)

### Centralized Hardware Configuration — `simut_config.h`

- **Single config file** — All user-configurable options now live in `src/simut_config.h`: display type, pin assignments, sensor enable/disable, Bluetooth, mDNS, theme packs, buzzer pin, and advanced system limits. Previously scattered across 8+ files.
- **9 documented sections** — Display type, TFT pins, Alpha/HD44780 pins (I2C and parallel), buzzer, sensors, communication, theme packs, 1-Wire default pin, advanced limits. Each option has explanatory comments.
- **`#ifndef` guards throughout** — Every define supports compile-time override via `-D` flags in `platformio.ini`. Defaults match the existing release configuration.
- **Backward compatible** — Existing config headers (`DisplayConfig.h`, `SensorConfig.h`) delegate to `simut_config.h`. All `#include` chains preserved. No breaking changes.
- **Arduino IDE support** — `__has_include("simut_arduino_config.h")` guard at the top of `simut_config.h` for release packages. Release configs simplified to set overrides before including.

### Build System Cleanup

- **`platformio.ini` deduplicated** — Sensor and feature flags removed from `[pico_base]` (now in `simut_config.h`). Only environment-specific overrides remain in `[env:pico_w_alpha]`.
- **Release packages simplified** — `release/*/simut_arduino_config.h` now includes `simut_config.h` instead of duplicating all defines.

### Bug Fixes

- **BluetoothManager.cpp** — Added missing `#if SIMUT_BLUETOOTH` guard around all method implementations. Prevents redefinition errors when `SIMUT_BLUETOOTH=0` and the file is compiled (debug builds).
- **HD44780_16x2.h** — Wrapped `_initLcd()` and its call site in `#if HD44780_MODE_PARALLEL`. The 4-bit parallel init sequence was incorrectly compiled in I2C mode.

### Theme Pack Selection

- **Moved to `simut_config.h`** — Theme packs (`SIMUT_THEMES_HEALTH`, `_PRO`, `_MEDICAL`, `_SAFETY`, `_RETRO`, `_NATURE`, `_UTILITY`) are now enabled by uncommenting lines in the config file, not by editing `Themes.cpp`.
- **`Themes.h` includes `simut_config.h`** — Theme flags are visible wherever `Themes.h` is included.

### PIO Resource Coexistence — Multi-Sensor Conflict Resolution

- **pio0 conflict identified** — OneWirePIO (DS18B20, 27 instruction slots) + WirePIO (BME280 I2C, 32 slots) = 59 > 32 available. WirePIO loaded first, blocking OneWirePIO entirely (DS18B20 dead — no GPIO fallback).
- **pio1 SM saturation** — 2× DHT22 (2 SMs) + CYW43 WiFi (1 SM) + BuzzerPIO (2 SMs) = 5 > 4 SMs. Resolved by BuzzerPIO auto-fallback to pio0.
- **`BME280Driver.h` fix** — Added `forceGPIO(true)` before each `begin()` call. BMx280PIO now uses GPIO bit-bang I2C only (skips PIO+DMA), keeping pio0 instruction slots free for OneWirePIO. GPIO mode is slightly slower but fully reliable.
- **`docs/PIO_ANALYSIS.md`** — Comprehensive PIO resource allocation analysis covering all libraries (OneWirePIO, DHTBus, WirePIO, BuzzerPIO, CYW43), instruction slot budgets per block, state machine counts, DMA channels, conflict scenarios, and resolution mechanisms.

### Hardware Validation — 4-Sensor Coexistence Test

Tested on Pico W with TFT display + buzzer + WiFi:

| Sensor | GPIOs | Type | Status |
|--------|-------|------|--------|
| BMP280 | GP0 (SDA), GP1 (SCL) | BME280 driver | ✅ Reading (GPIO bit-bang) |
| DHT22 #1 | GP2 | DHT22 | ✅ Detected, reading |
| DHT22 #2 | GP3 | DHT22 | ✅ Detected, reading |
| DS18B20 | GP4 | DS18B20 | ✅ Detected (ROM: 283C21…), reading |

- **WiFi**: Connected (RSSI -45 dBm), web server responding
- **PIO after fix**: pio0 31/32 slots (OneWirePIO + BuzzerPIO fallback), pio1 23/32 slots (DHTBus×2 + CYW43)
- **Heap**: 94.3 KB stable, no leaks over 11+ minutes of continuous operation
- **Sensor readings**: 857/916 OK (93.2%), 59 errors concentrated during initial setup
- All 4 sensors configured and activated via CLI, configuration persisted to flash

### Flash Budget

- **Release (TFT + all sensors + mDNS)**: 94.1% (982604 / 1044480 bytes)
- **Alpha (HD44780 parallel + all sensors + mDNS)**: 85.4% (891920 / 1044480 bytes)
- **RAM (release)**: 35.8% (93760 / 262144 bytes)

## v1.4.4-beta (2026-06-07)

### GPIO Resource Management — Guided Slot Assembly

- **`gpio` command** — GPIO resource map showing all 16 pins with allocation status (FREE or `[Slot XX] Type (Role)`), plus a consolidated free-GPIO list. GPIOs are now a visible, trackable limited resource.
- **`sensor <slot> create <type>`** — Guided slot creation. Sets the driver type, clears previous pin assignments, activates the slot, and shows: pin count, each pin's role and flags (e.g., `1-Wire (pull-up)`), available free GPIOs, and a hint for the next command (`sensor <slot> pin <idx>,<gpio>`).
- **`sensor <slot> type <type>`** — Now shows pin requirements and current GPIO assignments per pin after changing the type, so the user knows what to wire.
- **`sensor <slot> pin <idx>,<gpio>`** — Now shows the role label for context (e.g., `pin[0]=GPIO 3 (1-Wire)`). Detects when all required pins are assigned and suggests the next step (`sensor <slot> name "<name>"`).
- **`sensor <slot> active on`** — Validates prerequisites before activating: type must be set, driver must be compiled in, and all declared pins must be assigned. Reports exactly which pins are missing.
- **`show sensor types`** — Lists compiled-in sensor drivers with pin count, channel summary, and role labels (e.g., `BME280 | 2 pins | Temp+Hum+Press | SDA,SCL`).

### BME280 Driver — Temperature + Humidity + Pressure

- **`BME280Driver.h`** (~9KB flash) — Self-contained I2C driver using forced-mode measurements. No external library dependency (avoids Adafruit_BME280 at ~15KB).
- **Async state machine** — BME_IDLE → trigger forced measurement → BME_WAITING → read results, matching the DS18B20/DHT22 async pattern.
- **Compensation formulas** — Integer math per Bosch BME280 datasheet §4.2.3 for temperature, humidity, and pressure. Oversampling ×1 on all channels (~9ms per reading).
- **TFT panel rendering** — Temperature + humidity on dashboard (mirrors DHT22 layout), min/max panel support. Pressure available via API (`CH_PRESS` channel).
- **I2C auto-detect** — Probes 0x76 and 0x77 addresses. Hardware scan detects BME280 on the active I2C bus.
- **Multi-pin GPIO init** — `gpioInitForRole()` now called for ALL declared pins (not just `pins[0]`). I2C bus initialized once when the first I2C sensor is found. `ROLE_POWER` defaults to output LOW.

### Improved Diagnostics

- **`show sensors`** — Redesigned output: slot, GPIO assignments, driver type, channels (e.g., `T+H+P`), friendly name, ROM (1-Wire), HWID, alarm status, and alarm limits per channel.
- **`show sensor types`** — Available drivers with pin count, channel summary, and pin role labels.
- **`PIN_ONEWIRE_DEFAULT`** — Fixed preprocessor redefinition warning (8 instances eliminated).
- **All 4 sensor channels initialized** — `MAX_SENSOR_CHANNELS` loop sets `avgValue` to NAN and `calibrationOffset` to 0.

### Other Changes

- **mDNS enabled by default** — `-DSIMUT_MDNS=1` in platformio.ini. Device accessible via `http://simut.local`. Cost: ~15KB flash, negligible RAM.
- **I2C0/I2C1 auto-detection** — `i2cPeripheralForPins()` selects the correct peripheral at runtime. Any GPIO 0-15 pair works for I2C sensors (hardware permitting).
- **`checkAndAutoHealSensors()`** — No longer reports false "Sensor missing" warnings for non-DS18B20 sensor types (DHT22, BME280).
- **BME280 boot guard** — I2C timeout (50ms) + ACK probe prevents boot hang when BME280 is configured but not physically connected.
- **Hardcoded GPIO assumptions removed** — DHT22 `begin()` no longer references GPIO 10. DS18B20 legacy methods use first active sensor's pin. Zero fixed GPIO-to-type coupling.

### Flash Budget

- **Release (DS18B20 + DHT22 + mDNS)**: 93.1% (972KB / 1044KB) — ~72KB free
- **With BME280**: 93.7% (979KB / 1044KB) — ~65KB free
- **RAM**: 35.7% (~93.7KB / 262KB)

## v1.4.3-beta (2026-06-07)

### Flash Diet — 86KB Freed (97.8% → 91.2%)

- **LEAmDNS disabled by default** — Wrapped with `#ifdef SIMUT_MDNS`. Enable with `-DSIMUT_MDNS` in build_flags when needed. Saves ~196KB library from link.
- **BluetoothManager stub** — When `SIMUT_BLUETOOTH=0` (default), entire class is inline no-ops. `BluetoothManager.cpp` excluded from build. `SerialBT` library still compiled by framework but unused symbols are linker-stripped.
- **`sensor pin <slot> <index> <gpio>` CLI** — Assign specific GPIOs to sensor slots with conflict detection across all active sensors. Validates GPIO range (0-15) and pin index (< MAX_SENSOR_PINS).
- **Flash budget**: 91.2% (952KB / 1044KB) — 92KB free for future features.

## v1.4.2-beta (2026-06-07)

### Sensor Entity Architecture — Driver-based Pin Roles

- **PinRole enum** — Each GPIO pin now has a declared role (`ROLE_DATA`, `ROLE_I2C_SDA`, `ROLE_I2C_SCL`, `ROLE_SPI_MOSI`, `ROLE_SPI_MISO`, `ROLE_SPI_SCK`, `ROLE_SPI_CS`, `ROLE_UART_TX`, `ROLE_UART_RX`, `ROLE_ANALOG`, `ROLE_POWER`).
- **PinRequirement in SensorFormat** — Each driver declares pin count, role, label, and flags (pull-up, open-drain) via `SensorFormat::forType()`. No hardcoded per-type GPIO setup.
- **`gpioInitForRole()`** — Auto-configures GPIO direction, pulls, and function based on declared role. Replaces `#if SIMUT_SENSOR_DHT22` hardcoded init blocks.
- **API pin metadata** — `/api/status` now returns `pc` (pin count) and `pr` (role labels: "Data", "SDA,SCL") per sensor.
- **WebUI pin info** — Dashboard table shows pin count + roles next to sensor type (e.g., `DHT22 ⚡1p Data`, `BME280 ⚡2p SDA,SCL`).
- **Adding a new sensor** now requires only a driver file + `SensorFormat::forType()` entry — display, API, calibration, and GPIO init all follow the format metadata automatically.

## v1.4.1-beta (2026-06-07)

### Universal Slot Architecture — 16 GPIO Slots

- **16 universal sensor slots** — `MAX_SENSORS` expanded from 10 to 16, covering GPIO0–GPIO15. All slots are now uniform with configurable type, hwId, friendlyName, pins, and alarm limits.
- **Ambient sensor eliminated** — The special `ambientSensor` field in `SystemConfig` has been removed. Slot 10 (GPIO10) is now a regular universal slot, treated identically to all others. The `idx: -1` API convention is replaced by standard slot index `10`.
- **Sensor channels generalization** — `RuntimeSensor` now uses `avgValue[4]`, `buffers[4]`, and `calibrationOffset[4]` arrays with `SensorChannel` enum (CH_TEMP, CH_HUM, CH_PRESS, CH_LUX). Each sensor driver declares its channels via `SensorFormat::forType()`. Adding a new sensor type (e.g. BMP280 pressure) requires only a driver — display, web API, and calibration adapt automatically.
- **Web dashboard sensor type column** — Table now shows driver type (DHT22/DS18B20) per sensor. Calibration form conditionally shows humidity fields per-sensor based on `hasHum` flag.
- **Unified alarm system** — Per-slot alarm mask now covers all 16 slots. The separate `ambTempAlarm`/`ambHumAlarm` flags are removed.
- **Config migration v16→v17** — Automatic migration: `ambientSensor` moved to `sensors[10]`, slots 11–15 initialized as inactive.

### Fixes

- **Boot hang after flash** — Eliminated blocking `Serial` calls in boot path (`BLOG`, `LogManager`, `CommandManager`, `SoundManager`). Removed `Serial.ignoreFlowControl(true)` that caused 1s delays per log line.
- **Stack overflow prevention** — `SystemConfig` allocations moved to heap (`tempConfig`, `encBuf`) to avoid RP2040 4KB stack limit with the larger v17 struct.
- **Bluetooth disabled** — `SerialBT.begin()` hardfaults on CYW43 after warm boot (picotool reset). Bluetooth is now disabled to ensure reliable boot. USB Serial + Web interface provide equivalent functionality.
- **API JSON fixes** — Restored missing `first = false` and `if (!safeSend(buf))` calls in `/api/sensors`, `/api/status`, and `/api/users` that caused invalid JSON (missing commas between objects).
- **WebUI calibration** — Removed duplicate ambient card. All sensors rendered uniformly with type-aware fields.

### Breaking Changes

- **Config format v17** — `SystemConfig` layout changed. v16 configs are auto-migrated on first boot. Downgrade to ≤v1.3.x requires factory reset.
- **API `/api/sensors`** — Ambient sensor no longer reported as `idx: -1`. Slot 10 appears in the standard sensor array.
- **History format** — `BinaryHistoryRecord` changed from 28 to 40 bytes. Existing `.bin` files are incompatible.
- **Bluetooth removed** — `SerialBT` disabled due to CYW43 warm-boot hardfault. Use USB Serial or Web interface instead.
- **`/api/status` sensor format** — Added `type` and `ch` fields. Humidity field now uses generic `sensorHasChannel()` instead of hardcoded `TYPE_DHT22` check.

## v1.3.0-beta (2026-06-07)

### Alpha Display — HD44780 16×2 Alphanumeric Support

- **HD44780 dual-mode driver** — I2C (PCF8574 backpack) and 4-bit parallel GPIO, selectable via `HD44780_MODE_I2C` / `HD44780_MODE_PARALLEL` build flags
- **Compile-time display selection** — `SIMUT_DISPLAY_TFT` and `SIMUT_DISPLAY_ALPHA` flags allow building for ILI9341 TFT (default) or HD44780 16×2 (alpha), mutually exclusive
- **I2C mode** — Uses I2C1 on GPIO 26 (SDA) / GPIO 27 (SCL), address 0x27 (configurable via `HD44780_I2C_ADDR`). Zero sensor slot conflicts — all 10× DS18B20 + DHT22 available
- **Parallel 4-bit mode** — RS=GPIO 16, EN=GPIO 17, D4=GPIO 18, D5=GPIO 19, D6=GPIO 20, D7=GPIO 21. Also zero sensor slot conflicts
- **GPIO 0-15 reserved for sensors** — Display pins mapped to GPIO 16+ exclusively, no sensor displacement
- **Alpha display loop on Core 1** — Character framebuffer with blit(), auto-cycling temperature/humidity display
- **GFX library exclusion** — Adafruit GFX Library, ILI9341, and XPT2046 excluded from alpha build via `lib_ignore`. SPI init and touch detection guarded with `#if SIMUT_DISPLAY_TFT`
- **UART1 clock preserved** — `uart_init()` called in alpha mode (clock only, no GPIO takeover) to keep StorageManager debug markers safe
- **WiFi skip timeout** — Alpha builds without touch skip button get a 30-second WiFi connection timeout to prevent infinite boot hang
- **`pico_w_alpha` build environment** — Clean build at 89.0% flash (929 KB), 34.6% RAM (90 KB). Saves ~84 KB vs release build

### Fixes

- **Touch calibration infinite loop** — Guarded with `#if SIMUT_DISPLAY_TFT`; alpha build has no touch controller
- **SPI pin conflict on alpha parallel** — `SPI.begin()` was configuring GPIO 16-19 before HD44780 init, causing boot failure in parallel mode
- **Flash storage corruption recovery** — Full `picotool erase` resolves corrupted filesystem partition after repeated flashing

### Documentation

- **WIRING.md** — Complete rewrite with three pinout diagrams (ILI9341 TFT, HD44780 I2C, HD44780 Parallel), comparison table, HD44780 pin reference, and wiring checklists for each mode

## v1.2.1-beta (2026-06-06)

### Dual Independent Dash Panels

- **Unified panel architecture** — Both dash panels use the same `drawSlotPanel()` function. The dedicated ambient panel (`drawAmbientPanel`) eliminated (~280 lines saved).
- **Top panel: fixed/interactive modes** — Long-press (1s) toggles between fixed (pinned sensor, normal styling) and interactive mode (dark gray background + white elements, follows slot selector to choose which sensor to pin).
- **Bottom panel: always interactive** — Short tap toggles min/max only. Always follows the bottom SLOT buttons.
- **S10 button** — Added slot 10 (ambient DHT22 on GPIO 10) to the bottom button bar. Hidden when top panel is fixed on it.
- **Min/max rendering moved to drivers** — `DS18B20_renderMinMax()` and `DHT22_renderMinMax()` in respective drivers, dispatched via `sensorRenderMinMax()`. Shared primitives in `SensorDrawing.h` reuse existing icons.
- **Slot humidity min/max tracking** — Per-slot humidity arrays with real-time accumulation every loop cycle.
- **Independent top panel data** — `topSlot*` fields in `SystemState` with dedicated `setTopSlotData()`/`setTopSlotMinMax()` setters.
- **Instant panel updates** — Incremental render now compares `topSlot*` fields. `pullSnapshot()` keeps `topSlotIdx` synced for AppManager mirroring.
- **Alarm flash fix** — Top panel alarm flash checks `isSlotAlarming(topSlotIdx)` instead of old ambient flags.
- **Border color fix** — Normal mode content strip uses `borderColor` instead of hardcoded `C_TEXT_SUB`.
- **Background fill fix** — Content strip uses `panelBg` instead of `C_BG_MAIN` for correct alarm red and selection mode gray.

### Community & Docs

- **Third community contribution** 🎉 — Complete Spanish documentation suite by [@f-p-0](https://github.com/f-p-0): README.md (337 lines, PR #66), CONTRIBUTING.md (140 lines, PR #68), and CODE_OF_CONDUCT.md (39 lines, PR #68), making SIMUT accessible to Spanish-speaking users worldwide
- **Second community contribution** 🎉 — Docker development environment so contributors can build and test without installing PlatformIO locally ([@JohnMartin0301](https://github.com/JohnMartin0301))
- **First community contribution** 🎉 — 672-line HistoryCodec v2 test suite covering roundtrip encoding, anchor frame boundaries, NaN compression, and buffer overflow ([@LorenzoLongaretto](https://github.com/LorenzoLongaretto))

### Flash Budget

| Configuration | Flash |
|---|---|
| Both sensors ON | 1030872 (98.7%) |

## v1.2.0-beta (2026-06-06)

### OTA Subsystem — Full Upgrade to v4.6.2

- **F-OTA-BOOTLOOP fixed** — Loop20 OTA 100% PASS. Root cause: reentrant LittleFS deadlock during README.md write + Core 1 startup deferred to post-WiFi + safeReboot uses MMIO identical to applier_reboot.
- **F-RESTORE** — Reliable backup/restore via API (98/100 PASS). Config snapshot preserved across OTA apply with CRC32 integrity. Atomic rewrite of calib.csv with VERSION=epoch.
- **F-RAM-SLIM** — RAM usage 49.6% → 33.7% (-41 KB / -16pp). Eliminated graph caches, removed unused font glyphs, shared buffer pools.
- **F-TEL-HTTPS-RESILIENT** — Fix crash + reboot when HTTPS server drops. More conservative heap budget for TLS connections.
- **F-OTA-STAGE-NOBLOCK + F-FLASH-DIET** — Fix TCP drop during OTA firmware staging. Non-blocking upload with adaptive chunk sizing.
- **F-DISPLAY-MARGINS** — `fillMarginsBlack` + `fillScreen` override in `TftWithOffset` for clean display edges.
- **F-BOOT-CYW43-CYCLE** — Power-cycle `WL_REG_ON` always in `setup()` for reliable WiFi initialization.
- **F-SCREENSHOT-INTEGRITY** — Eliminate row loss/corruption in `/api/screenshot` via multi-sample readRow with majority vote.
- **F-OTA-ADMIN-ONLY** — OTA endpoints require `PERM_FULL_ADMIN`.
- **F-TEL-ADAPTIVE** — Adaptive-throughput telemetry (backend-only batch sizing).
- **F-UI-OTA-FLOW** — User-facing OTA + restore UX messages with progress feedback.

### Documentation & Tooling

- **Glossary** — `docs/GLOSSARY.md` decoding all inline tags (F-\*, BUG-\*, SEC-\*, CON-\*, DOC-\*, REF-\*) used in source comments.
- **Comment cleaner** — `tools/cleanup_comments.py` strips version history references and changelog markers from source comments for release preparation.

### Flash Budget

| Configuration | Flash |
|---|---|
| Both sensors ON | 1031464 (98.8%) |
| DS18B20 only | ~1028400 (98.5%) |
| DHT22 only | ~1029500 (98.6%) |
| Both OFF | ~1024900 (98.1%) |

### Tests

49/49 tests passing (27 validators + 22 HistoryCodec).

## v1.1.0-beta (2026-06-06)

### Sensor Architecture — Modular Driver System

- **Compile-time sensor feature flags** — `SIMUT_SENSOR_DS18B20`, `SIMUT_SENSOR_DHT22`, `SIMUT_SENSOR_BME280` in `platformio.ini` allow disabling unused drivers to reclaim flash (DS18B20: -2.7 KB, DHT22: -1.6 KB, both: -6.1 KB)
- **Universal slot configuration** — `SensorRecord` v16 with explicit `sensorType` field + multi-pin support (`pins[4]`), ready for I2C, SPI, ADC, and UART sensors
- **Sensor drivers organized** — `src/sensors/` directory with `DS18B20Driver.h`, `DHT22Driver.h`, `SensorConfig.h`, `SensorHelpers.h`
- **Flash migration v15→v16** — Automatic schema upgrade preserving all sensor configs, ROM-based type detection during migration
- **SensorPresets catalog** — 130+ predefined display formats in `sensors/SensorPresets.h` covering 30+ physical quantities (temperature, humidity, pressure, weight, light, chemistry, electrical, flow, etc.)
- **SensorFormat system** — `SensorValueFormat` (unit, decimals, icon) + `SensorFormat` (1-3 values per sensor) + factory `forType()` in `sensors/SensorHelpers.h`

### Display — Driver-Owned Panel Rendering

- **Icon drawing in drivers** — `sensors/SensorDrawing.h` with procedural icons (thermometer, drop, gauge, bulb, ruler, vial, bolt, pulse, pipe, compass, flag, atom, battery, etc.) guarded by compile flags
- **Driver-based panel rendering** — `DHT22_renderPanel()` and `DS18B20_renderPanel()` handle full panel layout (icons, formatting, units) via `sensorRenderPanel()` dispatch
- **Slot panel now shows humidity** — DHT22 in any slot displays both temperature and humidity with drop icon and translated suffix (%RH/%UR)
- **Theme-aware colors** — Drivers receive `C_TEXT_SUB`, `C_TEMP_OK`, `C_TEMP_HOT`, `C_HUMIDITY` from active theme; icons follow theme changes
- **Exact original positioning** — `textAnchor=92`, `iconX=14`, `rightMargin=15` matched from original `drawAmbientPanel`
- **Generic value formatter** — `formatSensorValue()` in `DisplayManager_FmtFloat.h` handles NaN and variable decimal places

### Bug Fixes

- **AP Mode via touch at boot** — XPT2046 receives SPI wake-up command during early boot; PENIRQ pin read directly via `gpio_get()`. AP window always opens regardless of settle state.
- **Mandatory touch calibration on first boot** — Full sensitivity + 4-point position calibration runs before dashboard when `magic != 0xCA`. Cancel during boot applies safe defaults.
- **`sensor define` command** — Extended syntax accepts sensor type: `sensor define <gpio> <rom> <type> <hwId> <name>`. Legacy 4-token syntax auto-detects from ROM.
- **`sensor accept` command** — Sets `sensorType` explicitly on accepted DS18B20 sensors.

### Flash Budget

| Configuration | Flash |
|---|---|
| Both sensors ON | 1031464 (98.8%) |
| DS18B20 only | ~1028400 (98.5%) |
| DHT22 only | ~1029500 (98.6%) |
| Both OFF | ~1024900 (98.1%) |

### Tests

49/49 tests passing (27 validators + 22 HistoryCodec).

## v1.0.0 (2026-06-03)

### Initial Public Release

- **Multi-sensor support** — Up to 10 DS18B20 (1-Wire) + 1 DHT22 ambient sensor
- **Zero-trust sensor pipeline** — ROM verification, hardware mismatch detection, error hysteresis
- **320×240 ILI9341 TFT display** — Dashboard, real-time graphs, touch-driven settings (XPT2046)
- **50 built-in themes** + custom theme support via LittleFS
- **Embedded web server** — Multi-user sessions, RBAC (10 permission bits), file manager
- **gzip-compressed WebUI** — Minified inline pages with shared CSS/JS
- **Telemetry** — HTTP POST and MQTT with JSON/CSV/custom templates, TLS/SSL
- **Dual-channel CLI** — USB Serial + Bluetooth (BLE)
- **NTP time sync** — Exponential backoff, multi-server fallback, virtual RTC
- **History codec v2** — Delta + sensor-mask + anchor encoding, ~45% size reduction
- **Hardened authentication** — HMAC-SHA256, per-user random salt, 5000 rounds
- **OTA firmware updates** — Upload via web UI, config snapshot preservation, auto-reboot
- **Backup & restore** — Full LittleFS backup/restore with CRC32 integrity (BKP1 format)
- **Crash forensics** — Watchdog scratch register autopsy with cross-core health monitoring
- **Internationalization** — English + Portuguese/Spanish via external language packs
