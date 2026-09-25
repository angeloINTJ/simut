#!/usr/bin/env python3
"""Builds docs/MANUAL.pt-BR.html, the illustrated product manual, from docs/_manual/.

WHY THIS EXISTS
---------------
The illustrated manual used to be one hand-edited 1.5 MB HTML file with its
screenshots pasted in as base64. It was right for v2.1.10 and nobody could
update it: a paragraph change meant editing inside a file most editors choke
on, and a new screenshot meant re-encoding by hand. It stayed at v2.1.10 for
six releases while the firmware grew accounts, PINs, a mirror and an Air.

Now the text lives as one Markdown file per chapter in docs/_manual/ and this
script renders them into the single self-contained page, in the Ângulo visual
standard (the tokens are the ones simut-rx generates in web/angulo.css; the
display font is its 13 kB subset, OFL, embedded so the page needs nothing from
the network).

FIGURES
-------
Every image is declared in the chapter where it belongs:

    ::: {.figura #fig-11-dashboard tipo="tft" arquivo="11-dashboard.png"
         captura="screen dash; two sensors active, none in alarm"}
    Caption.
    :::

If docs/images/manual/<arquivo> exists, it is embedded in the right frame
(panel, browser, LCD, photo). If it does not, the page shows a marked
placeholder with the capture instructions, and the figure goes into Appendix A
and into docs/images/manual/CAPTURAR.md. Capturing the missing screens is
therefore: take the shot, save it under that name, run this script again.

USAGE
-----
    python3 tools/build_manual.py            # writes docs/MANUAL.pt-BR.html
    python3 tools/build_manual.py --check    # fails on a [[VERIFICAR marker, a
                                             # repeated figure id, a broken internal
                                             # link or markup that leaked as text

Requires pandoc (tested with 3.x). Not part of the firmware build or of CI.

Project: SIMUT
License: MIT
"""
import base64
import html
import json
import os
import re
import subprocess
import sys
from datetime import date

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "docs", "_manual")
IMG = os.path.join(ROOT, "docs", "images", "manual")
OUT = os.path.join(ROOT, "docs", "MANUAL.pt-BR.html")
CAPTURAR = os.path.join(IMG, "CAPTURAR.md")
LOGCODES = os.path.join(ROOT, "tools", "logcodes.tsv")
FONT = os.path.join(ROOT, "docs", "assets", "fonts", "angulo-display-600.woff2")

VERSION = "v2.7.1"

# Parts of the book, by chapter number. The chapter files carry their own
# number in the name (capNN-*.md); this only decides where a part starts.
PARTS = [
    (1, "Parte I", "Conhecer"),
    (5, "Parte II", "Configurar"),
    (11, "Parte III", "Usar"),
    (15, "Parte IV", "Operar e manter"),
    (20, "Parte V", "Integrar com servidores"),
    (28, "Parte VI", "Referência"),
]

FIG_KINDS = {
    "tft": "Tela do painel",
    "lcd": "LCD 16×2",
    "web": "Página web",
    "foto": "Foto",
    "diagrama": "Diagrama",
}


def pandoc(md: str) -> str:
    """Markdown (pandoc dialect) to an HTML5 fragment, one level down: the
    chapter's # becomes <h2>, because the cover owns the only <h1>."""
    r = subprocess.run(
        # No automatic ids: the chapters are converted one by one and then
        # joined, so two "## Exemplo" in different chapters would collide.
        # Only explicit {#cap-NN-...} ids exist, and those are unique by rule.
        ["pandoc", "-f", "markdown-auto_identifiers", "-t", "html5",
         "--shift-heading-level-by=1", "--no-highlight", "--wrap=none"],
        input=md.encode("utf-8"), capture_output=True, check=True)
    return r.stdout.decode("utf-8")


def data_uri(path: str, mime: str) -> str:
    with open(path, "rb") as f:
        return f"data:{mime};base64," + base64.b64encode(f.read()).decode("ascii")


def mime_of(name: str) -> str:
    ext = name.rsplit(".", 1)[-1].lower()
    return {"png": "image/png", "jpg": "image/jpeg", "jpeg": "image/jpeg",
            "gif": "image/gif", "svg": "image/svg+xml", "webp": "image/webp"}.get(ext, "application/octet-stream")


