# PicoHand Operation Manual — for Claude Code

This manual describes how **Claude Code** should use the `pico_hand` tool
(robotic fixture that drives BOOTSEL/RESET of another Pico via USB serial)
during automated test pipelines.

The premise is simple: whenever the **target** Pico hangs to the point of
refusing flash via `picotool`, `pico_hand` is the automatic path to force
it back into BOOTSEL mode **without human intervention**.

[Português](MANUAL_CLAUDE_CODE.pt-BR.md)

---

## 0. Bench status — verified 2026-07-23

Everything below was exercised against the real fixture, not read off the
source.

| Line | GPIO | State |
|------|------|-------|
| RESET | GP0 | ✅ **Works end to end.** Target uptime went 134 s → 19 s on `hand RESET`. |
| BOOTSEL | GP1 | ✅ **Works end to end.** Target entered BOOTSEL and was flashed without a human touching it. |

The full recovery recipe (§4.2) was executed start to finish: `hand BOOTSEL`
returned `OK`, the target's CDC port vanished, `picotool info` reported the
device, `picotool load` wrote the image and rebooted it, and the target came
back answering `Firmware: 1.5.2-rc4`. Zero human intervention.

**Wiring that matters.** RESET keeps a 100 Ω series resistor and works fine
through it. BOOTSEL must be a direct connection — 100 Ω on that line stops it
working, for the reason in §7.1. Getting there took two false diagnoses, both
recorded in §7.1 so they are not repeated.

For routine flashing, `pio run -t upload` is still the simpler path: it performs
its own 1200 bps touch reset and needs no fixture. The hand is what saves you
when the target is hung too hard to service USB at all.

---

## 1. Prerequisites (verify once)

1. The `pico_hand.ino` firmware is flashed onto the controller Pico (the "hand").
2. Hand and target are connected to the same computer via USB.
3. Physical wiring between hand and target is in place (see `pico_hand/README.md`).
4. The user can access `/dev/ttyACM*` (usually requires `dialout` group membership).
5. The `pico_hand.sh` wrapper is accessible (known path by the agent).

> **Never** assume hand and target are at fixed ports. USB enumeration order
> can change on every reset or re-plug.

---

## 2. Port identification — before anything else

### Fastest and most reliable: ask udev

The hand is a plain **Pico**; the SIMUT target is a **Pico W**. USB descriptors
say so directly, with no serial traffic and no risk of sending a command to the
wrong board:

```bash
for p in /dev/ttyACM*; do
  udevadm info -q property -n "$p" | grep -q 'ID_MODEL=Pico_W' && echo "$p target" && continue
  udevadm info -q property -n "$p" | grep -q 'ID_MODEL=Pico'   && echo "$p hand"
done
```

On this bench: hand `ID_SERIAL_SHORT=E660C062131E3E27`, target
`E6642815E34C1824`. Serial numbers are stable across re-enumeration, so they
are the safest anchor of all if you need to pin one board.

### Or use the wrapper

```bash
source /path/to/pico_hand.sh
hand_init
# stderr: "[pico_hand] hand detected at: /dev/ttyACM1"
# exit 0 = success, 1 = not found
```

`hand_init` probes each port with `PING` and keeps the one that answers `PONG`.
`PICO_HAND_PORT` is exported afterwards and reused by every later `hand` call.

### If you want a fixed port (e.g., udev rule)

```bash
export PICO_HAND_PORT=/dev/pico_hand   # persistent symlink created by udev
hand RESET
```

`hand_detect_port` respects `PICO_HAND_PORT` if already set and the device exists.

---

## 3. Command reference

Twelve commands. Responses are one line each, except `HELP` and `PULSE_TEST`.

