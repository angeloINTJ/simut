#!/usr/bin/env python3
"""Testa a conferencia de rotas por feature do build_webui_gz.py.

Rodar: python3 tools/test_webui_feature_routes.py

Uma rota que so as imagens com painel registram (registerScreenRoutes( ), no
WebManager_History.cpp) tem de ser chamada de dentro de um bloco `@IF tft` do
WebUI.h. Fora dele, a alpha e o Air levam um botao que responde 404. Foi o
defeito de 2026-09-26: o #184 tirou /api/reset_touch_cal dessas imagens, e a
secao "Touch Calibration" da /config ficou em todas. Antes do #184 o mesmo
botao respondia ok e dizia "o assistente de calibracao esta rodando no
display", num aparelho sem display.

Os casos sinteticos pinam a regra; o ultimo roda a conferencia sobre o WebUI.h
de verdade, que e o que o build faz em toda imagem.
"""
import os
import sys

# Carregado como o test_webui_minify.py carrega: sem disparar o generate( ) do
# final, que gravaria o src/WebUI_GZ.h com o gzip desta maquina. O diretorio
# de trabalho e a raiz porque e dela que o script le o C++.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)
GEN = os.path.join(ROOT, "tools", "build_webui_gz.py")
_ns = {"__name__": "build_webui_gz"}
exec(compile(open(GEN, encoding="utf-8").read().replace("\ngenerate()\n", "\n"),
             GEN, "exec"), _ns)
feature_routes = _ns["_feature_routes"]
strip_web_features = _ns["_strip_web_features"]

ok = fail = 0


def check(label, cond, detail=""):
    global ok, fail
    if cond:
        ok += 1
    else:
        fail += 1
        print(f"  FALHOU {label}" + (f"\n    {detail}" if detail else ""))


def verdict(content):
    """None se a conferencia passa; a mensagem do SystemExit se ela recusa."""
    try:
        strip_web_features(content)
        return None
    except SystemExit as e:
        return str(e)


# 1. As rotas saem do C++. As sete de hoje, e nada do stub vazio.
tft = feature_routes("tft")
want = {"/api/screenshot", "/api/screenshot_chunk", "/api/screen_stream",
        "/api/touch", "/api/keypad", "/api/themes", "/api/reset_touch_cal"}
check("as rotas do painel sao as sete do registerScreenRoutes( )", tft == want,
      f"obtido {sorted(tft)}")
check("feature sem registrador nao tem rota", feature_routes("nada") == set())

# 2. Chamada fora do bloco: recusada, com o nome da rota.
fora = """
<button onclick="resetTouchCal()">Reset</button>
<script>
async function resetTouchCal() { await fetchSafe('/api/reset_touch_cal', { method: 'POST' }); }
</script>
"""
v = verdict(fora)
check("rota do painel chamada fora do @IF tft e recusada",
      v is not None and "/api/reset_touch_cal so e registrada com tft" in v, repr(v))

# 3. A mesma chamada dentro do bloco: passa. O botao e a funcao somem juntos.
dentro = """
/* @IF tft */
<button onclick="resetTouchCal()">Reset</button>
/* @ENDIF */
<script>
/* @IF tft */
async function resetTouchCal() { await fetchSafe('/api/reset_touch_cal', { method: 'POST' }); }
/* @ENDIF */
</script>
"""
check("a mesma chamada dentro do @IF tft passa", verdict(dentro) is None, repr(verdict(dentro)))

# 4. Prefixo: '/api/screenshot_chunk?i=' acusa so a si mesma, e uma rota que
#    comeca como uma do painel nao e confundida com ela.
v = verdict("<script>fetch(`/api/screenshot_chunk?i=${i}`);</script>\n")
check("template com '?' acusa /api/screenshot_chunk",
      v is not None and "/api/screenshot_chunk so e" in v, repr(v))
check("... e nao acusa /api/screenshot", v is None or "/api/screenshot so e" not in v, repr(v))
check("'/api/screenshots' nao e '/api/screenshot'",
      verdict("<script>fetch('/api/screenshots');</script>\n") is None)

# 5. Comentario nao e chamada: prosa citando a rota nao desarma nem dispara.
check("rota citada em comentario /* */ nao e chamada",
      verdict("<script>/* chama '/api/touch' no painel */ x();</script>\n") is None)

# 6. Rotas cercadas por #if no C++ (2026-09-30): as de /api/ dentro das regioes
#    `#if SIMUT_WEB_HISTORY` do WebManager_Core.cpp, sem a pagina /history (que
#    e alcancada por link, e o link sai no bloco do menu).
hist = feature_routes("web_history")
check("as rotas da pagina de historico sao as duas /api/ da regiao #if",
      hist == {"/api/logcodes", "/api/clear_logs"}, f"obtido {sorted(hist)}")
exp = feature_routes("web_export_api")
check("as rotas de exportacao sao as tres da regiao #if",
      exp == {"/api/history_multi", "/api/export/history.bin", "/api/export/logs.bin"},
      f"obtido {sorted(exp)}")
v = verdict("<script>fetch('/api/history_multi?s=0');</script>\n")
check("uma pagina que chama /api/history_multi e recusada",
      v is not None and "/api/history_multi so e registrada com web_export_api" in v, repr(v))

# 7. Um nome definido dentro do bloco E fora dele (cada pagina com o seu helper)
#    nao e dependencia do bloco: a chamada de fora vai para a funcao de fora.
#    Foi o `fmt` do dashboard quando a pagina de historico virou um bloco.
dois = """
<script>function fmt(v){return v;} x = fmt(1);</script>
/* @IF tft */
<script>function fmt(v){return v;} y = fmt(2);</script>
/* @ENDIF */
"""
check("helper homonimo definido fora nao e dependencia do bloco",
      verdict(dois) is None, repr(verdict(dois)))
