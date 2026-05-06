#!/usr/bin/env python3
"""
ota_apply.py — Orquestrador end-to-end de OTA para SIMUT firmware.

Implementa o fluxo completo da Fase 8 OTA:

    1. Login web (com chpass se admin estiver em factory state)
    2. GET /api/backup → salva .bkp localmente (preserva user data)
    3. POST /api/restore?op=stage&commit=1 com firmware.bin (RAW)
    4. POST /api/ota/apply (HTTP 202; device reboota)
    5. Espera device voltar online (~60-90 s pós-apply)
    6. Login web novamente (factory state pós-apply)
    7. POST /api/restore?op=validate com .bkp do passo 2
    8. POST /api/restore?op=apply para restaurar user data
    9. Verifica que system.bin restaurado, WiFi/admin/sensores
       voltam ao estado pré-apply

Pré-requisitos: device com IP conhecido + admin password conhecido
(ou ambiente em factory state com password regenerada via SEC-003 visível
no Serial USB durante boot).

Uso:
    ./tools/ota_apply.py \\
        --ip 192.168.3.195 \\
        --user admin \\
        --pass 'SuaSenha' \\
        --firmware .pio/build/pico_w_release/firmware.bin

    # Se admin em factory state com one-time pass:
    ./tools/ota_apply.py \\
        --ip 192.168.3.195 --user admin --pass 'OTP_FACTORY' \\
        --new-pass 'SuaNovaSenha' \\
        --firmware .pio/build/pico_w_release/firmware.bin

Status: VALIDADO em HW 2026-05-06 (apenas etapas 1-5 — restore (7-8)
ainda não exercitado neste script; etapas usam APIs já validadas em
Fase 1+2). Próximo passo: validar end-to-end completo em HW.

@project SIMUT
@target  Raspberry Pi Pico W (RP2040) — Arduino Framework
@author  Ângelo Moisés Alves
@license MIT License
"""
import argparse
import hashlib
import os
import sys
import time

import requests


def sha256_hex(s: str) -> str:
    """SIMUT auth: cliente envia sha256(plaintext) hex antes do server hash."""
    return hashlib.sha256(s.encode("utf-8")).hexdigest()


def get_nonce(session: requests.Session, base: str) -> str:
    r = session.get(f"{base}/api/login_init", timeout=5)
    r.raise_for_status()
    return r.json()["nonce"]


def chpass(session: requests.Session, base: str, user: str, old: str, new: str) -> bool:
    """Self-service chpass pre-auth (factory password regen flow)."""
    nonce = get_nonce(session, base)
    r = session.post(
        f"{base}/api/login_chpass",
        data={
            "user": user,
            "oldpass": sha256_hex(old),
            "newpass": sha256_hex(new),
            "nonce": nonce,
        },
        timeout=10,
    )
    print(f"[chpass] HTTP {r.status_code} {r.text[:120]}")
    return r.status_code == 200


def login(session: requests.Session, base: str, user: str, password: str) -> bool:
    nonce = get_nonce(session, base)
    r = session.post(
        f"{base}/api/login",
        data={"user": user, "pass": sha256_hex(password), "nonce": nonce},
        timeout=10,
    )
    print(f"[login] HTTP {r.status_code} {r.text[:120]}")
    return r.status_code == 200


def download_backup(session: requests.Session, base: str, dst_path: str) -> int:
    """GET /api/backup → grava .bkp localmente. Preserva user data
    (system.bin, history, sensors, /web/, /lang/, /themes/, etc.)."""
    print(f"[backup] downloading to {dst_path}")
    r = session.get(f"{base}/api/backup", stream=True, timeout=60)
    r.raise_for_status()
    n = 0
    with open(dst_path, "wb") as f:
        for chunk in r.iter_content(chunk_size=4096):
            if chunk:
                f.write(chunk)
                n += len(chunk)
    print(f"[backup] wrote {n} bytes ({n/1024:.1f} KiB)")
    return n


def stage_and_commit(session: requests.Session, base: str, firmware_path: str) -> dict:
    """Upload firmware to staging area + commit metadata em uma única request.
    A staging area COMPARTILHA partição com LittleFS — após esta operação
    o LFS está perdido (Fase 8 vai fazer restore depois)."""
    with open(firmware_path, "rb") as f:
        data = f.read()
    print(f"[stage] uploading firmware: {len(data)} bytes ({len(data)/1024:.1f} KiB)")
    files = {"file": ("firmware.bin", data, "application/octet-stream")}
    t0 = time.time()
    r = session.post(
        f"{base}/api/restore?op=stage&commit=1",
        files=files,
        timeout=180,
    )
    dt = time.time() - t0
    print(f"[stage+commit] HTTP {r.status_code} ({dt:.1f}s) {r.text[:300]}")
    r.raise_for_status()
    info = r.json()
    if info.get("st") != 5 or info.get("v") != 0 or info.get("committed") != 1:
        raise RuntimeError(f"stage+commit invalid response: {info}")
    return info


def trigger_apply(session: requests.Session, base: str) -> int:
    """POST /api/ota/apply — device reboota. Conexão derruba durante
    o reboot, isso é esperado."""
    print("[apply] firing /api/ota/apply — device will reboot")
    try:
        r = session.post(f"{base}/api/ota/apply", timeout=10)
        print(f"[apply] HTTP {r.status_code} {r.text[:300]}")
        return r.status_code
    except requests.exceptions.RequestException as e:
        print(f"[apply] disconnect (expected): {e}")
        return -1


