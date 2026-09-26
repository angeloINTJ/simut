# Authorization matrix

Every HTTP route the firmware serves, the permission it requires, and why the
unauthenticated ones are safe to leave open. This file is the human-readable
half; `tools/check_authz.py` is the enforced half — it parses the route
registrations across `src/WebManager*.cpp`, follows each route to its handler,
and fails CI if a route is neither gated nor on its allowlist. It scans every
`WebManager*.cpp`, not just the core, because the feature-model seam work
(`docs/analysis/MODELO_DE_RECURSOS.md`, P2) moves each feature's routes into its
own unit — the panel mirror into `WebManager_History.cpp`, `POST /api/tls` into
`WebManager_Tls.cpp` — so a route is audited wherever it registers. Run
`python3 tools/check_authz.py --list` to print the live matrix straight from the
source.

## How a refusal is answered

Every JSON route gates through `requirePerm(bits)` (`WebManager_Auth.cpp`):

- **401** `{"error":"Unauthorized"}` — no live session at all. The token was
  never issued, expired (15 min idle), or died with a reboot. The fix is to
  log in again.
- **403** `{"error":"Forbidden"}` — a live session whose account lacks the
  bits. Logging in again changes nothing; the account does.

Before this branch every route but `/api/perms` answered 403 for both, and a
client could not tell "log in again" from "this account cannot" without a
second request. HTML pages keep redirecting to `/login`.

The session token travels as the `SIMUTSESS` cookie **or** as
`Authorization: Bearer <token>` — the same token, the same three slots, the
same idle timeout. `Basic` remains the credential of `/metrics` only.

## Permission bits

A session carries a 16-bit permission mask (`SystemDefs_Limits.h`). A handler
gates by testing the bits it needs against `getAuthPerms()`.

| Bit | Value | Grants |
|-----|-------|--------|
| `PERM_DASHBOARD`  | `0x0001` | live readings, status, themes |
| `PERM_HISTORY`    | `0x0002` | history pages, export, replay |
| `PERM_LOGS`       | `0x0004` | event log read/export |
| `PERM_SYS_CONFIG` | `0x0008` | system config, alarms, sensors, time, screenshot |
| `PERM_NET_CONFIG` | `0x0010` | network config |
| `PERM_FILE_READ`  | `0x0020` | file listing / download |
| `PERM_FILE_UPLOAD`| `0x0040` | file upload / mkdir |
| `PERM_FILE_DELETE`| `0x0080` | file delete |
| `PERM_USER_MGR`   | `0x0100` | user management, security status |
| `PERM_CALIB`      | `0x0200` | sensor calibration |
| `PERM_ALARM_LIMITS` | `0x0400` | **panel**: edit a sensor's alarm limits (config v24) |
| `PERM_ALARM_BLOCK`  | `0x0800` | **panel**: enable/disable a sensor's alarms, and "Deactivate" on the alarm pop-up |
| `PERM_MAINT`        | `0x1000` | **panel**: open/close a sensor's maintenance window |
| `PERM_ALL_BITS`   | `0x1FFF` | all thirteen named bits — **the ceiling any web-created account can hold** |
| `PERM_FULL_ADMIN` | `0xFFFF` | the built-in admin (config slot 0) or a CLI-granted mask |

### The two privilege tiers — this is the load-bearing invariant

`/api/commit_all` refuses any `perms` value above the CALLER's own mask, and
above `PERM_ALL_BITS` (0x1FFF), when
creating users (there is no edit action — a role change is `del` + `add`) (`WebManager_Commit.cpp`). So a web administrator can
hand out at most all thirteen named bits. `PERM_FULL_ADMIN` (0xFFFF) is reachable
only two ways: the factory seed sets it on user slot 0 (`StorageManager.cpp`),
and the serial CLI `user perm <name> admin|0xFFFF` can assign it
(`AppManager_CmdHandlers.cpp`).

Four routes require **exactly** `perms == PERM_FULL_ADMIN`:

