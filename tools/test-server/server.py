#!/usr/bin/env python3
"""
SIMUT Test Server — recebe e decodifica payloads de telemetria.

Listeners (start/stop em runtime via web UI):
  * HTTP   (default 9080) — POST catch-all
  * HTTPS  (default 9443) — self-signed cert auto-gerado em certs/
  * MQTT   (subscriber, broker externo)
  * MQTTS  (subscriber TLS, broker externo)

Web UI: http://localhost:8080  (porta -p / --ui-port pra mudar)
"""
import argparse
import asyncio
import csv
import io
import json
import re
import socket
import ssl
import sys
import threading
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

import paho.mqtt.client as mqtt
import uvicorn
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import NameOID
from fastapi import FastAPI, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles

ROOT = Path(__file__).parent
STATIC = ROOT / "static"
CERTS = ROOT / "certs"
CERTS.mkdir(exist_ok=True)
CERT_FILE = CERTS / "cert.pem"
KEY_FILE = CERTS / "key.pem"


# ──────────────────────────────────────────────────────────────────────────
# Utilities
# ──────────────────────────────────────────────────────────────────────────
def now_iso() -> str:
    return datetime.now(timezone.utc).astimezone().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def ensure_cert():
    """Gera self-signed cert válido por 10 anos. Idempotente."""
    if CERT_FILE.exists() and KEY_FILE.exists():
        return
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = issuer = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "BR"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "SIMUT Test Server"),
        x509.NameAttribute(NameOID.COMMON_NAME, "simut-test-server"),
    ])
    cert = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(datetime.now(timezone.utc))
        .not_valid_after(datetime.now(timezone.utc).replace(year=datetime.now(timezone.utc).year + 10))
        .add_extension(
            x509.SubjectAlternativeName([
                x509.DNSName("localhost"),
                x509.DNSName("simut-test-server"),
            ]),
            critical=False,
        )
        .sign(key, hashes.SHA256())
    )
    KEY_FILE.write_bytes(key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.TraditionalOpenSSL,
        encryption_algorithm=serialization.NoEncryption(),
    ))
    CERT_FILE.write_bytes(cert.public_bytes(serialization.Encoding.PEM))


# ──────────────────────────────────────────────────────────────────────────
# Decoders
# ──────────────────────────────────────────────────────────────────────────
def decode_json(body: str):
    obj = json.loads(body)
    if not isinstance(obj, list):
        # SIMUT manda array de records, mas custom mode pode vir como obj com {"data":[...]}
        if isinstance(obj, dict) and isinstance(obj.get("data"), list):
            return {"records": obj["data"], "envelope": {k: v for k, v in obj.items() if k != "data"}}
        return {"records": [obj]}
    return {"records": obj}


def decode_csv(body: str, sep: str = ";"):
    reader = csv.reader(io.StringIO(body), delimiter=sep)
    rows = list(reader)
    if not rows:
        return {"records": []}
    header = rows[0]
    records = []
    for row in rows[1:]:
        rec = {}
        for i, name in enumerate(header):
            val = row[i] if i < len(row) else ""
            if val == "":
                rec[name] = None
            else:
                try:
                    rec[name] = float(val) if "." in val else int(val)
                except ValueError:
                    rec[name] = val
        records.append(rec)
    return {"records": records, "header": header}


_TOKEN_RE = re.compile(r"\{([A-Za-z0-9_]+)\}")


def decode_custom(body: str, template: str):
    """Best-effort: tenta JSON primeiro (custom default forma JSON válido).
    Se falhar, faz extração regex baseada nos tokens do template."""
    body_stripped = body.strip()
    if body_stripped.startswith("{") or body_stripped.startswith("["):
        try:
            return decode_json(body)
        except json.JSONDecodeError:
            pass
    tokens = _TOKEN_RE.findall(template)
    if not tokens:
        return {"raw": body, "tokens_found": []}
    # Build regex by replacing each {TOKEN} with named capture
    pat = re.escape(template)
    for t in tokens:
        pat = pat.replace(re.escape("{" + t + "}"), f"(?P<{t}>[^,}}\\]\\s]*)")
    m = re.search(pat, body)
    if m:
        return {"records": [m.groupdict()], "matched_tokens": tokens}
    return {"raw": body, "tokens_searched": tokens, "matched": False}


