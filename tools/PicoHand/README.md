# PicoHand — Automated Test Fixture

Robotic "hand" firmware for a Raspberry Pi Pico that remotely drives the
**BOOTSEL** and **RESET** buttons of a target Pico via USB serial commands.

Built to unlock fully automated test pipelines (SIMUT) when the target
hangs before accepting the next binary — zero human intervention.

## How it works

```
┌─────────────┐        ┌──────────────┐
│ Host (CI)   │──USB──▶│  PicoHand    │──GP0──▶ BOOTSEL (target)
│ pico_hand.sh│        │  (this fw)   │──GP1──▶ RESET   (target)
└─────────────┘        └──────────────┘──GND──▶ GND     (target)
```

The host sends text commands over USB CDC (`PING`, `RESET`, `BOOTSEL`, etc.).
PicoHand drives its GPIOs in **emulated open-drain** — pulling lines LOW to
"press" and going high-impedance to "release". Never drives HIGH, so it's
safe even if someone presses the physical buttons simultaneously.

## Quick start

```bash
# Flash the firmware once (Arduino IDE or picotool)
# Then use from any shell script:

source tools/PicoHand/pico_hand.sh
hand_init                      # auto-detect which /dev/ttyACM* is the hand
hand PING                      # → PONG
hand BOOTSEL                   # target enters BOOTSEL mode
```

## Files

| File                          | Purpose                                                      |
|-------------------------------|--------------------------------------------------------------|
| `pico_hand/pico_hand.ino`     | Arduino sketch (single file, self-contained)                 |
| `pico_hand.sh`                | Bash wrapper — `hand_init`, `hand <cmd>`, `hand_release_all` |
| `MANUAL_CLAUDE_CODE.md`       | Claude Code agent operation manual (English)                 |
| `MANUAL_CLAUDE_CODE.pt-BR.md` | Same manual in Portuguese                                    |
| `pico_hand/README.md`         | Hardware wiring and protocol reference (English)             |
| `pico_hand/README.pt-BR.md`   | Same reference in Portuguese                                 |

## Electrical safety

- **Always connect GND** between PicoHand and target. Without it, virtual
  buttons have no common reference.
- GPIOs operate in emulated open-drain: `OUTPUT LOW` = pressed, `INPUT` =
  released. **Never** driven HIGH.
- Optional 470 Ω – 1 kΩ series resistor on each control line limits current
  in case of wiring accidents.
- Safe to keep connected while someone presses physical buttons on the target.
