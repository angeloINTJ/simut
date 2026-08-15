# Changelog

**English** | [Português](CHANGELOG.pt-BR.md)

All notable changes to SIMUT firmware.

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
