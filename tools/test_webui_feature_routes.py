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

# 6. O WebUI.h de verdade: a conferencia que o build roda em toda imagem.
real = open(os.path.join(ROOT, "WebUI.h"), encoding="utf-8").read()
v = verdict(real)
check("WebUI.h: nenhuma rota do painel chamada fora do @IF tft", v is None, v)

print(f"{ok} ok, {fail} falha(s)")
sys.exit(1 if fail else 0)
