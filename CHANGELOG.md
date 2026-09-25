# Changelog

**English** | [Português](CHANGELOG.pt-BR.md)

All notable changes to SIMUT firmware.

## v2.7.3 (2026-09-25)

**The login page says which firmware answers it, and the Configuration page can
restart without saving.** Two small features asked for at the bench, and the
brand, the READMEs, the site and the documentation brought under one visual
standard.

`CONFIG_VERSION` stays 25: no migration runs, and a device updating over the air
keeps its configuration.

### The firmware version on the login page

The login page shows the version under the product name, `v2.7.3`, before
anyone signs in. The page has no session to ask the device with, so the number
is stamped into it when it is built: `tools/build_webui_gz.py` reads
`SIMUT_VERSION` from `src/SystemDefs_Limits.h`, and the version is part of the
generated header's stamp, so bumping it regenerates the pages. It costs 74 B of
compressed page. It is the number the release image already announces in its
mDNS TXT record; SECURITY.md §8 says so, and why only the latest release is
supported.

Verified on the bench in both themes, on the release image and the test build.

### Restart without saving (#165)

The Configuration page ends in a *Restart* section with one button, **Restart
without saving**. It restarts the device and writes nothing: edits still staged
on the page are dropped, and so is anything applied with **Test**, because the
device comes back with its saved configuration. It asks first.

There is no new route. The button calls `POST /api/action?op=reboot`, which
already existed behind the System permission and logs `SYS_REBOOT_USER`
(code 2), with the guards a commit has: 409 while a password change is pending,
503 within 5 s of a touch on the panel.

On the bench, on the release image and the test build: the device went offline
3.3 s after the click and answered again 26.4 s after it, and a device name
edited on the page but never saved was the old one after the restart. The
pt-BR and es-ES packs carry the section's five strings.

### The Ângulo standard (#166)

`ANGULO.md`, the visual standard shared with simut-rx, now lives in this
repository too, and AGENTS.md §7 makes it the rule for anything built from here
on. `tools/check_angulo.py` holds it in CI: the design tokens
(`docs/assets/angulo.css`) must be the byte-exact copy of simut-rx's, pinned by
sha256; the site's CSS may use tokens only; the brand files must use the brand
colours; the READMEs, the site, the guides and every Living document stay free
of emoji; badges are flat.

- **Icon and wordmark.** `tools/gen_logo.py` draws every brand asset from the
  tokens and the display face, Bricolage Grotesque 600. The device's favicon
  went from 835 to 731 B, and the login page's wordmark is the same Bricolage
  outline.
- **The site.** GitHub Pages moved to the tokens, light and dark, with its own
  layout. Four facts on the landing page were wrong and are fixed in its three
  languages: Wi-Fi is not configured from the panel, the asset names, "it is
  beta", and a colophon still naming v2.3.2-beta.
- **READMEs and docs.** The logo, flat badges and new taglines in all three
  READMEs, and about 260 emoji out of the Living documents.

### Flash

Against the published v2.7.2 `.bin`: release +168 B; alpha and air keep their
size to the byte, because what they gained fits inside the 4 KiB alignment pad
before `.data`. Slack under the 1,040,384 B OTA ceiling: release 30,132, alpha
62,324, air 21,460. No budget moved.

### The bench

- **No restart after flashing.** The bench had a habit of `reload confirm`
  after every flash, "for the BMP280". It fixed nothing: 13 of 13 flashes
  without it (picotool, `pio run -t upload`, and alternating v2.7.2 with this
  release) brought the BMP280 up reading temperature and pressure within
  seconds, and the device's own log shows 19 of 19 post-flash boots bringing
  its driver up on the first probe, with no sensor error. The failure the
  habit worked around was fixed in a46b0de (2026-07-31); the note that kept the
  habit alive was not retired until now.
- **The secret gate reads names.** `tools/scan_secrets.sh` treats any name
  containing *token* that is assigned a quoted literal as a credential, and
  `check_angulo.py`'s `TOKENS` constant, whose value is the path to the design
  tokens, failed CI once. Renamed rather than allowlisted: the allowlist is for
  credentials published on purpose.

### Documentation

- The READMEs, both manuals, the landing page and the product manual describe
  this release. The READMEs' "On `main`, not yet released" row, which still
  listed three things v2.7.2 had shipped, is gone. The Portuguese and Spanish
  READMEs count 62 HTTP routes like the English one, and all three count the
  410 host test cases CI runs.
- `tools/build_manual.py` takes the version from `SIMUT_VERSION`. Typed by
  hand, it stayed at v2.7.1 through the whole v2.7.2 release, and the product
  manual's cover said so.

### Upgrading

Nothing to migrate. The language packs gained the five strings of the Restart
section. The packs are not part of the firmware image; upload the ones attached
to this release on the Files page and reboot. With the old packs everything
works, and the new section reads in English.

### Known, and not fixed here

- **With the setup access point open, the device does not measure.** The loop
  returns before the sensors, the alarms, the history and the telemetry. A
  device with no network configured boots into the AP and stays there (release
  and alpha).
- **An alarm refused by a full alarm-line queue is never reported**, and a full
  queue keeps its oldest records (#161).
- **The alpha's LCD rarely reaches its AP pages**, by the code. The key is on the
  USB console and in the `ap` reply.

## v2.7.2 (2026-09-24)

**Three reports from the field, and none of them was where it looked.** The Air
on 2.7.1 *"seems to wake early, sometimes late, and large batches come out
broken"*: it woke on time. What it wrote down was late, and what it sent after a
long queue was invalid JSON, with records the cursor then skipped. The panel, on
the 8ae8db2 test build: on **PIN security** the footer arrows closed the screen,
and behind them the editor had been writing the live policy, so tightening it
from the panel never marked a single account for a new PIN. On **Configuration
Mode**, Confirm opened the access point and nothing told the panel, which kept
asking "Are you sure?" with the AP already on the air.

Along the way every image lost 16 kB, a long press became reachable from the CLI
and from the browser, and es-ES devices stopped naming their events in
Portuguese.

`CONFIG_VERSION` stays 25: no migration runs, and a device updating over the air
keeps its configuration.

### The Air woke on time; its stamps did not

The probe line timed consecutive cycles at 59.70–59.92 s, while the day file
held 54, 59 and 69–70 s between records one wake apart. A wake without the radio
has no NTP, and its clock was seeded from the newest record, plus the slept
seconds, plus `millis( )/1000`. That loses what the previous wake did after its
record, the fractions (truncated twice), and the boot ROM and crt0 that run
before `millis( )` exists: **−0.8 s per wake**, which the next telemetry wake's
NTP then put back **+9–10 s at once**.

The sleep now carries the clock. `airEnterDormant( )` leaves the time, to the
millisecond, in two watchdog scratch registers with a magic and a cross-check.
The wake adds the sleep the RTC measured, 140 ms of boot (`AIR_WAKE_BOOT_MS`,
measured at 117–182) and `millis( )`.

| against an NTP-synced host | error per wake | NTP step | stored intervals |
|---|---|---|---|
| v2.7.1 | −0.8 s, accumulating | +9–10 s | 54–69 s |
| v2.7.2 | −0.085 … +0.030 s over 10 wakes, no drift | 0.08 s | 59–61 s |

The charger boot, which fell through to a 60-second guess and started 27 s fast,
uses the carry too.

### Long telemetry queues: invalid JSON, and a cursor that skipped the cut

Both need a queue that grew while the collector was away, then a drain of
190–250 records per batch. `buildPayload( )` reserved `n × 300 + 256` B, 60 KB
for 199 records, against a 46 KB largest block, and the failed reserve was
silent. Past ~29 KB `String::concat( )` started failing, also silently, and the
body went out as `},,,,,,,,,]`. Records are now appended whole: each one, its
separator and the closing bytes are reserved first. A record that does not fit
ends the batch, and a body that cannot close is a failed send, which the
existing retry takes over.

The cursor then advanced to the newest record *gathered*, not sent, so the
records the body had dropped were skipped for good, a regression of 29cc4f4
(2026-08-15). It now advances to what the batch was read from
(`src/TelemetryCursor.h`, three host tests).

Drains of the whole history into a collector that checks every record against
the flash:

| | bodies | invalid | records delivered |
|---|---|---|---|
| v2.7.1, awake | 69 | 68 | 139 of 13,672 |
| v2.7.2, awake | 72 | 0 | 13,681 of 13,682 |
| v2.7.2, hibernating | 62 | 0 | 13,670 of 13,671 |

The record still missing is one stamped out of order on flash, the case a scalar
cursor cannot express. `t_bat` is a ceiling, not a promise: at 250, batches
leave with 190–192 records, cut cleanly.

### PIN security: the arrows, and a policy that never marked anyone

Reproduced on the rig before anything was touched:

- The footer is the standard one, but the handler treated everything left of
  SAVE as BACK: the down arrow took UI mode 28 to 6. The arrows now move the
  selection, as in every other menu.
- A tap on a value cleared the panel and repainted the title and the footer to
  change one row. Only the rows are repainted now.
- The editor wrote every tap straight into the live config. Raising the minimum
  4 → 5 from the panel logged **`0 to renew` with six accounts holding a PIN**,
  because Core 0 read the "old" policy from the struct the editor had already
  overwritten. The web and `user policy` read it first and were never affected.
  One unsaved tap moved `/api/config` to 5, and it stayed there after the idle
  guard took the panel home. Leaving without saving re-read the whole config
  from flash, discarding an account added over the CLI and not yet written.

The editor works on a copy now. Verified on the rig: the arrows stay on the
screen, an unsaved tap followed by 38 s idle leaves the stored policy alone,
BACK keeps a RAM-only account, and SAVE of an unchanged policy answers "PIN
salvo!". Raising the policy with the fix was not exercised there: it would have
marked the rig's six real accounts.

### Configuration Mode shows how to join

Twelve seconds after Confirm the access point was up (IP 192.168.4.1) and the
panel was still in UI mode 29, captured through the AP itself. The boot path
draws the AP's lines; `startApMode( )`, which serves the menu, `ap` and the
reconnect fallback, never did, and on the TFT `setApInfo( )` is a no-op.

Now the panel switches to the boot terminal before the radio is touched. It ends
with the network, `PSK <key>`, `Access on mobile: 192.168.4.1` and `AP Active!`,
and it stays there instead of dropping to a dashboard that no longer updates.
The boot-time AP screen uses the same four lines and gains the address, which
the lines added in 2.7.1 had pushed off the ring. A `softAP( )` that fails no
longer leaves the device unmeasured and claiming "AP mode started": the console
and the panel say it did not start, and the loop keeps measuring.

### A long press from the CLI and from the browser

`touch hold <X> <Y> [ms]` (default 3500, clamped to 100–15000, privileged mode)
and an optional `ms` on `POST /api/touch`; the dashboard mirror turns a
press-and-hold into it. Both reach the 3-second hold that pins the top card,
which a ~100 ms tap cannot.

### Event names from the language packs

The history page carried its own two tables of event names, and the Portuguese
one, without accents, went to every non-English pack: **es-ES devices showed
their events in Portuguese**. `GET /api/logcodes` now streams the active pack's
`@LOGCODES` straight off the file, followed by the firmware's English names, so
a pack older than a code still names it. The route is gated on `PERM_LOGS`, and
the page escapes each name, because a pack is a file an uploader can replace.
Measured on the rig: 1,435 of 1,435 log rows named from the pt-BR pack, accents
included, and 1,349 of 1,349 in Spanish after uploading es-ES.

Also in the log view:

- The CSV's `timestamp_iso` exported a record from before the clock was set as
  `1969-12-31T21:00:30-03:00`. It is now `Boot +00:00:30`, as the table shows.
  Wrong since v1.0.0.
- The CSV's `uptime_sec` column was literally `undefined`.

In the CLI, `touch hold` was accepted at every prompt, user EXEC included, and
listed nowhere; so were the Air's five `air` commands. Both now have their mode
and their help. `check_cli_help.py` walks the parser instead of the mode table;
against the old sources it fails on exactly those six commands.

### The 16×2 learns the pending count

With one sensor installed, the alpha's LCD shows the telemetry records waiting
to be sent in its bottom-left corner, in the panel's format (`999`, `1k` …
`65k`). Its Wi-Fi icon now fills left to right, like the panel's bars, instead of
a row at a time from the bottom. Checked through the bitmaps and host tests, not
on glass: the bench has no HD44780.

### 16 kB back in every image

No budget rises and nothing moves to LittleFS:

- `translateCodeEn`'s sparse switch had become a 4,000 B jump table; a compare
  chain is half that (−2,000 B).
- HTTPClient's cookie parsing is compiled out by a new framework patch,
  `httpclient_no_cookies.patch`, applied by `patch.sh`. SIMUT never installs a
  cookie jar, but the header parser kept `strptime( )` and its locale tables
  linked: −5,864 B on the panel images, −4.6 kB on the alpha and the Air.
- `/force_chpass` is served by the login page in a forced mode; its own page was
  a strict subset (−3,728 B). Proven end to end on the bench.
- The two event-name tables (4,006 B gzipped), a dead `.simx` decoder, dead JS,
  never-read English dictionaries and an inline Portuguese fallback left the
  web pages.

| `.bin`, B | v2.7.1 | v2.7.2 | change | slack under the OTA ceiling |
|---|---|---|---|---|
| release | 1,026,356 | 1,010,084 | −16,272 | 30,300 |
| alpha | 994,444 | 978,060 | −16,384 | 62,324 |
| air | 1,035,308 | 1,018,924 | −16,384 | 21,460 |
| test | 1,031,252 | 1,015,636 | −15,616 | 24,748 |
| asserts | 1,028,532 | 1,012,260 | −16,272 | 28,124 |
| test_https | 1,038,884 | 1,023,260 | −15,624 | 17,124 |

The panel fixes above are included; they cost 200 B of it. `pico_w_test_https`
no longer warns.

### The bench

Rig screenshots had come out dark and oversaturated since 2026-09-23: bits 6, 5
and 2 of every byte read back as zero. On the TFT build the panel's MISO (GP16)
and the touch chip select (GP17) are wired to the PicoHand's probe and charger
lines, and the hand had been left driving CHARGER low with the probe armed. The
touch controller stayed selected and fought the panel on every GRAM read.
`air_test_suite.py` and `bt_auth_test.py` left it that way; they, and
`hand_release_all`, now let the lines go (`CHARGER HIZ`, `PROBE STOP`).
AGENTS.md §1 says what to check before a capture, and to judge a capture against
the colours the firmware wrote, not against another capture.

### Documentation

- A new product manual in Portuguese, `docs/MANUAL.pt-BR.html`: 31 chapters,
  generated from `docs/_manual/` by `tools/build_manual.py`, with 44 real
  screenshots. 22 of those were taken through the capture defect above and will
  be recaptured.
- The READMEs, the manuals and the landing page caught up with 2.7.1, and now
  with this release.

### Upgrading

Nothing to migrate. The language packs changed a little: the forced
password-change page's four keys are gone, since that page is the login page
now, and the mirror's hint mentions the long press. The packs are not part of
the firmware image; upload the ones attached to this release on the Files page
and reboot. With the old packs everything works, and the hint keeps its old
text.

### Known, and not fixed here

- **With the setup access point open, the device does not measure.** The loop
  returns before the sensors, the alarms, the history and the telemetry. The
  panel now at least says how to join. A device with no network configured still
  boots into the AP and stays there (release and alpha).