def decode(body: str, mode: str, template: str = "", csv_sep: str = ";"):
    try:
        if mode == "json":
            return {"ok": True, "result": decode_json(body)}
        if mode == "csv":
            return {"ok": True, "result": decode_csv(body, csv_sep)}
        if mode == "custom":
            return {"ok": True, "result": decode_custom(body, template)}
        if mode == "auto":
            # Try JSON, then CSV
            try:
                return {"ok": True, "result": decode_json(body), "detected": "json"}
            except json.JSONDecodeError:
                pass
            if csv_sep in body:
                return {"ok": True, "result": decode_csv(body, csv_sep), "detected": "csv"}
            return {"ok": False, "error": "auto-detect falhou (nem JSON nem CSV)"}
        return {"ok": False, "error": f"modo desconhecido: {mode}"}
    except Exception as e:
        return {"ok": False, "error": f"{type(e).__name__}: {e}"}


# ──────────────────────────────────────────────────────────────────────────
# Server state
# ──────────────────────────────────────────────────────────────────────────
class State:
    def __init__(self):
        self.config = {
            "mode": "auto",
            "custom_template": (
                '{"dev":"{DEV}","mac":"{MAC}","data":[{DATA}]}'
            ),
            "csv_separator": ";",
            "auth_header_name": "",
            "auth_header_value": "",
            "mqtt_broker_host": "localhost",
            "mqtt_broker_port": 1883,
            "mqtt_broker_port_tls": 8883,
            "mqtt_topic": "simut/data",
            "mqtt_username": "",
            "mqtt_password": "",
            "mqtt_tls_insecure": True,
            "http_port": 9080,
            "https_port": 9443,
        }
        self.listeners = {
            "http":  {"running": False, "port": None, "_server": None, "_task": None},
            "https": {"running": False, "port": None, "_server": None, "_task": None},
            "mqtt":  {"running": False, "broker": None, "_client": None},
            "mqtts": {"running": False, "broker": None, "_client": None},
        }
        self.samples: list[dict] = []
        self.max_samples = 200
        self.ws_clients: list[WebSocket] = []
        self.lock = threading.Lock()
        self.main_loop: Optional[asyncio.AbstractEventLoop] = None

    def add_sample(self, sample: dict):
        with self.lock:
            self.samples.append(sample)
            if len(self.samples) > self.max_samples:
                self.samples.pop(0)

    def public_listeners(self) -> dict:
        return {
            name: {k: v for k, v in info.items() if not k.startswith("_")}
            for name, info in self.listeners.items()
        }


state = State()


def log(line: str, level: str = "info"):
    """Append a server log line to all UI terminals."""
    sys.stdout.write(f"[{level.upper():5}] {line}\n")
    sys.stdout.flush()
    schedule_broadcast({"type": "log", "level": level, "ts": now_iso(), "line": line})


# ──────────────────────────────────────────────────────────────────────────
# Broadcast (called from any thread)
# ──────────────────────────────────────────────────────────────────────────
def schedule_broadcast(msg: dict):
    """Thread-safe: schedules broadcast on the main asyncio loop."""
    if state.main_loop and not state.main_loop.is_closed():
        asyncio.run_coroutine_threadsafe(_broadcast(msg), state.main_loop)


async def _broadcast(msg: dict):
    payload = json.dumps(msg, default=str)
    dead = []
    for ws in list(state.ws_clients):
        try:
            await ws.send_text(payload)
        except Exception:
            dead.append(ws)
    for ws in dead:
        if ws in state.ws_clients:
            state.ws_clients.remove(ws)


# ──────────────────────────────────────────────────────────────────────────
# Sample ingestion
# ──────────────────────────────────────────────────────────────────────────
def make_sample(transport: str, *, method: str = "", path: str = "",
                headers: dict | None = None, body: str = "",
                mqtt_topic: str = "", mqtt_qos: int = 0) -> dict:
    cfg = state.config
    auth_ok = True
    auth_msg = ""
    if transport.startswith("HTTP"):
        if cfg["auth_header_name"]:
            got = (headers or {}).get(cfg["auth_header_name"].lower(), "")
            if cfg["auth_header_value"] and got != cfg["auth_header_value"]:
                auth_ok = False
                auth_msg = f"esperado '{cfg['auth_header_value']}', recebeu '{got}'"
            elif not got:
                auth_ok = False
                auth_msg = f"header '{cfg['auth_header_name']}' ausente"
    decoded = decode(body, cfg["mode"], cfg["custom_template"], cfg["csv_separator"])
    return {
        "id": int(time.time() * 1000),
        "ts": now_iso(),
        "transport": transport,
        "method": method,
        "path": path,
        "mqtt_topic": mqtt_topic,
        "mqtt_qos": mqtt_qos,
        "headers": headers or {},
        "body": body,
        "body_size": len(body.encode("utf-8")),
        "decoded": decoded,
        "auth_ok": auth_ok,
        "auth_msg": auth_msg,
    }