| Command | When the agent should use it | Actual response |
|---------|------------------------------|-----------------|
| `PING` | Health check before starting tests | `PONG` |
| `RESET` | Reboot target without entering BOOTSEL | `OK RESET`, or `ERR RESET VFY:<fault>` |
| `BOOTSEL` | Force target into BOOTSEL mode (recovery; before new flash) | `OK BOOTSEL`, or `ERR BOOTSEL VFY:<fault>` |
| `HOLD BOOTSEL` \| `HOLD RESET` | Hold a line asserted (inspection, manual sequences) | `OK HOLD <name>` |
| `RELEASE BOOTSEL` \| `RELEASE RESET` | End the corresponding `HOLD` | `OK RELEASE <name>` |
| `STATUS` | Confirm both lines are released | `STATUS BOOTSEL=RELEASED RESET=RELEASED VFY:BOOTSEL_ACT=HIGH VFY:RESET_ACT=HIGH` |
| `PINOUT` | Confirm which GPIOs are in use | `PINOUT BOOTSEL=GP1 RESET=GP0 LED=GP25` |
| `VERIFY` | Read the logic analyzer (see §5) | `VFY BOOTSEL=OK RESET=OK HB=2us E:… A:…` |
| `VERIFY CLEAR` | Reset fault counters before a measurement | `OK VFY CLEAR` |
| `PULSE_TEST <line> <ms> <count>` | Drive timed pulses and report achieved width | multi-line, ends `DONE PULSE_TEST` |
| `DEBUG ON` \| `OFF` \| `STATUS` | Verbose logging — see the warning in §7.2 | `OK DEBUG ON` / `OK DEBUG OFF` |
| `SELF_BOOTSEL` | **Only** to reflash the hand's own firmware | `OK SELF_BOOTSEL` (port disappears) |
| `HELP` | Do not use in automation (multi-line output) | text list |

Pulse widths are honoured precisely — a `PULSE_TEST RESET 100 3` reported
`actual=100ms` on all three pulses.

### Wrapper exit codes

| Code | Meaning |
|------|---------|
| `0` | Response is a success reply for its command |
| `1` | Response starts with `ERR`, or is unrecognised |
| `2` | Timeout — no response within `PICO_HAND_TIMEOUT` |
| `3` | Hand not initialized / port disappeared |

Success is not only `OK`/`PONG`: query commands answer with their own banner,
so `STATUS`, `PINOUT`, `VFY` and `DONE` also count as success. An earlier
version of the wrapper treated those as failures, which made every diagnostic
command return 1.

---

## 4. Recipes

### 4.1 Health check at pipeline start

```bash
source /path/to/pico_hand.sh

if ! hand_init; then
    echo "FATAL: hand unavailable" >&2
    exit 1
fi

if ! hand PING > /dev/null; then
    echo "FATAL: hand not responding to PING" >&2
    exit 1
fi
```

### 4.2 Flash with automatic recovery

The core recipe, **verified end to end on this bench**: BOOTSEL forced by the
hand, image written by picotool, target back up reporting the new version, no
human involved.

```bash
flash_with_recovery() {
    local uf2="$1"

    # Attempt 1: normal path. pio's uploader does its own 1200 bps touch reset,
    # so this succeeds on any target that still services USB.
    if picotool load -x "$uf2" 2>/dev/null; then
        return 0
    fi

    echo "[flash] picotool failed — invoking pico_hand for forced BOOTSEL"
    hand BOOTSEL || return 1

    sleep 1.5      # wait for RPI-RP2 to enumerate
    picotool load -x "$uf2"
}
```

### 4.3 Simple reset between test cases

```bash
hand RESET && sleep 6    # SIMUT needs ~5 s to boot and rejoin Wi-Fi
```

Verified: uptime dropped from 134 s to 19 s, matching the ~17 s of wall time
since the pulse.

### 4.4 Clean state guarantee (defensive use)

Always release both lines at script start — this ensures no orphan `HOLD`
(from a previous interrupted run) left the target locked:

```bash
trap hand_release_all EXIT
hand_release_all   # release everything at startup
```

### 4.5 Diagnostics when something is off

```bash
hand PING       # hand alive?
hand STATUS     # any line stuck PRESSED?
hand PINOUT     # do GPIOs match the wiring?
hand VERIFY     # what does the logic analyzer actually see?
```

---

## 5. Reading the logic analyzer

Core 1 samples both lines at 10 kHz and compares them against what Core 0 says
it is driving. `VERIFY` reports:

```
VFY BOOTSEL=OK RESET=OK HB=2us E:BOOTSEL=HIGH E:RESET=HIGH A:BOOTSEL=HIGH A:RESET=HIGH
```

- `BOOTSEL=` / `RESET=` — fault code: `OK`, `STUCK_HIGH` (open circuit),
  `STUCK_LOW` (short to GND), `GLITCH`, `NO_VERIFIER`. A fault is followed by
  `(count,age_us)`.