- **An alarm refused by a full alarm-line queue is never reported**, and a full
  queue keeps its oldest records (#161).
- **The alpha's LCD rarely reaches its AP pages**, by the code. The key is on the
  USB console and in the `ap` reply.

## v2.7.1 (2026-09-22)

**A setup network you can actually get into.** The report was two symptoms on
two builds — holding the touch panel did not enter AP mode on the release
image, and a phone could not join the setup network on the alpha — and neither
of them was in the AP code. Measured on the rig: `ap` brings up
`<name>_SETUP` on WPA2, a phone-like client joins in **4,1 s**, gets a DHCP
lease and loads the portal (`302` → `/login` `200`), on the release AND on the
alpha with Bluetooth live, and again with a randomised MAC — which is what a
phone actually uses. The beacon carries `pair_ccmp group_ccmp psk` in **both**
its WPA and RSN information elements, so the `WPA1 WPA2` a scanner shows is two
IEs and no TKIP anywhere. That closes the residue
`docs/analysis/PLANO_DIVIDA_TECNICA.md` left open under 1.3, without monitor
mode.

What was broken is everything around it.

### The AP you could see and could not join

Found while re-testing the alpha at the end of the day, and it is very probably
the reported symptom itself. `ap` issued while the station was **hunting for an
SSID that is not there** brought up an access point the host could see at 94%
signal and could not associate with: **45 s and a timeout, twice**, against
**2,6 s** from the same build with the station connected.

One radio serves both. `WiFi.mode(WIFI_AP)` only assigns a field, and the
framework's `beginAP( )` tears the station down but cannot cancel a sweep
already issued to the driver — `cyw43_wifi_scan( )` owns the chip until it
finishes, and its `wifi_scan_state` is the same flag that made
`NET_SCANNING_RETRY` terminal on 2026-09-08. Starting an AP on top of that
produced beacons and no association.

`beginAP( )` now waits out the sweep (bounded at 4 s, because a sweep that
never finishes is the documented wedge and must not hold the recovery path
hostage), drops the result, disconnects and lets the chip settle before asking
for the AP. From the same failing state: **4,07 s**, DHCP lease, portal `200`.

This matters more than the command. The fallback below opens the AP exactly
when the ladder has been failing — which is exactly when a sweep is in flight.

- **`softAP( )`'s return value is read.** It used to be dropped, so a failed
  start still set `NET_AP_CONFIG` and still printed "AP mode started": the
  operator was told to join a network that was not on the air. It now answers
  `false`, logs `SYS_AP_START` with `ctx=-1`, and says so on the console.

### The panel asked for a gesture it could not show

The AP-hold window ran ~190 lines before `startCore1( )`, and Core 1 is the
only thing that draws the TFT. Measured with the PicoHand driving PENIRQ: the
window ran `[3919..7419] ms` and Core 1 was launched at **7457 ms** — the
instruction reached the glass **38 ms after** the last moment a finger could
start the gesture, and the five-entry boot-log ring still held it, so the
screen displayed an instruction that had already expired. The mechanism itself
was correct in all four bench cases; the gesture only ever worked blind, held
from before power-on, for ~8,3 s.

- The window now runs **after** `startCore1( )` **and after Core 1 has actually
  painted a frame**. `isCore1Ready( )` is set before the panel is initialised:
  the first boot frame lands 757 ms after the launch returns, so a bounded wait
  on a new frame counter is what makes the invariant exact — the window cannot
  open while the instruction is invisible. Re-measured: window `[4869..8369]`,
  first frame at `4667`.
- It reads PENIRQ and nothing else. The old block drove TOUCH_CS/TFT_CS/SCK/
  MOSI by hand because the SPI bus was still uninitialised that early; doing it
  now would fight Core 1, and `isScreenTouched( )` falls back to an SPI
  transaction on a bus Core 0 does not own.
- The 3-second progress bar is reachable for the first time, so it also stopped
  clearing 320×240 twenty times a second.
- ⚠️ The settle gate's comment claimed a touch that never goes quiet is treated
  as stuck and the window skipped. Nothing implemented that, and it is a good
  thing: a finger held from power-on is indistinguishable from a stuck
  controller, so the "fix" would have killed the gesture. The comment says so
  now.

### The alpha never showed the key

`getApPsk( )` had exactly two readers — the console line and the `ap` reply.
The suffix built for the boot's network line does carry the key, but
`DisplayManager_Alpha.cpp` draws a progress bar and never the boot log's text.
So an operator standing at an alpha with a phone saw the network in the list
and had no way to learn its password. **The LCD now takes the whole screen
while the AP is up** and cycles three 3-second pages: the address, the SSID and
the key, with the value on the second line so a 37-character SSID has sixteen
columns to scroll through.

### Nothing ever opened the AP by itself

`ApPsk.h` has said since V-05 that "AP mode is what an UNCONFIGURED device boots
into" — it is the reason the key is derived from the board id rather than
living in the configuration. Nothing implemented it: `begin( )` with an empty
SSID went to `NET_OFFLINE` and the ladder retried for ever, and the only two
callers of `beginAP( )` were the touch gesture and `ap`. A device whose router
was replaced was unreachable by every channel its owner had.

- An **unconfigured device boots into AP mode**. It sets the same flag the
  touch gesture does rather than joining the condition beside it, because the
  flag is read twice more after that — by the branch that would otherwise
  start the station, and by the one ~230 lines down that sets `_isApMode` and
  leaves the AP line on the screen. A second condition would have brought the
  AP up and then told the rest of the boot it was in station mode.
- A configured one falls back in **two speeds**, because the two failures are
  not the same failure. A device that has **never had an address since it
  booted** is not a link that dropped — the router was replaced, the password
  changed, the unit was moved — and there is no working LAN to protect, so the
  **first dormancy** is enough (**measured: 6–7 min**, and the host joined that AP in 4,08 s). One that **had an address
  and lost it** waits **a whole round of the ladder** (~68 min by arithmetic, not measured to completion),
  because taking a working LAN away for fifteen minutes over an outage that
  ends by itself is the worse trade. Either way the AP's own 15-minute timeout
  returns to STA while an SSID is configured.
- **SIMUT Air is excluded from both**: its radio only exists inside a wake, an
  AP would hold it awake for fifteen minutes a round, the AP timeout only
  returns to STA when an SSID is configured — so on a device with none that
  state has no exit — and there is nobody in front of a hibernating device to
  use it. Its channel is the CLI, over USB or Bluetooth, which it has.
- `APP_AP_MODE_TRIGGERED`'s `ctx` now says who asked — `0` a person, `1` the
  boot gesture, `2` unconfigured, `3` the ladder giving up — and its text went
  neutral in all five tables, which gave the es-ES pack 28 B back. It was 83 B
  from its 16 KB resident ceiling and is now 111.

### A twelfth row in Settings

**Settings → 12. Configuration Mode**, behind `PERM_NET_CONFIG`, with the same
two-button confirmation the Global Mute screen uses. Verified on the rig with a
throwaway account: the row opens the confirmation (UI mode 29) and confirming
puts `simuttft_SETUP` on the air. `TR_AP_MODE` is reused rather than a key
added — the es-ES pack has 111 B left and a pack that overflows is rejected
whole.

### Fixed on the way in

- **The settings menu was one row short for a full admin.** `_menuItems` was 10
  entries against an 11-entry table: v25 appended the PIN-policy row and did not
  grow the buffer, so an account holding every bit wrote `_menuItems[10]` — one
  past the end, with `_menuCount` as the member right behind it. Caught on the
  rig before the fix: an admin's menu on v2.7.0 draws ten rows and **no numbers**
  (the numbers are dropped when the list is filtered), and "11. PIN security" is
  unreachable. It is `MENU_ITEM_COUNT` now, and the three tables that must agree
  are sized from it.
- `CLAUDE.md` claimed `pio run` with no `-e` builds all six. `default_envs =
  pico_w_release` says otherwise.
- `docs/MANUAL.md` mentioned AP mode six times and never said how to enter it.
  §14 now lists all five ways in and all four places the key is published.

### Cost

`used`: +1,424 B on release, +1,440 test, +1,464 test_https, +1,424 asserts,
+5,160 alpha, **+224 air**. Per section, which is where the alpha's number comes
apart: `.text` +1,304 release, +1,064 alpha, +224 air; `.rodata` +120 release, 0
air — and +4,096 on the alpha, which is **one alignment step, not content**.
main's alpha `.rodata` is 442,368 B, exactly 108 pages of 4 KiB with no slack,
so the first byte added to it costs a whole page. Only the alpha budget is
raised (978,396 → 983,396, measured + the 3,000 B margin this file carries).

`.bin` deltas are +1,424/+1,440/+1,464/+1,424 on the four panel builds, +4,096
on the alpha (the same page) and **zero on the Air**. Real `.bin` slack under
the OTA ceiling: release 14,028, test 9,132, **test_https 1,500** (its tightest
yet, and still warning), asserts 11,852, alpha 45,940, air 5,076 — unchanged.

## v2.7.0 (2026-09-22)

**The line comes out of beta.** Nothing here is a new feature: this release is
the one that stops calling itself beta, and it does so on numbers rather than
on a decision. The image published below is the one that ran an **8.18 h soak
with zero reboots** and **six OTA round trips** with nothing lost, and it
carries one security fix and one piece of instrumentation that the campaign
said were missing.

### Nobody grants a permission they do not hold (V-09)

- **`users.add` capped `perms` at `PERM_ALL_BITS` and never at the mask of the
  account asking.** A caller holding `PERM_USER_MGR` and nothing else could
  create an account with every bit and receive its one-time password in the
  same reply. Refused now, field by field: a request carrying a bit the caller
  lacks comes back `200` with `"rejected":["users.perms"]` and **no account is
  created** — refused rather than silently truncated, so the caller can see
  what happened.
- **Same rule on the panel PIN of slot 0.** Setting the admin's panel PIN is
  how the web configures it, and `PERM_USER_MGR` alone used to be enough.
  Now only the admin itself or a full admin; anyone else gets
  `"rejected":["users.id"]`.
- Verified on hardware with positive controls, which is what makes the run
  mean anything: the restricted account asking for a bit it **holds** is still
  accepted and still gets its credentials, so this is the subset rule and not
  a blanket veto.
- ⚠️ **A service account must now carry every bit it hands out.** An
  integration that created panel users with `ALARM_BLOCK|MAINT` from an
  account holding only `USER_MGR` stops working. `docs/INTEGRACAO_SERVIDOR.md`
  carries the corrected recipe.

### A stall in the field leaves three numbers behind, not one

- The crash autopsy reads `C0=[…] C1=[…] at up=…ms sc3=0x… hp=…`, and only its
  first fact used to survive the reboot: the 12-byte record has one `int16` of
  context, spent on `200 + Core-0 module`. The sentence went to the boot serial
  and nowhere else, which is nothing at all in the field, where there is no
  serial.
- **Three sibling records now persist the rest**, told apart by context band:
  `1000 +` Core 1's module when Core 0 stopped feeding, `2000 +` free heap in
  KB, `4000 +` uptime at the stall in minutes. No format change, no new log
  code, and `FATAL` is never filtered, so all four records land.
- Cost: **+184 B** on the release image; **0 B** on the Air and the alpha,
  where the growth fell inside the linker's padding. Every flash budget passes
  unchanged.

### Measured on the image published here

| | |
|---|---|
| Soak | **8.18 h, 0 reboots**, 99 samples; largest contiguous heap block 32,313 → 32,271 B, floor 32,266 — **−42 B**, or −5.1 B/h. Flash-exposure counter and both Core 1 kill counters stayed at 0; Core 1 heartbeat peaked at 44 ms |
| OTA | **6 of 6 applies**, each judged by the version the device reads back. Stage 34.9–35.9 s, CRC matching on all six, device back in 52–56 s |
| Filesystem through the OTA battery | 57 files restored, **0 records missing** (76,588 backed up, 76,612 on the device — the 24 extra are unsealed records the restore absorbed); configuration identical across every field checked |

### Known, and not fixed here

- **A watchdog reset with an empty trace** (`ctx=209`/`ctx=455`) reproduced
  three times between 20 and 21 September on the bench image, and not once
  since in a large sample: 15 complete runs of the known reproducer, 828
  flash-write cycles, 321 policy changes and 41 sessions across the first 18
  minutes after a boot. Its known trigger does not exist in the published
  image — `write memory` is not compiled into the release CLI, and the
  equivalent web path gave 0 in 500. It is now instrumented on both sides, so
  the next occurrence arrives diagnosable instead of silent.
- **A response truncated on an idle connection**, measured at 7.1% of samples
  five minutes apart during the soak. The device is the one cutting: it waits
  4 s for room in the client's send window and then kills the stream, which a
  client sees as a broken chunk. Under load with large bodies the same symptom
  turned out to be the network path and is gone.

## v2.6.1-beta (2026-09-21)

**A scan button for the Wi-Fi network, and the web interface stops going whole
into every image.** The SSID had to be typed from memory, and the hardest
moment is the one the device forces: an unconfigured unit serves its setup page
over its own access point, so it is guaranteed not to be on the network whose
name it is asking for. It scans from there now. The same pass found that the
Air and the alpha were carrying the panel mirror, the screen capture and the
theme selector — **2,363 B of gzipped page** talking to four routes those
images do not even register — and cut them out, which took the Air from
**980 B** of OTA headroom back to **5,076 B**.

### Choosing the network instead of typing it

- **`Scan`, next to the SSID field on `/network`.** Name, whether it is
  secured, signal. Tapping a row fills the SSID and moves the cursor to the
  password. Twelve networks, sorted by signal; a mesh answering on several
  radios appears **once**, as its strongest, and hidden SSIDs are not listed —
  there is nothing to tap.
- **It works in AP mode**, which is what it was built for. `cyw43_wifi_scan( )`
  sweeps on the station interface, which AP mode leaves down, and asking anyway
  is the documented way to leave `wifi_scan_state` stuck at 1 for the rest of
  the boot — the 2026-09-08 field failure, 3 h 41 min dark. The device brings
  that interface up **beside** the access point, once per boot, and the page
  you are reading stays up.
- **A refusal is reported at once.** `scanNetworks(true)` returning anything but
  `-1` is a failure; `0` means the driver refused, and waiting out the 15 s
  deadline for a sweep that never started reads as a hang. A stale list is
  never served as a fresh one either: `again=1` that cannot start gets 503.
- **Measured on the rig, 18/18**, in STA mode and from inside the device's own
  access point with a second radio joined to it: **0.94 s** a sweep, **zero
  polls lost**, twice, and a second sweep in the same boot still returns a list.

### Only what the image can use gets compiled

- **`/* @IF tft */` blocks in `WebUI.h`**, cut per environment by
  `custom_web_omit` in `platformio.ini`. The default omits **nothing**, so an
  environment that forgets the option ships fat — the safe way to be wrong.
- **What the Air and the alpha stop carrying**: the panel mirror, the screen
  capture, the theme selector. Four routes back them (`/api/screen_stream`,
  `/api/touch`, `/api/screenshot`, `/api/keypad`) and `#if SIMUT_DISPLAY_TFT`
  does not register any of them there. They were buttons that answered 404. The
  alpha's two theme entry points are empty stubs.
- This is **not** the `custom_fs_pages` diet and the rule that a shipping image
  carries the complete interface still holds for that one: there the page exists
  and lives on the filesystem, and a missing file is a runtime error. Here
  nothing is missing — the control and the route go together.

### Three fixes on the glass

- **The lock and the signal are drawn.** They were an emoji and box-drawing
  characters; v2.1.5 removed exactly that from this interface when it put stroke
  icons in an SVG sprite. An open network gets no icon and keeps its slot, which
  is the phone convention and what keeps the names aligned.
- **The scan button is the height of the field beside it.** Every input carries
  `margin: 0 0 16px`, and a margin sits outside the border box: in a flex row
  with `align-items: stretch` the button stretched over the whole line and came
  out 16 px taller.
- **The Wi-Fi password has a reveal button**, and **the IP in the top-right
  corner stops truncating on a phone**. It rendered as `192.168.3…` because it
  was the only shrinkable item in the topbar — and the only information in it.
  The version that used to sit next to the brand moved into the drawer.

### The licence says the same thing everywhere

- The `/license` page carried **`Copyright (c) 2025`** while the `LICENSE` file,
  the firmware string and both language packs said 2026. The page is the copy a
  user opens.
- `tools/build_release.sh` packaged the Arduino IDE zips — a copy of the whole
  source tree — **without `LICENSE` in them**, which is the one artefact not
  honouring the "all copies" clause it contains.
- `tools/check_license.py` keeps the five copies agreeing and checks that both
  release scripts ship the file.

### Gates added

Three, all of them born from a defect found by reading or by looking at the
page rather than by any test:

- **an undefined CSS token.** `var(--x)` with no `--x:` is not a syntax error —
  the browser drops the whole declaration. Two invented token names shipped a
  button with no background and a label with no colour, in both themes.
- **a page block that something outside it depends on.** A function defined
  inside an `@IF` and called outside, a function inside one that nothing calls,
  an element id fetched from outside. The middle one is there because the theme
  selector broke exactly that way — `loadThemes( )` lost its only call when the
  block moved, and sat on "Loading..." forever.
- **licence consistency**, above.

All three run **even when nothing is omitted**, or whoever only builds the
release never learns they broke the Air.

### Flash

Both columns are PlatformIO's `Flash: used`.

| image | v2.6.0-beta | v2.6.1-beta | Δ | `.bin` | under the OTA ceiling |
|---|---:|---:|---:|---:|---:|
| `pico_w_release` | 1,009,276 B | **1,012,668 B** | +3,392 | 1,024,700 B | 15,684 B |
| `pico_w_alpha` | 978,036 B | **975,396 B** | **-2,640** | 990,348 B | 50,036 B |
| `pico_w_air` | 1,017,736 B | **1,019,184 B** | +1,448 | 1,035,308 B | 5,076 B |

The Air's `used` goes up by 1,448 and its `.bin` is unchanged from v2.6.0-beta:
the scan cost it a 4 KiB step, and cutting the panel UI gave the same step back.
Without that cut it would have shipped **980 B** under the OTA ceiling, one
addition away from refusing an update while `used` still read 21 kB of slack.

`pico_w_test_https` is **3,308 B** under the same ceiling and trips the gate's
warning. It is `pico_w_test` plus BearSSL; the next thing added to it has to
take a fourth page out to LittleFS.

### Upgrading

Nothing to migrate: the config schema stays at 25. The language packs gained
nine web keys and one log string — they are not part of the firmware image, so
upload the new ones on the Files page and reboot, or the new labels stay in
English.

## v2.6.0-beta (2026-09-20)

**Choosing the account before typing the PIN, and a keypad that stops
scrambling when scrambling buys nothing.** v24 made the PIN the identity: the
panel searched the whole account table for whoever the taps could belong to.
That cost two things it should not have. A blind guess counted against *every*
account at once — with the table full and four digits, **20.2%** per attempt —
and when two accounts matched one sequence **neither** got in, which was **15%
of logins** on a full table, measured. The account is picked first now, the
device verifies one digest, and both problems are gone: **0.81%** per attempt,
and no ambiguity by construction. Config schema 24 -> 25, migrated in place.

The same pass made the PIN policy an administrator's choice, gave the web a
banner that says what a change actually costs, and took the whole PIN mechanism
out of the two builds that have no touch panel.

### Identity at the panel (config v25)

- **The account comes first.** CFG opens the account list; a locked account
  says so *there*, before the PIN, which is the difference between "you typed
  it wrong" and "this account is out of tries".
- **A configurable PIN policy** — minimum length, glyphs per key and alphabet —
  from the CLI (`user policy`), the panel, or the web. The alphabet is the only
  axis that is not a trade: `0-9A-Z` costs a blind guess **168x** more at four
  characters and the search nothing. The keypad is zero-sum, because the set
  that hides a character from a watcher is the set the search has to walk.
- **The length ceiling is CPU, not taste**: 16/12/8 characters for 1/2/3 glyphs
  per key. The tap tree is `S+S^2+...+S^n` SHA-256 at **36.6 us** each, measured
  on the rig; at three glyphs, ten taps would stop Core 0 for 3.2 s.
- **One glyph per key is no longer scrambled.** A set of one hides nothing from
  anybody who can read the glass, so it is the **ordered** keypad instead: the
  numeric pad for digits, or a **two-tap alphanumeric** keyboard (nine groups
  and a popup) for `0-9A-Z`. That also made `0-9A-Z` at one glyph per key
  possible at all — it used to be 36 keys of 19 px — and fixed a gap nobody had
  noticed: the panel could never type a PIN containing a letter, because the
  PIN-setting screen was hard-wired to the numeric pad while the policy
  advertised 36 characters.
- **The lockout ladder is per account** (six failures lock it until reboot) with
  a **panel ceiling** above it (twenty failures lock the panel). Only per-panel
  was v24's behaviour, and it meant six wrong taps from anybody shut the panel
  for everyone.

### Audit records survive slot reuse

- **The acting account's NAME is frozen into the alarm record at push time**
  (16 -> 32 B), instead of being resolved from the slot when the payload is
  built minutes later. The queue waits for the server and a slot is reusable:
  deleting an account in that window made an *audit* record go out signed by
  whoever took the slot next. Proven on the rig, and now a permanent bench step.
- **Deleting an account overwrites the whole record**, on all four paths
  (panel, web delete, web slot allocation, CLI). v24 cleared only the PIN hash
  on one of them and left the name, password hash, salt and permission bits for
  the next occupant to inherit.
- The send path's 1,024 B stack array was removed, which is what paid for the
  wider record.

### The web says what a change costs

- **Three actions instead of one.** "Save & restart" now restarts even when the
  change did not require it — it used to take the live path and the page
  announced a reboot that never happened. "Apply now" saves and applies without
  restarting. "Test" applies **without saving**, so a restart undoes it.
- The last two appear only when the **device** says the staged set can be
  applied live. The page asks with a dry run instead of mirroring the rule,
  because the classification compares the staged configuration against the
  current one and a field typed back to its current value is not a change at
  all — something no client can know.
- `/api/commit_all` gained `_nosave=1` and `_reboot=1`; `_dry=1` now returns the
  classification (`reboot`, `applied`, `reboot_for`) and accepts the `alarms`
  section alongside `sys` and `net`.
- Permission tags in the users table are **collapsed to their count** and open
  on a tap; thirteen badges in a cell pushed the action buttons off the row on a
  phone. The delete button had never picked up the shared button geometry and
  was the one square-cornered control in the row.

### The panel stopped flickering, twice

- The account list is painted in **six 40-px strips**, one DMA blit each,
  instead of clearing the screen and then blitting the title, the footer, the
  scrollbar and four rows one at a time — which left the panel visibly dark in
  between. The PIN screen had the same fix in v2.5.0-beta.

### Fixed

- **`user policy` had never once worked.** The parser forwarded two of the three
  tokens, so `user policy 4 3 0` arrived as `"4 3"` and the handler's own
  two-separator guard rejected every well-formed call. Two cases in
  `native_cli`, which compiles the production parser, now pin it.
- **A PIN policy changed from the web left no audit record.** The panel and the
  CLI both write `APP_UI_PIN_POLICY`; the one surface most people use wrote
  nothing, and it can weaken the keypad or send every account to "choose a new
  PIN".
- **`_dry=1` was not dry.** Making it classify handed `classifyConfigChanges`
  the live configuration as its `before`, and that argument is *consumed* — it
  is the scratch buffer. A dry run therefore wrote the staged values into the
  running device, where a later save carried them to flash. Caught on the rig
  before release; `test_classify_consumes_its_before` now asserts the contract
  the header only described.

### The PIN costs nothing where there is no panel

`SIMUT_PANEL_PIN` (1 when `SIMUT_DISPLAY_TFT` is 1 *or undefined*, so the native
suites still test the rules) removes the validator, the digest search, `user
pin`, `user policy`, the web fields and seven display stubs from the Air and
alpha images. What stays is the **digest chain** (252 B) and the config layout:
the stored form of a PIN is schema, not feature, so a board flashed with the Air
and back with the release comes back with its PINs.

### Flash

Both columns are PlatformIO's `Flash: used`.

| image | v2.5.0-beta | v2.6.0-beta | Δ | `.bin` | under the OTA ceiling |
|---|---:|---:|---:|---:|---:|
| `pico_w_release` | 998,468 B | **1,009,276 B** | +10,808 | 1,021,308 B | 19,076 B |
| `pico_w_alpha` | 973,372 B | **978,036 B** | +4,664 | 990,348 B | 50,036 B |
| `pico_w_air` | 1,018,752 B | **1,017,736 B** | **-1,016** | 1,031,212 B | 9,172 B |

The Air got *smaller* while gaining the live-apply flags: the PIN machinery it
was carrying for a panel it does not have is worth more than the new code. Four
budgets were raised in this change; the Air's was not.

### Upgrading

The language packs in `/lang` are **not** part of the firmware image, so an OTA
leaves the old ones in place and the new labels stay English until they are
replaced. They ship as release assets from this version on — upload them on the
Files page or with `POST /api/upload`, never `uploadfs`, and reboot: a pack is
read at boot.

## v2.5.0-beta (2026-09-20)

**The panel knows who is standing at it.** Until now the display had one PIN for
everybody and the event log could only say that *somebody* changed a limit. An
account is now something the panel understands: each of 32 accounts can hold its
own PIN, three new permission bits say what it may do at the glass, and every
action carries that account's name into the event log and out on the alarm line.
Config schema 22 -> 24, migrated in place.

Along the way the panel mirror got 2.9x faster, alarms became something a server
can edit without a reboot, and two log records that had been silently dropped
for as long as they existed were found by a test that filled the account table.

### Identity at the panel (config v24)

- **32 accounts**, up from five, each with an optional panel PIN. The PIN is
  stored as a salted SHA-256 chain, never in clear, and the panel is unlocked by
  *finding* the account the PIN belongs to rather than by asking who you are
  first.
- **Three new permission bits**, all panel-side: `PERM_ALARM_LIMITS` (edit a
  sensor's alarm limits), `PERM_ALARM_BLOCK` (enable/disable its alarms, and
  "Deactivate" on the alarm pop-up) and `PERM_MAINT` (open and close a
  maintenance window). An account with none of them is offered no Alarms item at
  all; the sensor menu opens on the first row that account may actually use.
- **A Users item on the panel**: create an account, set its bits and its PIN,
  delete it — without a browser. The built-in `admin` is not listed there,
  because it cannot be edited from the glass.
- **The settings breadcrumb names whoever authenticated** — `Settings > maria`,
  not `Settings > Main`.
- **Five payload codes carry the acting user**, so the alarm line says who moved
  a limit, who blocked a sensor and who opened a maintenance window.
- Silencing an alarm stays unsigned and needs no PIN: it is the one action whose
  value is that anyone nearby can do it immediately.

### The scrambled keypad

The PIN is entered on a keypad that gives an onlooker almost nothing. Ten digits
and two decoy glyphs are dealt across four cards of three; **a whole card is one
tap**, and the deal is **re-rolled after every tap**. The device never learns
which of a card's three glyphs was meant — it resolves the whole tree of
sequences those taps can spell against every account's digest at the end.

This has a real cost, and it was measured rather than assumed. An entry of n
taps stands for up to 3^n PINs, so two accounts can fall inside the same
sequence; the panel cannot ask which was meant, so neither gets in. On a full
table of 32 accounts, `tools/panel_fulltable_test.py`:

| PIN length | logins at first try | retries | ambiguity events | predicted | measured |
|---|---|---|---|---|---|
| 4 digits | 22/25 (88%) | 4 | 4 | 18% | **15%** |
| 6 digits | 24/25 (96%) | 1 | 1 | 2% | **4%** |

Retries and logged ambiguity events match exactly in both passes. **Six digits
is the mitigation and it is worth roughly four times** — the prediction is
`1 - (1 - 3^n/10^n)^(accounts-1)`.

Making this affordable meant changing the digest: it is now a per-character
SHA-256 chain closed over the length, so walking the candidate tree costs one
hash per *node* rather than one full digest per leaf. Eight taps block Core 0 for
about 360 ms (449 ms worst HTTP response against 91 ms idle); four taps are lost
in the noise.

### Alarms a server can edit live, and maintenance windows

- `POST /api/commit_all` now classifies what a commit actually changed and
  **reboots only for what needs it**, by comparison rather than by a hand-kept
  list. The fail-safe is still a reboot.
- **A maintenance window per sensor**, with a deadline: the sensor stays in the
  history and on the screen, but its alarms are suppressed until the window
  closes. It is a third domain in the alarm payload, so a server can tell
  "suppressed on purpose" from "not alarming".
- The window is set in **seconds from now**, not as an absolute epoch, because
  the caller is a server and the two clocks need not agree.

### The panel mirror, end to end

613 ms -> **213 ms** per frame and 40% fewer bytes on the wire, from four
changes that each had to be measured separately: a 12 MHz read clock, a DMA read
pipeline, send coalescing, and a nibble codec. The Core 1 pause became a park
without the SDK lockout, so Core 1 idles only for the length of a capture.
The frame is now bus-bound: further codec work buys bandwidth, not time.

### Fixed

- **A deleted account's PIN digest stayed in its slot** and opened the panel as
  whoever took that slot next. Deleting an account only cleared its `active`
  flag; the record is now zeroed whole, on both allocation paths.
- **The two records that say who got in and who moved a limit were the two the
  log could drop.** `SEC_PIN_OK` (308) and `APP_UI_ALARM_SAVED` (442) were
  `LOG_INFO`, and the log's per-family latch drops a repeated INFO: on a run of
  logins, **3 of 25 identifications left no trace at all**. Both are `LOG_WARN`
  now, which the policy never filters. The other four panel actions — block,
  unblock, maintenance on and off — had always been WARN.
- **Core 1 dropped repaint requests that arrived while it was painting.** The
  flag was cleared *after* the draw, so a screen change Core 0 asked for during
  a blit was lost: 7 of 7 logins left the keypad on the glass with the PIN
  already accepted. Every branch of the dispatch now takes the flag before
  drawing.
- `user pin <name> <pin>` stored the lowercased token, which was harmless for
  digits and silently wrong for anything else.

### Added

- **`GET /api/keypad`** returns the four scrambled cards as they are dealt right
  now, gated at `PERM_SYS_CONFIG` — the same bit that already reads
  `/api/screenshot`, which returns a picture of the identical cards. It never
  says which slot of a card is the digit, and answers `"up": false` with empty
  faces when the keypad is not the live screen. Behind `SIMUT_DISPLAY_TFT`.
- **Log code 311 `SEC_PIN_AMBIGUOUS`**: "two accounts fit the same keypad entry"
  was being filed as a wrong PIN. It is not one, and the answer to it is a
  longer PIN rather than a retry.
- **`docs/API_POST.md`**: every POST route on one page, with what each
  permission mask can actually reach — checked against a running device rather
  than against the source.
- **`tools/panel_fulltable_test.py`**: fills every free account slot and drives
  the panel with all of them, then measures the ambiguity rate against its own
  prediction.

### Flash

Both columns are PlatformIO's `Flash: used`, and v2.4.10-beta's were taken by
building that tag in a clean worktree rather than by trusting a recorded number.

| image | v2.4.10-beta | v2.5.0-beta | Δ | `.bin` | under the OTA ceiling |
|---|---:|---:|---:|---:|---:|
| `pico_w_release` | 974,716 B | **998,468 B** | +23,752 | 1,010,500 B | 29,884 B |
| `pico_w_alpha` | 965,028 B | **973,372 B** | +8,344 | 986,252 B | 54,132 B |
| `pico_w_air` | 1,009,312 B | **1,018,752 B** | +9,440 | 1,031,212 B | 9,172 B |

The two imageless builds pay only for the migration, the digest, the wider
account table and the CLI and web PIN paths: the panel screens and their Core-0
side are behind `SIMUT_DISPLAY_TFT`, which took the Air's share from 10,304 B
down to 6,848. No budget ceiling was raised and every environment is inside its
margin. The ceiling that matters is the OTA-safe one, 1,040,384 B of `.bin`, not
the 1,044,480 program slot: the last 4 KiB sector holds the config snapshot at
stage time. **The Air has 9,172 B left, and every future byte there is a
trade.**

## v2.4.10-beta (2026-09-18)

**The panel mirror is 2.6x faster, the SIMUT Air gets its full console back, and
the HTTPS certificate can finally be installed on a device already in service.**
Three changes that had been waiting on each other: the first was found by a
study that asked a different question, the second by the flash the previous
release freed, and the third by discovering that the manual had been describing
a shut door for a month.

### The panel read was 2 MHz because someone wrote 2 MHz

Reading pixels back from the ILI9341 was the whole cost of a capture, and it was
slow for two reasons, neither of them the panel:

- the read clock was 2 MHz, spelled inline, with nothing saying why. Measured
  against a screen proven static — two frames at 2 MHz differing by zero pixels
  is the control — 6 MHz returns the same pixels, and so did 12 and 16 MHz over
  a 30-frame soak. 6 MHz is the default because the ILI9341's serial read cycle
  works out to ~6.6 MHz and this stays inside it; the value is one constant
  (`SIMUT_TFT_READ_HZ`), documented with the table it came from.
- the framework's `SPI.transfer(void*, size_t)` is a loop calling the one-byte
  transfer. The fast path is the two-buffer overload with a null transmit
  buffer, which lands in the SDK's `spi_read_blocking` and keeps the PL022 FIFO
  fed: the byte loop was costing ~4.3 us per pixel of pure software, more than
  the wire itself at 6 MHz.

`readRect( )` replaces `readRow( )`: one address window for a whole rectangle
instead of one per row, and one block transfer per chunk. On the rig:

| | before | after |
|---|---:|---:|
| mirror (`/api/screen_stream`) | 1.530 s | **0.59 s** |
| forensic capture (`/api/screenshot`) | 4.33 s | **1.69 s** |
| mirror against forensic | — | **0 differing pixels of 76,800**, nine pairs of nine |

The zero matters only because of how it was measured: two captures of the *same*
path five seconds apart differ by ~1,200 pixels, because the panel's content
moves. Only captures taken back to back compare the read paths rather than the
clock on the wall — a first attempt that ignored this reported 1,132 differing
pixels and was measuring the time between them.

Reading is no longer where a mirror frame goes: it was 91% and is now 64-70%,
and the Core 1 pause it waits on grew in absolute time (111 -> 129..180 ms)
precisely because the read got fast. That pause is the next thing to look at.
The study that found all of this is `docs/analysis/ESPELHO_DELTA.md`.

### SIMUT Air: the full CLI is back

The headless build shipped the fourteen-command emergency console and answered
"the settings live in the web interface" to anyone holding a serial cable —
which is the answer that helps least, since the web is exactly what cannot be
reached when someone reaches for the cable. That was never a judgement about
what the build should offer: on 2026-09-06 the image had 876 B of headroom and
the full CLI costs 45,056 B.

The v2.4.9-beta diet freed 62,420 B on that image, so it fits: **977,964 ->
1,023,020 B, 17,364 B still under the OTA ceiling**, so the Air stays
field-updatable. RAM went the other way by 1,904 B — the emergency profile's own
strings cost more than the full parser's statics. The four Cisco modes,
`write memory`, `gpio`, `show metrics` and the rest answer over USB and over
Bluetooth; the five `air` commands are where they were.

### POST /api/tls installs the certificate pair, in service

Two correct decisions had closed the only documented door. The manual said since
2026-08-19 that the pair goes in through the Files page; the 2026-08-29 audit
made every upload that resolves under `/config` a `400`, because a forged pair
in the credential store is a man-in-the-middle on the admin session after the
next reboot. Nobody noticed the second closed the first, and for a month the
manual described a route that refuses.

The new route is admin-only — the gate an OTA apply carries, because a
certificate decides who the browser trusts from the next boot onwards — takes
the two PEM blocks concatenated in either order, and **refuses the pair unless
it parses and the key belongs to the certificate**. That is the second gap this
closes: a pair that parses but does not match used to be discovered at the next
boot, as HTTPS quietly not coming up, with the working pair already deleted.
Both files are written to temporaries and renamed, so a failed write cannot cost
a working pair. `tools/install_tls_cert.py` drives the whole thing.

It is the only route that writes into `/config`, and it is narrow on purpose:
two fixed paths, no filename taken from the request. The Files page still
refuses `/config` outright.

On the rig the device then served HTTPS with the certificate installed over the
API, negotiating `ECDHE-ECDSA-AES256-GCM-SHA384` with an EC pair and
`ECDHE-RSA-AES256-GCM-SHA384` with an RSA one — the first validation of the
**server** half of the cipher-suite list v2.4.9-beta trimmed, which until now
could only be tested as a client, for exactly this reason.

### One behaviour change worth knowing

A calibration point with more than fifteen digits is now refused. `parseFloat( )`
is exact only while the running value fits 2^53, `parseFloatStrict( )` promises
the result is what the text says, and the fuzz gate found the sixteenth digit
where those two meet. Fifteen digits is eight more than a float carries.

### Housekeeping

- The manuals stop describing the certificate route that does not work, and gain
  the one that does. Three counts that were wrong are right: the emergency
  console has fourteen commands, `docs/` has thirty-three documents,
  `tools/` has 124 scripts.
- `AGENTS.md` went from a 45 kB chronological diary to a 21 kB manual in the
  present tense — same rules, each with its measurement, minus the narration of
  how each was found.
- Three dead language-pack entries the gate had warned about on every build are
  gone; `ota/applier.cpp` no longer cites fixes by a version scheme that appears
  in no tag.
- `pico_w_test_https` is a sixth build environment: the full CLI and the TLS
  server in one image, which is what validating anything about HTTPS needs and
  what no shipping image is. CI builds it; it is not published.
- The secret gate stopped reporting code that merely names a PEM marker, and
  still catches a key pasted into a source file.

### Flash

| image | v2.4.9-beta | this release |
|---|---:|---:|
| `pico_w_release` | 982,844 B | **986,748 B** (+3,904) |
| `pico_w_alpha` | 978,060 B | **978,060 B** (+8 of code, inside the alignment) |
| `pico_w_air` | 977,964 B | **1,023,020 B** (+45,056 — the CLI) |

The release image's growth is the TLS route (+3,872, only where HTTPS is
compiled) and the panel read (+72).