def ingest(sample: dict):
    state.add_sample(sample)
    schedule_broadcast({"type": "sample", "sample": sample})
    icon = "✓" if sample["decoded"]["ok"] and sample["auth_ok"] else "✗"
    detail = sample.get("path") or sample.get("mqtt_topic", "")
    log(f"{icon} {sample['transport']} {sample['method'] or ''} {detail} "
        f"({sample['body_size']}B)")


# ──────────────────────────────────────────────────────────────────────────
# Telemetry receiver app (HTTP & HTTPS share this)
# ──────────────────────────────────────────────────────────────────────────
def make_listener_app(transport_label: str) -> FastAPI:
    app = FastAPI()

    @app.api_route("/{path:path}",
                   methods=["GET", "POST", "PUT", "PATCH", "DELETE"])
    async def catch_all(path: str, request: Request):
        body_bytes = await request.body()
        body = body_bytes.decode("utf-8", errors="replace")
        sample = make_sample(
            transport_label,
            method=request.method,
            path="/" + path,
            headers=dict(request.headers),
            body=body,
        )
        ingest(sample)
        return JSONResponse({"status": "ok"}, status_code=200)

    return app


# ──────────────────────────────────────────────────────────────────────────
# MQTT subscriber
# ──────────────────────────────────────────────────────────────────────────
def start_mqtt(use_tls: bool):
    label = "MQTTS" if use_tls else "MQTT"
    cfg = state.config
    host = cfg["mqtt_broker_host"]
    port = cfg["mqtt_broker_port_tls"] if use_tls else cfg["mqtt_broker_port"]
    topic = cfg["mqtt_topic"]

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                          client_id=f"simut-test-server-{label.lower()}")
    if cfg["mqtt_username"]:
        client.username_pw_set(cfg["mqtt_username"], cfg["mqtt_password"] or None)
    if use_tls:
        ctx = ssl.create_default_context()
        if cfg["mqtt_tls_insecure"]:
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
        client.tls_set_context(ctx)

    def on_connect(c, ud, flags, rc, props=None):
        if rc == 0:
            log(f"{label} conectado a {host}:{port}, subscribe '{topic}'")
            c.subscribe(topic + "/#")
            c.subscribe(topic)
        else:
            log(f"{label} connect rc={rc}", "error")

    def on_disconnect(c, ud, dc_flags=None, rc=0, props=None):
        log(f"{label} desconectado (rc={rc})", "warn")

    def on_message(c, ud, msg):
        body = msg.payload.decode("utf-8", errors="replace")
        sample = make_sample(label, mqtt_topic=msg.topic, mqtt_qos=msg.qos, body=body)
        ingest(sample)

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    try:
        client.connect(host, port, keepalive=30)
    except Exception as e:
        log(f"{label} falha ao conectar a {host}:{port} → {e}", "error")
        return None
    client.loop_start()
    state.listeners["mqtts" if use_tls else "mqtt"]["broker"] = f"{host}:{port}"
    return client


def stop_mqtt(use_tls: bool):
    name = "mqtts" if use_tls else "mqtt"
    info = state.listeners[name]
    cli: Optional[mqtt.Client] = info.get("_client")
    if cli:
        try:
            cli.loop_stop()
            cli.disconnect()
        except Exception as e:
            log(f"{name} stop erro: {e}", "warn")
    info["_client"] = None
    info["broker"] = None
    info["running"] = False


# ──────────────────────────────────────────────────────────────────────────
# HTTP/HTTPS lifecycle (uvicorn programmatic)
# ──────────────────────────────────────────────────────────────────────────
async def start_http_listener(use_tls: bool):
    name = "https" if use_tls else "http"
    label = "HTTPS" if use_tls else "HTTP"
    port = state.config["https_port" if use_tls else "http_port"]
    app = make_listener_app(label)
    cfg_kwargs = dict(host="0.0.0.0", port=port, log_level="warning",
                      access_log=False, lifespan="off")
    if use_tls:
        ensure_cert()
        cfg_kwargs.update(ssl_certfile=str(CERT_FILE), ssl_keyfile=str(KEY_FILE))
    config = uvicorn.Config(app, **cfg_kwargs)
    server = uvicorn.Server(config)
    task = asyncio.create_task(server.serve())
    # Wait briefly for socket to bind (or fail)
    for _ in range(20):
        await asyncio.sleep(0.05)
        if server.started or task.done():
            break
    if task.done() and task.exception():
        log(f"{label} falhou ao iniciar :{port} → {task.exception()}", "error")
        return False
    state.listeners[name].update(running=True, port=port,
                                  _server=server, _task=task)
    log(f"{label} listener UP em 0.0.0.0:{port}")
    return True


