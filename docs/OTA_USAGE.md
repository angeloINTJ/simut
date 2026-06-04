# How to Use OTA on SIMUT

> Practical guide for updating Pico W firmware via web, without needing
> a USB cable or BOOTSEL button.

## What It Is

OTA (Over-The-Air) updates the Pico W firmware remotely via web —
no USB cable or BOOTSEL button needed.

## Prerequisites

- SIMUT device already booting with firmware **v1.0.0+** (any recent version)
- Wi-Fi configured and device responding on the local network IP
- Active admin login (known password)
- New firmware build: `firmware.bin` in `.pio/build/pico_w_release/`
- USB cable connected (only if something goes wrong and you need BOOTSEL recovery)

## ⚠️ WARNING Before You Start

**OTA reformats LittleFS** — you lose:
- WiFi config (SSID, password, IP mode)
- Admin password (reverts to factory OTP)
- Sensor mapping (slot N → sensor)
- Measurement history (`/history/*`)
- Custom themes and languages

**Recommended flow**: backup before → OTA → restore backup after.

## Procedure — Option 1: Automatic Orchestrator

The easiest way, with a single command:

```bash
cd /home/angelo/Documentos/SIMUT
./tools/ota_apply.py \
    --ip 192.168.3.195 \
    --user admin --pass 'YourCurrentPassword' \
    --firmware .pio/build/pico_w_release/firmware.bin
```

The script automatically: logs in → backs up .bkp → uploads firmware →
commits → applies → waits for boot.

If admin is in factory state (freshly reset), also use
`--new-pass 'NewPassword'` to chpass first.

## Procedure — Option 2: Manual Step-by-Step

### 1. Build Firmware

```bash
cd /home/angelo/Documentos/SIMUT
~/.platformio/penv/bin/pio run
```

Confirm `[SUCCESS]` and generates `.pio/build/pico_w_release/firmware.bin`.

### 2. Web Login + Backup

Open `http://<pico-IP>` in the browser, log in with admin.

In **Files** → **Backup** button → save `.bkp` locally.

### 3. Upload Firmware via curl or Web UI

Via curl (more robust):

```bash
# Get a nonce
NONCE=$(curl -s http://192.168.3.195/api/login_init | jq -r .nonce)
PASS_HASH=$(echo -n 'YourPassword' | sha256sum | cut -d' ' -f1)

# Login (saves cookie)
curl -s http://192.168.3.195/api/login \
    -d "user=admin&pass=$PASS_HASH&nonce=$NONCE" \
    -c /tmp/simut.cookie

# Upload firmware with commit=1
curl -s http://192.168.3.195/api/restore?op=stage\&commit=1 \
    -F "file=@.pio/build/pico_w_release/firmware.bin" \
    -b /tmp/simut.cookie
```

Expected response: `{"st":5,"bytes":...,"v":0,"committed":1}`.
- `st=5` = STAGED
- `v=0` = valid
- `committed=1` = metadata written

Time: ~30s for 947 KiB of firmware.

### 4. Trigger Apply

```bash
curl -s http://192.168.3.195/api/ota/apply -b /tmp/simut.cookie -X POST
```

Response: `{"accepted":true,"mode":"apply"}`. Device reboots immediately.

### 5. Wait for Boot

**Expected time**: 60-90 seconds. During this time:
- Device USB CDC enumerates but CLI stays silent
- LFS auto-format in progress (~13s)
- Core 1 lockout recovery (~10s)
- Factory init (touch cal default, admin OTP regen, etc.)

**If it takes more than 3 minutes without boot**: physical power cycle
(unplug + replug USB). This is the documented **Bug 2** — related to
CYW43/TFT chip state needing a real power-off to reset.

### 6. Capture OTP from USB Serial

After boot, connect to USB Serial and read the one-time admin password:

```
SEC-003: FACTORY DEFAULTS ACTIVE
Initial ADMIN password: ABCD1234
Change on first login (forced).
```

### 7. Reconfigure WiFi via Serial CLI

```
conf system ssid YourNetwork
conf system pass YourWiFiPassword
conf ip dhcp
write memory
reload confirm
```

### 8. Restore Backup

Web login with the OTP, do chpass, then in **Files** → **Restore**
→ select the `.bkp` downloaded in step 2.

Expected response: `{"st":0,"chip":"...","fc":N,"fsm":0}`.
- `st=0` = OK
- `fc` = files counted

Device reapplies config + history + sensors. No reload needed —
restore applies directly.

## Post-OTA Verification

Via CLI or web:

```bash
# CLI: confirm new version
SIMUT> show system info
 Firmware:  v1.0.0   ← new version

# Web: confirm restored config
curl -s http://192.168.3.195/api/perms -b /tmp/simut.cookie
# {"user":"admin","perms":65535,"version":"v1.0.0"}
```

## Recovery If OTA Fails

### Scenario A: Device in BOOTSEL (USB ID `2e8a:0003`)

Boot2/firmware corrupted. Reflash via picotool:

```bash
# Have a local firmware backup? Use it
picotool load -f -x ~/firmware-rollback-simut/firmware-v1.0.0.uf2

# Or flash the current build
picotool load -f -x .pio/build/pico_w_release/firmware.uf2
```

### Scenario B: Device in App Mode (USB ID `2e8a:f00a`) but Firmware Silent (Bug 2)

1. Try 1200bps trick to enter BOOTSEL:
   ```bash
   ./.venv/bin/python3 -c "
   import serial,time
   s=serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00',1200)
   time.sleep(0.5); s.close()
   "
   ```

2. If 1200bps fails (even USB hung): **physical power cycle** —
   unplug USB for 30s+, replug.

3. If device enters BOOTSEL or returns to app mode: follow scenario A
   or wait for complete boot.

### Scenario C: Everything Frozen, BOOTSEL Inaccessible

Press and hold the BOOTSEL button on the Pico W while plugging in the
USB cable. Device enters forced BOOTSEL. Reflash via picotool.

## Technical Limits

| Item | Value |
|------|-------|
| Maximum firmware size | 1020 KiB (app slot) |
| Current v1.0.0 size | 947 KiB (91.6% of limit) |
| Free margin | ~85 KiB |
| Upload time | ~32s for 947 KiB |
| Apply time (erase+write) | ~13s |
| Post-apply boot time | 60-90s typical |
| Apply attempts (anti-loop) | 3 max |

## Quick FAQ

**Q: Do I need to backup every OTA?**
A: Yes. The LFS partition is erased during the stage upload. Without
backup, you lose everything.

**Q: Can I OTA from compressed firmware (.bin.gz)?**
A: The server only accepts RAW (.bin). The PIO build already generates
.bin directly.

**Q: How many times can I apply OTA?**
A: No theoretical limit. Pico W QSPI flash withstands ~100k erase cycles
per sector. Each apply does ~255 erases.

**Q: Does it work over the internet (not just LAN)?**
A: Technically yes if the device has a public IP + port 80 forwarded.
But there's no HTTPS — credentials travel in the clear. Use only on LAN
or via VPN.

**Q: Does the .bkp backup work for another Pico W?**
A: No. The backup is tied to the chip_id (RP2040 unique 64-bit ID).
Restore on a different chip returns error `st=6 chip mismatch`.

---

## Related Documentation

- `docs/OTA_FASE8.md` — full flow + Bug 2 intermittent boot diagnosis
- `docs/RECOVERY.md` — recovery procedures via BOOTSEL
- `docs/test_reports/` — hardware validation reports

**Last updated**: latest firmware.