## v2.4.9-beta (2026-09-18)

**The release image gives back 57 kB and gives up nothing.** 1,039,900 -> 982,844
bytes, which takes the room left for an over-the-air update from 484 B to
57,540 B. Six changes, each measured on its own, none of them removing a
feature:

| lever | Δ `.bin` |
|---|---:|
| zopfli instead of `gzip -9` on the 13 embedded web pages | −2,888 |
| `-DNDEBUG` on release and alpha — test and air already had it | −6,832 |
| the SDK's `printf`/`puts`/`putchar` routed through `vsnprintf` | −9,836 |
| the timezone held as a number instead of a POSIX `TZ` string | −13,148 |
| the last two `atof( )` callers moved to the inline parser | −7,640 |
| BearSSL offering only ECDHE + AES-GCM, TLS 1.2 | −16,688 |

Where those bytes were is the interesting part, and it was not in this
project's own code: newlib parsed the timezone string back with `scanf` on
every change; `vfprintf` and the stdio object stack were linked for the
pico-sdk's panic banner and the radio driver's error lines, while the same
formatter was already linked for our `vsnprintf`; `atof` dragged in hex-float
and NaN parsing for values that become floats; and the TLS stack advertised 41
cipher suites spanning TLS 1.0 to 1.2 when four suffice.

Three things change that can be observed. **A TLS peer that offers only CBC,
only CCM, only ChaCha20-Poly1305, only TLS 1.1 or older, or only a static-RSA
key exchange no longer connects** — as either side, since the same lists serve
the telemetry client and the device's own HTTPS server; a certificate chain
signed with MD5 or SHA-1 no longer verifies, which matters only when a CA
certificate is loaded. Nothing reads the `TZ` environment variable any more. A
panic banner longer than 160 bytes is truncated.

`-DNDEBUG` also removed a 116-byte absolute path from the build machine's home
directory that was going out inside the published image, carried by an inlined
assert in the SDK's DMA layer.

Measured on the bench, rig at 192.168.3.24: the web suite passes 95 checks with
every page gunzipping and its JavaScript parsing; a calibration round-trip
writes `21.75 -> 22.25` and reads back `raw 21.77 / read 22.27`; the same epoch
window resolves to 2, 1 and 2 day files at −03, UTC and +09 as the arithmetic
predicts, with the clock untouched; and telemetry over HTTPS delivers 6
requests and 924 bytes to the bench sink with the trimmed cipher list. The
study behind the numbers is `docs/analysis/DIETA_FLASH.md`, and
`tools/flash_compose.py` reproduces the attribution from a linker map.

**The top panel answers one gesture per meaning.** A short tap anywhere on it
opens min/max; a press of three seconds anywhere on it switches between pinned
and interactive. There used to be a third rule: a touch-down past `x = 280`
toggled pinned/interactive immediately, because that corner draws the
`[Amb]/[Sx]` indicator. v2.4.7-beta narrowed that strip so it would stop eating
the min/max graph button, and the narrowing fixed the wrong half — outside
min/max, the same quick tap on the right still jumped the panel into selection
mode, while the identical tap two centimetres to the left opened min/max. A
40-pixel strip that answers a different gesture than the panel it is drawn on is
a trap, not a shortcut, and this was the second report of it. The indicator is
still drawn and still means what it meant; it is read-only now.

Two smaller consequences, both deliberate: a short tap no longer pins an
interactive panel (that is the long press, and only the long press), and the
long press now works while min/max is showing instead of being ignored there —
entering the selection drops min/max, because those numbers belong to the sensor
the panel is about to stop following.

Bench: on the rig, a tap at (300, 70) — the exact region reported — opens
min/max, and so does the identical tap at (60, 70). The three-second press is
not reachable through `POST /api/touch`, which injects taps and not holds.

## v2.4.8-beta (2026-09-18)

**The min/max graph button's touch zone is the rectangle it is drawn on.** The
other half of the defect v2.4.7-beta fixed, on the same button: the graph branch
tested `x > 266` while the button is painted from 245 to 302, so its left third
fell through to the short-tap path and turned min/max **off**, and its zone also
ran to 319 — seventeen pixels past the button and past the card itself, opening
the graph from the margin. Neither number was wrong by accident: the geometry
lived in four places, one copy in each of the three drivers that draw a min/max
panel and a fourth written by hand in the touch handler, matching none of them.
It is now one definition in `SensorDrawing.h`, beside the function that paints
the button, and both the drawing and the hot zone read it. `CARD_X`, `CARD_W`,
`CARD_H` and `CARD_TOP_Y` moved to `DisplayManager.h` for the same reason.

A/B on the rig against the published v2.4.7-beta image, with the screen read
back through a classifier validated against known screens first: before, a tap
at x=250 (inside the drawn button) turned min/max off; after, x=245 through 302
all open the graph and x=310 — outside the button — no longer does.

The change pays for itself: three copies of the geometry became one, the release
image went from 1,027,900 to 1,027,868 B, and the `.bin` gained 32 B, taking the
OTA headroom from 452 to 484 B.


## v2.4.7-beta (2026-09-18)

**The panel, live in a browser, and clickable.** `GET /api/screen_stream` sends
one frame per request in 30 strips of 8 rows, each strip either palette RLE —
one colour index, one byte counting the pixels that follow — or raw RGB565,
whichever is smaller, decided per strip so the worst case is the raw size plus
a 3-byte header rather than the 2x an unguarded RLE costs on noise. Measured on
the rig across six screens: **1.53 s a frame against the BMP's 4.33 s, and
3.4-13.3 kB against 230,454 B** — 2.8x faster, 24x smaller. The speed comes
from reading each row once; the three-vote read stays in `/api/screenshot`,
which is the forensic capture that proves the wiring carries 62.5 MHz. The
noise that vote insures against did not appear: on the four screens that hold
still, two independent single-pass reads differ by **zero** pixels of 76,800,
and by zero from the voted BMP. A frame is 91% panel read, 7% Core 1 pauses and
3% network, so the codec bought back the wire and the read is what is left.

`POST /api/touch` makes the mirror a control: a click on the canvas becomes a
tap on the panel, mapped through the canvas rect so the device only ever sees
panel coordinates. Same injection the console's `touch sim` performs, same PIN
keypad in front of Settings, gated on `PERM_SYS_CONFIG`. Two things the bench
settled. A tap arms the 5 s touch-priority window, and blinding the mirror that
sent it would serve nobody — so `handleTouch` now records at its pressure gate
whether a touch was injected, and the stream is let through a window an
injection opened, while flash and telemetry keep backing off. And the panel
needs ~600 ms alone afterwards to repaint (measured: 150 ms and the tap does
not appear, 250-400 ms and the new screen arrives torn, 550 ms and it is
complete). That wait cannot live in the firmware: a tap becomes a UiEvent that
Core 0 consumes in its loop, and a web handler runs on that same core, so
waiting inside the device parks the very pump that makes the tap take effect.
The page waits; a caller driving the route by hand has to do the same.

**The min/max graph button stops dropping into selection mode.** In min/max the
top panel draws a graph button from x=245 to x=302, and the mode-indicator hot
zone at `x > 280` was tested first, so its right third answered a short tap
with a mode toggle — the panel jumped into selection, with no indicator drawn
there to explain it. A/B on the rig, instrumented with the UI mode `show
metrics` reports rather than a reading of a screenshot: without the fix the tap
leaves UI mode 0 (dashboard), with it UI mode 3 (graph view). Outside min/max
the same corner still toggles the mode, unchanged.

**The flash gate now measures the .bin against the OTA ceiling.** The budget it
checked is a sum of sections and moves in 4 KiB steps, so an image can cross
`OTA_APP_SAFE_MAX_SIZE` — past which the config snapshot overwrites its tail —
while every number still looks comfortable. Reading the ceiling out of
`ota_layout.h` rather than copying it, the check immediately found that the
release image has **452 B of OTA headroom**, that alpha was already at 780 B
before any of this, and that `pico_w_asserts` is now over it, recorded as an
exemption because it is a bench soak image flashed over USB.


## v2.4.6-beta (2026-09-16)

**The embedded web interface follows the Ângulo, and it fits: 2.5 kB less
flash.** The seventeen colour roles by function in both themes, a 4px grid,
three radii, one accent, a line before a shadow, labels without uppercasing,
stroke icons in an SVG sprite instead of emoji, and the theme through
`data-theme` with the system preference as the default. What paid for the
rework on an image with 1.1 kB to spare: the tokens left the nine per-page
"anti-flash" copies and now travel in `/lang.js`, which is synchronous in the
`<head>` — one copy, plus two inline in the login and first-password pages,
which do not load it. The 57 light-theme override rules are gone: the tokens do
that work. Shared rules (card, field, button, table, badge, banner, bar, modal,
toast) live in the common sheet and each page keeps only what is its own. The
13 gzipped blocks went from 98,458 to 95,985 B; the linker from 1,027,220 to
1,024,748 B; the `.bin` from 1,039,252 to 1,036,780 B — 3,604 B under the OTA
ceiling. Deviations from the standard are written down in §12 of `ANGULO.md`.
Proven on the bench (192.168.3.24, 2.4.3-beta → 2.4.5-beta by OTA on port
8081): `web_test_suite` 62 passed, 0 failed; `inline_tokens_check` with no
divergence; the pt-BR pack loaded and every served page captured in both themes.

**Logout works for a page in a browser, which until now it only appeared to.**
`/logout` read the `SIMUTSESS` cookie and nothing else, and a page on another
origin cannot send that cookie: `Cookie` is a forbidden header name in the Fetch
Standard, and the one this device sets is `SameSite=Strict`. So the fleet
manager asked to end its session, got a 302, and held one of the three session
slots for the full 15-minute idle timeout — with no way to tell. It now reads
the session the way `getAuthPerms` already did, through one shared reader, so
the two cannot disagree about what a session is; a caller that came by
`Authorization: Bearer` gets a 204 instead of a redirect to an HTML login page
it is not going to read. 96 B of flash.

## v2.4.5-beta (2026-09-16)

**And the origin can be set over the network, so a fleet is not a day of
cabling.** `system cors` on the serial console configures one device, with a
cable, in front of it; an install with dozens of them is an afternoon. The `sys`
section of `commit_all` gained a `cors` field, so the phone app can do the fleet
— and the app is the right tool for it because it is not a browser: it has no
origin policy to obey, and its session comes from the cookie. That asymmetry is
also the rescue path for anyone who points the origin at the wrong address and
locks the page out.

The obvious route was a file upload, and it stays closed: `/api/upload` refuses
any destination under `/config`, the credential store, because of two security
findings. `commit_all` costs nothing to reuse — per-section authorisation, the
`_dry=1` rehearsal, and the reboot the origin needs anyway — and the field is
diverted to the same file and the same whitelist the console writes. Nothing is
written under `_dry`: a rehearsal that leaves a side effect on disk is a
half-truth. 232 B of flash.

**A page on the operator's PC can now manage the fleet from a browser, and the
device is what makes it possible.** Nothing is installed on that PC: it opens a
URL. What stood between the two was not the network — that part was already
solved by routing between VLANs — but the browser's own rule: a page served from
one origin cannot read a response from another unless the server says it may.
Every call the page made was blocked, and no amount of work on the PC side can
change that. The server is what grants it.

`system cors <origin>` on the serial console writes the one origin this device
answers to, and `system cors off` clears it. Absent, which is how every device
ships and how every existing device stays, the firmware behaves exactly as
before — byte for byte, and at no cost: the header only exists when the file
does.

Three details are the whole feature, and each one is a failure that would
otherwise be debugged in the field:

**The header goes on every response, including the errors.** A 401 without it is
not read by the browser as "wrong password" — it is discarded, and the page sees
a network failure. Someone would go looking for a firewall when a password was
all that was wrong.

**`OPTIONS` is answered before anything else.** A browser sends a preflight
before any authenticated request, and this device used to answer it with a
redirect to the setup page — which a browser reads as a failed preflight, so the
real request is never sent. The preflight carries no credentials, by
specification, so it is answered without any: what it promises is only which
methods and headers the real request may carry, and the real request still has
to log in.

**The login returns the session token in the body — but only to that origin.** A
browser can never read `Set-Cookie` from JavaScript, and the session cookie goes
out `SameSite=Strict`, which is exactly what stops it from being sent from
another origin. So a page hosted anywhere but on this device had no way at all to
hold a session. It is gated on the request's `Origin` matching the configured
one, so the device's own pages are unaffected and their token stays where it is
today: in an `HttpOnly` cookie a cross-site script cannot exfiltrate.

The origin is validated as a whitelist on the way in and on the way out, because
it ends up verbatim inside a response header: a CR or LF in the middle of it
would end the header and let whoever wrote the file append headers of their own
to every response the device sends. A wildcard `*` is refused — that is the thing
this mechanism exists to avoid — and so is a trailing slash, which a browser
would compare against its own origin, find different, and block.

Cost: 2.232 B of flash, 0 of RAM, and nothing at all on a device that never sets
an origin.

## v2.4.1-beta (2026-09-08)

**A lost Wi-Fi link now reconnects on its own, instead of waiting for someone
to power-cycle the device.** Measured on a device in the field: the link
dropped, one scan started six seconds later, and the network stayed down for
three hours and forty-one minutes while everything else on the device kept
working normally — readings taken, history written, the display answering
someone's touch. It came back only when an operator restarted it. Four earlier
drops that same morning had recovered in five to eleven seconds, so nothing was
broadly broken; the fifth one simply parked.

Two things were wrong, and the second is the one that made the first fatal. The
scan the device starts after a drop had no deadline, so a scan that never
finished left the reconnect logic waiting forever. And underneath it, the
device would only try to associate if the network name had appeared in that
scan — while the same device at boot associates without asking, which is
exactly why restarting it always worked and waiting never did. Hearing a
network in a scan is harder than joining it, so at the edge of coverage, and
for a hidden network at any signal, the cheap check was the one that failed.

The scan now has a fifteen-second deadline, and a device that has been refused
by two scans associates anyway — the route back that a restart always had. If
the network is genuinely gone, the device still backs off to long waits so a
battery is not spent chasing it, but those waits now end: the retry ladder
restarts after half an hour instead of being pinned for the rest of the boot,
which is what used to make an access point that came back discoverable only on
a ten-minute grid, at best.

Scanning also stopped flooding the log. A device that could not reconnect wrote
a scan record every five seconds for as long as it stayed that way; the routine
line is now recorded once and then hourly, while a scan that never finished is
still recorded every time, because that one names the fault.


**A failure is written to the log once, not once per attempt.** The device
already understood that repeating good news is not news: a successful upload
reached the log the first time and then went quiet until something changed. Bad
news had no such rule, so a collector that stopped answering wrote a pair of
records on every retry and filled the whole forensic window with the same
sentence. Now a failure is recorded when it starts, implied while it lasts, and
the recovery is recorded when it arrives — with one reminder per hour, so a
device that is still failing never looks like one that quietly got better, and
an hourly count of what was left out. Measured against a dead collector: twelve
records in six minutes before, none after the first.

The same treatment now covers the alarm line, unusable certificates, the mDNS
announcement and a missing network name, each of which used to repeat at the
pace of its own retries. Sensors, security events, configuration changes and
crash diagnostics are deliberately untouched, and nothing at all can filter a
fatal.

**A wake no longer writes down that it booted.** On SIMUT Air every wake from
hibernation is a full boot, so the eight records the firmware emits on its way
up — provisional clock, language, sensors, calibration, alarm line, HTTP
transport, history snapshot, ready — were rewritten once a minute, always the
same eight in the same order. They were 68% of the entire forensic window,
which filled in about an hour and a quarter. A boot that came out of
hibernation now skips them; a boot that did not still writes every one, because
there the burst is the record of what happened. Measured on the bench over
eight wakes with the radio kept down: sixty-seven records before, ten after.

A cold boot also gains one record saying so, which on a battery deployment is
the report of a power interruption rather than an ordinary wake. Nothing else
changes: a boot step that fails still writes, and everything an operator does
after the boot — a language change, a calibration — still writes. What is
skipped is a fixed, known list of eight, so unlike a suppressed outage there is
no count worth reporting: on a device that reboots every minute the hourly
accounting record would never come due anyway.

### A SIMUT Air wake is 2.7x shorter

A reading wake took 25.5 s, and about 23 s of that was not work. Measured on the
rig, marker by marker, then measured again after the change:

| | before | after |
|---|---|---|
| `setup()` to the first cycle phase | 10.73 s | **2.47 s** |
| SAMPLE (sensor stabilisation) | 14.81 s | **6.83 s** |
| whole wake to DECIDE | 25.55 s | **9.31 s** |

**Waits for hardware this build does not have.** Three of them could never
finish early, because what they poll is a compile-time constant: the boot's
touch-settle gate and AP-hold window ask `isScreenTouched()`, which is
`return false` in both the headless and the alphanumeric builds, and the
"wait for Core 1" loop polls a flag whose only writers are two files that are
not in those links — so it always burned its full 1500 ms in a busy spin.
`DisplayManager::kHasTouch` and `kUsesCore1` now decide at compile time whether
those loops exist at all. The alphanumeric build gains the same 3.7 s.

**Waits for an operator who is not there.** The 1 s after `Serial.begin`, the
800 ms boot step, the 800 ms that holds "System ready" on screen, and the 600 ms
CYW43 power cycle now run only when the boot did not come out of hibernation.
A wake is a reset this firmware issued itself from a state it had just
quiesced; the reboot paths that power cycle exists for are UF2 flashes, OTA
applies, watchdogs and resets, and it is still done for all of them.

**Sampling without the idling.** `readInterval` is a rate limit for a device
that samples continuously to keep a display current, and it does not overlap
the conversion — `lastReadTime` is stamped when the read completes, so a
DS18B20 costs 1000 ms of waiting plus 750 ms of converting, ten times over,
before the wake may write its one record. During WARMUP and SAMPLE the sensors
now read back to back. The window, the filter and the values are unchanged.

The theme scan is also skipped on a wake: the palette is read by the display
and `/api/themes`, and a wake has neither.

Not changed, because it would change the recorded measurement: the size of the
moving-average window and the DS18B20 resolution. With the idling gone, SAMPLE
is now pure conversion time, so those two are what is left to spend: at 11 bits
the wake would be ~6.2 s and at 10 bits ~4.4 s, against 9.31 s today — worth
about 4.7 and 9.1 extra days on an 18650, by arithmetic on the bench currents.
The return falls off fast: below ~4 s of wake the budget is dominated by
`setup()` and by sleep itself, so 9 bits buys little for half a degree of step.

Note for whoever takes that step: `DS18B20_CONVERSION_TIME_MS` is a fixed
750 ms and is the only clock the driver waits on, so lowering the resolution
today costs precision and returns no time. The wait has to follow the
configured resolution first.

### The console's password reset now survives the next boot

`system admin reset confirm` is the documented way back into a device whose web
login nobody can pass, and the 2026-09-07 audit made it USB-only for that
reason. On the bench it turned out to announce a password the device forgot at
the next boot: the emergency console has no `write memory`, so nothing wrote
the new hash to flash. Login with the printed password succeeded inside the
boot that printed it and answered 401 after a reboot. On a SIMUT Air unit,
where every wake is a boot, that password was good for about a minute — the one
recovery for a locked-out web recovered nothing.

The reset now saves before it announces, and says so plainly if the save fails.
Two cases up the same switch, `system ssid` and `system pass` had always saved
for themselves; what hid this was a comment claiming `debug` was the only
command that relied on the shared "changed" flag.

Persisting the reset also persists the forced-change flag, and the serial
"factory defaults" announcement keyed off that flag alone — which would have
made every Air wake announce factory defaults the device does not have. The
announcement now keys off the one-time password still being in RAM, which is
what actually dates it to the boot that regenerated the config.

Bench test T17 covers the whole loop: reset on the console, reboot, and the
printed password must still log in.

### Security: the 2026-09-07 audit, closed

Eight findings (V-01..V-08) plus three observations, all fixed or decided.
The full record — decisions, per-symbol flash cost, positive controls and the
bench work still outstanding — is in
`docs/security-audit/IMPLEMENTACAO_2026-09-07.md`.

**The Bluetooth CLI had no attempt limit** (V-01a). It is compiled into both
published images (alpha and Air) and authenticates with the admin's web
password, so wrong guesses could be retried as fast as the RFCOMM link answers
and dropping the link reset nothing. There is now the same exponential backoff
the web login uses — 2 s on the first failure, ceiling 300 s — held in RAM so
reconnecting does not clear it. Recovery commands (`system factory`,
`system format`, `admin reset`, `system https off`) are refused over Bluetooth;
`ap` deliberately still works, since bringing up the setup access point from a
phone is why that CLI exists. The CLI applies the web's password policy too.

**Writing that lockout exposed one in the web login.** The old form computed
`(1 << failCount) * 1000` and clamped the product, with an unbounded counter:
at 29, 30 and 31 consecutive failures the multiplication wraps to exactly zero
— 2^29 x 1000 is 125 x 2^32 — so the penalty was zero and the account was open,
and past 31 the shift was undefined. Roughly 108 unattended minutes of
escalation bought free attempts. The shift is clamped before it happens now,
and the counter saturates.

**The device stopped advertising itself forever** (V-01b). Bluetooth discovery
closes 5 minutes after boot instead of staying on for the whole uptime.
Connectability is untouched, so a paired phone keeps working; rebooting reopens
the window to pair a new one.

**Only an authenticated request holds a SIMUT Air unit awake** (V-03).
"Activity" used to mean "a response was sent", which counted a 403 handed to a
stranger — an unauthenticated poll held a battery device awake with its radio
up indefinitely (measured at 491 s against an expected 306 s). The hibernation
timer is now reset by a matching session cookie, a successful login or valid
`/metrics` credentials; the login page itself gets a budget of three extensions
per boot, which is what an operator needs to finish logging in.

**The setup access point is WPA2** (V-05). It used to be open, so anyone in
radio range reached the captive portal of a device whose Wi-Fi had just failed.
The key is derived from the board's unique id — 10 characters, stable per
device — and shown on the USB console, on the display and in the reply to `ap`.
It is not a secret (the board id is printed by `show system info`); it moves
the bar from "in radio range" to "was told the key". `SIMUT_AP_OPEN=1` keeps
the open AP for bench work.