- `GET /api/backup` — downloads the entire LittleFS image, secrets included.
- `POST /api/restore?op=stage` and `POST /api/ota/apply` — stage and apply a
  firmware image (erases 1 MB of flash).
- `POST /api/restore?op=apply` — overwrites the whole LittleFS from a `.bkp`,
  `/config/system.bin` included (the `.bkp` is the full-FS dump from
  `/api/backup`).

Therefore **no web-created account, however fully privileged, can read a full
backup or flash firmware** — those stay with the physical/CLI admin. This is a
deliberate boundary, not an accident of bit assignment; keep it when adding
routes. (`/api/restore?op=apply` used to sit at `PERM_FILE_UPLOAD`; it was
raised to `== PERM_FULL_ADMIN` because a forged `.bkp` can name
`/config/system.bin` as a destination — finding ACH-03.)

## The matrix

Generated view (`tools/check_authz.py --list`) annotated with the exact bit each
handler checks.

### Pages (cookie session)

| Route | Requires |
|-------|----------|
| `GET /` | `PERM_DASHBOARD` |
| `GET /history` | `PERM_HISTORY \| PERM_LOGS` (either) |
| `GET /config` | `PERM_SYS_CONFIG` |
| `GET /telemetry` | `PERM_SYS_CONFIG` |
| `GET /network` | `PERM_NET_CONFIG` |
| `GET /users` | `PERM_USER_MGR` |
| `GET /files` | `PERM_FILE_READ` |
| `GET /alarms` | `PERM_SYS_CONFIG` |
| `GET /license` | `PERM_DASHBOARD` |
| `GET /force_chpass` | authenticated (`perms != 0`) **and** password-change-required |

### API — read

| Route | Requires |
|-------|----------|
| `GET /api/status` | `PERM_DASHBOARD` |
| `GET /metrics` | `PERM_DASHBOARD` (cookie **or** HTTP Basic; shares the login lockout) |
| `GET /api/perms` | authenticated (`perms != 0`) |
| `GET /api/network` | `PERM_NET_CONFIG` |
| `GET /api/wifi/scan` | `PERM_NET_CONFIG` — same gate as the page that consumes it. It lists the neighbourhood's SSIDs, which is exactly what someone configuring the radio needs and nothing an unauthenticated caller should be able to survey. |
| `GET /api/config` | `PERM_SYS_CONFIG` |
| `GET /api/users` | `PERM_USER_MGR` |
| `GET /api/themes` | `PERM_DASHBOARD` |
| `GET /api/alarms` | `PERM_SYS_CONFIG` |
| `GET /api/sensors` | `PERM_SYS_CONFIG` |
| `GET /api/calib` | `PERM_CALIB` |
| `GET /api/history_multi`, `/api/history_days`, `/api/export/history.bin`, `/api/history/open` | `PERM_HISTORY` |
| `GET /api/export/logs.bin`, `/api/logs` | `PERM_LOGS` |
| `GET /api/logcodes` | `PERM_LOGS` — the event names the log view prints: the active pack's `@LOGCODES`, then the firmware's English names (`?l=en`: English only). Static labels, no device data; gated anyway because its only reader is the log view, which already needs the bit. |
| `GET /api/screenshot`, `/api/screenshot_chunk` | `PERM_SYS_CONFIG` |
| `GET /api/screen_stream` | `PERM_SYS_CONFIG` |
| `POST /api/touch` | `PERM_SYS_CONFIG` — drives the panel UI; the panel's PIN keypad still guards the settings screens, and every panel action tests the bit of the account the PIN identified (below) |
| `GET /api/keypad` | `PERM_SYS_CONFIG` — the four scrambled card faces, the same ones `show display keypad` prints. It describes the glass, not the secret: an account that may read `/api/screenshot` already has a picture of the identical cards, and neither says which slot of a card is the digit. Empty faces when the keypad is not the live screen |
| `GET /api/sec_status` | `PERM_USER_MGR` |
| `GET /api/ls` | `PERM_FILE_READ` |
| `GET /download` | `PERM_FILE_READ`, **plus** `PERM_HISTORY` for `/history/...` and `PERM_LOGS` for `*.blog` (`downloadPermFor`) |
| `GET /api/backup` | **`== PERM_FULL_ADMIN`** |

