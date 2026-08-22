# Authorization matrix

Every HTTP route the firmware serves, the permission it requires, and why the
unauthenticated ones are safe to leave open. This file is the human-readable
half; `tools/check_authz.py` is the enforced half — it parses the route table
in `src/WebManager_Core.cpp`, follows each route to its handler, and fails CI if
a route is neither gated nor on its allowlist. Run `python3 tools/check_authz.py
--list` to print the live matrix straight from the source.

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
| `PERM_ALL_BITS`   | `0x03FF` | all ten named bits — **the ceiling any web-created account can hold** |
| `PERM_FULL_ADMIN` | `0xFFFF` | the built-in admin (config slot 0) or a CLI-granted mask |

### The two privilege tiers — this is the load-bearing invariant

`/api/commit_all` refuses any `perms` value above `PERM_ALL_BITS` (0x03FF) when
creating or editing users (`WebManager_Commit.cpp`). So a web administrator can
hand out at most all ten named bits. `PERM_FULL_ADMIN` (0xFFFF) is reachable
only two ways: the factory seed sets it on user slot 0 (`StorageManager.cpp`),
and the serial CLI `user perm <name> admin|0xFFFF` can assign it
(`AppManager_CmdHandlers.cpp`).

Three routes require **exactly** `perms == PERM_FULL_ADMIN`:

- `GET /api/backup` — downloads the entire LittleFS image, secrets included.
- `POST /api/restore?op=stage` and `POST /api/ota/apply` — stage and apply a
  firmware image (erases 1 MB of flash).

Therefore **no web-created account, however fully privileged, can read a full
backup or flash firmware** — those stay with the physical/CLI admin. This is a
deliberate boundary, not an accident of bit assignment; keep it when adding
routes. Note the matching-but-weaker rung: restoring *individual files* via
`/api/restore?op=apply` needs only `PERM_FILE_UPLOAD`, consistent with plain
`/api/upload` — writing a file you were granted upload rights to is not the same
capability as reading the whole image at once.

## The matrix

Generated view (`tools/check_authz.py --list`) annotated with the exact bit each
handler checks.

### Pages (cookie session)

| Route | Requires |
|-------|----------|
| `GET /` | `PERM_DASHBOARD` |
| `GET /history` | `PERM_HISTORY \| PERM_LOGS` (either) |
| `GET /config` | `PERM_SYS_CONFIG` |
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
| `GET /api/config` | `PERM_SYS_CONFIG` |
| `GET /api/users` | `PERM_USER_MGR` |
| `GET /api/themes` | `PERM_DASHBOARD` |
| `GET /api/alarms` | `PERM_SYS_CONFIG` |
| `GET /api/sensors` | `PERM_SYS_CONFIG` |
| `GET /api/calib` | `PERM_CALIB` |
| `GET /api/history_multi`, `/api/history_days`, `/api/export/history.bin`, `/api/history/open` | `PERM_HISTORY` |
| `GET /api/export/logs.bin`, `/api/logs` | `PERM_LOGS` |
| `GET /api/screenshot`, `/api/screenshot_chunk` | `PERM_SYS_CONFIG` |
| `GET /api/sec_status` | `PERM_USER_MGR` |
| `GET /download`, `/api/ls` | `PERM_FILE_READ` |
| `GET /api/backup` | **`== PERM_FULL_ADMIN`** |

### API — write

| Route | Requires |
|-------|----------|
| `POST /api/force_chpass` | authenticated (`perms != 0`) **and** password-change-required (`isPasswordChangeRequired`) — completes the forced password change |
| `POST /api/calib` | `PERM_CALIB` |
| `POST /api/save_sys` | `PERM_SYS_CONFIG` |
| `POST /api/commit_all` | `PERM_SYS_CONFIG` **plus per-section authz** (`WebCommitSections.h`) |
| `POST /api/reset_touch_cal`, `/api/history_rebind`, `/api/set_time` | `PERM_SYS_CONFIG` |
| `POST /api/clear_logs` | `PERM_LOGS` **and** `PERM_SYS_CONFIG` (both) |
| `POST /api/action` | `PERM_SYS_CONFIG` (per-`op` selector inside) |
| `POST /api/delete` | `PERM_FILE_DELETE` |
| `POST /api/mkdir` | `PERM_FILE_UPLOAD` |
| `POST /api/upload` | `PERM_FILE_UPLOAD` (enforced in the data callback **and** the completion handler) |
| `POST /api/restore` | validate `PERM_FILE_READ` · apply `PERM_FILE_UPLOAD` · stage `== PERM_FULL_ADMIN` — checked at the **first byte** of the multipart feed, not only at finish |
| `POST /api/ota/apply` | **`== PERM_FULL_ADMIN`** |

The `/api/restore` first-byte check is the fix for a real hole: the gate once
lived only in the finish handler, which the framework calls *after* the whole
body has streamed through the upload callback — and an apply feed writes each
entry straight to its final path, so `/config`, `/calib.csv` and `/history` were
already overwritten by the time the 403 was sent. Any new upload-callback route
must gate on the **first** `UPLOAD_FILE_START`, the way `handleUploadData` and
both restore branches now do.

## Unauthenticated by design

These answer without a session. Each is on the `PUBLIC_ALLOWLIST` in
`tools/check_authz.py`; adding to that list is a security decision.

| Route | Why it is safe open |
|-------|---------------------|
| `GET /login`, `GET /logout` | the login page and session teardown |
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
