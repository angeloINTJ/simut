# PicoHand Operation Manual — for Claude Code

This manual describes how **Claude Code** should use the `pico_hand` tool
(robotic fixture that drives BOOTSEL/RESET of another Pico via USB serial)
during automated test pipelines.

The premise is simple: whenever the **target** Pico hangs to the point of
refusing flash via `picotool`, `pico_hand` is the automatic path to force
it back into BOOTSEL mode **without human intervention**.

[Português](MANUAL_CLAUDE_CODE.pt-BR.md)

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

The hand **responds** to `PING` with `PONG`. The target **does not** — it is
either running user firmware that doesn't speak this protocol, or in BOOTSEL
mode (which doesn't appear as serial). Use this to distinguish.

### Recommended approach: use the wrapper

```bash
source /path/to/pico_hand.sh
hand_init
# stderr: "[pico_hand] hand detected at: /dev/ttyACM1"
# exit 0 = success, 1 = not found
```

The `PICO_HAND_PORT` variable is exported after `hand_init` and reused by
all subsequent `hand` calls.

### If you want a fixed port (e.g., udev rule)

```bash
export PICO_HAND_PORT=/dev/pico_hand   # persistent symlink created by udev
hand RESET
```

`hand_detect_port` respects `PICO_HAND_PORT` if already set and the device exists.

---

## 3. Command reference

| Command         | When the agent should use it                                | Expected response                |
|-----------------|-------------------------------------------------------------|----------------------------------|
| `PING`          | Health check before starting tests                          | `PONG`                           |
| `RESET`         | Reboot target without entering BOOTSEL (e.g., re-run flashed firmware) | `OK RESET`            |
| `BOOTSEL`       | Force target into BOOTSEL mode (recovery; before new flash) | `OK BOOTSEL`                     |
| `HOLD BOOTSEL`  | Start of manual sequence (rare; prefer `BOOTSEL`)           | `OK HOLD BOOTSEL`                |
| `HOLD RESET`    | Hold target in reset (e.g., to inspect circuit)             | `OK HOLD RESET`                  |
| `RELEASE BOOTSEL` / `RELEASE RESET` | End corresponding `HOLD`                  | `OK RELEASE …`                   |
| `STATUS`        | Diagnostics: confirm lines are released                     | `STATUS BOOTSEL=… RESET=…`       |
| `PINOUT`        | Diagnostics: confirm which GPIOs are in use                 | `PINOUT BOOTSEL=GP14 RESET=GP15 …` |
| `SELF_BOOTSEL`  | **Only** to reflash the hand's own firmware                 | `OK SELF_BOOTSEL` (port disappears) |
| `HELP`          | Do not use in automation (multi-line output)                | text list                        |

### Wrapper exit codes

| Code | Meaning                                    |
|------|--------------------------------------------|
| `0`  | Response starts with `OK` or `PONG`        |
| `1`  | Response starts with `ERR` or unexpected   |
| `2`  | Timeout — no response within deadline      |
| `3`  | Hand not initialized / port disappeared    |

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

This is **the core recipe**. Try normal flash; if it fails, force BOOTSEL
via the hand and retry.

```bash
flash_with_recovery() {
    local uf2="$1"

    # Attempt 1: target already in BOOTSEL or accepting USB reset?
    if picotool load -x "$uf2" 2>/dev/null; then
        return 0
    fi

    echo "[flash] picotool failed — invoking pico_hand for forced BOOTSEL"
    hand BOOTSEL || return 1

    # Wait for RPI-RP2 to enumerate (USB mass storage mount).
    sleep 1.5

    # Attempt 2: should work now.
    picotool load -x "$uf2"
}
```

### 4.3 Simple reset between test cases

```bash
hand RESET && sleep 0.5    # time for target firmware to boot
```

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
```

---

## 5. Decision flowchart (recovery)

```
Upload firmware to target
        │
        ▼
   picotool load
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
   OK? ────────── NO ──▶ abort: wiring? dead target?
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

## 6. Pitfalls to avoid

1. **Don't confuse the ports.** Sending `BOOTSEL` to the wrong port puts the
   **wrong device** in recovery. Always use `hand_init` or confirm with
   `hand PING`.

2. **Never call `SELF_BOOTSEL` in automation.** This puts the *hand itself*
   in BOOTSEL mode — the serial port disappears and the pipeline hangs.
   `SELF_BOOTSEL` is exclusively for reflashing the hand's firmware.

3. **`HOLD` requires paired `RELEASE`.** If the script dies between them,
   the target stays locked in reset/BOOTSEL forever. Always use:
   ```bash
   trap hand_release_all EXIT
   ```

4. **USB enumeration time.** After `hand BOOTSEL`, the target needs ~1-2
   seconds to reappear as an `RPI-RP2` device. Don't call `picotool`
   immediately — add `sleep 1.5` or poll.

5. **Serial buffer may contain garbage.** The first read after connecting
   may bring fragments of old responses. The wrapper already drains the
   buffer during detection; for extended use, always prefer `hand` (which
   reads one line at a time) over raw `cat /dev/ttyACM*`.

6. **Don't open multiple consumers on the port.** A `cat /dev/ttyACM1`
   running in background "steals" responses from `hand`. Use one tool
   at a time.

7. **Permissions.** On systems where the user is not in the `dialout` group,
   `stty -F /dev/ttyACM*` fails silently. Verify with:
   ```bash
   groups | grep -q dialout || echo "MISSING: user not in dialout group"
   ```

---

## 7. Summary: what Claude Code should do

1. At the start of any pipeline involving Pico flashing:
   ```bash
   source /path/to/pico_hand.sh
   hand_init || exit 1
   trap hand_release_all EXIT
   ```

2. When flashing firmware, use `flash_with_recovery` (recipe 4.2) instead
   of raw `picotool load`.

3. Between test cases that need a clean state, use `hand RESET`.

4. **Never** call `SELF_BOOTSEL` in automation.

5. On any failure: confirm via `hand PING` that the hand is still alive
   before declaring a target problem.