async def stop_http_listener(use_tls: bool):
    name = "https" if use_tls else "http"
    label = "HTTPS" if use_tls else "HTTP"
    info = state.listeners[name]
    server: Optional[uvicorn.Server] = info.get("_server")
    task: Optional[asyncio.Task] = info.get("_task")
    if server:
        server.should_exit = True
    if task:
        try:
            await asyncio.wait_for(task, timeout=3)
        except asyncio.TimeoutError:
            task.cancel()
    info.update(running=False, port=None, _server=None, _task=None)
    log(f"{label} listener DOWN")


# ──────────────────────────────────────────────────────────────────────────
# UI app
# ──────────────────────────────────────────────────────────────────────────
ui = FastAPI(title="SIMUT Test Server")


@ui.get("/")
async def index():
    return FileResponse(STATIC / "index.html")


ui.mount("/static", StaticFiles(directory=STATIC), name="static")


@ui.get("/api/state")
async def api_state():
    return {
        "config": state.config,
        "listeners": state.public_listeners(),
        "samples": state.samples[-50:],
    }


@ui.post("/api/config")
async def api_set_config(payload: dict):
    allowed = set(state.config.keys())
    for k, v in payload.items():
        if k in allowed:
            state.config[k] = v
    log(f"config atualizado: {list(payload.keys())}")
    schedule_broadcast({"type": "config", "config": state.config})
    return {"ok": True, "config": state.config}


@ui.post("/api/listeners/{name}/start")
async def api_start(name: str):
    if name == "http":
        ok = await start_http_listener(False)
    elif name == "https":
        ok = await start_http_listener(True)
    elif name in ("mqtt", "mqtts"):
        cli = start_mqtt(name == "mqtts")
        ok = cli is not None
        if ok:
            state.listeners[name]["_client"] = cli
            state.listeners[name]["running"] = True
    else:
        return JSONResponse({"ok": False, "error": "listener inválido"}, status_code=400)
    schedule_broadcast({"type": "listeners", "listeners": state.public_listeners()})
    return {"ok": ok, "listeners": state.public_listeners()}


@ui.post("/api/listeners/{name}/stop")
async def api_stop(name: str):
    if name in ("http", "https"):
        await stop_http_listener(name == "https")
    elif name in ("mqtt", "mqtts"):
        stop_mqtt(name == "mqtts")
    else:
        return JSONResponse({"ok": False, "error": "listener inválido"}, status_code=400)
    schedule_broadcast({"type": "listeners", "listeners": state.public_listeners()})
    return {"ok": True, "listeners": state.public_listeners()}


@ui.post("/api/clear")
async def api_clear():
    state.samples.clear()
    schedule_broadcast({"type": "cleared"})
    log("samples limpos via UI")
    return {"ok": True}


@ui.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await websocket.accept()
    state.ws_clients.append(websocket)
    try:
        # Snapshot inicial
        await websocket.send_text(json.dumps({
            "type": "init",
            "config": state.config,
            "listeners": state.public_listeners(),
            "samples": state.samples[-50:],
        }, default=str))
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        pass
    except Exception:
        pass
    finally:
        if websocket in state.ws_clients:
            state.ws_clients.remove(websocket)


from contextlib import asynccontextmanager


@asynccontextmanager
async def lifespan(app: FastAPI):
    state.main_loop = asyncio.get_running_loop()
    ip = local_ip()
    log(f"SIMUT Test Server pronto. Web UI em http://{ip}:{UI_PORT}")
    log("Listeners de telemetria iniciam parados — controle via web UI.")
    yield


ui.router.lifespan_context = lifespan


def local_ip() -> str:
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "localhost"


# ──────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────
UI_PORT = 8080


def main():
    global UI_PORT
    parser = argparse.ArgumentParser(description="SIMUT Test Server")
    parser.add_argument("-p", "--ui-port", type=int, default=8080,
                        help="porta da web UI (default 8080)")
    args = parser.parse_args()
    UI_PORT = args.ui_port
    uvicorn.run(ui, host="0.0.0.0", port=UI_PORT, log_level="warning",
                access_log=False)


if __name__ == "__main__":
    main()
