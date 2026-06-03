#!/usr/bin/env python3
"""
SIMUT Theme Editor — launcher.

Proxy para /api/* do simut.local (resolve CORS de page-served-from-localhost)
e serve a UI estática.

Uso:  .venv/bin/python tools/theme-editor/server.py [--port 8090] [--simut http://simut.local]
Browser: http://localhost:8090
"""
import argparse
from pathlib import Path

import httpx
import uvicorn
from fastapi import FastAPI, Request, Response
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

ROOT = Path(__file__).parent
SIMUT_HOST = "http://simut.local"

app = FastAPI(title="SIMUT Theme Editor")
client = httpx.AsyncClient(timeout=30.0)


@app.api_route("/api/{path:path}",
               methods=["GET", "POST", "PUT", "DELETE", "PATCH"])
async def proxy(path: str, request: Request):
    body = await request.body()
    headers = {k: v for k, v in request.headers.items()
               if k.lower() not in ("host", "content-length")}
    cookies = {k: v for k, v in request.cookies.items()}
    upstream_url = f"{SIMUT_HOST}/api/{path}"
    try:
        r = await client.request(
            request.method, upstream_url,
            content=body, headers=headers,
            params=dict(request.query_params),
            cookies=cookies,
            follow_redirects=False,
        )
    except httpx.RequestError as e:
        return Response(content=f'{{"error":"upstream: {e}"}}',
                        status_code=502, media_type="application/json")
    resp_headers = {k: v for k, v in r.headers.items()
                    if k.lower() not in ("transfer-encoding", "content-encoding",
                                          "content-length", "connection")}
    return Response(content=r.content, status_code=r.status_code,
                    headers=resp_headers, media_type=r.headers.get("content-type"))


@app.get("/")
async def index():
    return FileResponse(ROOT / "index.html")


app.mount("/static", StaticFiles(directory=ROOT), name="static")


def main():
    global SIMUT_HOST
    parser = argparse.ArgumentParser(description="SIMUT Theme Editor")
    parser.add_argument("-p", "--port", type=int, default=8090)
    parser.add_argument("--simut", default=SIMUT_HOST,
                        help="URL base do SIMUT (default http://simut.local)")
    args = parser.parse_args()
    SIMUT_HOST = args.simut.rstrip("/")
    print(f"Editor em http://localhost:{args.port}  → upstream {SIMUT_HOST}")
    uvicorn.run(app, host="127.0.0.1", port=args.port,
                log_level="warning", access_log=False)


if __name__ == "__main__":
    main()
