# pico_hand (Arduino IDE version)

Firmware for a Raspberry Pi Pico to act as a **"robotic hand"** that remotely
drives the **BOOTSEL** and **RESET** buttons of another Pico (the "target")
via USB CDC serial commands.

Designed to unlock automated test pipelines (SIMUT) when the target hangs
before accepting the next binary.

[Português](README.pt-BR.md)

---

## Arduino IDE prerequisites

1. Install the **arduino-pico** core (Earle Philhower):
   - In **File → Preferences → Additional Boards Manager URLs**, add:
     ```
     https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
     ```
   - In **Tools → Board → Boards Manager**, search for `pico` and install **"Raspberry Pi Pico/RP2040"**.

2. Select:
   - **Board**: `Raspberry Pi Pico` (or `Pico W`, depending on your "hand" hardware).
   - **Port**: the serial port where the "hand" is connected.

> The `SELF_BOOTSEL` command uses `rp2040.rebootToBootloader()`, which is
> specific to this core. If using the official Arduino Mbed core, that command
> needs adaptation — everything else works unchanged.

---

## Wiring

```
   Pico "hand" (this firmware)           Target Pico
   ----------------------------          -----------
   GP0   ─────────────────────────────►  BOOTSEL button pad/pin (hot side)
   GP1   ─────────────────────────────►  RUN (reset) button pad/pin (hot side)
   GND   ─────────────────────────────►  GND  (mandatory!)
```

> GPIOs operate in **emulated open-drain**: they become `OUTPUT` in `LOW`
> when "pressed" and return to `INPUT` (high impedance) when "released."
> This avoids fighting the target Pico's pull-up and eliminates short-circuit
> risk if the physical button is pressed simultaneously. **Never** driven HIGH.

Need different pins? Change `PIN_BOOTSEL` / `PIN_RESET` at the top of
`pico_hand.ino`.

---

## Compilation and flashing

1. Open `pico_hand/pico_hand.ino` in the Arduino IDE.
2. Put the Pico that will become the "hand" into BOOTSEL mode (hold BOOTSEL and plug USB).
3. Click **Upload** (`Ctrl+U`). After the first flash, the board reappears as a serial port and the IDE can upload without manual BOOTSEL.

To reflash later without touching buttons: send `SELF_BOOTSEL` over serial and the board enters BOOTSEL mode on its own.

---

## Serial protocol

- USB CDC, 115200 8N1 (baud rate is ignored on CDC; use this by convention).
- One command per line, terminator `\n` or `\r\n`.
- Response always starts with `OK`, `ERR`, or `PONG` (for `PING`).
- Commands are *case-insensitive*.

| Command             | Response                          | What it does                                                   |
|---------------------|-----------------------------------|----------------------------------------------------------------|
| `PING`              | `PONG`                            | Connectivity test.                                             |
| `RESET`             | `OK RESET`                        | Pulses target RUN (reset).                                     |
| `BOOTSEL`           | `OK BOOTSEL`                      | Full sequence: hold BOOTSEL, pulse RESET, release all.         |
| `HOLD BOOTSEL`      | `OK HOLD BOOTSEL`                 | Holds BOOTSEL pressed indefinitely.                            |
| `HOLD RESET`        | `OK HOLD RESET`                   | Holds RESET pressed indefinitely.                              |
| `RELEASE BOOTSEL`   | `OK RELEASE BOOTSEL`              | Releases BOOTSEL.                                              |
| `RELEASE RESET`     | `OK RELEASE RESET`                | Releases RESET.                                                |
| `STATUS`            | `STATUS BOOTSEL=... RESET=...`    | Current state of each line (`PRESSED`/`RELEASED`).             |
| `PINOUT`            | `PINOUT BOOTSEL=GP.. RESET=GP..`  | Shows which GPIOs are in use.                                  |
| `SELF_BOOTSEL`      | `OK SELF_BOOTSEL`                 | Puts the **hand itself** in BOOTSEL (for reflashing via cmd).  |
| `HELP`              | text list                         | Lists all commands.                                            |

> Warning: `HOLD` without a matching `RELEASE` keeps the button pressed
> until the next `RELEASE`, hand reset, or disconnection. In scripts,
> always ensure pairing (use `trap` in bash, `try/finally` in Python, etc.).

---

## Bash examples

Assuming the "hand" appears as `/dev/ttyACM1` (the target is usually
`/dev/ttyACM0`):

```bash
HAND=/dev/ttyACM1

# Configure the port once (raw, no echo, no CR/LF mangling).
stty -F "$HAND" 115200 raw -echo -echoe -echok -echoctl -echoke

# Helper: send command and read one response line.
hand() {
    echo "$1" > "$HAND"
    timeout 1 head -n 1 < "$HAND"
}

hand PING        # → PONG
hand RESET       # → OK RESET
hand BOOTSEL     # → OK BOOTSEL  (target enters BOOTSEL and reappears as RPI-RP2)
```

Typical recovery flow inside your test script:

```bash
if ! picotool info >/dev/null 2>&1; then
    echo "[hand] target unresponsive — forcing BOOTSEL"
    hand BOOTSEL
    sleep 1            # time for RPI-RP2 to mount
fi
picotool load -x firmware.uf2
```

---

## Project structure

```
pico_hand/
└── pico_hand.ino   # single, self-contained sketch
```

The folder must have the **same name** as the `.ino` file — Arduino IDE requirement.

---

## Electrical safety notes

- **Connect GND** between both boards. Without it, virtual buttons have no common reference and behavior is unpredictable.
- No series resistor is strictly required, but a 470 Ω – 1 kΩ resistor in series on each control line is good practice to limit current in case of wiring accidents.
- The firmware **never** drives lines HIGH, so it is safe to keep the hand connected even while someone presses the physical buttons on the target Pico.
