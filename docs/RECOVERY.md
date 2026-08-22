# Recovery — Pico W Brick After OTA

> 📖 English · [Ver documentación en español](../README.es-ES.md) · [Ver documentação em português](../README.pt-BR.md)
>
> Recovery procedures for a Pico W that no longer boots. Read BEFORE
> any destructive test. Keep the `.uf2` of the last stable version saved
> locally in a folder separate from the project.

---

## When This Document Is Needed

Use in **one and only one** of these situations:

1. After `POST /api/ota/apply` (without `?test=1`) the device doesn't come
   back online in ~30 s, ping fails, serial silent.
2. Visible boot loop (`SIMUT firmware vX.Y.Z` repeated on serial without
   reaching "[BOOT] AP detect").
3. After power loss during apply (extremely rare, but plan §7 R3).
4. `picotool info` returns success for `2e8a:0003` (BOOTSEL) but the
   device in application mode (`2e8a:f00a`) doesn't enumerate CDC or
   doesn't respond to `\r\n`.

DO NOT use this document if:

- Boot OK but Wi-Fi failed (network problem, not firmware — see
  CLI `show net status`).
- Touch broken — doesn't affect boot from v1.0.0 onward (default cal
  applied automatically).

---

## Physical Prerequisites

- Access to the Pico W **BOOTSEL button**. On the official Pico W it is
  the only button on the board, on the side opposite the USB port.
- USB cable connected to the Linux host.
- Permission to write to `/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*`
  (member of `dialout`/`uucp` group, or run as sudo).
- `picotool` installed at `/usr/local/bin/picotool` (already present in
  this setup; also available at `~/.platformio/packages/tool-picotool-*/`).

---

## BOOTSEL Procedure (Without Needing Current Firmware)

This is the **guaranteed** path — it does not depend on software on the
device. It's what you use when the firmware is completely broken.

### Steps

1. Disconnect the USB cable from the Pico W.
2. **Press and hold the BOOTSEL button.**
3. Connect the USB cable with the button still pressed.
4. Release the button. The Pico W should appear as
   `Bus 003 Device NNN: ID 2e8a:0003 Raspberry Pi RP2 Boot` in
   `lsusb`.
5. Confirm:
   ```bash
   picotool info | head
   ```
   Should list the device in BOOTSEL.
6. Erase everything (defensive — forces LittleFS reformat on next boot):
   ```bash
   picotool erase
   ```
7. Flash the `.uf2` of the last stable version:
   ```bash
   picotool load -x /path/to/firmware.uf2
   ```
   `-x` reboots the device automatically after flashing.
8. Wait ~30s, then verify:
   ```bash
   ls /dev/serial/by-id/                  # Should show Raspberry_Pi_Pico_W_*
   lsusb | grep 2e8a:f00a                 # Application mode (not BOOTSEL)
   ```
9. Confirm version via serial:
   ```bash
   ./.venv/bin/python3 -c "
   import serial, time
   s = serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00', 115200, timeout=2)
   time.sleep(0.5); s.write(b'\r\nshow system info\r\n'); time.sleep(2)
   print(s.read_all().decode(errors='replace'))
   s.close()"
   ```

### Post-Recovery

After reflash, the device will be in **factory state** (LFS wiped). You
will need to reconfigure via CLI:

```
conf system ssid <SSID>
conf system pass <PASSWORD>
write memory
reload confirm
```

The admin web password is regenerated automatically — find it in the
serial boot output (line `SEC-003: FACTORY DEFAULTS ATIVADO` + one-time
password).

---

## Software BOOTSEL Trigger (1200 bps Trick)

When the device **still responds to USB CDC** but the app firmware is
frozen (boot loop, but USB descriptor enumerates):

```bash
./.venv/bin/python3 -c "
import serial, time
s = serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00', 1200, timeout=0.5)
time.sleep(0.1); s.close()
"
sleep 4
lsusb | grep 2e8a:0003
```

Arduino native USB convention: opening the port at 1200 bps + close
triggers reset-to-BOOTSEL. Works as long as the firmware has the USB
stack alive (before `WiFi.end` in `ota_apply_pending_update`, for
example).

After confirming BOOTSEL via lsusb, proceed as in the procedure above
from step 5.

---

## What NOT to Do

- ❌ `picotool erase` while the device is in APP mode — fails; only
  works in BOOTSEL.
- ❌ `picotool reboot` expecting to recover a frozen device — only
  works if the device responds.
- ❌ Reflash without `picotool erase` first — staging area + metadata
  may have garbage from the broken apply, causing strange behavior
  (even if the sketch slot is OK).
- ❌ Delete the old version `.uf2` before confirming the new one is
  stable on HW. Always keep the **rollback** available.

---

## Diagnosis — Which Apply Stage Broke

If the device can be recovered, read the serial output of the **first
post-apply boot** to identify where the orchestrator stopped:

| Serial Symptom | Stage | Likely Cause |
|---|---|---|
| No log at all | Pre-orchestrator | Crash in `WiFi.end()` or `LittleFS.end()` |
| Banner repeated without `[BOOT] OTA post-apply` | Pre-applier | metadata write failed |
| `[BOOT] OTA post-apply detected` appears | Apply worked | Post-update boot OK |
| `[BOOT] OTA post-apply detected: state=2 attempts=N` with N>1 | Loop | `OTA_MAX_APPLY_ATTEMPTS` will be reached |

If `OTA_MAX_APPLY_ATTEMPTS` (3) is reached, `ota_apply_pending_update`
returns `MAX_ATTEMPTS` without destroying — the device continues booting
the current firmware.

---

## Versions Kept as Rollback

Keep the previous release's `simut_vX.Y.Z.uf2` from the
[GitHub releases page](https://github.com/angeloINTJ/simut/releases) as
your rollback image — every release publishes one.

Recommended: copy it locally (not in the repo, out of reach of accidental
erase) each time a new stable version is validated.
