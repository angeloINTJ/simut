# OTA Update Guide

The authoritative OTA documentation lives in [docs/MANUAL.md](MANUAL.md), section 12 "Firmware updates" — this page is just a quick recipe.

## Requirements

- Device running **v1.6.2-beta or newer**. Every earlier build shipped a defective applier that reported success without installing anything (see MANUAL §12 "Read this first"); older devices must be flashed over USB once.
- The `simut_vX.Y.Z.bin` from the GitHub release page — **not** the `.uf2` (that one is for USB/BOOTSEL flashing). It is signed: a device running a build with signed OTA stages nothing else (`"v":8` to `"v":12`, [analysis/OTA_ASSINADA.md](analysis/OTA_ASSINADA.md)). A local build reaches a field device by USB only; the bench rig takes it signed with the bench key (`tools/ota_test.py` does that).
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

Takes about 30 s for a ~1 MB image, about 2 s of it the signature check — do not power off. The response must report `"v":0` and `"committed":1` before apply will do anything. The device's panel shows each step meanwhile — the bar while the image arrives, the check, and the reason when it refuses one (MANUAL chapter 11, "A tela de atualização").

### 3. Apply

```bash
curl -s -b cookies.txt -X POST http://<device-ip>/api/ota/apply
```

Answers **202**, about 2 s later — the signature is checked again on what the staging area holds — and the device reboots. Until it does, the panel says it is installing. If it answers **503 "Display in use"**, retry after a few seconds. A **409** with `"v"` means the staged image no longer verifies (another stage was started after it): stage it again.

### 4. Verify

Read the firmware version back from `/api/perms` (`version`) — or, from this branch on, from `/api/status` (`sys.ver`, with `sys.env` naming the variant) — or from the display. **Never infer success from timing or HTTP codes alone** — the only proof of an installed update is the new version reporting itself.

## What survives, what doesn't

A config snapshot carries **Wi-Fi credentials, users and sensor slots** across the update. The image is received into the flash area of the LittleFS filesystem, so the rest of it — **history, language packs, calibration** — is lost once the stage starts writing, also when the upload is cut or the image refused (MANUAL chapter 17, "O que sobrevive e o que se perde"). Take a backup first (web file manager → **Backup**) and restore it afterwards.

Without a pack in `/lang` the interface falls back to English, which is a real
outcome of a normal upgrade and not a fault. Since v2.6.0-beta the `.lng` files
ship as **release assets** beside the images, so the pair always matches the
firmware: upload one on the Files page or with `POST /api/upload` (never
`uploadfs` — it wipes `/lang` and `/history` with it) and reboot. A pack is read
at boot and nowhere else.

## Network note

If uploads on port 80 stall on your network, some routers kill long port-80 flows; using an alternate HTTP port works around it.