FIG_RE = re.compile(r'<div id="(fig-[^"]+)" class="figura"([^>]*)>(.*?)</div>', re.S)
ATTR_RE = re.compile(r'data-([a-z]+)="([^"]*)"')


def render_figures(frag: str, chapter_no: int, chapter_title: str, figures: list) -> str:
    def one(m):
        fid, attrs, body = m.group(1), dict(ATTR_RE.findall(m.group(2))), m.group(3).strip()
        kind = attrs.get("tipo", "web")
        name = attrs.get("arquivo", fid[4:] + ".png")
        how = html.unescape(attrs.get("captura", ""))
        caption = re.sub(r"^<p>(.*)</p>$", r"\1", body, flags=re.S)
        path = os.path.join(IMG, name)
        have = os.path.exists(path)
        figures.append({"id": fid, "kind": kind, "file": name, "how": how,
                        "caption": re.sub(r"<[^>]+>", "", caption), "chapter": chapter_no,
                        "chapter_title": chapter_title, "have": have})
        n = len(figures)
        label = f'<b>Figura {n}.</b> {caption}'
        if have:
            img = f'<img src="{data_uri(path, mime_of(name))}" alt="{html.escape(re.sub(r"<[^>]+>", "", caption))}" loading="lazy">'
            if kind == "web":
                inner = f'<div class="moldura-web"><div class="barra"><span></span><span></span><span></span></div>{img}</div>'
            else:
                inner = f'<div class="moldura moldura-{kind}">{img}</div>'
        else:
            inner = (f'<div class="pendente pendente-{kind}" role="img" aria-label="Imagem a capturar: {html.escape(re.sub(r"<[^>]+>", "", caption))}">'
                     f'<span class="selo selo-alerta"><i></i>Imagem a capturar</span>'
                     f'<span class="pend-tipo">{FIG_KINDS.get(kind, kind)} · <code>{html.escape(name)}</code></span>'
                     f'<span class="pend-como">{html.escape(how)}</span></div>')
        return f'<figure id="{fid}" class="fig fig-{kind}">{inner}<figcaption>{label}</figcaption></figure>'
    return FIG_RE.sub(one, frag)


def wrap_tables(frag: str) -> str:
    return re.sub(r"(<table.*?</table>)", r'<div class="tw">\1</div>', frag, flags=re.S)


def spans(frag: str) -> str:
    # [release]{.img} -> <span class="img">release</span>; [PERM_X]{.perm} likewise.
    frag = frag.replace('<span class="img">', '<span class="selo selo-img">')
    frag = frag.replace('<span class="perm">', '<span class="selo selo-perm">')
    return frag


CALLOUT_TITLES = {"nota": ("Nota", "neutro"), "atencao": ("Atenção", "alerta"), "perigo": ("Perigo", "perigo")}


def callouts(frag: str) -> str:
    def one(m):
        cls = m.group(1)
        title, tone = CALLOUT_TITLES[cls]
        return f'<aside class="faixa faixa-{cls}"><span class="selo selo-{tone}"><i></i>{title}</span>{m.group(2)}</aside>'
    return re.sub(r'<div class="(nota|atencao|perigo)">(.*?)</div>', one, frag, flags=re.S)


def chapter(path: str, figures: list, toc: list, check_errors: list):
    base = os.path.basename(path)
    no = int(re.match(r"cap(\d+)", base).group(1))
    md = open(path, encoding="utf-8").read()
    if "[[VERIFICAR" in md:
        for line_no, line in enumerate(md.split("\n"), 1):
            if "[[VERIFICAR" in line:
                check_errors.append(f"{base}:{line_no}: {line.strip()[:160]}")
    frag = pandoc(md)
    m = re.match(r'\s*<h2 id="([^"]+)">(.*?)</h2>\s*(<p>.*?</p>)?', frag, re.S)
    if not m:
        raise SystemExit(f"{base}: the first line must be '# Title {{#cap-NN}}'")
    cid, title, lede = m.group(1), m.group(2), m.group(3) or ""
    rest = frag[m.end():]
    title_txt = re.sub(r"<[^>]+>", "", title)
    # Callouts first: their regex stops at the first </div>, so it has to run
    # before tables are wrapped in <div class="tw">.
    rest = spans(wrap_tables(render_figures(callouts(rest), no, title_txt, figures)))
    lede_html = lede.replace("<p>", '<p class="lede">', 1) if lede else ""
    toc.append((no, cid, title_txt))
    return no, (f'<section class="capitulo" id="{cid}">'
                f'<p class="rotulo-cap">Capítulo {no}</p><h2>{title}</h2>{lede_html}{rest}'
                f'<p class="volta"><a href="#sumario-t">Voltar ao sumário</a></p></section>')