### API — write

| Route | Requires |
|-------|----------|
| `POST /api/force_chpass` | authenticated (`perms != 0`) **and** password-change-required (`isPasswordChangeRequired`) — completes the forced password change |
| `POST /api/calib` | `PERM_CALIB` |
| `POST /api/save_sys` | `PERM_SYS_CONFIG` |
| `POST /api/commit_all` | **ANY ONE** of `PERM_SYS_CONFIG`, `PERM_NET_CONFIG`, `PERM_USER_MGR` at the door (`commitEntryPerms( )`), **then per-section authz** (`WebCommitSections.h`) — a pure user manager gets in on purpose, or the role could manage nobody. Since V-09 the `users` section also refuses to grant a bit the CALLER does not hold (`commitGrantAllowed`), and only the admin itself or a full admin may set slot 0's panel PIN (`commitPinTargetAllowed`). `_dry=1` runs the same gates and parsers on a copy and writes nothing — it needs the same bits as the real thing |
| `POST /api/reset_touch_cal`, `/api/history_rebind`, `/api/set_time` | `PERM_SYS_CONFIG` |
| `POST /api/clear_logs` | `PERM_LOGS` **and** `PERM_SYS_CONFIG` (both) |
| `POST /api/action` | `PERM_SYS_CONFIG` (per-`op` selector inside; `op=reboot` also answers 409 with a pending password change and 503 during touch calibration, like `commit_all`) |
| `POST /api/delete` | `PERM_FILE_DELETE` |
| `POST /api/mkdir` | `PERM_FILE_UPLOAD` |
| `POST /api/upload` | `PERM_FILE_UPLOAD` (enforced in the data callback **and** the completion handler) |
| `POST /api/restore` | validate `PERM_FILE_READ` · apply **`== PERM_FULL_ADMIN`** · stage `== PERM_FULL_ADMIN` — checked at the **first byte** of the multipart feed, not only at finish |
| `POST /api/ota/apply` | **`== PERM_FULL_ADMIN`** |
| `POST /api/tls` | **`== PERM_FULL_ADMIN`** — the only route that writes into `/config`, and the reason it may is in `WebManager_Tls.cpp`: two fixed paths, no filename from the request, and the pair must parse and belong to each other before either file is written. Compiled only where HTTPS is (`pico_w_release`). |

The `/api/restore` first-byte check is the fix for a real hole: the gate once
lived only in the finish handler, which the framework calls *after* the whole
body has streamed through the upload callback — and an apply feed writes each
entry straight to its final path, so `/config`, `/calib.csv` and `/history` were
already overwritten by the time the 403 was sent. Any new upload-callback route
must gate on the **first** `UPLOAD_FILE_START`, the way `handleUploadData` and
both restore branches now do.

## The panel (config v24)

The panel is a fourth surface, next to the web, the CLI and Bluetooth, and it
has its own identity: a **PIN** of 4 to 8 digits (`PinKb::FIRST`..`LAST`) per
account, unique across accounts because the keypad has no username field — the
PIN *is* the lookup.
CFG on the dashboard opens the keypad; the account it identifies is the panel
session until the settings tree is left. `EVT_AUTH_PIN` hands Core 0 the taps of the
attempt, and each one carries THE THREE GLYPHS THAT WERE ON THE CARD when it
was made — the deal is rolled again after every tap, so a card index would name
something else by the time it is read. The keypad never asks which of the three
was meant, so an entry stands for up to 3^n strings.
`StorageManager::findUserByPinSet( )` walks that tree depth-first against the
device-wide salt (`pinAuth.pinSalt`), one hash per node because the digest is a
per-character chain, and returns the account a candidate belongs to. It walks
the WHOLE tree: if two accounts both match, neither is let in, because the
panel cannot ask which was meant. An attempt costs the same whether or not a
PIN exists, and 8 taps cost ~360 ms of Core 0 (measured on the rig,
2026-09-19; 4 taps are lost in the noise).