- `HB=` — age of Core 1's heartbeat. A few µs is healthy; approaching
  1 000 000 µs means the verifier core is dead and fault codes are stale.
- `E:` — expected level (what Core 0 is driving).
- `A:` — actual level read back from the pin.

A mismatch must persist 5 ms before it is flagged, which is what makes the
BOOTSEL readings in §7.1 meaningful rather than noise.

---

## 6. Decision flowchart (recovery)

```
Upload firmware to target
        │
        ▼
   pio run -t upload      (1200 bps touch — no fixture needed)
        │
   success? ──── YES ──▶ done
        │
       NO
        │
        ▼
   hand PING
        │
   PONG? ──────── NO ──▶ abort: hand unavailable, needs human
        │
       YES
        │
        ▼
   hand BOOTSEL
        │
   OK? ────────── NO ──▶ see §7.1: series resistor on the line? bad contact?
        │
       YES
        │
        ▼
   sleep 1.5s   (wait for RPI-RP2 mount)
        │
        ▼
   picotool load (second attempt)
        │
   success? ──── YES ──▶ done
        │
       NO
        │
        ▼
   abort: target likely in hardware hard fault
```

---

## 7. Pitfalls to avoid

### 7.1 The BOOTSEL line is not a passive button node

The target's BOOTSEL pad is QSPI_SS, the flash chip select — not a quiet node
that only a button touches. Two consequences, both measured here.

**The verifier cannot validate it while the target runs.** A running RP2040
drives QSPI_SS constantly, so the hand reads the line toggling between HIGH and
LOW with nothing pressed — three consecutive `VERIFY` calls returned
`A:BOOTSEL=HIGH`, then `LOW`, then `HIGH`. During boot the target reads flash in
long bursts, holding the line low past the 5 ms fault threshold, which is why
`hand BOOTSEL` reports `ERR BOOTSEL VFY:BOOTSEL_STUCK_LOW` right after its own
reset pulse. **That fault is an artifact of the measurement, not proof of a
short.** The toggling is itself good news: it means the wire is landed on a
live node rather than floating.

**BOOTSEL needs a direct connection; RESET tolerates a series resistor.** The
two target pins are electrically different, and the bench proved it in both
directions:

- **RUN** is a high-impedance input with a weak internal pull-up. 100 Ω to GND
  overwhelms it easily, which is why `hand RESET` works through the resistor —
  verified, not assumed.
- **QSPI_SS** is a push-pull output actively driven by the target. Pulling it
  through 100 Ω against a driver whose output impedance is tens of ohms forms a
  divider that never reaches a logic LOW, and BOOTSEL silently fails. The
  button it emulates is a hard short to GND, 0 Ω by design. With the resistor
  removed, BOOTSEL worked on the first attempt.

### The hold test, and when it lies

Holding BOOTSEL asserted while the target runs should crash it: with a direct
link, pulling QSPI_SS low corrupts every instruction fetch. It is the only way
to prove the wire is landed, because `VERIFY` cannot (see above). But it has
one precondition that is easy to miss.

**With a series resistor in the line, the test is meaningless.** The line never
actually reaches logic low, so the target carries on regardless of whether the
wire is connected. Reaching a "disconnected" verdict that way is wrong.

**With a direct link, the test is decisive** — and it earns its keep. A run
with the resistor already removed showed the target surviving the hold, uptime
advancing 54 s → 72 s. That correctly indicated a bad connection, which turned
out to be exactly right: the wire was reseated and BOOTSEL began working
immediately.

So: check for series resistance first, then trust the hold test.

### 7.2 DEBUG mode changes what the first response line is

With `DEBUG ON`, the hand echoes every received line first:

```
[DBG t=869359] RX line: 'PING'
PONG
```

Any reader that takes the first line gets the debug echo instead of the reply.
This broke `hand_init` outright — it looked for `PONG`, saw `[DBG …]`, and
reported the hand as absent. The wrapper now skips `[DBG` lines, so it works
either way, but anything reading the port directly must do the same.

### 7.3 Never hold the port open with two readers

A second consumer steals responses. This is not hypothetical: the Arduino IDE's
serial monitor had `/dev/ttyACM1` open during this session and every connection
attempt failed with `Errno 16, Device or resource busy`. Check before blaming
the fixture:

