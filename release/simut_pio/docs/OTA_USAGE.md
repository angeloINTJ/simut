# OTA Update Guide

The authoritative OTA documentation lives in [docs/MANUAL.md](MANUAL.md), section 12 "Firmware updates" — this page is just a quick recipe.

## Requirements

- Device running **v1.6.2-beta or newer**. Every earlier build shipped a defective applier that reported success without installing anything (see MANUAL §12 "Read this first"); older devices must be flashed over USB once.
- The `simut_vX.Y.Z.bin` from the GitHub release page — **not** the `.uf2` (that one is for USB/BOOTSEL flashing).
- An admin session on the device's web server.

## Steps

### 1. Log in (cookie `SIMUTSESS`)

```bash
NONCE=$(curl -s http://<device-ip>/api/login_init | jq -r .nonce)
PASS_HASH=$(echo -n '<password>' | sha256sum | cut -d' ' -f1)
curl -s -c cookies.txt http://<device-ip>/api/login \
     -d "user=admin&pass=$PASS_HASH&nonce=$NONCE"
```

### 2. Stage the image

```bash
curl -s -b cookies.txt \
     -F "file=@simut_vX.Y.Z.bin" \
     "http://<device-ip>/api/restore?op=stage&commit=1"
```

Takes about 30 s for a ~1 MB image — do not power off. The response must report `"v":0` and `"committed":1` before apply will do anything.

### 3. Apply

```bash
curl -s -b cookies.txt -X POST http://<device-ip>/api/ota/apply
```

Answers **202** and the device reboots. If it answers **503 "Display in use"**, retry after a few seconds.

### 4. Verify

Read the firmware version back from `/api/status` or from the display. **Never infer success from timing or HTTP codes alone** — the only proof of an installed update is the new version reporting itself.

## What survives, what doesn't

A config snapshot carries **Wi-Fi credentials, users and sensor slots** across the apply. The LittleFS filesystem — **history, language packs, calibration** — is **REFORMATTED** by the apply. Take a backup first (web file manager → **Backup**).

## Network note

If uploads on port 80 stall on your network, some routers kill long port-80 flows; using an alternate HTTP port works around it.