Account records are cleared whole on delete, and a PIN digest is cleared when a
slot is allocated. Until 2026-09-19 deletion only lowered the `active` flag, so
the next account to land on that slot inherited the previous holder's PIN and
could be opened with it — found on the rig, where a freshly created account
already reported holding a PIN.

The consequence is worth stating plainly: a blind four-tap entry covers 81 of
the 10,000 four-digit PINs, so against 32 accounts it has roughly a 26% chance
of matching one. Six digits bring it to ~2%, eight to ~0.2%. What bounds the
online attack is the lockout ladder below, and the deployment answer is longer
PINs. The keypad lockout ladder is the one the device PIN always had: two
free tries, 5 s, 15 s, 60 s, then a lockout only a reboot clears
(`SEC_PIN_FAIL`/`SEC_PIN_LOCKOUT`, 309/310; `SEC_PIN_OK` 308 with the account).
An entry that fits two accounts is `SEC_PIN_AMBIGUOUS` (311) and not a wrong
PIN: it is the failure a full account table produces, and the answer to it is
longer PINs, not another try — although another try usually works, because the
deal is different.

| Panel action | Requires |
|---|---|
| Settings menu items Themes, Sounds, Language, Touch calibration, Display alignment | `PERM_SYS_CONFIG` (the menu lists only what the session's bits reach) |
| Alarms → a sensor → Alarm limits | `PERM_ALARM_LIMITS` |
| Alarms → a sensor → Alarms ON/OFF · "Deactivate" on the alarm pop-up | `PERM_ALARM_BLOCK` |
| Alarms → a sensor → Maintenance (open / close) | `PERM_MAINT` |
| Users (list, new account, bits, another account's PIN, delete) | `PERM_USER_MGR` |
| One's own PIN, License, System status | any identified account |

Core 0 checks the bit again on every event (`AppManager_Panel.cpp`,
`panelAllowed`) and logs a refusal as `APP_UI_PERM_DENIED` (458); Core 1 only
decides what to draw. A user created at the panel holds panel bits only — no
web page opens for it until an administrator grants a page bit and resets its
password.

## Unauthenticated by design

These answer without a session. Each is on the `PUBLIC_ALLOWLIST` in
`tools/check_authz.py`; adding to that list is a security decision.

| Route | Why it is safe open |
|-------|---------------------|
| `GET /login`, `GET /logout` | the login page and session teardown — `/logout` ends **only the session whose own token it carries** (cookie or `Authorization: Bearer`), which is why presenting the token is authentication enough |
| `GET /api/login_init` | issues the login nonce — the pre-auth step |
| `POST /api/login` | the credential check itself; gated by the per-IP exponential lockout, not a prior session |
| `POST /api/login_chpass` | forced first-login password change; gated by lockout + must-change |
| `GET /lang.js`, `GET /style.css` | static assets the login page needs before a session exists |
| `GET /api/lang` | the UI translation dictionary — **UI strings only**, served from a fixed internal path (no user input, no traversal surface), same tier as `/lang.js`. If anything sensitive ever needs to ride this path, gate it. |
| `GET /favicon.ico`, `GET /apple-touch-icon.png` | site icons; the latter a 204 stub so iOS stops 404-spamming |

`onNotFound` is not in the table: it redirects off-host requests (captive-portal
behaviour) and otherwise returns 404, serving no protected data.

## Invariants the gate enforces

1. Every `_server->on(...)` route resolves to a handler that contains an
   authorization check, **or** the route is allowlisted with a reason.
2. A route bound to two handlers (the upload/restore callback pairs) must gate
   in **both**.
3. The check is conservative by design: it proves a gate is *present*, not that
   the *bit is correct*. A wrong bit is a review question against this document;
   a missing gate is what CI blocks. Keep this file in sync when a route's
   required permission changes — the table above is the reference the bit-level
   review reads from.