def appendix_images(figures: list) -> str:
    pending = [f for f in figures if not f["have"]]
    rows = []
    for i, f in enumerate(figures, 1):
        if f["have"]:
            continue
        rows.append(
            f'<tr><td class="num">{i}</td><td><a href="#{f["id"]}"><code>{html.escape(f["file"])}</code></a>'
            f'<br><span class="tipo">{FIG_KINDS.get(f["kind"], f["kind"])}</span></td>'
            f'<td class="num">{f["chapter"]}</td><td>{html.escape(f["how"])}</td></tr>')
    by_kind = {}
    for f in pending:
        by_kind[f["kind"]] = by_kind.get(f["kind"], 0) + 1
    resumo = " · ".join(f"{FIG_KINDS.get(k, k)}: {n}" for k, n in sorted(by_kind.items()))
    return (
        '<section class="capitulo apendice" id="ap-a"><p class="rotulo-cap">Apêndice A</p>'
        '<h2>Imagens a capturar</h2>'
        f'<p class="lede">{len(pending)} de {len(figures)} figuras deste manual ainda não foram capturadas ({resumo}). '
        'Cada uma aparece no texto como um quadro marcado. Salve a captura em '
        '<code>docs/images/manual/</code> com o nome da coluna Arquivo e rode '
        '<code>python3 tools/build_manual.py</code>: a figura entra no lugar do quadro.</p>'
        '<h3>Como capturar</h3>'
        '<ul><li><b>Telas do painel:</b> grave a imagem <code>pico_w_test</code> num aparelho com o painel ligado, '
        'navegue com <code>screen &lt;tag&gt;</code> ou com toques pelo <code>POST /api/touch</code>, espere 6 s após o último '
        'toque e baixe <code>GET /api/screenshot</code>. <code>tools/screen_mapper.py</code> faz isso para as telas que ele conhece; '
        'amplie 2× sem suavização.</li>'
        '<li><b>Páginas web:</b> <code>tools/capture_web_shots.py</code> (Playwright), em 1280 px de largura e, onde indicado, em 390 px.</li>'
        '<li><b>LCD e fotos:</b> fotografia, de frente, sem reflexo.</li></ul>'
        f'<div class="tw"><table><thead><tr><th>#</th><th>Arquivo</th><th>Cap.</th><th>Como capturar</th></tr></thead>'
        f'<tbody>{"".join(rows)}</tbody></table></div></section>')


def capturar_md(figures: list) -> str:
    pending = [f for f in figures if not f["have"]]
    out = ["# Imagens a capturar para o manual", "",
           "Gerado por `tools/build_manual.py` — não edite à mão. Salve cada captura",
           "nesta pasta com o nome indicado e rode o script de novo.", "",
           f"{len(pending)} pendentes de {len(figures)}.", ""]
    cur = None
    for f in pending:
        if f["chapter"] != cur:
            cur = f["chapter"]
            out += ["", f"## Capítulo {cur} — {f['chapter_title']}", ""]
        out.append(f"- [ ] `{f['file']}` ({FIG_KINDS.get(f['kind'], f['kind'])}) — {f['how']}")
        out.append(f"      {f['caption']}")
    return "\n".join(out) + "\n"


