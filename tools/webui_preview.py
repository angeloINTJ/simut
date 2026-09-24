#!/usr/bin/env python3
"""Pré-visualiza a interface web do WebUI.h sem gravar o firmware.

Serve as páginas, o /lang.js e o /style.css DIRETO do WebUI.h — relido a cada
requisição, então editar e recarregar basta — e encaminha todo o resto
(/api/*, /logout, /download…) para um SIMUT de verdade. A página que se vê é
a do arquivo em edição; o dado que ela mostra é o do aparelho.

Por que um proxy e não um dublê da API: a página é metade do contrato, e um
dublê que "reproduz o firmware campo a campo" já concordou consigo mesmo e
discordou do aparelho neste projeto (o /api/config plano). Contra o ferro,
o que a pré-visualização mostra é o que o operador vai ver.

A sessão é do proxy, não do navegador: ele faz o login (nonce + SHA-256 do
air_test_suite) e carrega o cookie em cada requisição encaminhada. Assim uma
captura por Chrome headless — que não guarda cookie entre execuções — vê a
página autenticada sem nenhum passo a mais.

    SIMUT_WEB_PASS=… python3 tools/webui_preview.py --device 192.168.3.24
    python3 tools/webui_preview.py --device 192.168.3.24 --theme light --lang pt

--theme e --lang injetam, só aqui, o localStorage que a página leria do
navegador do operador; --run executa um trecho de JavaScript depois do load
(abrir a gaveta, um modal) para a captura mostrar o que só existe ao toque; --remote-pages busca as próprias páginas do aparelho
(para comparar o que ele serve com o que o arquivo diz). O build carimba
?v=<hash> nas URLs do /lang.js e do /style.css; aqui as páginas saem com
Cache-Control: no-store e os assets também, então nada fica preso.

Sem dependência além de `requests`, que o air_test_suite já exige.
"""
import argparse
import os
import re
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WEBUI = os.path.join(ROOT, "WebUI.h")

PAGES = {
    "/": ("DASH_PAGE", "text/html"),
    "/history": ("HIST_PAGE", "text/html"),
    "/config": ("CFG_PAGE", "text/html"),
    "/telemetry": ("TEL_PAGE", "text/html"),
    "/network": ("NET_PAGE", "text/html"),
    "/users": ("USR_PAGE", "text/html"),
    "/files": ("FILE_PAGE", "text/html"),
    "/alarms": ("ALARMS_PAGE", "text/html"),
    "/license": ("LICENSE_PAGE", "text/html"),
    "/login": ("LOGIN_PAGE", "text/html"),
    "/force_chpass": ("LOGIN_PAGE", "text/html"),  # same blob; the page picks its forced mode from the pathname
    "/lang.js": ("LANG_JS", "application/javascript"),
    "/style.css": ("STYLE_CSS", "text/css"),
}

_BLOCK = re.compile(r'static const char ([A-Z_]+)\[\] PROGMEM = R"raw\((.*?)\)raw";', re.S)

# Cabeçalhos que não atravessam um proxy de uma conexão só.
_HOP = {"connection", "keep-alive", "transfer-encoding", "content-encoding",
        "content-length", "host"}


def blocks():
    with open(WEBUI, encoding="utf-8") as fh:
        return dict(_BLOCK.findall(fh.read()))


def seed_tag(theme, lang, run):
    """O localStorage que a pagina leria do navegador do operador, e um trecho
    a rodar depois do load — semeados so aqui, para a captura. Uma instancia
    atende todas as variantes: cada login do proxy ocupa um dos tres slots de
    sessao do aparelho, e tres instancias derrubaram a primeira."""
    seed = ""
    if theme:
        seed += "localStorage.setItem('simut_ui_theme',%r);" % theme
    if lang:
        seed += "localStorage.setItem('simut_lang',%r);" % lang
    if run:   # p.ex. run=toggleDrawer() para capturar a gaveta aberta
        seed += "window.addEventListener('load',function(){setTimeout(function(){%s},300);});" % run
    return ("<script>" + seed + "</script>") if seed else ""


def make_handler(args, session, lock):

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *a):
            sys.stderr.write("  %s %s\n" % (self.command, fmt % a))

        def _local(self, path):
            return (not args.remote_pages) and path in PAGES

        def do_GET(self):
            path, _, query = self.path.partition("?")
            if self._local(path):
                return self._serve_local(path, parse_qs(query))
            self._proxy()

        def do_POST(self):
            self._proxy()

        def _serve_local(self, path, q):
            name, ctype = PAGES[path]
            body = blocks()[name]
            if ctype == "text/html":
                tag = seed_tag(q.get("theme", [args.theme])[0], q.get("lang", [args.lang])[0],
                               q.get("run", [args.run])[0])
                body = body.replace("<head>", "<head>" + tag, 1)
            data = body.encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", ctype + "; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def _proxy(self):
            n = int(self.headers.get("Content-Length") or 0)
            body = self.rfile.read(n) if n else None
            hdrs = {k: v for k, v in self.headers.items() if k.lower() not in _HOP
                    and k.lower() != "cookie"}
            url = args.base + self.path
            with lock:   # o aparelho atende um cliente por vez; não o afogar
                try:
                    r = session.request(self.command, url, headers=hdrs, data=body,
                                        timeout=args.timeout, allow_redirects=False,
                                        stream=True)
                    payload = r.content
                except Exception as exc:  # aparelho fora, reiniciando…
                    msg = ("preview: o aparelho não respondeu (%s: %s)"
                           % (type(exc).__name__, exc)).encode()
                    self.send_response(502)
                    self.send_header("Content-Type", "text/plain; charset=utf-8")
                    self.send_header("Content-Length", str(len(msg)))
                    self.end_headers()
                    self.wfile.write(msg)
                    return
            self.send_response(r.status_code)
            for k, v in r.headers.items():
                if k.lower() in _HOP or k.lower() == "set-cookie":
                    continue
                self.send_header(k, v)
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

    return Handler


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--device", required=True, help="host[:porta] do SIMUT que responde a API")
    ap.add_argument("--port", type=int, default=8098, help="porta local (padrão 8098)")
    ap.add_argument("--theme", choices=["light", "dark"], help="semeia simut_ui_theme na página")
    ap.add_argument("--lang", choices=["pt", "en"], help="semeia simut_lang na página")
    ap.add_argument("--run", help="JavaScript executado 300 ms depois do load (p.ex. toggleDrawer())")
    ap.add_argument("--remote-pages", action="store_true",
                    help="busca as páginas do aparelho em vez do WebUI.h")
    ap.add_argument("--timeout", type=float, default=30)
    args = ap.parse_args()
    args.base = "http://" + args.device

    from air_test_suite import Web  # noqa: E402 — só aqui, para o --help não exigir requests
    web = Web(args.device, timeout=args.timeout)
    pw = os.environ.get("SIMUT_WEB_PASS")
    if pw:
        web.login(os.environ.get("SIMUT_WEB_USER", "admin"), pw)
        print("preview: sessão aberta em %s" % args.base)
    else:
        print("preview: SIMUT_WEB_PASS ausente — a API vai responder 401")

    srv = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler(args, web.s, threading.Lock()))
    print("preview: http://127.0.0.1:%d  (páginas: %s)"
          % (args.port, "do aparelho" if args.remote_pages else WEBUI))
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
