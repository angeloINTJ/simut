#!/usr/bin/env python3
"""Testa o minificador do build_webui_gz.py contra as armadilhas conhecidas.

Rodar: python3 tools/test_webui_minify.py

Cada caso aqui corresponde a um jeito que o minificador anterior — um regex
sem contexto — errava. Ele casava toda aspa do documento como delimitador de
string, entao uma aspa dentro de um comentario ou de um literal de regex
deslocava o pareamento e ele passava a achar que estava dentro de uma string
quando estava no codigo. As consequencias medidas: uma pagina embarcada com 9%
do tamanho (todos os handlers perdidos, e ainda assim JavaScript valido), e
92 KB da /history mais 9.990 B de comentario da lang.js viajando intactos para
o firmware porque a minificacao nao os alcancava.

Quem mexer no escaner roda isto antes de commitar.
"""
import os
import sys

# O minificador mora dentro do build_webui_gz.py de proposito: o build tem de
# funcionar sem dependencia nenhuma, inclusive para quem compila a partir do
# zip da release. Aqui ele e carregado sem disparar o generate() do final.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, "tools", "build_webui_gz.py")
_ns = {"__name__": "build_webui_gz"}
exec(compile(open(GEN, encoding="utf-8").read().replace("\ngenerate()\n", "\n"),
             GEN, "exec"), _ns)
_js_tokens = _ns["_js_tokens"]
minify_js = _ns["_minify_js"]
minify_css = _ns["_minify_css"]
minify_html = _ns["_minify_html"]
significant_tokens = _ns["_essence"]

ok = fail = 0


def check(label, got, want):
    global ok, fail
    if got == want:
        ok += 1
    else:
        fail += 1
        print(f"  FALHOU {label}\n    esperado: {want!r}\n    obtido  : {got!r}")


def toks(src):
    return [(k, t) for k, t in _js_tokens(src)]


def roundtrip(label, src):
    """Todo byte tem de estar coberto pelos tokens, na ordem."""
    global ok, fail
    j = "".join(t for _, t in _js_tokens(src))
    if j == src:
        ok += 1
    else:
        fail += 1
        print(f"  FALHOU cobertura {label}: perdeu/duplicou bytes")
        print(f"    entrada {len(src)} B, tokens {len(j)} B")


print("== literal de regex com aspa dentro (o gatilho do HIST_PAGE) ==")
s = r'''const esc = s => String(s).replace(/[<>&"]/g, ch => m[ch]);
/* comentario com aspa solta " que antes engolia tudo */
var depois = 1;'''
roundtrip("regex+aspa", s)
check("regex reconhecido", [k for k, _ in toks(s) if k == "regex"], ["regex"])
check("comentario removido", "comentario com aspa" in minify_js(s), False)
check("codigo depois sobrevive", "var depois = 1;" in minify_js(s), True)

print("== aspas escapadas e apostrofo ==")
roundtrip("escape", r"""var a = 'don\'t'; var b = "it's"; var c = 'say \"hi\"';""")
check("3 strings", [k for k, _ in toks(r"""var a='don\'t';var b="it's";var c='x';""") if k == "string"],
      ["string", "string", "string"])

print("== template literal com ${} ==")
s = "var h = `<div class=\"x\">${obj.name} e ${f('a')}</div>`; var z = 2;"
roundtrip("template", s)
check("z sobrevive", "var z = 2;" in minify_js(s), True)
s2 = "var h = `a${ `b${ c }d` }e`; var w = 3;"
roundtrip("template aninhado", s2)
check("template aninhado ok", "var w = 3;" in minify_js(s2), True)

print("== template com varios ${} (o bug que engolia o resto do arquivo) ==")
for lbl, t in [("dois", "var h=`a${x}b${y}c`; var z=1;"),
               ("tres", "var h=`${a}-${b}-${c}`; var z=1;"),
               ("com objeto", "var h=`${arr.map(x=>{return x;}).join('')}`; var z=1;"),
               ("aninhado", "var h=`a${ `b${c}d` }e`; var z=1;"),
               ("crase escapada", "var h=`a\\`b`; var z=1;")]:
    roundtrip("template " + lbl, t)
    check("template " + lbl + ": codigo apos sobrevive", "var z=1;" in minify_js(t), True)

print("== CSS: espaco em volta de : e significativo em seletor ==")
check("descendente preservado", minify_css(".a :hover { color: red; }"), ".a :hover{color: red;}")

print("== divisao NAO e regex ==")
s = "var r = (a + b) / 2; var t = x / y / z; var u = 9;"
roundtrip("divisao", s)
check("nenhum regex", [k for k, _ in toks(s) if k == "regex"], [])
check("u sobrevive", "var u = 9;" in minify_js(s), True)