um = """
<script>x = fmt(1);</script>
/* @IF tft */
<script>function fmt(v){return v;} y = fmt(2);</script>
/* @ENDIF */
"""
v = verdict(um)
check("... mas sem a definicao de fora, a chamada ainda e recusada",
      v is not None and "fmt( ) e definida dentro de @IF tft e chamada fora" in v, repr(v))

# 8. O bloco negado (2026-09-30): `@IF !feature` so entra na imagem SEM a
#    feature — o que a pagina diz quando o recurso nao existe (a telemetria
#    que o aparelho guardou pedindo MQTT, numa imagem sem MQTT). As duas
#    metades nunca coexistem, entao cada uma e "fora" da outra.
def cut(content, omit):
    saved = _ns["WEB_OMIT"]
    _ns["WEB_OMIT"] = set(omit)
    try:
        return strip_web_features(content)
    finally:
        _ns["WEB_OMIT"] = saved


par = """
/* @IF tft */
<select id="sel"></select>
/* @ENDIF */
/* @IF !tft */
<div id="aviso">sem painel</div>
/* @ENDIF */
"""
check("@IF !tft entra na imagem sem painel",
      "aviso" in cut(par, {"tft"}) and "sel" not in cut(par, {"tft"}))
check("... e sai da imagem com painel",
      "aviso" not in cut(par, set()) and "sel" in cut(par, set()))
check("os marcadores somem nas duas", "@IF" not in cut(par, {"tft"}) + cut(par, set()))

v = verdict("/* @IF !nada */\nx\n/* @ENDIF */\n")
check("@IF !<feature desconhecida> e recusado",
      v is not None and "@IF !nada nao e uma feature conhecida" in v, repr(v))

v = verdict("""
/* @IF !tft */
<div id="aviso"></div>
/* @ENDIF */
<script>document.getElementById('aviso').style.display = '';</script>
""")
check("id que so existe no @IF !tft e buscado fora e recusado",
      v is not None and "#aviso so existe dentro de @IF !tft" in v, repr(v))

v = verdict("""
/* @IF !tft */
<script>fetch('/api/touch');</script>
/* @ENDIF */
""")
check("rota do painel chamada dentro do @IF !tft e recusada (la nao existe)",
      v is not None and "/api/touch so e registrada com tft" in v, repr(v))

v = verdict("""
/* @IF tft */
<div id="sel"></div>
/* @ENDIF */
/* @IF !tft */
<script>document.getElementById('sel').value = 1;</script>
/* @ENDIF */
""")
check("o @IF !tft que busca um id do @IF tft e recusado",
      v is not None and "#sel so existe dentro de @IF tft" in v, repr(v))

# O par: a mesma funcao definida nos dois lados, chamada so no codigo comum.
# Toda imagem tem exatamente uma — nem "chamada fora", nem "nunca usada".
stub = """
/* @IF tft */
<script>function prep() { document.getElementById('sel').value = 0; }</script>
<select id="sel"></select>
/* @ENDIF */
/* @IF !tft */
<script>function prep() { }</script>
/* @ENDIF */
<script>prep();</script>
"""
check("funcao definida no @IF tft e no @IF !tft, chamada no comum, passa",
      verdict(stub) is None, repr(verdict(stub)))
v = verdict("""
/* @IF !tft */
<script>function prep() { }</script>
/* @ENDIF */
<script>prep();</script>
""")
check("... mas so no @IF !tft, a chamada comum e recusada (a imagem com painel nao a tem)",
      v is not None and "prep( ) e definida dentro de @IF !tft e chamada fora" in v, repr(v))
v = verdict("""
/* @IF !tft */
<script>function orfa() { }</script>
/* @ENDIF */
""")
check("funcao do @IF !tft que ninguem chama e recusada",
      v is not None and "orfa( ) e definida dentro de @IF !tft e nunca usada" in v, repr(v))

# 9. Busca que confere o nulo nao e dependencia: `let e = getElementById('x');
#    if (e) ...` nao vira TypeError na imagem sem o bloco. E o que o
#    toggleTransport( ) do MQTT faz com o rotulo do TLS, numa imagem com MQTT e
#    sem TLS. A conferencia tem de testar a MESMA variavel.
guard = """
/* @IF tft */
<span id="rot"></span>
/* @ENDIF */
<script>let s = document.getElementById('rot'); if (s) s.textContent = 'x';
var w = document.getElementById('rot'); if (!w) return;</script>
"""
check("busca que confere o nulo, fora do bloco, passa",
      verdict(guard) is None, repr(verdict(guard)))
v = verdict("""
/* @IF tft */
<span id="rot"></span>
/* @ENDIF */
<script>let s = document.getElementById('rot'); if (t) s.textContent = 'x';</script>
""")
check("... mas conferir OUTRA variavel nao protege",
      v is not None and "#rot so existe dentro de @IF tft" in v, repr(v))

# 10. O WebUI.h de verdade: a conferencia que o build roda em toda imagem.
real = open(os.path.join(ROOT, "WebUI.h"), encoding="utf-8").read()
v = verdict(real)
check("WebUI.h: nenhuma rota de feature chamada fora do seu @IF", v is None, v)

print(f"{ok} ok, {fail} falha(s)")
sys.exit(1 if fail else 0)