def appendix_codes() -> str:
    rows = []
    for line in open(LOGCODES, encoding="utf-8"):
        if not line.strip() or line.startswith("#"):
            continue
        cols = line.rstrip("\n").split("\t")
        code, enum, pt = cols[0], cols[1], cols[4]
        rows.append(f'<tr><td class="num">{html.escape(code)}</td><td><code>{html.escape(enum)}</code></td><td>{html.escape(pt)}</td></tr>')
    return ('<section class="capitulo apendice" id="ap-b"><p class="rotulo-cap">Apêndice B</p>'
            '<h2>Códigos de evento</h2>'
            f'<p class="lede">Os {len(rows)} códigos que o log de eventos pode registrar, com o texto que o aparelho mostra em português. '
            'O número ao lado de cada registro no log é o contexto (<code>ctx</code>) do evento, explicado no <a href="#cap-16">capítulo 16</a>. '
            'A lista é gerada de <code>tools/logcodes.tsv</code>, a mesma tabela de onde o firmware gera os seus.</p>'
            '<div class="tw"><table><thead><tr><th>Código</th><th>Nome</th><th>Descrição</th></tr></thead>'
            f'<tbody>{"".join(rows)}</tbody></table></div></section>')


CSS = r"""
/* Ângulo — tokens copiados de simut-rx/web/angulo.css (gerado de design/tokens*.json). */
@font-face{font-family:"Ângulo Display";font-weight:600;font-style:normal;font-display:swap;src:url("__FONT__") format("woff2")}
:root{color-scheme:light;
 --fundo:#f6f6f4;--superficie:#ffffff;--superficie-2:#ecebe6;--tinta:#201e1a;--tinta-2:#5f5b54;
 --linha:#dcdad3;--linha-forte:#827e76;--acento:#1f6355;--acento-forte:#174d42;--acento-tinta:#f1faf6;
 --positivo:#20784e;--positivo-suave:#e1f0e7;--alerta:#8a6116;--alerta-suave:#f3ead2;
 --perigo:#b3382e;--perigo-suave:#f7e3e0;--perigo-tinta:#fff5f3;
 --sombra-flutuante:0 12px 32px rgba(23,22,20,.16);
 --font-display:"Ângulo Display","Bricolage Grotesque","Segoe UI",system-ui,sans-serif;
 --font-texto:system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif;
 --font-mono:ui-monospace,"SF Mono","Cascadia Mono",Menlo,Consolas,monospace;
 --espaco-1:4px;--espaco-2:8px;--espaco-3:12px;--espaco-4:16px;--espaco-5:24px;--espaco-6:32px;--espaco-7:48px;
 --raio-controle:6px;--raio-cartao:12px;--raio-total:999px}
@media (prefers-color-scheme:dark){:root:not([data-theme="claro"]):not([data-theme="light"]){color-scheme:dark;
 --fundo:#161513;--superficie:#201e1b;--superficie-2:#2a2723;--tinta:#ebe7df;--tinta-2:#a39c90;
 --linha:#383430;--linha-forte:#78716a;--acento:#5fb39a;--acento-forte:#7cc7b2;--acento-tinta:#0e211b;
 --positivo:#6fbe8e;--positivo-suave:#24352b;--alerta:#d9a84e;--alerta-suave:#38301c;
 --perigo:#e07862;--perigo-suave:#382220;--perigo-tinta:#2b100c;--sombra-flutuante:0 16px 40px rgba(0,0,0,.5)}}
:root[data-theme="escuro"],:root[data-theme="dark"]{color-scheme:dark;
 --fundo:#161513;--superficie:#201e1b;--superficie-2:#2a2723;--tinta:#ebe7df;--tinta-2:#a39c90;
 --linha:#383430;--linha-forte:#78716a;--acento:#5fb39a;--acento-forte:#7cc7b2;--acento-tinta:#0e211b;
 --positivo:#6fbe8e;--positivo-suave:#24352b;--alerta:#d9a84e;--alerta-suave:#38301c;
 --perigo:#e07862;--perigo-suave:#382220;--perigo-tinta:#2b100c;--sombra-flutuante:0 16px 40px rgba(0,0,0,.5)}
*,*::before,*::after{box-sizing:border-box}
html{scroll-behavior:smooth}
body{margin:0;background:var(--fundo);color:var(--tinta);font:400 16px/24px var(--font-texto);-webkit-text-size-adjust:100%}
:focus-visible{outline:2px solid var(--acento);outline-offset:2px}
.pagina{max-width:880px;margin:0 auto;padding:0 var(--espaco-4) var(--espaco-7)}
a{color:var(--acento);text-decoration:none}
a:hover{text-decoration:underline}
p,ul,ol{max-width:70ch}
p{margin:0 0 var(--espaco-4)}
ul,ol{padding-left:var(--espaco-5);margin:0 0 var(--espaco-4)}
li{margin:0 0 var(--espaco-2)}
li>p{margin:0 0 var(--espaco-2)}
h2,h3,h4{font-family:var(--font-display);font-weight:600;color:var(--tinta)}
h3{font-size:18px;line-height:24px;letter-spacing:-.005em;margin:var(--espaco-6) 0 var(--espaco-3)}
h4{font:600 16px/24px var(--font-texto);margin:var(--espaco-5) 0 var(--espaco-2)}
h5{font:600 15px/24px var(--font-texto);color:var(--tinta-2);margin:var(--espaco-4) 0 var(--espaco-2)}
code{font:500 .875em/1.5 var(--font-mono);background:var(--superficie-2);padding:1px var(--espaco-1);border-radius:var(--raio-controle)}
pre{background:var(--superficie-2);border:1px solid var(--linha);border-radius:var(--raio-cartao);padding:var(--espaco-4);overflow-x:auto;margin:0 0 var(--espaco-4);max-width:100%}
pre code{background:none;padding:0;font:500 13px/20px var(--font-mono)}
hr{border:0;border-top:1px solid var(--linha);margin:var(--espaco-6) 0}
dl{max-width:70ch;margin:0 0 var(--espaco-5)}
dt{font-weight:600;margin:var(--espaco-4) 0 var(--espaco-1)}
dd{margin:0 0 var(--espaco-2);padding-left:var(--espaco-4);border-left:2px solid var(--linha)}
dd>p{margin:0 0 var(--espaco-2)}
blockquote{margin:0 0 var(--espaco-4);padding:0 var(--espaco-4);border-left:2px solid var(--linha-forte);color:var(--tinta-2)}
/* capa */
.capa{padding:var(--espaco-7) 0 var(--espaco-6);border-bottom:1px solid var(--linha);margin-bottom:var(--espaco-6)}
.capa-topo{display:flex;justify-content:space-between;align-items:center;gap:var(--espaco-4);margin-bottom:var(--espaco-6)}
.marca{font:600 18px/24px var(--font-display);letter-spacing:-.02em}
.capa h1{font:600 34px/38px var(--font-display);letter-spacing:-.02em;margin:0 0 var(--espaco-3)}
.capa .sub{font-size:18px;line-height:26px;color:var(--tinta-2);max-width:60ch;margin:0 0 var(--espaco-5)}
.fatos{display:flex;flex-wrap:wrap;gap:var(--espaco-2);padding:0;margin:0;list-style:none}
.fatos li{margin:0}
.botao{font:500 15px/20px var(--font-texto);min-height:40px;padding:var(--espaco-2) var(--espaco-4);border-radius:var(--raio-controle);
 background:var(--superficie);border:1px solid var(--linha-forte);color:var(--tinta);cursor:pointer}
.botao:hover{background:var(--superficie-2)}
/* selos */
.selo{display:inline-flex;align-items:center;gap:var(--espaco-1);font:600 13px/16px var(--font-texto);padding:var(--espaco-1) var(--espaco-2);
 border-radius:var(--raio-total);background:var(--superficie-2);color:var(--tinta-2);white-space:nowrap;vertical-align:1px}
.selo i{width:6px;height:6px;border-radius:50%;background:currentColor;display:inline-block}
.selo-alerta{background:var(--alerta-suave);color:var(--alerta)}
.selo-perigo{background:var(--perigo-suave);color:var(--perigo)}
.selo-positivo{background:var(--positivo-suave);color:var(--positivo)}
.selo-perm{font-family:var(--font-mono);font-weight:500;color:var(--tinta)}
/* sumário */
.sumario{margin:0 0 var(--espaco-7)}
.sumario h2{font:600 24px/30px var(--font-display);letter-spacing:-.01em;margin:0 0 var(--espaco-5)}
.parte-toc{margin:0 0 var(--espaco-5)}
.parte-toc h3{margin:0 0 var(--espaco-2);font-size:18px}
.parte-toc h3 small{font:600 13px/16px var(--font-texto);color:var(--tinta-2);margin-right:var(--espaco-2)}
.parte-toc ol{list-style:none;padding:0;margin:0;display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:0 var(--espaco-5)}
.parte-toc li{margin:0}
.parte-toc a{display:flex;gap:var(--espaco-3);padding:var(--espaco-2) 0;color:var(--tinta);border-bottom:1px solid var(--linha)}
.parte-toc a:hover{color:var(--acento);text-decoration:none}
.parte-toc .n{font:500 13px/24px var(--font-mono);color:var(--tinta-2);min-width:3ch;text-align:right}
/* partes e capítulos */
.parte{margin:var(--espaco-7) 0 var(--espaco-6);padding-top:var(--espaco-6);border-top:1px solid var(--linha-forte)}
.parte p{font:600 13px/16px var(--font-texto);color:var(--tinta-2);margin:0 0 var(--espaco-2)}
.parte h2{font:600 34px/38px var(--font-display);letter-spacing:-.02em;margin:0}
.capitulo{margin:0 0 var(--espaco-7);padding-top:var(--espaco-5)}
.rotulo-cap{font:600 13px/16px var(--font-texto);color:var(--tinta-2);margin:0 0 var(--espaco-2)}
.capitulo>h2{font:600 24px/30px var(--font-display);letter-spacing:-.01em;margin:0 0 var(--espaco-3)}
.lede{font-size:18px;line-height:28px;color:var(--tinta-2);margin:0 0 var(--espaco-5)}
/* faixas */
.faixa{background:var(--superficie);border:1px solid var(--linha);border-left:4px solid var(--linha-forte);border-radius:var(--raio-cartao);
 padding:var(--espaco-4);margin:0 0 var(--espaco-4);max-width:74ch}
.faixa>.selo{margin-bottom:var(--espaco-2)}
.faixa p:last-child,.faixa ul:last-child,.faixa ol:last-child{margin-bottom:0}
.faixa-atencao{border-left-color:var(--alerta)}
.faixa-perigo{border-left-color:var(--perigo)}
/* tabelas */
.tw{overflow-x:auto;margin:0 0 var(--espaco-5);border:1px solid var(--linha);border-radius:var(--raio-cartao);background:var(--superficie)}
table{border-collapse:collapse;width:100%;font-size:15px;line-height:22px}
th{text-align:left;font:600 13px/16px var(--font-texto);color:var(--tinta-2);background:var(--superficie-2);padding:var(--espaco-3) var(--espaco-4);vertical-align:bottom}
td{padding:var(--espaco-3) var(--espaco-4);border-top:1px solid var(--linha);vertical-align:top}
td code{overflow-wrap:anywhere}
#ap-a td:nth-child(2){min-width:230px}
#ap-a .tipo{font-size:13px;line-height:18px;color:var(--tinta-2)}
td.num,th.num{font-family:var(--font-mono);font-variant-numeric:tabular-nums}
/* figuras */
.fig{margin:var(--espaco-5) 0}
figcaption{font:400 13px/18px var(--font-texto);color:var(--tinta-2);margin-top:var(--espaco-2);max-width:74ch}
figcaption b{color:var(--tinta);font-weight:600}
.moldura{border:1px solid var(--linha);border-radius:var(--raio-cartao);background:var(--superficie-2);padding:var(--espaco-3);display:inline-block;max-width:100%}
.moldura img{display:block;max-width:100%;height:auto;border-radius:var(--raio-controle)}
.moldura-tft img{width:640px;image-rendering:pixelated}
.moldura-web{border:1px solid var(--linha);border-radius:var(--raio-cartao);overflow:hidden;background:var(--superficie)}
.moldura-web .barra{display:flex;gap:var(--espaco-1);padding:var(--espaco-2) var(--espaco-3);border-bottom:1px solid var(--linha);background:var(--superficie-2)}
.moldura-web .barra span{width:8px;height:8px;border-radius:50%;background:var(--linha-forte)}
.moldura-web img{display:block;width:100%;height:auto}
/* A pending figure is a compact marked box, not an empty frame the size of the
   image: with a hundred of them the manual would be mostly blank rectangles. */
.pendente{display:flex;flex-direction:column;gap:var(--espaco-2);border:1px dashed var(--linha-forte);border-radius:var(--raio-cartao);
 background:var(--superficie);padding:var(--espaco-4);max-width:640px}
.pendente>.selo{align-self:flex-start}
.pend-tipo{font:400 13px/18px var(--font-texto);color:var(--tinta-2)}
.pend-como{font:500 13px/20px var(--font-mono);color:var(--tinta)}
/* rodapé e tema */
.volta{font:400 13px/18px var(--font-texto);margin-top:var(--espaco-5)}
.colofao{border-top:1px solid var(--linha);margin-top:var(--espaco-7);padding-top:var(--espaco-5);font:400 13px/18px var(--font-texto);color:var(--tinta-2)}
.colofao p{max-width:none}
@media (max-width:600px){.capa h1,.parte h2{font-size:28px;line-height:32px}.moldura-tft img{width:100%}}
@media print{#tema{display:none}.parte{break-before:page}.capitulo{break-inside:auto}pre,.tw,.fig{break-inside:avoid}}
"""