**Values a permitted user could store no longer break the API for everybody
else** (V-04, O-2). An SSID of `a"b` made `/api/network` un-parseable, an hwId
of `X\` did the same to `/api/status` *and* corrupted the telemetry payload at
the collector, and a quote in a language pack's `@NAME` took the entire UI down
until a different pack was uploaded — a `.lng` is only read at boot, so a
reboot did not help. Every string in the JSON APIs is escaped now, and SSID,
Wi-Fi password, NTP server, hwId and pack identity are validated on the way in.

**`/download` respects the permissions the users page shows** (O-1). An account
holding only "read files" could pull `/history/*.h5` and the forensic log;
those now require `PERM_HISTORY` and `PERM_LOGS` as well.

**SIMUT Air refuses the CYW43 pins** (V-06). GP23/24/25/29 are the radio's own
side band on a Pico W, and `air charger 25` was a valid command that took the
radio down on a device with nobody watching the console. The rule sits in
`airPinValid`, which the config sanitiser already calls, so a forged or
restored `air.bin` cannot smuggle one in either.

**CI builds all three images** (V-08). It built only `pico_w_release` and ran
four of six native environments, on `main` only — so the alpha and Air images,
which is where most of the above lives, had no gate at all. It now builds
release, alpha and Air, runs all six native environments and the Air
consistency check, on every feature branch and every pull request.

**The secret gate stopped having a blind spot** (V-02). The rig's admin
password sat in cleartext in five tracked bench scripts while
`tools/scan_secrets.sh` reported the tree clean: it only matched
`name = "value"`, and the hits were positional arguments and one bare test
vector. Two new steps catch both shapes, the second by looking for the bench's
actual values listed in a file kept outside the repository. Bench scripts read
their credentials from the environment now, with no defaults.

**SECURITY.md said the opposite of what the images do** (V-07). It described
the Bluetooth CLI as compiled out of release images and authenticated by the
display PIN; it is compiled into the two published images and authenticates
with the admin web password. Rewritten, along with the AP, Air hibernation and
`/download` sections.

### For anyone building from source

There is no `pico_w_debug` environment any more. It had never linked in this
project's history — a hundred kilobytes over the application slot — and a build
target that cannot be built teaches the wrong thing about the ones that can.
The concurrency tripwire it was documented to carry has lived in
`pico_w_asserts` for some time, and that one links.

Warnings in the firmware's own sources are now errors. Sixty-one of them were
fixed first, one of which was hiding a real defect: a constructor that listed
its members in a different order from the header, which the compiler silently
ignores in favour of the declared order.

Continuous integration went from building one image to building all five and
running all six test suites, and every image now has a flash budget that fails
the build when it grows past it. `tools/README.md` and `docs/README.md` are new
and say which scripts and which documents are current.

## v2.4.0-beta (2026-09-07)

**The history block survives a wake.** A block holds sixty readings so that its
header is paid once an hour instead of once a reading. On the hibernating build
it never did: the boot found the open block's snapshot, closed it into the day
file and started over, which is the right thing to do when a boot means
something went wrong — and every wake is a boot. Measured on a real day file:
448 readings spread across 316 blocks, 253 of them holding a single one, at 16
bytes each against the five the format is designed for. The boot now carries the
block on instead, and the snapshot file stays where it is, so the open block is
never held only in memory. Measured after the change: one block of 27 readings
where there had been 27 blocks. History now costs roughly what it was meant to,
and a device keeps months of it again rather than weeks.

### SIMUT Air: headless build with a deep-sleep hibernation cycle (experimental)

New PlatformIO environment `pico_w_air`: no display, no buzzer, the Alpha-like
web/serial/Bluetooth stack on cold boot (M0), and a hibernation cycle (M1)
entered by `air hibernate` or after an idle timeout. Each RTC wake reads the
sensors until they stabilise while the Wi-Fi connects in parallel, always saves
the sample to local history, drains pending telemetry when online, and sleeps
again for the history interval (or the telemetry backoff when it is longer).
Hibernation is RP2040 SLEEP (WFI on the XOSC with the RTC alarm) — DORMANT was
tried and dropped as non-deterministic on the bench. The CYW43 is powered down
through WL_REG_ON, the USB pull-up is released so the host sees a clean
disconnect, the watchdog is disarmed and the wake is marked as a clean reboot so
the boot autopsy stays silent. Air settings live in `/config/air.bin`;
`CONFIG_VERSION` is untouched. The emergency console gains `system ssid` and
`system pass` on every image, and `air status|hibernate|stop|idle` on Air.
Host-side tests: `pio test -e native_air`.

**Fixed before shipping: the wake never happened on time.** Disabling the ring
oscillator before the WFI saved a little current but left it stopped across the
wake, because the reset that follows does not pass through the ROSC reset
domain. The boot ROM then came up with no ring oscillator and the wake took a
long, variable time: measured on the bench at 16 to 48 minutes for a 2-minute
interval, against 147 to 151 seconds on the build that predates the change. The
oscillator is now re-enabled right after the WFI, before the reset, and the same
bench measured a 110.8 s sleep with a 26.5 s awake window. Cost: 32 bytes.

**The wake now lands on the configured interval, not one awake window late.**
The alarm was anchored on the moment the device fell asleep, so the period was
the interval plus however long the wake had taken: about 147 seconds for a
configured 120. It is now anchored on the wake itself, which for this cycle is
simply `millis()` at the moment of sleeping, because an M1 wake is a boot. The
subtraction is floored at 5 seconds, and a wake that outlasts its own interval
says `OVERRUN` in the log rather than degenerating into boot-sleep-boot. Only a
boot that really was a wake compensates; a cold boot or an `air stop` has no
previous wake to anchor to.

The device also measures its own sleep now. After the WFI it reads the RTC, the
one clock that crosses the sleep, and leaves the seconds in a watchdog scratch
register for the next boot to print. That measurement is what settled the last
second of error: the alarm was exact all along, and the loss was integer
truncation in the alarm arithmetic, always in the same direction. With rounding,
a 120-second interval measured 119.3 seconds end to end.

That was not the end of it, and the device's own account of the sleep was the
thing hiding the rest. The passive probe on the PicoHand, which never touches
the target, measured the period at 119.31 and 118.69 seconds while the device
reported 119.84: a flat 0.90 seconds lost on every cycle. The cause was printed
on the console the whole time. The RTC is written with zero and reads one three
milliseconds later, because the load pulse lands a tick of its own, so an alarm
armed at N seconds was only N-1 ticks away and the device woke a second early
every cycle. The alarm is now armed relative to the value the RTC reads back
rather than to the zero that was written, which is immune to whatever the load
does, and the self-report subtracts that base so it states real sleep instead of
the alarm value. Measured after the change, probe and console in the same
window: 120.23 and 120.35 seconds for a 120-second interval, with the remaining
0.11 second accounted for by the work between reading the millisecond clock and
loading the RTC.

**A missing Wi-Fi network no longer holds the wake open.** Connection attempts
are capped per wake; past the cap the sampling phase stops pumping the network
for the rest of that wake, the sensors still finish, the history is still
written, and the device hibernates. The next wake is a fresh boot, so it tries
again with a clean counter. Measured with a deliberately wrong SSID, the awake
window was 26.7, 26.4 and 26.2 seconds, against 26.3 to 29.5 seconds with the
right one.

**The telemetry cursor is now persisted before sleeping.** Cursor writes are
coalesced over a five-second window, which the M1 cycle never reached: the flush
phase ends about 150 ms after the send, and the sleep loses SRAM, so the next
boot re-read the old cursor and re-sent a batch that had already been accepted.
The pre-sleep write is now forced past both the coalescing window and the
touch-priority gate.

**The open history block is written to flash once per cycle, not four times.**
The snapshot file is rewritten whole every time, and on a device that reads
once a minute that rewrite is the largest thing it does to its own flash.
Three places asked for a snapshot unconditionally — the pre-reboot hook, the
entry into hibernation, and the phase that saves the reading — each of them
moments after the reading itself had already written one. Measured on the
bench: three to four whole-block writes per cycle, now one. The write is
skipped only when the bytes on flash are provably identical, which includes
the clock-provenance flag that can change without a reading being added.

`air status` and the pre-sleep log line now report how many snapshots this boot
has written, which is the only window the firmware has into its own flash wear.

**`air idle` no longer accepts a number that puts the device to sleep.** The
setting is stored in a 16-bit field, and the command used to accept up to
86400 and convert: 86400 became 20864, and 65536 became zero. Zero is the one
that hurt, because an idle timeout of zero sends the device to sleep on the
very next pass of its loop, and the only way back in is to catch a wake window
on the serial console. Anything the field cannot hold is now refused outright.

**A wake starts what a wake needs, and nothing else.** The web server, the
Bluetooth CLI, the mDNS announcement and the dashboard's min/max cache all used
to come up on an M1 wake, which lasts under a minute and drops off the network
when it ends. Nobody browses a device like that, nobody resolves its name, and
there is no dashboard to fill — so on the battery all four were spending the
wake to be torn down again. They now belong to M0: a cold boot, or `air stop`,
which is the operator's window for configuration. `air stop` during a wake
still starts the web server itself, so the way in is unchanged.

**The device stays awake while it is charging.** A GPIO reads high through a
divider off the 5 V rail; the pin is configurable and defaults to GP17. With
the charger connected there is no battery to protect, so the idle timeout does
not apply, and a wake that finds the charger present cancels its own
hibernation cycle for that boot and comes up as a normal M0 device — web server
and all. The cycle stays armed in the Air configuration, so unplugging and
letting the idle timeout run puts it straight back to sleeping, with nothing to
re-enable by hand. `air status` reports the line. The pin took over a field that
had been stored and never read since the beginning, so the configuration file
keeps its size, its checksum and everything already in it.

**Telemetry is triggered by how much is waiting, not by a clock.** The two
settings are now a minimum and a maximum batch: the device transmits once the
minimum number of records is pending, and sends them in batches of at most the
maximum until the queue is empty. A minimum of zero disables telemetry, as the
interval of zero did. On the battery build this is the whole point — a wake with
nothing to say never powers the radio, and one that has enough sends and goes
back to sleep. The fields keep their names and their places in the
configuration (`t_int` and `t_bat` on the web and CLI), so a stored
configuration still loads; what changed is what the numbers mean, and config
version 22 converts the old millisecond interval into the number of records
that would have accumulated in it. A wake whose send failed now books five
reading wakes of silence, because a count-based trigger would otherwise be true
on every wake while a collector is down, and the radio would run the battery
flat answering nobody.

**The telemetry cadence and the batch size are automatic now.** The configured
interval used to be a floor between batches, which made it the throughput
ceiling: at the five minutes a field device is set to, one batch every five
minutes, so a backlog of 35,000 records needed 31 hours to clear. It is now the
period between *drains*. A drain runs until there is nothing left, and the pace
inside it comes from the server: a send cycle that finishes under the fast mark
for its transport earns the next batch immediately, a slower one earns a gap
that doubles per slow batch up to ten seconds, and one fast batch clears the
escalation. The batch size follows the same signal, growing by half on a fast
success and halving on a failure, always under the heap ceiling that was
already there. Measured on the bench with the field configuration: 35,382
records drained in 26.8 seconds over plain HTTP, and HTTPS went from 59 to 142
records per second. The old floor was slowing things down even at its minimum
setting — dropping it took the plain cost per request from 127 ms to 79 ms and
the HTTPS one from 1,685 ms to 704 ms. An operator's touch now defers the next
batch by a second, so a drain cannot make the screen feel dead; headless builds
have no touch provider and are unaffected.

**A telemetry wake now sizes itself against the reading interval.** The flush
had a flat 30-second cap that knew nothing about how often the device reads its
sensors, so with readings every minute the wake ran past its own next reading.
The budget is now whichever is smaller: that cap, or what is left of the
interval after the wake's tail. And when the uploader asks for a gap the rest
of the wake cannot cover, the device sleeps instead of waiting with the radio
on — the records stay on flash and the next telemetry wake continues the drain.

**The cursor also reaches flash during a fast drain now.** The same coalescing
window was restarted on every cursor update, so at a back-to-back cadence (one
batch every 73 to 281 ms on the bench) the five seconds never elapsed and
nothing was written for the whole drain: thousands of batches, and a power loss
in the middle would have re-sent all of them on the next boot. The window is
now anchored on the first dirty update, which gives one write per five seconds
under load — the original intent. Found while measuring what the telemetry
cadence and batch size actually cost on the Air build; the measurements and the
plan that follows from them (automatic cadence and batch, hibernate-and-resume)
are in `docs/analysis/SIMUT_TELEMETRIA_PLANO_CADENCIA.md`, with the bench in
`tools/telemetry_bench/phase_cadence.py`.

The plan, the bench evidence and the acceptance tests are in
`docs/analysis/SIMUT_AIR_PLANO_FIX.md`, `tools/air_test_suite.py` (16 cases over
the serial console, the web API and the PicoHand fixture, including a 10 kHz
probe that times the cycle without touching the target, and a `--selftest` that
needs no hardware) and `tools/check_air_consistency.py`.

### Known issues

The Air build has only ever run on a bench. None of the following is a
regression; the plan carries the detail.

* **F04** — a wake that never reaches NTP stamps history from a guessed interval
  (about 80 s) instead of the measured sleep, and the post-NTP correction never
  runs in M1.
* **F08** — the connect phase requires a fully ready stack, which requires NTP
  with no fallback, so a lost packet or a network without internet sleeps
  without transmitting.
* **F10** — a reading interval of 1440 minutes arms an alarm that never fires.
* **F11** — M1 never runs the filesystem budget sweep or flushes deferred logs.
* **F12** — `air stop` over Bluetooth does not work in M1; only the USB console
  is pumped.
* **F14** — the sensor power pin cannot be changed and is not reported.
* **Intermittent** — twice in one session a cycle entered the sleep sequence,
  where the watchdog is already disarmed, and never armed the alarm. Not
  reproduced on demand; six controlled runs across the two firmware versions did
  not discriminate.
* **Silent reboots** — three boots with no autopsy record and no watchdog
  signature, always near a failed send. Hypothesis is USB power.
* **Current draw has never been measured.** Every energy claim here is about time
  awake and about the radio not being powered.
* **Security** — an external audit on 2026-09-07 left eight findings open
  (`docs/security-audit/`). Two touch this build: an unauthenticated request
  keeps an Air device awake, and `air charger` accepts the CYW43's own pins.
* **CI does not build `pico_w_air` or run `native_air`.**
* **`pico_w_debug` does not fit in flash**, and has not for some time.

## v2.3.9-beta (2026-08-29)

### Alpha web selector fixed (was stuck in English)

The alphanumeric build's language selector and interface were falling back to
English on the authenticated pages: the language-pack locator truncated its
header buffer while reading @NAME, so @CODE was never parsed and /api/perms
returned an empty language code. The header is now parsed without mutating the
buffer, so the web UI adopts the installed PT/ES pack as its default.

## v2.3.8-beta (2026-08-29)

### Web interface defaults to the installed language

On first visit the web UI now opens in the language of the pack installed in
/lang (PT or ES, when present and compatible) instead of always English. The
language selector keeps working the same way, and the choice is remembered in
the browser — only the first-visit default changed.

## v2.3.7-beta (2026-08-29)

### Alpha CLI help speaks the device language

The alphanumeric build's emergency CLI was printing its `help` in English even
with a Portuguese or Spanish language pack loaded. Two stubs in the alpha
variant were responsible: `getActiveHelpText()` returned nothing, and
`unaccent()` was a no-op. The first now reads the `@HELP` section from the
language pack on demand, and the second converts the UTF-8 text to ASCII for the
serial terminal — so `help` matches the device language (PT/ES).

## v2.3.6-beta (2026-08-29)

### License page rewritten and translations fixed

The /license page now follows the simut-rx pattern: a plain-language summary
(translated to PT/ES), a short legal note, then the original English MIT text
and third-party notices — the only text with legal weight.

Third-party notices were completed with the four firmware libraries that were
missing — TwoWirePIO_RP2040, BMx280PIO_RP2040, lwIP, and BTstack — both on the
web page and on the on-device license screen.

The alpha build now serves its web translations straight from the language pack
(GET /api/lang), and the TFT build keeps only the @DICT resident in RAM, reading
@HELP and @LICENSE from LittleFS on demand.

Fixed a regression where the CLI help and the on-device license screen always
fell back to English: the lazy reader sized its buffer as `sizeof(char)` instead
of the buffer itself, so it always read zero bytes.

## v2.3.5-beta (2026-08-24)

### Alpha gains Bluetooth and a multi-sensor display

The alphanumeric (HD44780) build now cycles through every active sensor on its
16x2 display, prefixing each reading with `S<n>` when more than one slot is
active, and shows `AP` instead of the WiFi signal icon while serving the Access
Point. Pressure renders in plain characters (big-font 4-digit hPa is deferred).

Bluetooth is enabled in the alpha build: the SerialBT CLI starts at boot even
without a WiFi connection, and a new `ap` command — on USB and Bluetooth alike —
starts Access Point mode for setup.

The TFT-only screenshot ("Display Capture") code is now excluded from the alpha
build to reclaim flash, and the dashboard hides the Display Capture box when the
firmware reports no capture capability. The web server also answers OS
captive-portal probes in AP mode, so the Restore file picker can open on
iOS/Android.

## v2.3.4 (2026-08-24)

### The 2.3 line goes stable

v2.3.4 promotes the RAM diet shipped in v2.3.4-beta — graph scratch buffers to
heap on demand and the language pack loaded only when needed — to the stable
channel after the full validation campaign (237/237 native tests, all host
gates green, and the directed bench A/B).

## v2.3.4-beta (2026-08-24)

### A RAM diet that keeps every feature

The graph rendering path no longer holds its scratch buffers permanently. The
bucket accumulator and the two `GraphDataPackage` work areas — together
~18.6 KB of static `.bss` — are now allocated on demand from the heap and
released when the graph closes. Because they are transient, the linker gives the
space back to the heap: static RAM drops from 49.3% to 42.2% (129,256 → 110,616
bytes) and the heap region grows by the same amount. On the dashboard this moves
the RAM gauge from ~95% to ~83%, back above the telemetry guard that protects
the TLS handshake.

The language pack is now loaded only when a non-English language is selected, so
an English device keeps its ~15 KB of translation heap free.

Validated before shipping: a full release build, 237/237 native unit tests, and
a bench A/B confirming the change is neutral to web serving.

## v2.3.3-beta (2026-08-23)

### Alarms get their own telemetry line, and sensor errors get their own voice

The firmware now ships a **second, independent telemetry line dedicated to
alarms**, running alongside the measurement line that already exists. Each
alarm key carries two domains — `alarm` (the limit threshold) and `err` (a
hardware/communication failure) — so a sensor that trips a limit and a sensor
that stops answering are told apart end to end, on screen, in syslog and in the
uploaded payload. The `{err}` domain carries the full lifecycle: silencing and
per-slot deactivation become codes in the payload, not just local UI state. The
alarm queue is held in RAM with ACK, its payload is editable, and it inherits
the transport already configured for the main telemetry line (CLI, Web and
metrics all see it).

Sensor errors are now first-class citizens. A sensor that loses communication —
or is swapped on a live channel — fires a dedicated **ERROR alarm** with an
amber-and-white panel that stays fixed at the top of the dashboard. Limit and
error alarms are fully independent per slot: clearing one never clears the
other, and a sensor re-established after a fault regenerates its own error
alarm. Disabling a sensor that is in ERROR clears the amber display, and syslog
now reflects the real state. Per-slot deactivation is **RAM-only** — it never
touches the filesystem, so it survives neither a reboot nor a stray write.

The web UI gains a dedicated **Telemetry page with a Live Preview of alarms**
that mirrors the main builder, plus a visual standardization pass on buttons,
text boxes and toggles. On the transport side, a telemetry cursor that ran
ahead of the data could wedge the sender — it now self-resets instead of
stalling the upload.

Hardware validation completed on the bench before publishing: HTTP (14/14),
TLS (15/15), MQTT+ACK (8/8) and CLI dump/flush (3/3) suites all pass.

## v2.3.2-beta (2026-08-21)

### The white flash between pages is gone

Every authenticated page kept its theme tokens only in `/style.css`, which
arrives by a JavaScript fetch *after* the first paint (the delivery queue that
keeps the one-connection TLS server alive), and page HTML is deliberately
`no-store` — so every click painted the browser's default white and then
snapped to dark when the CSS landed. The login and setup pages already carried
the inline antidote; the other eight pages now do too: the dark `:root` tokens
plus `html`/`body` painted with the theme variables, inline in each page. The
first paint is born dark; the shared CSS that arrives later only adds the
chrome. Light-theme users were never affected — `lang.js` is a synchronous
head script and injects the light tokens with higher specificity before paint
— so the light palette needed no inline copy.

Cost: +976 bytes of flash across the eight pages (~122 B gzipped each).
Nothing changes in how assets load or cache (`?v=` build-hash invalidation was
already in place).

Also in this release: the v2.3.1-beta changes below are promoted to Latest —
see that entry for the RAM refund and the telemetry batch ceiling.

## v2.3.1-beta (2026-08-20)

### The RAM the accept-stall fix took hostage is returned, and telemetry breathes again

The v2.3.0 static TLS pool fixed a real watchdog stall, but it charged its
~21.5 KB as BSS on **every** configuration — including HTTP-only setups,
where no TLS accept can ever happen. Measured on the bench: idle free heap
fell from ~38 KB to ~16 KB, which is below the telemetry pre-flight gate, so
every telemetry cycle aborted into backoff before touching the network — on
every transport, with the missing RAM impossible to win back by turning
features off. The pool is now reserved once at boot, from a still-unfragmented
heap, and **only when the HTTPS server is actually starting**: HTTPS setups
keep the exact anti-stall behaviour (accepts stay malloc-free), HTTP setups
get the 21.5 KB back (`pico_w_release` RAM 55.0% → 46.8%).

With the room back, the telemetry batch ceiling rises from 50 to **250
records per upload** (web UI, `/api/commit_all` and `tel batch` all accept
1–250). The ceiling is what you may ask for; what actually ships each cycle
is still sized by free heap — reserve 32 KB under TLS / 12 KB plain, ~350 B
per JSON record, ~160 B per CSV record (the old flat estimate halved CSV
batches for no protective reason), plus the existing shrink-under-pressure in
the payload builder. The pre-flight heap gate is now transport-aware too:
plain HTTP/MQTT telemetry no longer sits silent below 24 KB free — that
floor exists for the TLS scratch, which plain transports never allocate.

Nothing in the payload format, the cursor logic or the transports changed.

## v2.3.0-beta (2026-08-20)

### HTTPS pages load in a third of the time, and the reboot hiding under them is gone

Every response used to carry `Connection: close`, so a browser paid a full
~510 ms TLS handshake for every single request — eight times per page. The
web server now keeps the connection alive by default: one handshake per
session, and the measured page time fell from ~6.1 s to ~2.4 s (−61%). The
switch lives on the network page under Web Server, appears only when a TLS
certificate pair is present (where the handshake actually costs), and is on
by default; existing configurations inherit the default. HTTP pages gain a
smaller cut (~−20%) from the same reuse.

Underneath it, a defect that predates the keep-alive work is fixed. Every
TLS accept allocated ~22 KB from the heap (server context + I/O buffers)
while the telemetry TLS client churned ~10 KB per retry on the same heap;
with ~29 KB free and a fragmented free list, the 16.7 KB allocation could
stall Core 0 past the watchdog window. Four independent fine-grained
autopsies put the death on the same line, and the failure clustered in the
first minutes after every boot — a self-sustaining reboot cycle under plain
"open a page, read, click" browsing. The server context and buffers now come
from a static pool (+22 KB BSS), taking every large allocation out of the
accept path: a 30-minute soak that reproduced 3 watchdog reboots without the
pool ran clean with it, and the intermittent soft page failures (~10-20%
under pause-heavy browsing) went to zero with the same change.

The fixes ship as framework overrides in `tools/arduino_pico_overrides/`
(`webserver_keepalive.patch`, `clientcontext_acked_feed.patch`,
`bearssl_server_static_pool.patch`), applied by `patch.sh` as before.

## v2.2.18-beta (2026-08-20)

### The HTTPS-to-HTTP login loop now explains itself

Signing in over HTTPS gives the session cookie its `Secure` flag, and by the
browsers' "leave secure cookies alone" rule a plain `http://` page may neither
send that cookie back nor overwrite it. So the first sign-in after HTTPS is
turned off — the certificate deleted, the device back on HTTP — can accept the
credentials and then bounce straight back to the login screen, because no
session cookie ever reaches the device. It is a browser rule rather than a
server fault (a fresh HTTP login, with no leftover cookie, works), and the
server cannot clear a `Secure` cookie over HTTP to break out of it.

The login page now turns that silent loop into an instruction. A successful
login stamps the browser; reaching an authenticated page clears the stamp; and
if the login page loads with the stamp still fresh — it was just bounced back —
it shows a banner, in the device's language, telling the operator to open a
private window or clear this site's cookies. The session cookie is per-session,
so closing and reopening the browser clears it too. The manual's HTTPS section
gains the same note.

## v2.2.17-beta (2026-08-19)

### The browser can drive the web UI over HTTPS, and a bad certificate can no longer lock it out

The HTTPS transport was validated end-to-end from a real browser for the first
time, and it exposed two problems this release fixes — one that made the UI
barely usable over TLS, and one that could brick the web entirely.

**The single TLS slot vs. the browser.** BearSSL's server side needs a 16 KB
buffer per connection and this heap fits exactly one, so the web server serves
**one TLS connection at a time**. A browser loads a page by opening several
connections at once — the stylesheet, the shared script, and the dashboard's
API calls — and the ones that cannot get the single buffer are dropped. The
retriable `fetch` calls eventually won; the stylesheet `<link>`, which a
browser never re-requests, did not, so the dashboard came up unstyled and
half-populated. Login was worse: its nonce raced the parallel page-load fetches
and arrived stale, a guaranteed 401.

The fix matches the client to the server. Over HTTPS every `fetch` is funnelled
through a **concurrency-1 queue** — one request in flight at a time, each with
an 8-second abort so a stalled connection cannot freeze the rest. The
stylesheet moves from a racing parallel `<link>` to a queued, retried fetch; the
shared script stays a blocking `<script>` so page code keeps its globals in
order. Login fetches its nonce sequentially at submit time and retries the post.
Over plain HTTP, where the buffer is not the bottleneck, none of this installs —
that path is byte-for-byte unchanged.

Measured on hardware over Chrome: the dashboard now loads 4 of 4 — styled, with
live data and no console errors — where it was 0–1 of 4 before; config,
history, network and alarms all load; login is 5 of 5. HTTPS page loads are
serial and so slower than HTTP (~9.6 s vs ~4 s), but they are reliable. Cost:
about 220 bytes of gzipped UI.

**A mismatched certificate can no longer trap you.** A certificate and key that
each parse but do not belong together start an HTTPS server whose every
handshake fails — and the existing fallback only covers a cert that fails to
*parse*, so the web went dark on 443 with 80 already closed, reachable from
nothing but the serial console. Two recoveries now exist. `system https off
[confirm]` on the serial console deletes the pair and reverts to HTTP, taking
its place beside admin-reset, factory and format as a web-locked escape. And
the setup Access Point (hold the touchscreen at boot) now always serves HTTP,
ignoring any certificate, so the recovery UI is reachable over the network too.

## v2.2.16-beta (2026-08-19)

### HTTPS uploads stop dying at the fourth kilobyte

The optional HTTPS transport shipped in v2.2.6-beta and went untested on
hardware until today. The first full bench run found the hole: every upload
past ~4 KB killed the connection mid-body (clean bisection: 3.5 KB passes,
5 KB dies in half a second), so a language pack or an OTA image could not
cross an encrypted session — while downloads worked fine, because the
device's own transmit records are small.

The cause is TLS record framing, not the upload path. A record must fit the
receive buffer **whole**; the extension that would cap record size
(max-fragment-length) is offered by clients only, and stock OpenSSL and
browsers never offer it — they ship 16 KB records for any large body. The
server's receive buffer was 4,096 B, a cap chosen when the heap could not
spare more. It is now 16,709 B (`BR_SSL_BUFSIZE_INPUT`, one full record),
allocated per accepted connection rather than at boot; the post-boot largest
free block measures 33.6 KB, so one TLS client fits with room.

Measured on hardware after the fix: the 32 KB es-ES language pack uploads
over TLS in 1.9 s, 120 KB round-trips with matching checksums, `/api/lang`
stays byte-identical to the HTTP baseline, and the handshake holds at
0.5–0.7 s (TLSv1.2, `ECDHE-ECDSA-AES256-GCM-SHA384`, `Secure` cookie). The
price is honest and transitory: while one TLS connection is alive the free
heap drops to ~15 KB — which is also why the server keeps its other shape,
**one TLS client at a time**; a second simultaneous connection is dropped
without a response. Firmware updates remain recommended over plain HTTP.

The feature also never reached the documentation. The manual gains *Serving
the UI over HTTPS* (§6) — the openssl one-liner, Files-page provisioning,
the port-443 default, and the fallback contract: an absent or unparseable
pair can never lock the operator out, and overwriting the key with garbage
is the off switch. `SECURITY.md`'s attack-surface list and the README
comparison row now mention the HTTPS mode in all three languages.

## v2.2.15-beta (2026-08-19)

### The web UI learns to be operated without a mouse

First accessibility slice for the embedded web UI (issue #60's declared gap).
The starting point, counted on the source: six `aria-*` attributes across the
whole UI, zero `tabindex`, one `.focus()` call, no reduced-motion handling.
The slice covers the shared UI chrome, so every authenticated page inherits
it:

- The credentials dialog is a real dialog: `role="dialog"`/`aria-modal`,
  focus moves in, Tab is held on the single button, and focus returns to the
  opener on close. Escape deliberately does **not** close it — the password
  is shown exactly once, and a reflex Esc before copying would cost the
  credential.
- The custom select was mouse-only. Arrow keys now change the value like a
  native closed select, Enter/Space toggles the menu, Escape closes and
  keeps focus; the button carries `aria-haspopup`/`aria-expanded` and the
  menu proper `listbox`/`option` roles with `aria-selected`.
- The drawer tracks `aria-expanded`, moves focus to the first link on open,
  returns it to the hamburger when closed from the keyboard, and closes on
  Escape. The toast is a polite live region. Nine icon-only buttons
  (calendar, graph navigation, sound tests) get accessible names.
- The login and force-password pages, which do not load the shared
  stylesheet, get their own `:focus-visible` outline and
  `prefers-reduced-motion` block; the shared stylesheet gains the
  reduced-motion block it lacked.

Zero new translatable strings — accessible names reuse visible text
(`aria-labelledby`) or the existing English `aria-label` convention, because
the es-ES pack had 723 B of ceiling left when this was written (see below).
Behaviour was verified with real keyboard input over CDP against the local
bench, branch vs `main`: 30/30 checks. One claim died by measurement on the
way: the shared `:focus-visible` outline already reached the custom select —
the injected `outline:none` loses the cascade tie because the injected
`<style>` lands before the stylesheet `<link>`. Verified by computed style,
and the redundant rule was dropped instead of shipped.

Cost: **+847 B** of gzipped UI. Also fixed: the one `-Wcomment` warning
(a `/config/*` glob inside a block comment, shipped since the HTTPS commit)
— the only warning a routine incremental build showed.

### The language pack ceiling stops taxing RAM that web strings never use

The 32,768 B pack ceiling was one number guarding two different things. About
60% of a pack (`@WEBDICT`, 18.8 KB in es-ES) is JSON served to the browser by
`GET /api/lang` straight from flash — yet the loader malloc'd the whole file,
parsed it, then excised the blob into a second buffer, peaking near 45 KB of
heap to keep 13 KB. Meanwhile es-ES sat at 32,045 B — **723 B from the
ceiling** — and every new web string was priced against RAM it never used.

`loadLangFile()` now locates the `@WEBDICT` marker by streaming the file
through a 256 B stack chunk and reads **only the resident prefix** into RAM.
The blob's byte range is recorded for the web handler with a formula that is
byte-for-byte what the old excision served; the excise-and-rebase pass is
deleted. One contract appears: `@WEBDICT` must be the file's **suffix** (both
shipped packs already are) — the device rejects violations and
`tools/check_lang_packs.py` refuses to ship them, now reading both ceilings
from the parser source so the gate cannot drift. `tools/test_lang_gate.py`
provokes every new failure mode on synthetic packs: 8/8.

Two ceilings replace the one: `LANG_RESIDENT_MAX` = 16,384 B for the malloc
that lives the whole uptime, `LANG_FILE_MAX` = 49,152 B for the file on
flash. es-ES moves from 97% of one ceiling to **resident 80% + file 65%**:
about 17 KB is now available for web translations. Peak load heap drops from
~45 KB transient to 13.3 KB. Flash cost: −8 B.

Validated on hardware over OTA: with the unchanged pack, `/api/lang` is
byte-identical before and after the swap; a 36,545 B inflated es-ES — which
the old loader rejects whole, silently reverting the UI to English — loads
with es-ES active and serves its 23,285 B body byte-for-byte.

**Deploy order**: a pack over 32,768 B needs this firmware first. The shipped
packs are unchanged and work on both firmwares.

## v2.2.14-beta (2026-08-19)

### Remote syslog — the audit trail leaves the box

The on-device event log lives in a rotating ring of at most ~1600 records; a
regulated deployment needs a copy that leaves the device, append-only, and
that is what this adds. When enabled (System Settings → *Remote Syslog*), each
qualifying log event is forwarded as an [RFC 5424](https://www.rfc-editor.org/rfc/rfc5424)
message over UDP to a collector or SIEM.

It is deliberately **not** a third telemetry transport. UDP is
fire-and-forget: no handshake, no TLS client, no on-flash cursor, no
reconnection state machine — none of the machinery the telemetry send loop
carries. Threading is the whole design: `logCode()`/`log()` run on either
core, but all network on this chip is Core 0, so the log sink formats and
enqueues under the log mutex (where the tag and the code description are still
valid to read) and the main loop drains and sends on Core 0. The lock order is
always log-mutex → ring, never inverted. A `WARN`/`FATAL` raised just before a
reboot is flushed on the way out; a hard dual-core hang saves nothing, which
nothing could.

Two traps the format was built around, both pinned by native golden vectors:
the clock never reads 0 but falls back to the build epoch and time-travels, so
below the sync threshold the timestamp is the RFC 5424 NILVALUE `-` rather than
a line stamped in the past; and the space-delimited header would shear on a
device name containing a space, so every structured field is sanitized to
printable ASCII. The context, core and uptime ride a structured-data element;
the numeric log code is the MSGID (stable and language-independent).

Config is an 8-byte overlay in the last free bytes of `reserved[]` — which is
now full. The collector is a raw IPv4, not a hostname: there is no room for a
64-character name in 8 bytes, a LAN collector is addressed by IP in practice,
and it avoids a DNS-resolution failure path in the logging hot loop. The
address reuses the same input validator hardened below.

Cost: ~2 900 B of flash for the whole feature (engine, config, web UI, and six
new i18n keys). The es-ES language pack rises to 97 % of its 32 KB ceiling.

### Strict integer/float parsers now reject overflow

`parseIntStrict` checked "digits only" but not "fits an int": it answered true
for `"2147483648"` with the value silently saturated to `2147483647`, because
`String::toInt()` is `atol()` and newlib's `strtol` saturates instead of
failing — a value the client never wrote, handed to callers whose contract
said "well-formed". The parser now accumulates the digits itself with an
overflow guard, so the result is exactly the number written and cannot depend
on the platform's `long` width. `parseFloatStrict` had the float spelling of
the same hole: ~40 digits saturate `atof` to ±inf, which would poison any
threshold compared against it — a non-finite result now returns false. Found
and closed while adding fuzz coverage for the web-API input validators.

Cost: 112 B of flash.

### A dead route removed, and a matrix pinned so it cannot rot

`/favicon.ico` was registered twice; the web server matches the first handler
for a path, so the second registration (a 204 stub after the real icon
handler) never reached a client — removed. Alongside it, the authorization
matrix — every HTTP route's required permission — is now enforced in CI: a
build-time gate parses the route table and fails if a route neither checks a
permission nor is on a documented public allowlist, so a new route cannot ship
ungated the way the restore path once did. The matrix is written out in full
in `docs/AUTHORIZATION.md`, including the two-tier privilege boundary that
keeps full backup, OTA and restore-stage reachable only by the built-in admin,
never by any web-created account.

## v2.2.13-beta (2026-08-19)

### The device now introduces itself to Home Assistant

With the MQTT transport and JSON payload selected, a new opt-in checkbox makes
the device publish retained [MQTT Discovery](https://www.home-assistant.io/integrations/mqtt/#mqtt-discovery)
config messages on every broker connect. Home Assistant then creates the
device and one sensor entity per measurement automatically — temperature and
humidity per active slot, plus pressure — with availability driven by the LWT
status topic that already existed. No YAML on the HA side. On the bench rig,
six sensors became eight entities whose `value_json` templates matched the
live payload key for key.

The design bends around one fact: `commit_all` reboots. So there are no live
refresh hooks — the connect after the reboot reconciles instead, guided by a
persisted `FLAG_HA_PUBLISHED` bit that remembers retained configs sit on the
broker. That bit is what lets a freshly **un**checked box publish the empty
retained payloads that remove the entities: verified on a real mosquitto, a
fresh subscriber sees zero retained configs after disabling.

Side unification: the LWT topic used to derive from the raw `cfg.mqttTopic`
while data publishes trimmed it with a `simut/data` fallback — a blank topic
put the will on the degenerate `/status`. Will, data and discovery now come
from the same two resolvers and cannot drift.

Cost: 2 544 B of flash, 0 B of RAM.

### reserved[52..53] had a squatter, and it ate the feature's magic byte

Every map of `SystemConfig::reserved[]` said `[48..63]` was free. It was not:
the TFT dashboard has been persisting its slot selection at `[52..53]`
through raw literals registered nowhere. The discovery overlay first landed
on those bytes and its magic was overwritten by the dashboard's `0xFF`
"unpinned" sentinel three seconds after every boot — on the bench it read as
a toggle that refused to stay on, and what closed the case was dumping the
raw bytes over the API instead of trusting the accessors.

The two bytes are now named (`RESERVED_DASH_TOP_IDX` / `RESERVED_DASH_CUR_IDX`),
the raw literals are gone, both maps tell the truth, and the overlay lives at
`[54..55]`. If you are adding an overlay: grep for `reserved[<offset>` before
believing any comment.

### GET /metrics — Prometheus without writing a server

The pull complement to the push telemetry. Everything served already existed
in RAM for `/api/status` or `show metrics`; the route spells it in the text
exposition format — 37 metric families: build info, heap and filesystem,
WiFi/MQTT state, the telemetry counters, the flash-op and Core-1 lifecycle
counters (the release-image observability a long soak needs readable from
outside), and one gauge per live measurement with `slot`/`hwid`/`name`
labels. The body parses clean under the official `prometheus_client` parser.

A scraper cannot run the login flow, so besides the session cookie the route
accepts HTTP Basic against the existing user table, requiring the same
dashboard permission as `/api/status`. Failed credentials feed the **same
per-IP exponential lockout as the login form** — the slot allocation moved
out of `login_init` into a shared helper precisely so this route cannot
become the cheap door around a brute-force limit the login already enforces.
While locked, even correct credentials get 429; verified live. Each scrape
verifies the password in full (~0.7 s on the device), so keep
`scrape_interval` at 15 s or more — six sustained scrapes held ~690 ms each
with the heap flat.

Cost: 4 560 B of flash, 0 B of RAM, no new config and no UI strings — the
es-ES pack ceiling is untouched at 96 %.

## v2.2.12-beta (2026-08-19)

### An icon nobody looked at was holding 11 KB of flash

`data/favicon.ico` is not a filesystem asset — a pre-build hook embeds its
literal bytes into `src/Favicon.cpp`, so one byte in the icon is one byte of
firmware. It was **11 047 B**, which was **22 % of all remaining headroom**,
spent on a 32×32 image.

The weight was never the drawing. The mark is three colours — cyan `#00DCFF`
on navy `#0A172F` with a `#7ADFFF` ring — but it was stored with **982 unique
RGBA values** in its 48 px frame. The rasteriser had left ±1 noise, so pixels
that look identical differ numerically:

    (10,23,48)  (10,23,47)  (10,22,47)  (10,23,49)  (11,25,49)

all of them the same navy, none of them compressible as a pattern. On top of
that the container carried four frames, the 48 px one alone costing 5 283 B.

So this is a re-encode, not a redesign. Clear alpha below 32 — which also stops
the quantiser promoting near-transparent pixels into opaque speckles around the
disc — quantise to 16 colours, keep the two frames a browser tab actually uses,
and assemble the ICO container by hand, because Pillow's ICO writer re-encodes
the payloads and undoes the quantisation.

    16×16   306 B  +  32×32   491 B  +  38 B header  =  835 B

    release flash   994 212 → 983 996 B      headroom 50 268 → 60 484 B

Chrome decodes the result and it is indistinguishable from the original at 16
and 32 px. Dropping the 24 and 48 px frames means requests above 32 px now
scale up from the 32 px frame.

### A static-analysis gate, and the two real defects it found

`tools/run_cppcheck.sh` runs cppcheck 2.11 — pinned, because the check set
moves between versions and an unpinned gate fails on a day nobody changed any
code. It runs in CI as its own job, so its minutes cost no wall clock against
the build.

Most of what it reported was already correct and is now answered in place with
a documented suppression: the zigzag sign-smear the history codec is specified
against, the Bresenham octant mirror, a member initialisation cppcheck parses
as a call. Two findings were real, both in the dashboard's packet counter:

    snprintf(pktBuf, sizeof(pktBuf), "%u", state.pendingPkts);

`pendingPkts` is `uint32_t`, which on this target is `unsigned long`, not
`unsigned int` — so `%u` was the wrong conversion for the argument's actual
type. Both call sites now cast explicitly.

### Every field of CliDemand now has an initialiser

Three of its nine fields had none, and the one read without being written is a
`bool` whose two values are "encrypt telemetry" and "do not". It is not
reachable today — the only producer sets it on the same line it sets the type —
but that is the same shape as the `/api/commit_all` defect closed in
v2.2.10-beta, where a boolean read the wrong way round turned on a setting the
user had turned off. No behaviour changes.

### CI runs all five native environments

It ran two. The other three — HistoryV4, the CLI parser, LogPolicy — existed
and were green locally while nothing enforced them on a pull request. All five
now run as separate steps, so a failure names itself instead of hiding behind
whichever environment broke first, and `tools/scan_secrets.sh` runs as the very
first step: a tracked secret is not a build failure to be discovered at the end.

    252 cases   95 validators · 56 HistoryV4 · 54 HistoryV5 · 29 CLI · 18 LogPolicy

### The project has a logo

`docs/images/logo-mark.svg` (880 B) and `logo-wordmark.svg` (2 146 B) — the
shipped icon re-drawn as vector, so the identity is one thing instead of an
icon and an unrelated badge. The letterforms are DejaVu Sans Bold outlines,
chosen by measured overlap against the original mark rather than by eye: 91.2 %
IoU, against 85.3 % for the runner-up.

The wordmark uses a fixed `#1a73e8` and no `prefers-color-scheme` switch. That
switch was written first and removed: inside an `<img>` the media query follows
the reader's OS theme, not the theme they picked on GitHub, so the two can
disagree and the text lands white-on-white. One colour that holds on both is
safer — measured 4.51:1 on `#ffffff` and 4.20:1 on `#0d1117`.

## v2.2.11-beta (2026-08-18)

### "Connection lost", with the device on the LAN

The history page loaded Chart.js from `cdn.jsdelivr.net`. With no internet the
`new Chart(...)` call threw a ReferenceError, the loader's `catch` swallowed it,
and the user read **Connection lost.** — while the device was on the local
network and the `.h5` had already been downloaded and decoded. The failure blamed
the network for a missing script, in a product whose first promise is
offline-first.

The same tag carried two more defects. It pinned no version: `npm/chart.js`
resolves to whatever major jsDelivr publishes, and v3 to v4 already broke the
options API once — a future release would stop the graph on devices already in
the field, with nothing anyone could do. And it had no `integrity`, in a document
that holds the session cookie, served without a CSP.

### Trimming the library was measured, and does not solve it

Registering only what the page uses — `LineController`, `LineElement`,
`PointElement`, `LinearScale`, `Legend`, `Tooltip` — takes Chart.js 4.5.1 from
70 592 to 56 818 B gzipped. Twenty percent, because the weight is not in the
chart types nobody uses:

    core alone, nothing registered ..... 43 527 B   draws zero pixels
    + LinearScale ...................... 43 534 B   +7
    + Line/Point ....................... 49 049 B   +5 515
    + Legend + Tooltip ................. 56 818 B   +7 769
    + everything else .................. 70 719 B   +13 901

Adding the linear scale to that core costs **seven bytes**, because the scale
engine — ticks, autoSkip, rotation, label measurement, axis layout — is already
in it. What is irreducible is generality: the Proxy-based option cascade, the
animation engine shipped even with `animation: false`, six interaction modes, a
full CSS colour parser, spline maths. None of it is used here.

### h5g

A renderer that already knows it has three axes, one series type and fixed tick
steps: 784 lines, **4 721 B gzipped**, 15× smaller than the CDN bundle. It
reproduces the page as it stood — x linear in epoch ms with the window forced to
the period asked for even when empty; three independent Y axes, because pressure
near 1000 hPa would flatten %RH and °C into straight lines; line broken on
`null`, so a period without data looks like one; per-quantity dash, so identity
never rests on colour alone; visible radius on a sample isolated between two
gaps; the min/max band; clickable legend that hides the band with its series;
nearest-x tooltip; resize, device pixel ratio and touch.

Two behaviours were changed deliberately rather than copied:

- **The band is inside the Y range.** Chart.js draws it from a plugin the scale
  cannot see, so its peak was clipped by the edge of the plot. In a cold chain
  that peak *is* the excursion. The MAX/MIN badges above the chart give the
  number, but only with a single sensor selected — with two or more the badges
  are hidden and the extreme disappeared entirely. The mean line loses about a
  quarter of its vertical resolution, knowingly.
- **X labels rotate 45° when they no longer fit**, instead of thinning the ticks.
  Thinning cost the grid along with the labels: at 375 px the axis went from
  seven vertical lines to three, and locating an event in time meant estimating
  between twelve-hour marks. Rotation costs ~17 px of height. It is a fallback,
  not a default: at 390 px with one sensor the labels stay straight, because they
  fit.

### Proved against the engine it replaces

`scratchpad/h5g_20260818/` renders eight frozen cases and seven interaction
captures through **the page's own `renderChart`**, extracted from `WebUI.h`
rather than copied, with Chart.js pinned in the directory and no network reached.
The first gate was the control: two runs of the *same* Chart.js, requiring zero
differing pixels — an A/B whose A-against-A does not close measures its own noise.

Every X axis matches Chart.js exactly in range, step and tick count. The ten Y
axes that differ all differ because of the band decision, and the gate says so
instead of reporting them as failures; the one case without a band is unchanged,
which is the control in the other direction. Thirty chart swaps leave listeners
at 5 → 5 and retain 0,36 KB each, against 3,91 for Chart.js — the leak detector
was itself verified by removing `destroy()`'s unbind, which takes the count to
93.

On hardware, delivered by OTA twice: the page served by the device went from
carrying `cdn.jsdelivr` to carrying the renderer, and a browser pointed at the
device with external DNS disabled made **33 requests, none external, no JS
errors**, and drew the graph.

### Flash

    release headroom ... 44 044 B -> 38 604 B

5 440 bytes, in exchange for removing a 70 592 B external download from every
visit to the page.

## v2.2.10-beta (2026-08-18)

### `false` turned the setting on

Four fields in the `sys` section of `POST /api/commit_all` and two in `net` read
their booleans like this:

    cfg.telEncryption = (getNum("t_sec") != "0");

`getNum` extracts a *number*. Hand it the legitimate JSON boolean `false` and the
comparison against the string `"0"` is true, so the field turns **on**. Only the
literal `0` turned anything off, and no boolean spelling could turn any of the
six off at all.

What makes it more than a curiosity is where that payload comes from. `GET
/api/config` emits `"log":false`, `"t_sec":false`, `"m_retain":false`,
`"ntp_enabled":false`; `GET /api/network` emits `"use_dhcp":false` and
`"dns_auto":false`. **The device's own output was the payload the parser read
backwards.** Fetch the configuration, change one field, post it back — the most
ordinary thing a script does with a configuration API — and all six came back on.
The web interface never saw it, because its forms emit `1`/`0`.

`t_sec` decides whether telemetry leaves the device encrypted, so a parser that
stores the opposite of the request there is a security finding and not only a
correctness one. `m_retain` forced on leaves the last reading sitting on the
broker for any subscriber that connects later.

### Three readers, three different wrong answers

The file carried three separate ways to read a boolean out of JSON, and they had
drifted apart. Sixteen fields, all silent, all under `200 OK`:

    getNum/getN(k) != "0"   the literal `false` turned the field ON
                            log, t_sec, m_retain, ntp_enabled,
                            use_dhcp, dns_auto
    startsWith("true")      the numeric `1` turned the slot OFF
                            slot "a", slot "al"
    jsonBoolValue           1/0 fell through to the stored value
                            alarms.active and the seven sound flags

`{"a":1}` deactivated the sensor slot it was asking to enable; the history keeps
recording that channel as NaN and nothing in the log says why. `{"sounds":
{"mute":1}}` answered 200 and changed nothing.

### One reader, and a third state

`jsonValuePos`, `jsonRawToken` and `jsonFlag` now live in `WebJsonSlice.h`, the
header that already exists for the hand-rolled JSON walkers, over a
`parseBoolStrict` that sits next to `parseIntStrict`. Both spellings are
accepted — `true`/`false` and `1`/`0` — because both are in use and neither is
going away.

The third state is the part that was missing. `jsonFlag` distinguishes *absent*
from *unreadable*, and both are negative so every caller keeps the stored value
on either; only the unreadable one is reported, through the `"rejected":[...]`
array the numeric fields already used. `{"log":2}` used to turn logging on
without a word. Swapping to the existing `getBool` would not have worked: it
answers 0 for `1` and would have broken the page.

Unifying the readers killed two more defects. The `net` copy never learned to
skip the whitespace a pretty-printed payload puts after the colon, so
`{"use_dhcp": 0}` produced the token `" 0"` — that field was broken twice over.
And the slot reader was the exact mirror of the reported bug, from the other
side.

### Proved against a device that still had the bug

`tools/commit_bool_cases.py` writes every boolean in each spelling a real client
uses and reads it back from the endpoint that publishes it. It was run as an A/B
on the same board: **21/32 against the firmware that still carried the defect,
32/32 after**. That first number is the point — a suite that passes on both
images measures nothing. The numeric cases pass on both, which is the control in
the other direction: the page's own spelling never stopped working.

Eleven cases were added to `test/test_validators` (84 to 95), including
transliterations of the three removed readers that assert the wrong answer each
one gave. Putting the old semantics back inside `jsonFlag` fails 6 of the 11 —
a test that passed on its first run has not yet shown it can fail.

Hardware coverage is 5 of the 16 fields; the rest are covered natively.
`use_dhcp:false` is deliberately not exercised on hardware, because committing it
moves the device onto its static address.

### Flash

    release headroom ... 43 676 B -> 44 044 B
    test headroom ...... 46 916 B -> 47 284 B

The correction returns 368 bytes: sixty-two lines of hand-rolled scanning become
five delegations and one shared implementation. It is affordable because it is
subtraction — written as a fourth reader beside the other three, it would have
cost.

## v2.2.9-beta (2026-08-18)

### The build was shipping its own comments

`tools/build_webui_gz.py` minifies every page before gzipping it. The minifier
matched string literals with a single regular expression, saved what it matched,
stripped comments from the rest, and put the literals back. A regular expression
has no context: to it, every quote character opens or closes a string. A quote
that is not a delimiter — inside a comment, or inside a regex literal such as
`/[<>&"]/` — shifts the pairing by one, and from that byte onward the matcher
believes it is inside a string while it is in code, and in code while it is
inside a string.

That defect had already cost a release in its loud form, where a run of code is
taken for the inside of a string and **discarded**: `ALARMS_PAGE` once shipped
at 9% of its source with every handler gone, and `node --check` saw nothing
wrong, because what remained was still valid JavaScript.

The quiet form had never been measured. Once the matcher desynchronises, the
region that follows is treated as one long literal, so minification never
reaches it, and comments and indentation travel into the firmware intact:

    LANG_JS ......... 96.5% retained — 9 990 B of surviving comments
    HIST_PAGE ....... 93.2% retained — a 92 432 B protected region, 84% of the page
    ALARMS_PAGE ..... 97.7% retained
    healthy pages ... 68-76% retained

The cure is not a better regular expression. The minifier is now a stateful
scanner: it knows whether it is in HTML, in a `<script>`, in a `<style>`, in a
string, in a template literal, in a comment or in a regex literal, because it
arrived there through every preceding byte. No new dependency — building from
the release zip must work with the machine's Python and nothing else.

    12 assets, gzipped ......... 103 418 B -> 83 861 B   (-19 557)

### Three gates, so the silent failure cannot come back

The old failure mode passed every check the build had. Code vanished, what
remained parsed, and the build printed SUCCESS.

`_assert_only_whitespace_removed` re-scans the **output** and requires that the
literals, byte for byte, and the code with all whitespace removed, are identical
to the input. Nothing but whitespace and comments may leave. It has a positive
control in the test suite proving it fires on a renamed symbol, a deleted
function, and an altered string — a gate that never fires is indistinguishable
from one that works.

`node --check` now reaches `LANG_JS`. It is the largest block of JavaScript in
the project and it had been outside that gate for its whole life, because the
search only looked inside a `<script>` tag and `/lang.js` is not HTML.

The retention ratio stays as the blunt third check. It is also more meaningful
now: the numbers across the twelve assets are consistent (56-97%) instead of
spanning 60-97%, and that spread was itself the symptom.

`tools/test_webui_minify.py` holds 49 cases — every trap the old minifier fell
into, plus two the scanner had to learn.

### The repeated page skeleton is paid once

Eight authenticated pages each carried their own copy of the top bar and of
`initSession`, and each page is gzipped on its own, so the same bytes were paid
eight times. Measured by isolating each block:

    initSession -> /lang.js .................... 3 766 B
    top bar and breadcrumb HTML -> /lang.js .... 1 272 B
    4 CSS rules -> /style.css .................. 1 109 B

`/lang.js` and `/style.css` are served with `Cache-Control: max-age=604800`, so
those bytes also stop being downloaded on every navigation.

The top bar is installed during parsing, from a `<script>` right after `<body>`,
not on `DOMContentLoaded` like the drawer: it sits above the fold, and installed
from an event it would appear after the first frame and push the page down in
front of the reader.

### Fixed along the way

The minifier used to collapse the blank lines inside `<pre>`, so the MIT licence
text on `/license` rendered as one run-on paragraph. `<pre>` and `<textarea>`
now pass through untouched, and the page is 43 B larger and correct.

`/alarms` was the only one of the eight pages whose palette lacked
`color-scheme: dark`, and it redefined `--ok` to the value `/style.css` already
had. Drift, not intent; it now uses the shared palette.

### Flash

    release headroom ... 18 740 B -> 43 676 B   (2.33x)
    test headroom ...... 22 044 B -> 46 916 B

Measured with `arm-none-eabi-size`, both environments, before and after, by
building each tree.

## v2.2.8-beta (2026-08-17)

### The whole web interface is back inside the firmware

Two pages — `/license` and `/alarms` — had been streaming from LittleFS since
August, which meant a device only served them if someone had uploaded the files
first. They are linked into the image again, and so is every other page. A unit
you flash now answers all twelve routes with nothing else deployed, and the
"Page asset missing" notice cannot be produced at all: the function that emits
it is not in any shipping binary.

The pages left in the first place because `FS_PAGES` was a **global** constant
in `tools/build_webui_gz.py`. One page layout for every environment. The
environment out of flash was `pico_w_test`, which carries the full serial CLI —
but a page listed there left every image, so the release paid for a problem
that was not its own. It paid twice: the pages went, and `data/web/*.gz` became
a deploy step no release package performed, which is the defect v2.2.6-beta had
to fix after the fact.

Measured with `arm-none-eabi-size` — the PlatformIO percentage omits `.ota` and
`.partition` — with all twelve assets embedded:

    release, 10 embedded + 2 on LittleFS (before) ...... 30 892 B free
    release, all 12 embedded .......................... 18 740 B free
    test, all 12, no lever ............................ overflowed by 856 B
    test, all 12 + parseIntStrict ..................... 6 604 B free
    test, all 12 + parseIntStrict + SIMUT_MDNS=0 ...... 21 980 B free

The release never needed the diet. The instrumented image ends up with nearly
twice the headroom it had while carrying more pages than before.

### The page diet became a property of the environment

`custom_fs_pages` in `platformio.ini` names the pages an environment serves
from LittleFS. Declare one in a shipping environment and the build fails,
naming it. Only the eight routes that go through `serveProtectedPage` are
eligible: `/login` and `/force_chpass` lock the device out if their file is
missing, and `/style.css` and `/lang.js` have no filesystem route at all.

The generator now emits the route beside the asset — a `<PAGE>_SERVE` macro
bound to `serveProtectedPage` or `serveProtectedFsPage` according to the
layout — so a page changing partitions no longer needs an edit in the handler.
Those were two places that could disagree in silence.

### Two levers, both charged to the test image alone

`SIMUT_MDNS=0` returns **15 376 B**. The knob had been repaired earlier and
never spent; no test suite resolves `SIMUT.local`, they find the board over USB
serial and talk to it by IP. The release keeps mDNS.

The last `sscanf` in the tree returns **7 524 B**. It parsed one CLI argument
and pulled in the whole scanf machinery for it; it now uses the project's own
`parseIntStrict`, which the native suite already covers. The handler lives
inside `#if SIMUT_CLI_FULL`, so the release never linked it. Behaviour is
unchanged for every input the tokenizer can produce — trailing junk like
`1,5x`, which `sscanf` accepted, now gets the usage line.

### Also in this release

The generator's "already up to date" shortcut read line 1 while the hash was
written on line 2, so it had never once matched and every build regenerated the
header. It works now, and the stamp covers the source, the generator and the
page layout — without the last part, building two environments in a row would
hand the second one the first one's header.

`web_test_suite.py` now fails a route that answers "Page asset missing". That
reply comes back as HTTP 200 with a body long enough to pass a length check, so
neither of the existing assertions could see it.

Upgrading: nothing to deploy. If your device has `/web/alarms.html.gz` or
`/web/license.html.gz` from an earlier version, they are dead weight now —
deleting them through the Files page frees 12.8 KB of the partition and is also
the cleanest way to prove the pages are being served from flash.

## v2.2.7-beta (2026-08-17)

### A translation can go missing without anything looking broken

Both language packs were audited entry by entry against the sources that
define them: the `LangKey` enum for `@DICT`, `tools/logcodes.tsv` for
`@LOGCODES`, every `TRL()` literal under `src/` for `@TRL`, and every
`data-i18n` / `t()` consumer for `@WEBDICT` — in the `WebUI.h` bundle **and**
in the two pages that moved to LittleFS, which read the same dictionary.

`@TRL` is keyed by an FNV-1a hash of the **English** string. Editing that
English orphans the translation: the lookup misses, the line comes out in
English, and nothing is logged. Three entries were carrying dead text that way
and one live literal had no entry at all.

`@WEBDICT` was missing five keys the browser actually asks for, among them the
TLS-without-certificate-validation warning and the whole one-time-password
dialog — security-relevant text that silently fell back to English. Eighteen
other keys had no consumer anywhere in the repository and were spending bytes
against a ceiling `es-ES` sits 4% under.

Content, not only coverage: `es-ES` said *Histórico*, *Reter*, *resetada* —
Portuguese leaking into Spanish, where the same packs elsewhere use
*Historial*, *Retener*, *restablecida*. *Fallo la subida* and *Fallo la
validación* wanted the verb, *Falló*. Spanish exclamations were missing their
opening mark. In `pt-BR`: *indisponivel*, *conexao*, *Pre-carga*, an untranslated
*Apply falhou*, and a `Colisão de flash ocupada` whose Spanish row already had
the right preposition. The `pt-BR` `@LICENSE` had been left half-accented by
the historical generator, down to *E concedida* where the verb *É concedida*
belongs — invisible on the TFT, which folds that screen to ASCII, but it is the
text that ships.

    language_pt-BR.lng   30 405 B -> 30 000 B
    language_es-ES.lng   31 770 B -> 31 382 B   (95% of the 32 768 B ceiling)

Four `@DICT` slots read as untranslated in `es-ES` and were left alone on
purpose: nothing has drawn `TR_TEMP_MIN/MAX` or `TR_HUM_MIN/MAX` since alarm
rows moved to `channelLabel() + MIN/MAX`. They stay because `@DICT` is
positional — removing a line shifts every key after it.

### The boot terminal drew Latin-1 through a CP437 font

The boot log box calls `setFont(NULL)` — the built-in 5x7 GFX font — but prints
`tr()`, which has returned Latin-1 ever since the TFT fonts started carrying
accents. Those two do not meet: in `glcdfont.c`, byte `0xE7` is a tau, `0xE9` a
theta, `0xE3` a pi. **Eighteen boot lines across the two packs** rendered as
maths symbols. The fold to ASCII that `drawSettingsLicense` has always applied
for this same font is now applied here too.

The fold also has to precede the padding. An accent is two bytes in and one
out, so measuring the pad on the pre-fold length left the tail of a longer
previous line on screen — a second, quieter defect in the same five lines.

`unaccent()` gained the Spanish opening marks while it was there. They used to
fall through to the `'?'` default, turning *¡Sistema Listo!* into *?Sistema
Listo!*; dropping them reads correctly, and the closing mark already carries the
sentence type.

### The build now gates the sections a line count cannot see

`check_lang_packs.py` counted `@DICT` lines, which catches the failure that
turns the whole UI English. It saw nothing of `@TRL` or `@WEBDICT`, and those
rot one string at a time — which is exactly how the gaps above accumulated. A
missing entry now fails the build naming the `file:line` of the `TRL()` call or
the consumer asking for the key; an orphan only warns, since it costs bytes but
renders nothing wrong.

Upgrading: upload the revised `data/lang/language_pt-BR.lng` (or `es-ES`) to
`/lang/` through the Files page or `POST /api/upload` with `uploadDir=/lang`,
then reboot — a pack is only read at boot. Never `uploadfs`; it reformats the
partition.

## v2.2.6-beta (2026-08-16)

### The test build had 152 bytes left, and no release zip carried its pages

The security work in v2.2.5-beta consumed the 3.5 KB that moving the licence
page to LittleFS had returned earlier the same day. Real headroom on
`pico_w_test` was down to **152 bytes** — the next test written for that
environment would not have linked, and the symptom is an `overflowed by N
bytes` that does not move when you shrink an asset, because `.rodata` is page
aligned and absorbs small changes in steps. The percentage PlatformIO prints
says 98.8% and hides all of this; it leaves out the `.ota` section and the
alignment padding.

The alarms page (8507 B gzipped) moves to the filesystem. It is the only large
page that passes all three parts of the test — big, rarely opened, and not
needed to bring the device up. A unit missing the file still samples, logs and
fires the alarms it already has; it just cannot edit them from the web. The
history page is four times bigger but is the hottest page there is, the config
page broke the bootstrap when this was tried in July, and the files page is
circular, being how these assets get uploaded in the first place.

    real headroom   pico_w_test   152 B -> 8 640 B
                    release    19 740 B -> 28 228 B

**A deploy bug shipped with the previous release, and this found it.**
`data/web/` was gitignored and no release script copied it, so no zip had ever
carried `license.html.gz` — while the comment in `build_webui_gz.py` claimed
they all did. Built from the v2.2.5-beta pio zip, `/license` answers "Page
asset missing". The ignore rule now excludes the contents and negates the
pages, the way `data/themes` already did — git does not descend into an
excluded directory, so a negation inside one never matches — and the pio zip
carries `data/web/*.html.gz`. The Arduino zips carry no `data/` by design.

Upgrading: upload `data/web/alarms.html.gz` to `/web/` alongside
`license.html.gz`, through the Files page or `POST /api/upload`. Never
`uploadfs` — it reformats the partition.

## v2.2.5-beta (2026-08-16)

### Every permission is checked where the payload is parsed

An audit against `docs/diretrizes_seguranca_vibecoding.md` found a two-request
path from a low-privilege account to full admin, needing no exploit at all.
`/api/commit_all` asked for `PERM_SYS_CONFIG` once and then parsed whatever
sections the body carried — `users.actions` and `net` among them — so an
operator who could edit a threshold could also mint an administrator. On the
bench a `sectest` account with `perms=8` created an admin with `perms=1023`
and got HTTP 200 for it.

Authorization now happens per section: `WebCommitSections.h` maps each section
to the bit it requires, and every parser reads the offset the gate recorded
rather than searching the body again, so a gate and a parser cannot drift
apart. The scan is deliberately **flat** — `indexOf` over the whole body, the
same shape the parser uses — because a nesting-aware gate in front of a parser
that is not would wave `{"sys":{"users":{...}}}` straight through. Same
account, same request, 200 became **403 section=users** and **403 section=net**
while its own `sys` section still returns 200.

The second half of that path was the temporary password. A new account was
born `*PENDING*` and `getDynamicExpectedHash` made its password
`Nome@DDMMYYYY` — public by construction. Accounts now get 8 random characters
from an unambiguous alphabet (~40 bits) with a random salt and
`mustChangePassword`, returned exactly once in the `commit_all` response for
the admin to hand over. The `*PENDING*` login branch is gone, so accounts left
behind by older builds simply stop logging in and must be reset — which is the
intended outcome, not a regression.

Two more findings from the same audit: `/download` served `/config/*` to any
account holding `PERM_FILE_READ`, handing out `system.bin` — it now refuses
secret paths through `FsSecretPath.h`, and `/api/backup` requires
`PERM_FULL_ADMIN` to match `/api/restore`. And `/api/mkdir` filtered `..` by
deleting the substring, which is not a filter; directory names go through an
allowlist, while the file manager and user list escape what they render and
navigate through a delegated `data-nav` handler instead of interpolating names
into `onclick`.

### The web UI can speak TLS, and the server judges the password

With `/config/web_cert.pem` and its key present the web server runs HTTPS and
the session cookie's `Secure` flag finally matches the real transport; without
them it stays on HTTP rather than locking the operator out. This is
release-only — BearSSL's server side costs about 20 KB and the test build has
no room. Over TLS the browser sends the password itself and the server applies
the policy (8 characters, letter and digit) before hashing, closing the gap
where a client that skipped the JavaScript could set anything. The stored hash
format is unchanged.

### Telemetry admits when it is not authenticating anything

A missing `/cert.pem` made the TLS client fall back to `setInsecure()` — still
encrypted, authenticating nobody — behind a warning whose message was empty and
read like a file error. It now logs `TEL_CERT_READ_ERR ctx=1` once per boot
saying MITM is possible, exposes `t_cert` in `/api/config`, and the config page
carries a badge when the transport is secure but unverified.

The MQTTS client also never got the buffer cap the HTTPS path received back in
v1.5.3-beta. BearSSL asks for 16 KB contiguous; the first connection took it
and every reconnect afterwards found a 9.5 KB largest free block. Measured with
the same backlog and TLS as the only variable, MQTTS froze at 39234 pending
records with `telSent` stuck at 1 while plain MQTT drained to `telSent` 72. The
broker completed every handshake it saw — it was never at fault.

### Secrets leave the repository, and a gate keeps them out

A device backup carrying the real Wi-Fi password had been committed and public
since the bench toolkit landed. The password is rotated, the file and the
password are purged from the history, and the bench scripts read
`SIMUT_WIFI_SSID` / `SIMUT_WIFI_PASS` from the environment. The whole
`scratchpad/` tree — 1267 files, including a TLS private key — is untracked;
`.gitignore` never desugared what was already committed.

`tools/scan_secrets.sh` now runs before either release script packages
anything, and refuses both shapes of the leak: the file type and the literal
in the source. It also audits the finished zips, because those are built from
the working tree — an untracked secret in `tools/` would ride into an archive
that a git-only check never looks at. Credentials that are published knowingly
live in `tools/.secretscan-allow`.

### The log records transitions, not heartbeats

Routine events are persisted when the state actually changes rather than on
every pass, so silence in the log now means "nothing changed" instead of
"nothing was written". Reading it means reading transitions: the cadence of
routine records is no longer the sampling rate.

## v2.2.4-beta (2026-08-16)

### The snapshot says which clock stamped it

The seed ceiling bounded a `.wip` to one block span past the day it belonged
to, and that day came from the newest **sealed** file's name. But a `.wip` is
by definition newer than everything sealed, so the two only agree once the
current day has sealed a block. Until then the window is yesterday's — or
older, if the device was off for a while — and a perfectly good snapshot was
refused:

- powered at 03:00 after a night off, restarted before its first 60 records
  seal: the `.wip` reads 03:00 and the window stops at 01:00;
- the block that crossed midnight starts in yesterday, so once today's file
  exists it fails the window's floor instead.

Neither is distinguishable, from `t0` alone, from the 2026-08-14 snapshot that
really was stamped into the future — what separates them is not the value of
`t0` but where it came from, and that is knowable only while the snapshot is
being written. So it is recorded there. `H5_FLAG_CLOCK_SYNCED` (bit 2 of the
chunk flags, previously unused) marks a block whose timestamps came from real
time rather than the provisional clock. The seed skips the day window for a
snapshot carrying it, and applies the old gate — unchanged — to one without.

The bit is inside the CRC's first span, so it cannot be forged onto a block
without invalidating it. Storage learns the answer through a callback instead
of reaching for the network, and an unset callback reads as "provisional", so
a build that forgets to wire it gets the strict gate rather than a free pass.
Readers ignore flag bits they do not know, so `H5_VERSION` is unchanged.

One bound went the other way and was removed: the record walk briefly rejected
anything past `t0 + 60 × interval`. Blocks close by **count**, not by clock, so
one that lived through a sensor outage spans far more wall time than its count
suggests — the bound would have dropped good records, and the payload is
already inside the CRC.

Measured on the bench with the day file removed so the window is stale, a hard
reset, and a `.wip` stamped 7 h 47 min past the old ceiling: with the bit the
seed is accepted and NTP corrects −40 s at INFO; with the bit cleared and the
CRC re-sealed, the seed is refused and NTP corrects 31757 s at WARN. Same
stamp, same payload, same window — only the provenance differs.

## v2.2.3-beta (2026-08-16)

### The boot knows what a day is before it asks

The graph showed nothing between 00:00 and 00:46 on 16 August. The 46
measurements were never lost: they sat in `20260816.h5` stamped `04:19:07`,
exactly 15546 s ahead of where they belonged — the size of an NTP correction
that had been applied to data that was already right.

`getLastRecordedTimestamp( )` turns the newest file's name into a day window
with `mktime( )` and judges local timestamps against it. `mktime( )` reads the
process timezone, and the only `applyTimezone( )` on the boot path sat inside
`NetworkManager::begin( )`, about 130 lines after the seed ran. So the window
was built in UTC against −03 data and came out three hours early. Everything
recorded after 21:00 fell outside its own day: four evening blocks were
discarded from the seed, the `.wip` that had crossed midnight cleared
`h5SeedCeiling`, and the provisional clock started 4 h 20 min behind. NTP then
measured the gap and `shiftHistoryTimeV5` carried the midnight block along with
the correction.

The zone is now applied before the seed runs, which is the whole fix. Any
reboot between 21:00 and midnight was exposed to this, not only one at the day
boundary.

Two changes come with it. The name-to-window step moves into
`h5DayWindowFromName( )`, beside the gates that consume it — the test suite had
been passing `dayStart`/`dayEnd` in as literals, exercising the gates and never
the step that derives them, which is the step that was wrong. And the block a
boot inherits from the previous session is named outright: `shiftHistoryTimeV5`
bounds itself with the provisional base, which is only as sound as the seed
behind it, so `_h5AdoptedT0` excludes the just-adopted block without consulting
a clock under suspicion.

Measured on the bench with the same `.wip` and a hard reset on both images: the
old one refused the seed, warned at 18545 s and rewrote the adopted block's
`t0` from `00:49:59` to `05:59:04`; this one accepts the seed, corrects 2047 s
at INFO, and moves no block at all.

## v2.2.2-beta (2026-08-15)

### The block that crosses midnight is read again

A block is filed under the day its **first** record belongs to, so one still
open at 00:00 goes on collecting records into the previous day's file. Both
history walks — the telemetry sender and its pending counter — cut the file
list at the cursor's own day, so the moment the cursor crossed midnight that
file stopped being opened. Everything the block took after 00:00 was stranded:
not missing from flash, not older than the cursor, simply in a file nobody
looks at again.

Worse than a gap in the data, the sender **stalled** there. With nothing left
to read in the files it was still willing to open, the cursor stopped advancing.

The floor is now one block span behind the cursor rather than the cursor's day
(`h5ScanFloor`, alongside `h5SeedCeiling`, which bounds the same thing from the
other side). A block cannot reach further past its own start than the records
it can hold, so yesterday's file stays in the scan for exactly as long as it can
matter — an hour at the default interval, and not a minute of the rest of the
day. The pending counter gets the same floor, because a count that agrees with
the bug hides it instead of showing it.

Measured on hardware with a block straddling the boundary and holding 32
records past it, batch size small enough that the cursor crosses while the block
is still half-read:

| | before | after |
|---|---|---|
| post-midnight records delivered | **0 of 32** | **32 of 32** |
| records drained before stalling | 10 | 829 |

This was found by the hardware validation written for the v2.2.1-beta clock
work, which is the argument for having written it.

## v2.2.1-beta (2026-08-15)

### The history hole that was never a hole

A device recorded normally through the night of 14 August and the graph showed
nothing between 21:32 and 02:15. The measurements were on flash the whole time,
stamped 4 h 43 min into the future, and part of the gap was the reader throwing
away good data on top of that. Both halves are fixed.

**The clock seed.** Before NTP arrives the clock is seeded from the open block
snapshot in `/history/.wip`, and that snapshot was trusted twice over: `t0` was
read straight out of the header, ahead of the CRC that certifies it, and any
epoch up to a full day past the file's own day was accepted. Everything written
before the sync inherits that value. The window is now one block span past
midnight — derived from the sampling interval, since blocks close by count and
not by clock, so an hour at the default rate instead of a day — and the seed
moves only after the decoder has verified the block. A forged `t0` now fails the
CRC instead of reaching the clock.

Measured on the same board that had the incident, planting a snapshot the old
rule accepted: without the fix the boot's NTP correction saturates its field at
18.5 hours, with it, 77 seconds.

**An NTP correction larger than an hour now logs at WARN.** It is still applied
— refusing one would leave timestamps wrong for good after a long outage — but
it is no longer invisible, and it measures how far the clock had drifted while
the records were being written.

**The graph reader.** Series assembly dropped any record not newer than the
running maximum. The guard existed to remove duplicates, but it cannot tell a
duplicate from a record that arrived out of order, and file order is write
order. One block stamped ahead therefore hid every block behind it: 65 of 205
records on that file, including 31 whose timestamps were correct all along.
Series are now sorted and collapsed on equal instants after assembly.

**Block scanning.** `HistoryV5Scan::seek` assumed time order, which is a claim
about the writer's clock rather than a property of the format. Out of order,
more than one block straddles the cutoff and only the last was kept, skipping
records that were on flash. It now verifies the assumption during the header
walk it already performs and refuses to skip anything in a file that fails it.
Ordered files get the same answer, and the same fast path, as before.

**Telemetry cursor.** The cursor advanced to the last element of the batch,
documented as the high-water mark — true only while records ascend. A block
stamped hours ahead buried every correctly stamped record behind it,
permanently and silently. The cursor is now the maximum over what the transport
actually carried, clamped to the present.

### Themeable alarm, caution and selection chrome

The palette described 17 roles and the display drew more. Alarm fills, the
caution button, the slot-selection background and the graph's date stamps were
hardcoded, so a custom theme could restyle everything a user looks at and still
flash a stock red panel over it. Seven roles close the gap, bringing the palette
to 24; files carrying only the old 17 still load, with missing keys falling back
to stock. Icon highlights now derive from the colour underneath instead of a
fixed light blue.

Eleven ready-made palettes ship in `data/themes/`, each audited for contrast
against the backgrounds it actually renders on.

### Known, and deliberately not claimed as fixed

The seed ceiling narrows the blast radius from a day to an hour; it does not
make a bad seed impossible, and a stamp less than one block span past midnight
remains indistinguishable from a block that legitimately crossed it.

The telemetry clamp covers a stamp that is in the future when it is sent, which
is the live failure. A stamp ahead of its neighbours but already in the past
still advances the cursor over older unsent records — closing that needs the
cursor to become a scan position rather than an instant.

Hardware validation surfaced a separate defect that is not addressed here: a
block straddling midnight lives in the previous day's file, and once the
telemetry cursor crosses 00:00 the file selection stops looking back at it, so
that block is never sent.

## v2.2.0-beta (2026-08-15)

### Web interface visual overhaul

Both themes redesigned around measured WCAG contrast, verified on real
hardware with a 36-screenshot sweep (9 pages × light/dark × 1366 px/390 px)
before and after. Total flash cost: +860 B of gzipped UI.

**Dark theme** — surfaces finally separate: cool blue-tinted background
(`#0c0f13`), cards (`#161b22`) with a border that does its job (1.36:1
against the card, up from 1.19:1), inputs sit shallow (`--bg`) instead of
pure-black pits, and the top bar is distinct from the page.

**Light theme** — white cards on a cool paper background, and the accent
becomes `#0072CD`: same SIMUT hue, but 4.9:1 against white (the previous
`#0096FF` measured 3.1:1 and failed WCAG AA on links and buttons). Fixed
five dark-theme leftovers that never got light styles:

- Chart grid was hard-coded near-black (13.8:1 on white — the strongest
  element on the page) and axis labels washed out at 2.4:1; both now read
  the theme tokens (`--border`/`--sub`) at render time.
- Toggles in the ON state lost their accent color to a specificity
  conflict — ON and OFF rendered identically.
- The event-log search field kept its pure-black background inside a
  light card.
- RAM/storage bar troughs stayed dark (`#3f3f46`).
- Status greens/ambers measured ~2:1 on light cards; new `--ok`/`--warn`
  tokens use 700-grade tones (≥4.9:1).

**Login and forced password change** now follow the saved theme — they
are pre-session pages that never loaded the theme engine, so a light-mode
user used to get a black login screen before a white app.

**History charts** — the 11-color temperature palette had four
near-identical reds and two yellows that vanished on white; now 6 clearly
distinct warm tones that hold ≥3:1 on both grounds. Pressure trades the
light-only lilac for a violet that works on both. Grid and ticks follow
the active theme.

**Consistency** — sensor names use the same text color everywhere (they
were gray on the dashboard, cyan on alarms); the dashboard TYPE column's
amber/green coloring — which referenced tokens that never existed — now
actually renders; INF/WRN/ERR log filters fit one row on phones; a shared
`.b-pri` primary-button style; global `:focus-visible` outline for
keyboard navigation.

## v2.1.10 (2026-08-14)

### The 2.1 line goes stable

The same firmware that shipped as v2.1.10-beta — the only bytes that
change are the version string. Eleven beta releases between 2026-08-10
and 2026-08-14 took the 2.1 line from the first web-reboot sweep to a
display that survives its own alignment offset; every one of them passed
the same four release gates before publishing (both firmware builds,
34/34 native history tests, 20 000-case codec parity, 200 000-trial
selftest) and was flashed and verified on real hardware. This release
promotes that firmware to stable and becomes the recommended image.

### Documentation overhaul

- README rewritten in all three languages, with fresh screenshots of the
  2.1.10 interface — the previous set predated the v2.1.5 visual overhaul.
- User manual (pt-BR) recaptured and revised for 2.1.10: the fingertip
  password keyboard, the bucketed history graphs, the pressure series and
  the screen-alignment editor are documented with real screens.
- English manual rewritten to match the current firmware (it still
  described v1.6.3).
- Wiring guide verified against the pin map in the source.
- Repository root decluttered: engineering analyses moved under `docs/`.

## v2.1.10-beta (2026-08-14)

### The alignment offset can no longer crop the screen

SIMUT lets you shift the whole image by up to 4 pixels on each axis
(Settings → Screen alignment) to compensate for panels whose visible
window sits slightly off the pixel matrix. But several elements were
drawn closer than 4 px to an edge, so the extreme settings shaved
pixels off them — and one of them, the thin "working…" hint painted
while a graph loads, lived entirely in the top 3 rows: a −4 vertical
offset removed the only feedback that a tap had landed.

Every renderer was swept against one rule: content lives inside
x 4..315, y 4..235 — exactly the rectangle the alignment screen's
green frame draws. What moved: the graph busy hint (now over the
header), the graph period buttons (spanned 2..317, now 4..315), the
graph's Y-axis labels, hPa right-axis labels, last-value marker and
full-bleed header card, the dashboard's "SIMUT" brand and the web-busy
banner (now an inset chip that truncates long usernames by measurement
instead of clipping mid-glyph), the password keyboard's key grid
(reached x=317 and y=237; keys are now 74 px wide and the bottom row
ends at y=235), the system-status title bar, and the touch-sensitivity
threshold readout.

Where a screen and its touch handler used to keep separate copies of
the same geometry, the numbers were promoted to shared constants —
`GRAPH_PBTN_*` for the graph footer, alongside the keyboard's existing
shared header — so the drawn buttons and their hit zones cannot drift
apart again.

Validated on hardware: 19 framebuffer captures across every screen,
with the 4 px border verified 100 % background on all of them, and the
offset exercised live to its extremes and restored.

## v2.1.9-beta (2026-08-14)

### A password keyboard for fingertips

The password-change screen used to ask for surgery: 30-pixel keys — 5.4 mm
on the 2.8" panel — three layers hidden behind Shift and 123, and a row of
arrow buttons at the bottom as the official workaround, about five taps to
land one character. The new keyboard is eight group keys of 76×54 px
(13.7×9.7 mm): tap `pqrs` and a popup opens with `p q r s` above
`P Q R S` — both cases at once, 68×56 px each — tap the one you meant.
No Shift, no layers, no cursor to steer. `123` and `@#!` open the same
kind of popup for the ten digits and all 28 symbols; space and backspace
act directly; OK sits beside the password boxes; tapping outside a popup
cancels it. Every character of the same 91-character set now costs exactly
two taps on fingertip-sized targets, and the 4–7 character
type-then-confirm flow, its messages and its masking toggle are unchanged.

One header now owns the geometry and character tables for both the
renderer and the touch mapper — the old screen kept three hand-synced
copies of its layer tables — and the screen composes through the 6-strip
full-screen renderer instead of five hand-placed partial blits. The
rewrite returns ~2.5 KB of flash: the release image got smaller, and the
`pico_w_test` environment, which was 224 bytes from the ceiling, links
again with 2.7 KB of real headroom.

### OTA revalidated on the new image

Two stage+apply cycles on the bench with this firmware, verdict read back
as the version string (never inferred from timing or HTTP codes):
1 001 964 B staged in 30.7 s each, apply accepted on the first try both
times, distinct CRCs per image. The "Display in use" 503 seen on 08-13
did not reappear. Cycles ran on :8080, working around the bench router's
port-80 RST injection documented in v2.1.7-beta.

## v2.1.8-beta (2026-08-13)

### The web history graphs read the archive itself

The `.h5` files were always complete; the graph was not. The page asked
`/api/history_multi` for a pre-shrunk JSON, and the shrinking lied twice: the
decode path emitted one record in N (peaks survived by luck), and past a size
threshold the block-envelope path emitted the block minimum at t0 and the
block maximum at t0+30 min **as one series** — drawn as a line, that is a
sawtooth the sensor never produced, and a single freezer defrost renders as
two peaks with a valley between them. Worse, the threshold was estimated from
the bytes of the day files it would walk, not the requested window, so a
one-hour view anchored in the past arrived with **3 points** (6 h: 13; 24 h:
51) and the behavior changed with the time of day.

The page now downloads the day files themselves through `/download` — the
same road the CSV export already drove — decodes them in the browser with the
`h5Decode` it already had, and reduces for the screen with per-pixel-column
buckets that keep **min, max and mean**: a band behind a mean line. A
one-minute spike survives any window because the extreme IS the bucket edge;
an empty bucket is a null the chart draws as a real gap; a lone sample
between two gaps gets a visible dot; and the newest record always lands with
its own timestamp. Closed day files are cached by (name, size), so switching
ranges or sensors after the first load fetches nothing; the current day and
the open hour (`/api/history/open`) are always refreshed, tail last so a
mid-load seal can cost at most a gap, never a duplicate. Extremes badges are
computed from every record in the window during the same pass, and the CSV
export reuses the byte cache instead of re-downloading.

Measured on the bench against 64 days of ground-truth synthetic data: 1 h
3→60 points, 6 h 13→360, 24 h 51→1 398 (full resolution), 7 d 339→885, and
the day the device spent 6.5 h powered off finally shows a hole instead of a
bridge. Bonus robustness: each file is a short request, immune to the
router-injected RST that used to kill the single 500 KB response. The
firmware side of `/api/history_multi` is untouched and still serves tools.

### The TFT graphs get time buckets and an honest envelope

Same disease, native renderer: stride decimation fixed per range (1 in 51 on
the 7-day view) tuned for a one-minute cadence, X spaced by index rather than
time, and a Y axis scaled by the TRUE extremes over a curve that had lost
them — the axis announced −6.5 °C the line never reached, and identical
freezer defrosts drew at random heights, some missing entirely. A 6.5-hour
outage compressed into one invisible index step, and on a full 7-day window
the 200-point cap silently cut the open-hour tail, leaving the right edge
stale.

The loader now aggregates into buckets uniform in TIME
(`clamp(window/logging-interval, 40, 200)`), each carrying min/max/mean —
which makes the renderer's index-spaced X time-proportional for free, turns
empty buckets into gaps with their true width, and never overflows the cap.
The renderer paints the min/max band behind the 2-px mean line (replacing the
fill-to-baseline), gives lone buckets a 3×3 dot, and sits the peak markers on
the band edge of the bucket that holds the real extreme — marker, axis label
and badge finally agree. Detail-screen statistics (AVG/STDDEV/Δ and the n=
count) are now computed over every record in the window: n= on a 24 h view
went from 180 to 1 435. Cost: ~13 KB of static RAM (41.6% → 47.0%) and under
1 KB of flash.

### Bench and build notes

`pico_w_test` had been living 224 bytes from the flash ceiling and the new
web JS pushed it over; the env now builds with `-DNDEBUG` (the documented
~6.6 KB lever) and `-DSIMUT_LICENSE_STUB` (the license screen shows a short
pointer; the release image always carries the full MIT text). A real diet —
migrating pages to LittleFS via FS_PAGES — remains future work. Known
limitation, pre-existing: the web graph page loads Chart.js from a CDN, so
browser graphs need internet even though every byte of data now comes from
the device.


## v2.1.7-beta (2026-08-13)

### Pressure joins the history graphs

The graph reader only resolved temperature and humidity, so the one sensor
that exists to measure pressure (BMP280) plotted temperature alone, and its
pressure had no screen anywhere on the TFT. The pressure channel is now read
from both the day files and the hour still open in RAM: on a
pressure-without-humidity part it takes the plot's second curve and right
axis (hPa, one decimal, wearing the same color pressure has on the
dashboard), and on a BME280 — where humidity keeps the curve — it still gets
its own metrics page. Tapping the center of the detail screen cycles
temperature → humidity → pressure → back to the graph.

### The metrics screen becomes an instrument table

The four MAX/MIN/AVG/STDDEV cards gave way to full-width instrument rows
under a section strip that names the channel and its unit — "Pressure (hPa)"
— and shows page dots for the tap-to-cycle pages. Each row carries a
semantically colored icon (hot MAX, cold MIN), a value on one shared decimal
edge, and a right column with the **full dd/mm/yy hh:mm stamp** of the
extreme, the **window delta** with a trend triangle on the AVG row, and the
sample count on the STDDEV row. The layout is measured at runtime from the
actual glyphs, so any language or unit keeps its clearances; the only label
that could not fit, English "AVERAGE", became "AVG".

Three repairs rode along: detail labels no longer corrupt under a loaded
`.lng` (they stored pointers into `tr()`'s 4-slot rotating scratch and the
humidity page's unit calls recycled them mid-render — English never showed
it); the plot's secondary-axis minimum no longer sticks at the 1000.0
sentinel for pressure (sea level sits above it, so the curve rendered
squeezed against the top); and the graph header interval now wears the
dashboard clock color and carries the two-digit year.

### Long uploads stop dying at the first hiccup

The web server's multipart reader kept the Stream default of one second of
patience per byte. With the receive window at 4×MSS (the v2.1.4 lwIP fix), a
~1 MB upload closes the window many times a second; when the reopening
segment is lost to a radio blind spot, the flow stalls until the peer
retransmits — and one second turned that recoverable stall into an aborted
upload at ~13–15 s, every time. The reader now waits 3 s, and the OTA
stage→apply cycle was re-validated end to end twice on the bench, version
read back from the serial console each time.

While chasing this, a second, environmental killer was isolated and is worth
knowing about: consumer APs with "flood protection" features can inject RSTs
into sustained port-80 flows toward the station at a fixed connection age,
regardless of rate — ICMP unaffected, device counters clean. If large
transfers die at a suspiciously constant ~13 s on your network, try moving
the SIMUT web port off 80 or relaxing the router's DoS protection.

## v2.1.6-beta (2026-08-12)

### Screens stop loading top-to-bottom

2.1.5 brought the DMA blit, but it only engaged for pushes exactly 320 px wide.
The dashboard cards (312 px), every menu row (285 px) and all the screen chrome
still went out through the library's ~2 µs/pixel path — and ten screens opened
with a ~150 ms per-pixel `fillScreen`. That combination is what read as the
screen "loading top to bottom". This release finishes the job:

- **Sub-width blits ride the DMA too.** Slices narrower than the canvas are
  compacted in place and pushed as one burst. A dashboard card drops from
  ~40 ms to ~12 ms per redraw (at the old SPI clock — see below for the new one).
- **Screen clears at wire speed.** A dedicated DMA solid-fill (non-incrementing
  source) replaces the per-pixel `fillScreen` on every screen entry, the
  dashboard background filler, and the license page bands.
- **Menu chrome through the canvas.** Title bars and footers are composed in
  the shared canvas and pushed full-width instead of drawn widget-by-widget on
  the panel. Redundant canvas clears in the strip renderer are gone, and the
  main/sounds menus repaint only the two rows whose selection changed.

### SPI at the silicon's ceiling

The write clock goes from 31.25 MHz to 62.5 MHz — the RP2040's PL022 divider
offers nothing in between. Both write paths (library and DMA) now share one
constant, `SIMUT_TFT_SPI_HZ` in `simut_config.h`, so they cannot drift apart.
Validated on real hardware by reading the panel's GRAM back over three
consecutive captures: every differing pixel sat in live top-bar content, none
in static regions. If your wiring shows artefacts at this speed, override the
constant to `31250000u` — everything else in this release stands on its own.

Combined effect, measured/derived on hardware: entering a settings screen went
from ~240 ms to **~50 ms**, a full dashboard redraw from 121 ms to **~35 ms**,
and the alarm flash costs a quarter of what it did per blink.

### The graph answers the instant you touch it

Opening a graph from any screen now always shows the loading screen (it paints
in ~45 ms, so it reads as a transition, not a blank). Zoom, pan and calendar
taps *inside* the graph deliberately keep the old plot on screen for context —
and light a thin accent line across the top edge the moment the tap lands, so
a flash read that takes a second never feels like a dead touch. The next
render covers the line.

### Also

- Removed a 140×40 off-screen canvas allocated on every boot and drawn into by
  nothing since 2.1.5 — **11.5 KB of heap returned** (free heap after boot on
  the bench went from 46.6 KB to 58.2 KB).
- `blitCanvas` has a documented contract now: it consumes the canvas; compose
  before every blit. Every existing caller already did.
- Corrected the strip-renderer docs (6×40 px strips, not 3×80) and stale wire
  timing comments.

## v2.1.5-beta (2026-08-12)

### The display gets one visual system

The 17 TFT screens grew one at a time, and it showed: three typefaces mixed (the
System Status page used the stretched 5x7 terminal font), the degree sign had three
different spellings ("o" in 9pt, a tiny classic-font "c", a literal "oC"), closing a
screen looked different on every screen that could be closed, pagination had four
idioms, and a handful of hardcoded RGB values ignored the theme system entirely.
This release replaces all of that with a shared widget layer (`UiWidgets.h`) that
every screen composes from — title bar with accent tab and page dots, two button
styles (the primary action is always bottom-right; exit/close is never primary), one
standard close button, one scrollbar, menu icons. **Every touch zone is untouched**:
the widgets draw on the same rectangles the touch handler already derives its hit
areas from.

### Real accents on the TFT

The language packs always carried UTF-8 ("Configurações" was in the `.lng` all
along) — the display transliterated it to ASCII at runtime because the 7-bit GFX
fonts had no accented glyphs. The 9pt and 12pt faces are now regenerated from the
same GNU FreeSansBold.ttf the stock fonts came from, with Latin-1 coverage subsetted
to ASCII + the 32 glyphs pt-BR/es-ES need (+3.4 KB of flash), and `tr()` maps UTF-8
to Latin-1 instead of stripping it. Portuguese and Spanish render accented on the
panel; the serial CLI keeps its 7-bit transliteration. The degree sign is now the
font's own glyph everywhere — including the channel-unit helper, so "°C" in the
alarm editor, the dashboard cards, the statistics and the status page all agree.

### The strip renderer goes out through DMA

Full-width canvas strips — the hot path of every screen — are pushed with the SPI
peripheral in 16-bit frame mode fed by a DMA channel (no byte swap, no bounce
buffer, synchronous by design so the quiesce/flash-pause protocol is untouched).
A full dashboard redraw measured on hardware went from **254 ms to 121 ms**. When a
display alignment offset pushes a strip off-panel, the old library path is used.

### Also

- History graph: subtle area fill under the temperature curve; toolbar buttons
  carry the standard border; zoom icons follow the theme accent.
- System Status: values right-aligned so Serial/SSID/MAC fit on one line; fixed two
  pre-existing leaks — the footer band was never cleared (the previous screen's
  buttons survived in the gaps) and the fixed-width unit reserve clipped "°C" off
  the right edge.
- Calendar: month navigation lives only in the bottom bar; "Mês" finally spelled
  with its accent, as are the other hardcoded PT literals (Atenção, serão).
- Password keyboard: OK/123 in the UI face, thicker space/confirm strokes.
- Net binary cost of the whole release: about +2.2 KB of flash; no new translation
  keys, so installed `.lng` packs stay valid byte for byte.

## v2.1.4-beta (2026-08-11)

### The hour still open in RAM reaches the graphs and the CSV

A V5 block is held in RAM and reaches its day file only when it seals, which at one
record a minute is once an hour. Everything that read `.h5` therefore trailed the
present by up to that hour: opening a chart — on the display or on the web — showed
nothing for the last few minutes, and a CSV export stopped at the last seal however
recent the window asked for. Telemetry had already been given a way past this when a
fresh device was found to stay silent for its first 60 minutes; the graphs and the
export never were.

The samples were never missing. They are held plain in the encoder, not bit-packed, so
reaching them costs a copy and no decode — and the `/history/.wip` snapshot beside them
is a power-cut bound, not a read path: boot adopts it into the day file and nothing else
opens it.

- **Display graph** (`renderGraphOptimized`) and **web graph** (`/api/history_multi`,
  both the decode and the envelope paths) now continue into the open block after the day
  files. Channels are resolved against the live schema rather than the reader's, because
  the open block is encoded with the sensor set in force now, not the one the newest file
  on flash was written with.
- The newest record is emitted **whatever the decimation says**. Without that, a 24 h
  range (step 8) would still leave the right edge up to eight minutes stale, and a range
  decimated 40:1 forty minutes — the right edge being current is the point.
- **CSV export**: the device serves the open block at `GET /api/history/open` as a
  standalone one-block V5 stream — a SCHEMA chunk followed by the block sealed PARTIAL,
  byte for byte what a `.h5` file looks like. The page fetches it after the day files and
  runs the decoder it already has, so there is no second format and no second decoder.
  Fetched last on purpose: a seal mid-export can then only cost a gap, never a duplicate
  row. An export whose window has no day file at all — a device in its first hour after a
  factory reset — now returns the open hour instead of "no data recovered".

`/api/history_multi` reports `"ram"` (records taken from the open block) and marks
`"path"` as `decode+ram` / `envelope+ram` when the tail contributed, which is the only
field that distinguishes a live answer from a stale one.

Measured on hardware: the graph's right edge went from up to an hour behind to **0 s**,
with the seam visible across a reboot (flash ends at 18:33:51, RAM carries 18:35 →
18:41). The open-block stream was decoded by `tools/history_v5.py` — the reference
implementation the native tests already use as an oracle — with **0 frame/CRC errors**
over a 9-record block whose 37-byte payload carries 64 values, and independently by the
page's own decoder, both agreeing value for value with the plain copies the JSON path
emits.

**Not covered:** `/api/export/history.bin` (the `.simx` bundle) still reads files only.
It is no longer the CSV button's path — the page downloads `.h5` and decodes locally —
but it remains reachable by URL and stops at the last seal.

## v2.1.3-beta (2026-08-11)

### Core 1 parks before a flash pause kills it — the display-storm wedge is gone

A flash write on Core 0 (a config save, a history record) pauses Core 1 first, so
the erase never runs with Core 1 fetching from XIP. That pause asked Core 1 to park
at the top of its render loop and waited only 200 ms for it — and it spun there
without feeding the watchdog. But a single render measured up to ~1 s under load, so
200 ms routinely expired mid-render: Core 1 was then hard-reset while holding a lock
(the render's state mutex, the allocator, a spinlock), and the next Core-0 flash-path
acquisition of that lock blocked forever with the watchdog unfed. On the bench that
rebooted as `C0=[CLI] C1=[DISPLAY]` under a save+touch+read storm — the same shape as
the `C0=[STORAGE_WR]` history-write reboot a user hit configuring the device — and in
the worst case escalated to a QSPI wedge: a dead hang that a power-cycle was the only
way out of.

The park window now covers a whole render (1200 ms) and feeds the watchdog while it
waits, so Core 1 reaches a lock-free point before the reset instead of dying mid-work.
Applied to both pause paths — the quiet-mode save and the IRQ-lockout history write.
Measured against the same storm: the **wedge is gone** (the device self-recovers
instead of hanging), watchdog reboots dropped roughly threefold, and no flash write ran
unpaused (`fx` stayed 0).

**Still open:** one residual `C0=[CLI]` reboot survives the storm — Core 1 occasionally
does not park even within 1200 ms. Closing it needs the per-instruction marker pass that
located the drain reboot; tracked for the next cycle. The everyday failure (a single
reboot that used to also lose or misfile data — both fixed in this line) no longer wedges.

## v2.1.2-beta (2026-08-11)

### A reboot no longer drags the just-recovered block 15 minutes into the future

A reboot mid-hour lost a quarter-hour of history to the wrong timestamps, not to
the writer. The `.wip` snapshot recovered the open block correctly, with its own
pre-reboot timestamps — and then the NTP correction on boot moved it. The chain:
`getLastRecordedTimestamp()` seeds the provisional clock from the newest record,
but it read only the sealed day file, never the `.wip`. So after a reboot mid-hour
it seeded from the last *sealed* block — up to an hour behind the real newest data
that was sitting in the `.wip`. NTP then measured that stale base as a large error
(measured on the bench: +919 s) and `shiftHistoryTimeV5()` shifts every block with
`t0 >= base` — which caught the block `recoverWipV5()` had just restored, already
correctly stamped, and pushed it forward by the whole error. 05:48–06:03 was filed
as 06:04–06:18; the 05:48 window read empty and the reader stopped at its end.

Fix: `getLastRecordedTimestamp()` now also reads the `.wip`, taking the newest of
the sealed file and the snapshot. The provisional clock lands close to real (the
shift error shrinks to seconds) and, decisively, the shift floor rises above the
recovered block's `t0`, so the block the reboot just restored is exempt and stays
exactly where its own timestamps put it. Verified on hardware: a partial block at
06:32–06:36 was snapshotted, the target hardware-reset, and the block came back at
06:32–06:36 unmoved, with the NTP correction down to −13 s (was +919 s) and the
sealed hourly blocks untouched. The reboot that triggered it — a watchdog stall in
the storage-write path — is a separate stability item still open.

### Changing the sensor selection mid-load now cancels the transfer and starts one clean load

The graph page fetches history in slices, and each loader (`fetchAndDraw`) was
`async` but uncoordinated: changing the sensor selection — or the range or date —
while a graph was still loading started a *second* slice loop without stopping the
first. Two loops then raced on the same progress bar and the shared abort handle,
and fired overlapping `/api/history_multi` requests at the device — the "confused
loading bar, several downloads at once" the user reported. That overlap is also
what exposed the drain reboot (D-B8c, below), so this fixes the appearance and
removes the trigger at the source.

`fetchAndDraw` is now a coordinator: it bumps a generation, aborts the transfer in
flight, and queues the new load behind it on a promise chain, so exactly one graph
transfer is ever live and the newest selection wins. A load superseded before it
starts is skipped; one superseded mid-fetch drops its result instead of drawing
over the newer one. Client-side only (`WebUI.h`); pairs with the firmware
null-guard so the device is safe even if some other client still overlaps.

### A slow POST body rebooted the device — the loss behind "reboots when I configure"

The measurements were being lost to a reboot, not to the writer. Chasing "lost
data when I restart or configure" on the bench turned up a live watchdog reboot
with the signature `C0=[WEB_POLL] hp=0 (219)` — `hp=0` meaning `handleClient()`
never returned, so the stall was inside it. The D-B8 fix bounded the request line
and headers with a watchdog-fed, wall-clock reader; the request **body** was left
on the stock reads, which feed nothing and are consumed during the parse, before
dispatch and auth. A POST whose body dribbles in holds Core 0 across the 8388 ms
window and reboots — on the exact path taken to save configuration.

Reproduced deterministically (`scratchpad/repro_post_slow.py`): `POST /api/save_sys`
at 1 s/byte took the device from uptime 2815 s to 31 s; `/api/upload` and
`/api/restore` did the same through the RAW upload loop.

Three unbounded body reads, all now under the same discipline as `simutReadLine`:
- **`plain`/urlencoded/json** (`readBytesWithTimeout`): feeds the watchdog while
  the body dribbles, and caps the whole read by wall clock
  (`SIMUT_BODY_BUDGET_MS = 15000`). Feeding alone would trade "reboot in 8 s" for
  Core 0 frozen for hours on a large declared `Content-Length`; the ceiling makes
  an overrun return partial and drop the client. A real config POST is a few KB in
  one segment under a millisecond, so the budget is only ever spent by a stall.
- **RAW upload** (`/api/upload`, `/api/restore`): a new `simutReadRaw` reads only
  what is already buffered — so `readBytes` cannot block, the way it did per-byte —
  feeds the watchdog while waiting, and gives up after a short no-data window. No
  whole-transfer cap: a firmware/file upload is long and flash-bound.
- **multipart** (`_uploadReadByte`, `_parseForm`): the byte wait now feeds the
  watchdog and the header-line reads use the bounded reader.

Same fifth framework override (`webserver_parse_deadline.patch`), regenerated so
`restore → patch → rebuild` reproduces the flashed `firmware.bin` byte for byte.
Validated on the bench: the reboot is gone on all three paths (0 new `hp=0 (219)`
in the boot capture), a fast legitimate upload still works, `/`, `/history`,
`/config` still serve whole, the request-line slowloris still drops, `fx=0`.
Both firmware environments build (release 93.8 %, test 98.5 %).

The reboot the user also reported while *reading graphs* is a **separate
mechanism**, and the user pinned its trigger: **changing the sensor selection
while a graph loads** ("several downloads at once, the progress bar confused").
Three autopsies over three builds tracked it down. `hp=740` said `handleClient()`
returned and the stall was in the drain after it; a first fix guessed the lwIP
entry and only moved the marker to `hp=603`; a second (the drain's `feedWatchdog`
light-yield) missed too. Per-instruction markers then named the exact statement:
`hp=6031` = `WiFiClient c = _server.client();`, the copy of the current client.

Root cause, proven from the framework: `_server.client()` returns
`*(ClientType*)_currentClient`, and `handleClient()` deletes `_currentClient` and
sets it null whenever the peer is no longer connected — which a sensor change
mid-load causes, by RSTing the in-flight graph and opening a fresh connection. But
`_drainPending`, latched true by that response's completed send, is still set, so
`drainOrDrop()` copies `*(ClientType*)nullptr`: the copy reads through a null
`this`, takes a garbage `ClientContext*` from ROM and `ref()`s it — a load to a
wild address that parks the bus until the watchdog fires. Only under overlapping
requests, a microsecond race no synthetic client hit (the drain path was exercised
~5000× across five repro styles without it). Fix: `drainOrDrop()` and
`dropAbortedStream()` take the pointer, not a copy — `&_server.client()` folds to
`_currentClient` with no dereference, so a retired (null) client is caught by a
guard instead of read through, and the per-drain WiFiClient copy is gone too.
Lesson recorded twice over: `hp` locates the position; the cure needs knowing
*what* runs there — reasoning "it's instant" was wrong on code that, with a null
client, was not.

### Measurements were lost on power cut and on every reboot for configuration

The open history block lives in RAM and only reaches flash when it fills, once
an hour. A snapshot in `/history/.wip` bounded the exposure — and the bound was
ten minutes, because that is literally what R8 asked for: *"power-loss: maximum
loss of 10 min of data"*. The requirement was met exactly as written, and what
was written was not good enough.

Three separate loss paths, found with very different costs:

**Six of the seven voluntary reboots snapshotted nothing.** `reload confirm` from
the CLI was the only path that did it right — it seals the block explicitly
before calling `safeReboot()`. The other six did not, and one of them is the web
`commit_all`: the reboot you take *to configure the device*. Those rebooted
straight through and dropped everything since the last periodic snapshot, up to
ten minutes, deterministically, every time. That the CLI path had the seal and
the web path did not is the shape of the bug — the protection was written per
call site, so it was only ever as complete as the next caller remembered to be.
Hence a hook at the choke point instead: `safeReboot()` itself now writes the
snapshot on the way out, and a new reboot path cannot forget.

Two callers must suppress it, and do: `system format confirm` and an OTA restore
apply with `fs_mod`, where a snapshot from the pre-erase RAM block would
resurrect data the user asked to destroy on the next boot.

**The ten-minute timer is gone.** The snapshot is now taken once per accepted
record, inline, so a power cut loses nothing. The cost was measured before the
choice rather than after: 1440 `.wip` rewrites a day against 144. Endurance is
not the binding constraint (~2.6k erases per block per year against 100k rated);
the Core 1 lockout duty cycle is, which is why the write still yields to touch
priority and to the heavy-task lock.

**Some minutes were never measured at all.** The loop gated the *entire sample*
on those same two conditions, so a gate held across the minute boundary left that
minute with no reading — a hole no snapshot can fill, because nothing was ever
recorded.

Only one of those two gates could ever fire, which is worth stating because an
earlier draft of this entry claimed both. `isUserInteracting()` is real: the touch
timestamp is set by Core 1 and read by the loop, so it can be true while the
sampling line runs. `isHeavyTaskLocked()` could not be: every holder —
`_webMgr->update()`, `_telemetryMgr->update()`, the graph via UI events — runs
earlier in the *same* Core 0 loop, strictly sequential with the sampling call, and
nothing on the Core 1 path takes the lock at all. Measured: the heavy lock held to
a 57% duty cycle for six minutes deferred exactly zero snapshots and skipped
exactly zero records. Removing that half of the gate is correct but changes
nothing observable; the touch half is the one that was losing readings. Sampling and writing are now separate: the
record always lands in the RAM encoder (a memcpy, safe under any gate) and only
the flash write defers, latched so the catch-up sweep writes it within 2 s of
the gate opening instead of waiting for the next sample to carry it.

One trade-off is deliberate and worth stating: sealing a full block, and the
day-rollover seal, now run even with a gate closed. A full block cannot accept
another record, so the choice there is a lockout window or a lost sample — 24
forced windows a day against the promise that none are lost.

Three silent losses found while auditing the same function, all three from a
`sealHourV5()` return value nobody read:

- **The hourly seal discarded the whole block on failure.** The `reset()` that
  follows it empties the encoder unconditionally, so a failed seal threw away up
  to 60 records with nothing said beyond a generic write warning. This is the
  seal that fires *every hour*, making it by far the likeliest of the three to
  ever fail. It now refuses the incoming record while the held block still has
  retries left, because the block is what is worth protecting.

- **A failed day-rollover seal misfiled the block.** The code adopted the new
  day regardless, so the next record was spliced into yesterday's block and the
  whole block was then written to *today's* file — §14-6 broken, and with it
  "the file name IS the bound". It refuses the one record instead now, leaving
  the block intact for the next minute to retry. One record at risk on an
  already-degraded filesystem beats up to 60 misfiled with no error anywhere.
- **A failed seal on a sensor-set change discarded up to 60 records in
  silence.** `ensureH5Schema()` immediately re-runs `_h5Enc.begin()`, which
  drops any block in progress. The `.wip` is no escape — it would carry the old
  schema and `recoverWipV5()` validates against the compiled one, so the next
  boot would reject it. Now retried once (which is what a transient mutex
  timeout needs) and, if it still fails, logged with the number of records lost
  instead of vanishing behind a generic write warning.

### A reboot still lost one reading, and the block had nothing to do with it

Reported from the bench after the above landed, and both halves were true. Across
a web `commit_all`: `STO_H5_WIP ctx=50` from the pre-reboot hook, the next boot
adopting `ctx=50`, the block intact — and 108 s between the last record before and
the first after, against a 60 s interval. One record missing from the sequence.

`_lastHistoryTime` starts at 0, so the interval check cannot fire until `millis()`
passes a full interval: the first record of every boot landed at `up=60s`, on top
of the ~20 s the boot itself takes. Preserving the block was never going to fix
that, because the minute was never sampled in the first place.

The first record now goes as soon as the clock can be trusted, gated on the **raw**
system clock — deliberately not `getEpoch()` and not `isTimeSynced()`. `getEpoch()`
seeds a provisional clock from `SIMUT_BUILD_EPOCH` (2025-09-20) and returns it,
which sits above `HIST_EPOCH_MIN`, so both report a good clock on a device that has
none, and the record would be filed two years in the past. A wrong timestamp
poisons the day file worse than a missing minute.

Measured on a real `reload confirm`: first record at `up=23s`, gap 41 s, zero
records missing; the 108 s case becomes 71 s, also zero. `up=23s` is near the
floor, since NTP lands around 20 s and that is when a timestamp becomes truthful.
Residual: dropping a record now needs a gap over 120 s, which takes a boot running
~37 s past the record's due time — a WiFi retry or DHCP timeout could still manage
it.

### Bounded recovery for a failed seal

Both directions of a failed seal are a loss, so the recovery is bounded rather
than chosen: discarding the block on the first failure throws away up to 60
records for what is usually a transient `FLASH_OP` mutex timeout, while holding
it forever means a device that silently stops recording for good. Five refused
records — one interval's worth of patience, well under the block being
protected — then the block is written off, the loss is logged with its count,
and recording resumes.

No on-disk format change: bytes written before this still read, and the `.wip`
is still exactly one `PARTIAL` DATA chunk. Amendment E10 in
`docs/HistoryV5_Emendas_Rev2.md`; R8, §7.1, §7.2 and the §11 acceptance matrix
restated in the normative Rev 2.0.

## v2.1.1-beta (2026-08-10)

### A single slow HTTP request rebooted the device — remotely, no auth

v2.1.0-beta shipped with `C0=[WEB_POLL]` listed as an open residual, described
as a heavy-concurrency problem. The soak caught it on the shipped image with the
device essentially idle, and the mechanism turned out to be neither concurrency
nor a large response.

`WebServer::handleClient` parses a request by calling `readStringUntil`, which
waits the client timeout **per byte** and resets that timeout on every byte
received. A peer that dribbles one byte just under the timeout holds Core 0
inside the read indefinitely, and nothing feeds the hardware watchdog while it
does — the main loop feeds the watchdog before `handleClient`, never inside it.
So one slow request, requiring no authentication and no concurrency, took the
device down after about eight seconds. The live autopsy is unambiguous:
`C0=[WEB_POLL] hp=0 sc3=0x80088013 (219)` — `hp=0` means `handleClient` never
returned.

The request parser now reads each line under a single wall-clock budget with
the watchdog fed on every byte. A request that overruns the budget comes back
partial, so the server drops the client instead of stalling on it — a dropped
slow request rather than a reboot. On a LAN a real request arrives in one
segment in well under a millisecond, so the budget is only ever spent by a
stall.

Measured on the bench: the exact repro that rebooted v2.1.0-beta (one GET at
3 s/byte) no longer does, across three dribble rates (0,4 / 1,0 / 3,0 s per
byte), zero reboots; normal requests are unaffected (40/40 sequential, full
page and log downloads intact). Same fix pattern as the four framework
overrides already in `tools/arduino_pico_overrides/`, and it applies cleanly to
both 5.4.3 and 5.6.1 (the parser is byte-identical between them).

This closes the unauthenticated remote reboot. What stays open is the softer
case behind the same autopsy under six concurrent clients — narrowed, not
retested here. Full write-up as D-B8 in `docs/beta-sweep-2026-08-10/`.

## v2.1.0-beta (2026-08-10)

First beta. The version leaves the alpha line because the defects that kept
it there are closed and measured, not because the calendar moved.

### /api/restore wrote the files before it checked who was asking

The permission check for restore lived only in the finish handler. The
framework calls that handler after the entire multipart body has already been
streamed through the upload callback, and an apply feed writes each entry
straight to its final path — the entry's real name, no rename, written as the
bytes arrive.

So the 403 was honest about the verdict and late about the effect. An
**unauthenticated** POST to `/api/restore?op=apply` overwrote anything the
backup format can name: `/config`, `/calib.csv`, `/history`, the language
packs. The path check only rejects `..`, and no session cookie was needed to
get that far.

Measured on the bench with a one-entry backup carrying the device's own chip
id: before, the request answered 403 and the file appeared on the filesystem;
after, 403 and nothing written. The legitimate paths are untouched — an
authenticated validate of a real 807 KB backup still answers over its 106
files, and Core-1 exposure stayed at `metr.fx=0` through both.

If you run a device on a network you do not fully control, this is the reason
to take this build.

### The last silent drops learn to say so

`users.actions` had never been swept with the space-in-JSON family. It read
`type` and `name` through needles with the quote baked in, so a payload
carrying the space JSON allows after a colon matched nothing and the whole
action evaporated under a 200. Past that, every refusal was a bare `continue`:
an invalid or reserved name, a duplicate, a full table, a `del` naming a slot
that is not there. The page offers no client-side check for any of them, so
adding a fifth user meant clicking Save & Restart, waiting out the reboot and
finding the account simply absent. Each now names itself in the `rejected`
array the sys section already uses, and permissions are held to the ten bits
the page can actually set.

The sys string fields went straight into a copy that truncates to fit: a
70-character server became a 63-character one and the commit still answered
ok. They now pass the same validator the CLI has always used, and a value that
does not fit whole is refused rather than stored wrong.

`save_sys` answered ok for a theme index the build does not carry, so the page
could not tell applied from ignored. It answers 400 now.

### Core 1 is visible from the shipping image

The heartbeat, the launch count and the three kill counters reached only `show
metrics` — a command the release profile does not carry. A stalled display
reads exactly like a healthy one from outside, so a soak wired to that image
could have reported success straight through a Core-1 death. `/api/status`
now carries `c1a` (age of the stamp Core 1 writes once per loop), `c1n`
(launches), `c1kl`/`c1kh`/`c1kq` (kills split by cause) and `c1s` (stuck
lockouts), for the same reason `fx` and `cgd`/`cgg`/`cgx` are already there.

### A third path into the `C0=[WEB_POLL]` park, found by closing the one above

Gating the restore made its refusal path reachable by anyone — and the refusal
path rebooted the device. Repeating an unauthenticated apply took it down on
the 12th request in one run and the 31st in another, with the autopsy that has
been on the books as an open residual since the network-storm campaign.

It is the same defect that campaign cured in two places: the 403 answers
non-chunked and returns, so nothing in the abort discipline covers its tail,
and the framework retires the client with a bare `stop()` whose ACK-wait
renews on progress and never feeds the watchdog. Draining before the return
is what `safeStreamFile()` and `/api/backup` already do. 100 refused restores
afterwards: no reboots, every one answered 403, nothing written.

Two attributions were tried and discarded on the way, both of which had looked
convincing: that the log line the gate added inside the multipart callback was
to blame (removing it gave 40 clean requests — a false negative, since the
reboot returned on the 31st with the line elsewhere), and that `/api/logs` was
the trigger (51 fetches, nothing). An event that fires once in a few dozen
requests is not cleared by one clean run of forty.

### Still open

The residual `C0=[WEB_POLL]` park under six-way concurrent load, documented in
`docs/netstorm-campaign-2026-08-10/`, is narrowed but not closed: three paths
into it are now drained, and the six-client case was not retested here. The
IRQ-off window of 68–78 ms against a 60 ms criterion (D-NS7) is untouched.

## v2.0.3-alpha (2026-08-10)

### The receive window no longer promises the pbuf pool out twice over

`D14` had been on the books as a pbuf leak "with a second source not yet
located". It is not a leak, and the reason nobody could find the second source
is that there was never a first one left to find.

What had been measured was the pool's **peak** — a high-water mark that by
definition never comes down — and its failure count. The number that separates a
leak from pressure is what is still **in use once the load stops**, and it had
never been read. It comes back to baseline at every level of concurrency,
including the one that emptied the pool and failed 79 allocations. Nothing is
held.

The real cause is arithmetic. A pool entry is ~1514 B and `TCP_WND` was 8×MSS,
so one connection can hold 7,7 of them; six connections filling their windows
want 46 against a pool of 24. Four clients peak at 13 and never fail, five reach
24/24 with 45 failed allocations, six with 79.

`TCP_WND` is now 4×MSS. It costs nothing measurable because the device could
never use the window it was advertising: uploads run at 26 KB/s, bound by flash
writes, and at a ~5 ms round trip even 4×MSS allows about 1,1 MB/s. Downloads
are governed by `TCP_SND_BUF` and are untouched.

| | before | after |
|---|---|---|
| allocation failures, 5 / 6 clients | 45 / 79 | **0 / 0** |
| pool peak at 6 clients | 24/24 | 18/24 |
| successful requests at 6 clients | 98 | 166 |
| download | 221 KB/s | 216 KB/s |
| upload | 26 KB/s | 25 KB/s |

Growing the pool was the wrong lever: 24 entries are already 35,5 KB of BSS, and
doubling costs more than the whole free heap.

Worth saying plainly, because the old name suggested otherwise: running the pool
dry never rebooted the device. Requests fail and the pool comes back whole.

That is not the same as saying heavy concurrency is safe. Six clients hammering
the device still hit the residual `C0=[WEB_POLL]` park documented in
`docs/netstorm-campaign-2026-08-10/` — seen once here in about two minutes of
six-way load, and not reproduced in a 90 s repeat. It predates this change,
which targets allocation failures and nothing else, and it stays open.

## v2.0.2-alpha (2026-08-10)

### Survives a hostile network: the watchdog seam in the send path

A campaign ran the telemetry fault matrix, a concurrent web hammer and the
sensors **at the same time** — 26 fault windows over about two hours — because
every previous run had exercised those loads one at a time, and the overlap is
where the failures actually lived. Write-up and numbered defect list in
`docs/netstorm-campaign-2026-08-10/`.

**`HTTPClient`'s send loop never fed the watchdog, and it was most of the
reboots.** `StreamConstPtr::sendAll`'s 5 s budget bounds the loop, not a write;
each `write()` parks for the 4 s socket timeout, so a write entered near the end
of the budget finishes around 9 s — past the 8388 ms hardware watchdog. Closed
by a fourth framework override, wired into `patch.sh` so an upgrade cannot drop
it silently. Measured on the full HTTP group, same conditions before and after:
**5 reboots → 1**, MTBF under storm **~10 min → 58 min**, 557 history downloads
with no invalid JSON.

**A non-chunked response left its tail for the framework to park on.** The
existing hard close was gated on chunked responses, so `/download` and
`/api/backup` kept the polite path — and that path waits on ACKs with a clock
that renews on every one of them, unfed. It reproduced without any storm at all:
**one download per boot**. Draining before the handler returns fixed it —
`/download` went from 6/8 with 2 reboots to **24/24 with none**, `/api/backup`
(794 KB a piece) from 2/3 with 1 reboot to **6/6 with none**.

### Fixed

- **A single aborted send in the history tail could pin the display.** Three
  returns in the `extremes` tail skipped the handler's unwind, leaving the
  `_inHistoryHandler` latch set — every later `/api/history_multi` answering
  `503 Already processing` — and the display's web-busy overlay stuck with
  **touch blocked**, both until the next reboot. Ownership now lives in a
  destructor, which a return cannot skip.
- **`/api/sec_status` could write past its buffer.** Accumulated
  `pos += snprintf(...)` runs past the array once an entry truncates, and the
  remaining-room arithmetic is unsigned, so it wraps instead of going negative.
  Room is clamped before every write now, and a truncated entry is backed out so
  the JSON stays parseable with fewer slots.

### Added

- `metr.cgd` / `metr.cgg` / `metr.cgx` in `/api/status`: the three reasons a
  chunked response was cut short — deadline, guard latch, real disconnect.
  `show metrics` already printed them, but that command does not exist outside
  the full-CLI image, so from the network a truncated download and a client that
  walked away read identically.
- `tools/telemetry_bench/storm_net.py`, the combined-storm harness, plus
  `storm_report.py` and two fault modes in the sink (`never_read`,
  `tls_bigrecord`).

### Calibration curves: up to 5 points per quantity

Calibration grows from one constant offset to a **correction curve of up to 5
(raw → reference) points per quantity**, edited in the `/config` slot dialog.
The correction interpolates linearly between points and holds the end offset
beyond them; one point is exactly the old constant offset, and zero points is
an explicit "no correction — sensor default" state. Points can be typed from a
bench table or captured from the live reading (an empty raw field captures at
save time).

With 3+ points the interpolation is selectable per quantity: **Straight**
(piecewise linear) or **Smooth** — a monotone cubic (Fritsch–Carlson) on the
offsets that bends through the anchors without ever overshooting them and
flattens into the held zones. Smooth rows carry a `cub` cell after the name in
`calib.csv`; the API accepts `{"m":"cub","p":[[raw,ref],…]}` alongside the
plain-array (linear) form.

Corrections now apply to the **filtered mean instead of each raw sample**, so
outlier rejection always works on physical values and an edited correction
takes effect immediately instead of bleeding through a 10-sample window. For
constant offsets the arithmetic is identical, so existing deployments read the
same values they always did.

`/calib.csv` puts everything after the name as flat CSV cells:
`key,id,name,raw,ref[,raw,ref,…]` — one number per column,
spreadsheet-friendly, no dedicated offset column anymore. Row shapes are told
apart by field count: legacy 4-column `key,id,offset,name` files still read
as the constant offset they always were (and a carried anchor-free offset is
still written in that shape — it has no points to become); `key,id,name` is
an identity row. Older firmware reading a points row sees no correction,
never a wrong one. Removing a correction deletes the row (DS18B20 rows stay — they
double as the ROM→ID/name identity database). `POST /api/calib` accepts
`"cal":{"<channel>":[[raw,ref],…]}` with full validation before anything is
written; `GET /api/calib` channels gain `raw`, `min`, `max` and `pts`.

**Behavior change:** the legacy `refs`/`refTemp` fields (cached pages) now set
an absolute one-point correction at the current raw reading instead of
accumulating `offset += ref − reading`. Repeating the same reference is now
idempotent, which is what users expected all along.

The slot editor draws a **live mini-chart per quantity**: the dashed line is
the sensor default (zero correction), the curve is the staged correction with
its anchors, simulated in the chosen interpolation as you type. **DS18B20
pairing became automatic**: a probe provisioned through the editor gets its
ROM read and written into `calib.csv` on the restart that follows Save &
Restart, migrating any correction saved while unpaired; ROM verification then
guards against swapped probes.

### Fixed

- **The slot editor's "Alarms enabled" checkbox never saved.** Every
  `commit_all` walker sliced array elements at the first `}`, so any key
  staged after the nested `lim{}` object — which is where `al` sits — was
  silently truncated off and kept its stored value. All the hand-rolled JSON
  walkers now match braces by depth (quote-aware), which is also what lets
  the calibration payload carry nested point arrays at all.

## v2.0.1-alpha (2026-08-01)

History moves to V5: a compressed, self-describing time-series format whose hot
path never touches flash. The device now records a day in 7.6 KiB instead of
10.6, keeps four months of history in the same partition instead of under three,
and answers a 30-day graph from block envelopes in 187 ms — a query the previous
format could not finish at all.

> **Back up first.** V5 does not read V4. On the first boot after this update,
> `/history` is swept of everything that is not a `.h5` file and the history
> restarts empty. Download your `.sim4` files before updating and convert them
> on a computer with `python3 tools/history_v5.py --convert-v4 in.sim4 out.h5`.

> **Over-the-air updates do not work on this release, and did not work on
> 2.0.0-alpha either.** Staging aborts partway through the upload and the device
> resets; nothing in `src/ota/` changed in this release. Worse, a failed stage
> erases the filesystem, because the staging region *is* the LittleFS partition.
> Flash over USB until that is fixed. See `docs/test_reports/`.

### The hot path stopped writing to flash

Flash on the RP2040 is XIP: every program or erase means freezing Core 1 and
running Core 0 with interrupts off, and those windows are what the stability
work of the last month has been chasing. V4 wrote a record per sample — ~1440
flash writes a day, batched into ~360 lockout windows.

V5 keeps the hour in RAM. `writeHistoryEntryV5( )` is a `memcpy`. Flash is
touched when a block fills (60 records), at the day rollover, when the sensor
set changes, and every ten minutes for a `.wip` snapshot that bounds power-loss
to that window. About 168 writes a day instead of 1440.

### Graphs draw the peaks instead of sampling past them

Long ranges used to be decimated: one record in N, and whatever fell between
them was not drawn. A one-minute spike in a month-long range had roughly one
chance in 72 of appearing.

A V5 block header carries the true minimum and maximum of every channel over
its hour, so a long range emits those — two points per block, no payload read.
The extreme *is* the point; it cannot be sampled away. The 24-hour graph reads
in 5.8 ms this way against 107.6 ms decoding every record, and 30 days answers
in 187 ms. `?mode=decode|envelope` forces either path.

### Changing a sensor stops costing the day

A `.sim4` froze its schema in the file header, so changing a sensor identity
meant recreating the day's file and losing what was in it — which is why the
CLI demanded `confirm` — or running a streaming migration to carry the records
over. V5 writes a second SCHEMA chunk into the same file and keeps going; the
blocks before it stay readable under the schema in force when they were written.
`sensor reschema` and the web rebind endpoint kept their signatures and are no
longer destructive.

### Timestamps really are corrected now

`handleTimeSync` logged "correcting timestamps" and then "timestamps corrected",
with `/* V4: variable-length records — in-place correction unsupported. */`
between the two. Everything written before NTP came up kept the provisional
clock forever, and the log said otherwise. In V5 the only absolute stamp is `t0`
in each block header, so the fix is a stream rewrite touching four bytes and a
CRC per block, bounded to the blocks this boot wrote.

### Corruption costs an hour, not a day

Every block carries its own CRC and decodes independently. A corrupt block is
skipped and the rest of the day is served; a corrupt file is skipped and the
other days are served. Ten injected corruptions — payload, tails, `t0`, SCHEMA
CRC, magic, `nCh`, truncation — produced zero reboots and zero invalid
responses. Under V4 a break in the delta chain compromised the rest of the day.

### Files are readable without the firmware

`python3 tools/history_v5.py --dump-csv day.h5` decodes a device file with
nothing but the format document: the SCHEMA chunk states which channels exist,
what each measures and at what scale. The same tool converts legacy files
(`--convert`, `--convert-v4`), reports compression (`--stats`), generates
synthetic history (`--synth`) and runs the format's own test vectors
(`--selftest`).

### Smaller, and much less static RAM

Static RAM drops 44 060 B and the largest contiguous heap block grows 63 %
(29 733 → 48 522 B), which is the number BearSSL cares about for TLS. That is
not the format: V4 had five copies of the decode loop — web graph, CSV export,
export bundle, telemetry, preload, TFT graph — each carrying its own ~2.8 KiB
of codec state. V5 has one reader in `StorageManager`. Code flash grows 1 024 B.

### The first boot with a `.wip` on disk hung

Found and fixed on the bench before release. `recoverWipV5( )` deleted the
`.wip` snapshot outside its `Core1FlashPause`: the pause sat inside the branch
that decodes the snapshot, while the delete runs on every path out of the
function, including the ones that never decode anything. A delete is an erase
burst, and an erase with Core 1 still fetching from XIP wedges the QSPI — the
rule the `FLASH_OP` comment states and this call broke.

It hung rather than rebooted because the watchdog is armed on the first pass
through `loop( )`, so all of `setup( )` runs unprotected. The device stopped
with the boot screen frozen on the previous step, USB enumerated but answering
nothing, and no reboot to autopsy. It needed a `.wip` on disk, which only exists
once V5 has been recording, so it did not show up until the format was live.

`sealHourV5( )` had the same defect in its own `.wip` delete — reached in normal
operation, where the armed watchdog would have turned it into an unexplained
reboot instead. Both now hold the pause across the delete.

### Also

- `/api/status` reports the flash-write counters (`fo`, `fom`, `fot`, `f50`).
  They had been tracked since T0.1 but were only reachable from a CLI the
  shipping image does not carry.
- `/api/history_multi` reports `path`, `readMs` and `rejected`, so the device's
  own read time can be told apart from Wi-Fi latency.
- `preloadMinMax( )` reads block headers instead of decoding the day, so the
  dashboard no longer shows mid-morning extremes after an afternoon boot.
- Four log codes: `STO_H5_SEALED`, `STO_H5_WIP`, `STO_SCHEMA_MISMATCH`,
  `STO_LEGACY_PURGED`.
- Fixed: seeking to an instant before a file's first block left the scanner at
  EOF, so a range query whose cutoff preceded a file dropped it silently.
- `HistoryV4.cpp` is out of the shipping build. The V4 entry points remain as
  delegating shims so nothing that called them stops compiling.

### Known limitations

- The §10 latency budgets are not met: 0.28 ms per block on the envelope path
  against a budget implying 0.111 ms, and 4.48 ms to decode a block against
  1 ms. The floor is the LittleFS indexed read, confirmed by two optimisations
  that did not move it.
- No 72-hour soak, and no 20-cut power-loss campaign.
- Luminosity keeps its channel but drops to whole units: it is 24-bit at x100
  in the channel table and V5 values are `int16`. No sensor produces it today.

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