print("== regex depois de return/typeof ==")
s = "function f(){ return /ab+c/i.test(s); }"
roundtrip("regex apos return", s)
check("regex apos return", [k for k, _ in toks(s) if k == "regex"], ["regex"])

print("== // dentro de string nao e comentario ==")
s = "var u = 'http://exemplo/x'; var v = \"a//b\"; var q = 7;"
roundtrip("// em string", s)
check("nada de comentario", [k for k, _ in toks(s) if "comment" in k], [])
check("q sobrevive", "var q = 7;" in minify_js(s), True)

print("== comentario com aspas desbalanceadas ==")
s = '''/* leaves " and ' unescaped, value="..." */
window.escAttr = function(s){ return s; };
var fim = 1;'''
roundtrip("comentario aspas", s)
check("escAttr sobrevive", "window.escAttr" in minify_js(s), True)
check("fim sobrevive", "var fim = 1;" in minify_js(s), True)

print("== ASI: linha NAO pode ser juntada ==")
s = "function f(){\n  return\n  x\n}"
check("quebra preservada apos return", "return\n" in minify_js(s), True)

print("== indentacao e linhas em branco somem ==")
s = "var a = 1;\n\n\n        var b = 2;\n        \n    var c = 3;"
check("colapsado", minify_js(s), "var a = 1;\nvar b = 2;\nvar c = 3;")

print("== CSS ==")
check("comentario css", minify_css("a { /* nota */ color: red; }"), "a{color: red;}")
check("string css preservada", minify_css('a { content: "/* nao e comentario */"; }'),
      'a{content: "/* nao e comentario */";}')

print("== HTML ==")
h = '''<!DOCTYPE html><html><head>
  <!-- comentario -->
  <style>  .a { color : red ; } </style>
  <script>  var x = 1; /* c */ </script>
</head><body>
  <div  class="a"   id="b" >texto</div>
</body></html>'''
m = minify_html(h)
check("comentario html some", "comentario" in m, False)
check("css minificado", ".a{color : red;}" in m, True)
check("js minificado", "var x = 1;" in m, True)
check("comentario js some", "/* c */" in m, False)
check("atributos preservados", 'class="a" id="b"' in m, True)

print("== espaco DENTRO de valor de atributo nao pode encolher ==")
h2 = '<div style="margin: 0 auto;   padding: 0" data-x="a  b">t</div>'
check("valor intacto", 'style="margin: 0 auto;   padding: 0"' in minify_html(h2), True)
check("data-x intacto", 'data-x="a  b"' in minify_html(h2), True)

print("== equivalencia de tokens significativos ==")
src = open(os.path.join(ROOT, "WebUI.h"), encoding="utf-8").read()
import re as _re
m = _re.search(r'static const char LANG_JS\[\] PROGMEM = R"raw\((.*?)\)raw";', src, _re.S)
lang = m.group(1)
roundtrip("LANG_JS real", lang)
a = significant_tokens(lang, "js")
b = significant_tokens(minify_js(lang), "js")
check("LANG_JS: mesma sequencia de tokens", a, b)
print(f"  (LANG_JS: {len(a[0])} literais e {len(a[1])} B de codigo preservados; "
      f"{len(lang)} -> {len(minify_js(lang))} B)")

print("== o portao do build DISPARA quando alguem estraga o escaner ==")
# Um portao que nunca dispara e indistinguivel de um que funciona. Estes casos
# sao os tres modos de estrago reais: codigo trocado, codigo apagado, e
# conteudo de string alterado (que foi o defeito de verdade achado ao escrever
# este escaner — um re.sub de espaco rodando sobre o texto ja montado).
_gate = _ns["_assert_only_whitespace_removed"]
_good = minify_js(lang)
_casos = [
    ("simbolo trocado", _good.replace("window.escAttr = function", "window.NOPE = function", 1)),
    ("funcao apagada", _good.replace("window.escHtml", "", 1)),
    ("string alterada", _good.replace("'greet_hello'", "'greet_hell'", 1)),
]
for _lbl, _bad in _casos:
    if _bad == _good:
        check("portao/" + _lbl + ": caso construido", False, True)
        continue
    try:
        _gate("LANG_JS", lang, _bad, "js")
        check("portao dispara em " + _lbl, False, True)
    except SystemExit:
        ok += 1
try:
    _gate("LANG_JS", lang, _good, "js")
    ok += 1
except SystemExit as e:
    fail += 1
    print("  FALHOU: portao acusa minificacao legitima —", str(e)[:200])

print(f"\n{ok} passaram, {fail} falharam")
sys.exit(1 if fail else 0)