BOOT_JS = """(function(){try{var s=localStorage.getItem('angulo:tema');if(s==='claro'||s==='escuro'){document.documentElement.dataset.theme=s;}}catch(e){}})();"""

TOGGLE_JS = """(function(){var b=document.getElementById('tema');if(!b)return;
function atual(){var t=document.documentElement.dataset.theme;if(t==='claro'||t==='escuro')return t;
return matchMedia('(prefers-color-scheme: dark)').matches?'escuro':'claro';}
function rotulo(){b.textContent=atual()==='escuro'?'Usar tema claro':'Usar tema escuro';}
b.addEventListener('click',function(){var n=atual()==='escuro'?'claro':'escuro';document.documentElement.dataset.theme=n;
try{localStorage.setItem('angulo:tema',n);}catch(e){}rotulo();});rotulo();})();"""


def main():
    check = "--check" in sys.argv
    files = sorted(f for f in os.listdir(SRC) if re.match(r"cap\d+-.*\.md$", f))
    if not files:
        raise SystemExit(f"no chapters in {SRC}")
    figures, toc, errors = [], [], []
    chapters = [chapter(os.path.join(SRC, f), figures, toc, errors) for f in files]

    seen = {}
    for f in figures:
        seen[f["id"]] = seen.get(f["id"], 0) + 1
        if seen[f["id"]] == 2:
            errors.append(f"figure id repeated: {f['id']}")

    body = []
    pi = 0
    for no, html_ch in chapters:
        while pi < len(PARTS) and no >= PARTS[pi][0]:
            body.append(f'<div class="parte" id="parte-{PARTS[pi][0]}"><p>{PARTS[pi][1]}</p><h2>{PARTS[pi][2]}</h2></div>')
            pi += 1
        body.append(html_ch)
    toc_html = []
    pi = 0
    for i, (no, cid, title) in enumerate(toc):
        while pi < len(PARTS) and no >= PARTS[pi][0]:
            if toc_html:
                toc_html.append("</ol></div>")
            toc_html.append(f'<div class="parte-toc"><h3><small>{PARTS[pi][1]}</small>{PARTS[pi][2]}</h3><ol>')
            pi += 1
        toc_html.append(f'<li><a href="#{cid}"><span class="n">{no}</span><span>{html.escape(title)}</span></a></li>')
    toc_html.append("</ol></div>")
    toc_html.append('<div class="parte-toc"><h3><small>Apêndices</small>Referência rápida</h3><ol>'
                    '<li><a href="#ap-a"><span class="n">A</span><span>Imagens a capturar</span></a></li>'
                    '<li><a href="#ap-b"><span class="n">B</span><span>Códigos de evento</span></a></li>'
                    '<li><a href="#ap-c"><span class="n">C</span><span>Histórico de versões</span></a></li></ol></div>')

    ap_c_path = os.path.join(SRC, "ap-c-versoes.md")
    ap_c = ""
    if os.path.exists(ap_c_path):
        frag = pandoc(open(ap_c_path, encoding="utf-8").read())
        m = re.match(r'\s*<h2 id="([^"]+)">(.*?)</h2>\s*(<p>.*?</p>)?', frag, re.S)
        rest = spans(wrap_tables(callouts(frag[m.end():])))
        lede = (m.group(3) or "").replace("<p>", '<p class="lede">', 1)
        ap_c = f'<section class="capitulo apendice" id="ap-c"><p class="rotulo-cap">Apêndice C</p><h2>{m.group(2)}</h2>{lede}{rest}</section>'

    pending = sum(1 for f in figures if not f["have"])
    font = data_uri(FONT, "font/woff2") if os.path.exists(FONT) else ""
    css = CSS.replace("__FONT__", font)
    hoje = date.today().strftime("%d/%m/%Y")
    page = f"""<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Manual do SIMUT</title>
<meta name="description" content="Manual do produto SIMUT {VERSION}: instalação, configuração, interface e integração com servidores.">
<script>{BOOT_JS}</script>
<style>{css}</style>
</head>
<body>
<div class="pagina">
<header class="capa">
<div class="capa-topo"><span class="marca">SIMUT</span><button class="botao" id="tema" type="button">Usar tema escuro</button></div>
<h1>Manual do SIMUT</h1>
<p class="sub">Instalação, configuração, uso no dia a dia e integração com servidores, para as três imagens publicadas: o painel (release), o LCD (alpha) e o registrador a bateria (Air).</p>
<ul class="fatos">
<li><span class="selo">Firmware {VERSION}</span></li>
<li><span class="selo">Inclui o que está na main em 23/09/2026</span></li>
<li><span class="selo">{len(toc)} capítulos e 3 apêndices</span></li>
<li><span class="selo selo-alerta"><i></i>{pending} {"imagem" if pending == 1 else "imagens"} a capturar</span></li>
</ul>
</header>
<nav class="sumario" aria-labelledby="sumario-t"><h2 id="sumario-t">Sumário</h2>{"".join(toc_html)}</nav>
<main>
{"".join(body)}
<div class="parte" id="apendices"><p>Apêndices</p><h2>Referência rápida</h2></div>
{appendix_images(figures)}
{appendix_codes()}
{ap_c}
</main>
<footer class="colofao">
<p>Manual do SIMUT, gerado em {hoje} por <code>tools/build_manual.py</code> a partir de <code>docs/_manual/</code>. Licença MIT, como o firmware.</p>
<p>Visual no padrão Ângulo. Fonte de títulos: Ângulo Display, recorte da Bricolage Grotesque SemiBold, SIL Open Font License 1.1.</p>
</footer>
</div>
<script>{TOGGLE_JS}</script>
</body>
</html>
"""
    # Every internal link must land somewhere, and no pandoc markup may leak
    # through as text: both fail silently in a browser, so they fail here.
    ids = set(re.findall(r'\sid="([^"]+)"', page))
    for ref in sorted(set(re.findall(r'href="#([^"]+)"', page))):
        if ref not in ids:
            errors.append(f"broken link: #{ref}")
    for leak in (":::", "{.img}", "{.perm}", "{#cap-"):
        if leak in page:
            errors.append(f"unrendered markup {leak!r}: {page.count(leak)}x")
    if check:
        for e in errors:
            print(e)
        sys.exit(1 if errors else 0)

    open(OUT, "w", encoding="utf-8").write(page)
    os.makedirs(IMG, exist_ok=True)
    open(CAPTURAR, "w", encoding="utf-8").write(capturar_md(figures))
    print(f"wrote {os.path.relpath(OUT, ROOT)}: {len(page):,} B, {len(toc)} chapters, "
          f"{len(figures)} figures ({pending} pending)")
    for e in errors:
        print("warning:", e)


if __name__ == "__main__":
    main()