```bash
fuser -v /dev/ttyACM1
```

### 7.4 One open file descriptor per exchange, not two

Writing with `echo > port` and reading with `head < port` opens and closes the
device twice. The hand answers in the gap between the write handle closing and
the read handle opening, with no reader attached, and the reply is lost —
measured here as an empty string every time, while a single held descriptor
returned `PONG`. The wrapper now does:

```bash
exec 3<>"$port"
printf 'PING\n' >&3
read -r -t 2 resp <&3
exec 3>&-
```

### 7.5 Never call `SELF_BOOTSEL` in automation

This puts the *hand itself* in BOOTSEL mode — the serial port disappears and
the pipeline hangs. It exists exclusively for reflashing the hand's firmware.

### 7.6 `HOLD` requires a paired `RELEASE`

If the script dies between them, the target stays locked in reset or with
BOOTSEL asserted. Always:

```bash
trap hand_release_all EXIT
```

### 7.7 USB enumeration time

After `hand BOOTSEL`, the target needs ~1–2 seconds to reappear as `RPI-RP2`.
After `hand RESET`, SIMUT needs ~5 s to boot and rejoin Wi-Fi. Don't poll
immediately.

### 7.8 Permissions

Where the user is not in the `dialout` group, `stty -F /dev/ttyACM*` fails
silently:

```bash
groups | grep -q dialout || echo "MISSING: user not in dialout group"
```

---

## 8. The serial bridge

The hand's `Serial2` (UART1, GP4 TX / GP5 RX) is forwarded transparently to
USB. Wire it to the target's debug UART and the hand relays that traffic on its
own CDC port, so one USB connection carries both fixture control and target
console output. Bridged lines are interleaved with command responses, so a
parser must tolerate unsolicited text — one more reason to match on known
prefixes rather than assume the next line is your answer.

---

## 9. Summary: what Claude Code should do

1. At the start of any pipeline involving Pico flashing:
   ```bash
   source /path/to/pico_hand.sh
   hand_init || exit 1
   trap hand_release_all EXIT
   ```
2. To flash, prefer `pio run -e <env> -t upload`. It performs its own 1200 bps
   touch reset and needs no fixture. Reach for the hand only when that fails.
3. Between test cases needing a clean state, use `hand RESET` + `sleep 6`.
4. **Never** call `SELF_BOOTSEL` in automation.
5. On any failure, confirm with `hand PING` that the hand is alive before
   blaming the target — and with `hand VERIFY` before blaming the wiring.
6. Remember that `VERIFY` cannot validate the BOOTSEL line while the target
   runs (§7.1). Use the hold test instead.

---

## 10. Working with SIMUT Air (the hibernating build) — 2026-09-06

A target running `pico_w_air` **drops off the USB bus when it sleeps** (it
releases the D+ pull-up on purpose) and re-enumerates on every wake. Three
consequences for the hand:

1. **An absent target is not a dead target.** Wait one history interval
   (`air status` prints `wake=`) before reaching for the hand — the device
   comes back by itself. `tools/air_test_suite.py` does that wait.
2. **`hand RESET` gives a cold boot (M0).** The pulse drives RUN, a global chip
   reset: the firmware's own scratch map (`src/LogManager.cpp:605`) records
   that those registers are zeroed by "power cycle / physical reset", and the
   Air hibernation marker lives in `scratch[0]`. So the target comes back in
   operational mode, with no `air stop` needed. (An earlier revision of this
   section claimed the opposite; `tools/air_test_suite.py` now measures the
   post-reset mode in T02 instead of assuming it.)
3. **RESET proves nothing about the wake path.** Because it restores the ROSC
   and the default clocks, it recovers the target even if plan item F01 is real
   (ROSC disabled before sleep and never re-enabled on wake). The only proof
   that sleep/wake works is the target **re-enumerating on its own** within
   `wakeSec` plus margin. If not even RESET brings it back, the problem is
   power or cabling, not firmware.

**Timing probe.** `VERIFY` only sees the RESET and BOOTSEL lines; the hand has
no channel yet to read the target's GP16 (high awake, low asleep). The proposed
extension (`PROBE START|READ|STATUS` on GP2) is in §3 of the plan; until then
the suite measures awake/asleep from USB enumeration timestamps.
