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
source. Two findings matter before you rely on this tool:

Both control lines carry a **100 Ω series resistor**, added to protect the two
boards against a wiring short. That resistor is why the two lines behave
differently, and §7.1 explains it.

| Line | GPIO | State |
|------|------|-------|
| RESET | GP0 | ✅ **Works end to end.** Target uptime went 134 s → 19 s on `hand RESET`. |
| BOOTSEL | GP1 | ⚠️ **Wired, but does not put the target into BOOTSEL.** See §7.1. |

Because BOOTSEL does not currently take effect, the recovery recipe in §4.2
cannot complete on this bench. Use `pio run -t upload`, which performs its own
1200 bps touch reset and does not need the fixture at all; keep the hand for
`RESET` between test cases and for the day a target hangs hard enough to ignore
the touch reset.

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

The intended core recipe. **Blocked on this bench** until the BOOTSEL line is
rewired (§7.1) — `hand BOOTSEL` returns `ERR` and the target keeps running.

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
   OK? ────────── NO ──▶ check hand VERIFY; if BOOTSEL_STUCK_LOW, see §7.1
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

**The 100 Ω series resistor is very likely why BOOTSEL does not take.** It is
harmless on RESET and decisive on BOOTSEL, because the two target pins are
electrically different:

- **RUN** is a high-impedance input with a weak internal pull-up. 100 Ω to GND
  overwhelms it easily, which is why `hand RESET` works — verified, not assumed.
- **QSPI_SS** is a push-pull output actively driven by the target. Pulling it
  through 100 Ω against a driver whose output impedance is on the order of tens
  of ohms produces a divider that never reaches a logic LOW. The BOOTSEL button
  it emulates is a hard short to GND, 0 Ω by design.

This also invalidates an obvious-looking test. Holding BOOTSEL asserted while
the target runs left it perfectly healthy — uptime advanced 46 s → 55 s,
answering the CLI throughout — which would prove a disconnected wire if the
link were direct. Through 100 Ω against an active driver it proves nothing: the
line never actually goes low, so of course the target is undisturbed. **Do not
use the hold test to decide whether the BOOTSEL wire is landed while a series
resistor is present.**

If BOOTSEL needs to work, the series resistance on that line has to come down
to near zero. The safety argument for the resistor is weaker there anyway: the
hand only ever pulls low or goes high-impedance, so the dangerous case it
guards against — two outputs fighting — requires the target to drive QSPI_SS
high at the same moment, which is exactly the condition that also defeats the
100 Ω.

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
