#!/usr/bin/env python3
"""check_fsguard.py — the /config filesystem guards, pinned so they cannot silently rot.

The /config directory is the credential store: system.bin holds the per-user
password hashes and the wifi/mqtt/telemetry secrets, and web_cert.pem /
web_key.pem are the TLS keys. Two guards protect it, both checked here:

1. File-manager paths (delete / upload / ls) must refuse /config via
   isSecretFsPath / isSecretFsDir (findings ACH-01/02/04). A user with
   PERM_FILE_DELETE or PERM_FILE_UPLOAD could otherwise delete or overwrite the
   store or enumerate its filenames.

2. The restore-apply gate must be FULL_ADMIN, not PERM_FILE_UPLOAD (finding
   ACH-03). The .bkp backup is a whole-filesystem dump that includes
   /config/system.bin by design, so the restore legitimately writes it — the
   protection is the admin-only gate, matching /api/backup. A file-upload user
   applying a forged .bkp could otherwise overwrite the credential store.

Like tools/check_authz.py this is a *source* check (the arduino-pico WebServer
cannot be stood up on the native host), so it asks only that the guard be
PRESENT — not that it is correct. A wrong guard is a code-review question; a
REMOVED guard is what this catches mechanically.

Usage:  python3 tools/check_fsguard.py        (exit 0 clean, 1 on a finding)

CI runs the no-arg form as its own step.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (relative path, definition marker, human label, required guard token).
# handleApiLs takes a directory, so it uses isSecretFsDir (which also matches
# the bare "/config"); the file handlers use isSecretFsPath.
TARGETS = [
    ("src/WebManager_Files.cpp", "void WebManager::handleDelete", "handleDelete", "isSecretFsPath"),
    ("src/WebManager_Files.cpp", "void WebManager::handleUploadData", "handleUploadData", "isSecretFsPath"),
    ("src/WebManager_Files.cpp", "void WebManager::handleApiLs", "handleApiLs", "isSecretFsDir"),
]

# The restore-apply permission gate. These are the old (buggy) spellings that
# selected PERM_FILE_UPLOAD for apply; if any reappears, the gate regressed.
RESTORE_GATE_FILE = "src/WebManager_Ota.cpp"
RESTORE_GATE_FORBIDDEN = [
    "RestoreMode::APPLY) ? PERM_FILE_UPLOAD",
    "is_apply ? PERM_FILE_UPLOAD",
]


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def extract_body(text, marker):
    """Return the body (opening brace to its match) of the function whose
    definition contains the marker string, or None."""
    idx = text.find(marker)
    if idx < 0:
        return None
    start = text.find("{", idx)
    if start < 0:
        return None
    depth = 0
    i = start
    while i < len(text):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    return None


def main():
    failures = []

    # 1. /config path guard on the file-manager mutation handlers.
    for rel, marker, name, guard in TARGETS:
        text = read(os.path.join(ROOT, rel))
        body = extract_body(text, marker)
        if body is None:
            failures.append(f"{name}: function definition not found")
            continue
        if guard not in body:
            failures.append(f"{name}: /config guard ({guard}) missing from body")

    # 2. Restore-apply must be FULL_ADMIN, not PERM_FILE_UPLOAD.
    ota_text = read(os.path.join(ROOT, RESTORE_GATE_FILE))
    for bad in RESTORE_GATE_FORBIDDEN:
        if bad in ota_text:
            failures.append(f"restore apply gate: forbidden pattern present: {bad!r}")

    if failures:
        print("FSGUARD GATE: FAIL")
        for f in failures:
            print(f"  - {f}")
        print()
        print("The /config credential store must stay out of the file-manager")
        print("mutation paths, and restore-apply must stay admin-only.")
        print("See findings ACH-01..04 and FsSecretPath.h.")
        return 1

    print(f"FSGUARD GATE: clean ({len(TARGETS)} paths + restore-apply guard /config)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