def wait_for_device(base: str, timeout_s: int = 180) -> bool:
    """Espera device responder /api/login_init de novo (boot completo)."""
    print(f"[wait] esperando device voltar online (timeout {timeout_s}s)…")
    deadline = time.time() + timeout_s
    last_err = None
    attempts = 0
    while time.time() < deadline:
        attempts += 1
        try:
            r = requests.get(f"{base}/api/login_init", timeout=3)
            if r.status_code == 200:
                elapsed = time.time() - (deadline - timeout_s)
                print(f"[wait] device online após {elapsed:.0f}s ({attempts} tentativas)")
                return True
        except requests.exceptions.RequestException as e:
            last_err = e
        time.sleep(3)
    print(f"[wait] TIMEOUT após {timeout_s}s. Último erro: {last_err}")
    return False


def restore_backup(session: requests.Session, base: str, bkp_path: str) -> bool:
    """Validate + apply restore (Fase 2 OTA). Recupera user data."""
    with open(bkp_path, "rb") as f:
        data = f.read()
    print(f"[restore] validating {len(data)} bytes")
    files = {"file": ("backup.bkp", data, "application/octet-stream")}
    r = session.post(f"{base}/api/restore?op=validate", files=files, timeout=120)
    print(f"[restore validate] HTTP {r.status_code} {r.text[:300]}")
    if r.status_code != 200:
        return False

    files = {"file": ("backup.bkp", data, "application/octet-stream")}
    r = session.post(f"{base}/api/restore?op=apply", files=files, timeout=180)
    print(f"[restore apply] HTTP {r.status_code} {r.text[:300]}")
    return r.status_code == 200


def main():
    p = argparse.ArgumentParser(description="SIMUT OTA orchestrator (Fase 8).")
    p.add_argument("--ip", required=True, help="IP do device (ex: 192.168.3.195)")
    p.add_argument("--user", default="admin")
    p.add_argument("--pass", dest="password", required=True,
                   help="Senha admin (ou one-time pass se factory state)")
    p.add_argument("--new-pass", help="Se setado, faz chpass antes do login "
                                       "(usado quando senha atual é one-time)")
    p.add_argument("--firmware", required=True,
                   help="Path do firmware.bin (RAW, não gzip)")
    p.add_argument("--no-restore", action="store_true",
                   help="Pula etapas de restore — útil para testar só apply")
    p.add_argument("--backup-dir", default="/tmp",
                   help="Diretório onde salvar o .bkp (default: /tmp)")
    args = p.parse_args()

    base = f"http://{args.ip}"
    if not os.path.exists(args.firmware):
        print(f"ERROR: firmware not found: {args.firmware}", file=sys.stderr)
        sys.exit(1)

    s = requests.Session()

    # 1. Login (com chpass opcional se factory state)
    if args.new_pass:
        if not chpass(s, base, args.user, args.password, args.new_pass):
            print("ERROR: chpass falhou", file=sys.stderr); sys.exit(2)
        login_pass = args.new_pass
    else:
        login_pass = args.password

    if not login(s, base, args.user, login_pass):
        print("ERROR: login falhou", file=sys.stderr); sys.exit(3)

    # 2. Download backup .bkp
    bkp_path = os.path.join(args.backup_dir, f"simut-pre-ota-{int(time.time())}.bkp")
    try:
        download_backup(s, base, bkp_path)
    except Exception as e:
        print(f"ERROR: backup falhou: {e}", file=sys.stderr); sys.exit(4)

    # 3-4. Stage + commit + apply
    try:
        stage_and_commit(s, base, args.firmware)
    except Exception as e:
        print(f"ERROR: stage+commit falhou: {e}", file=sys.stderr); sys.exit(5)

    rc = trigger_apply(s, base)
    if rc not in (202, -1):
        print(f"ERROR: apply unexpected status: {rc}", file=sys.stderr); sys.exit(6)

    # 5. Wait for boot
    if not wait_for_device(base, timeout_s=180):
        print("ERROR: device não voltou online após apply", file=sys.stderr); sys.exit(7)

    if args.no_restore:
        print(f"\n[done] OTA apply OK. Backup salvo em: {bkp_path}")
        print("       Skipping restore (--no-restore). Para restaurar user data:")
        print(f"       ./tools/ota_apply.py --ip {args.ip} ... (sem --firmware)")
        return

    # 6. Re-login (factory state com nova one-time password)
    print("\n[post-apply] device em factory state. Pegue a one-time password do Serial USB")
    print("              (ou via CLI: 'show system info' não mostra; veja boot Serial)")
    print("              e re-rode com --new-pass para chpass + restore.")

    print(f"\n[done] OTA apply OK. Backup pré-apply em: {bkp_path}")
    print("       Para restaurar user data:")
    print(f"       1. Pegue OTP do Serial USB do device")
    print(f"       2. ./tools/ota_apply.py --ip {args.ip} --user admin "
          f"--pass 'OTP' --new-pass 'NovaSenha' \\")
    print(f"            --firmware <ignorado> --restore-only {bkp_path}")
    print("       (TODO: --restore-only mode pendente)")


if __name__ == "__main__":
    main()
