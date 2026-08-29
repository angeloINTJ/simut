#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Gerador do Relatorio de Auditoria de Seguranca — SIMUT.
Ambiente isolado: docs/security-audit/.venv (reportlab + matplotlib via system-site-packages).
Regenerar:  .venv/bin/python gerar_relatorio.py
"""
import os, datetime, math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

from reportlab.lib.pagesizes import A4
from reportlab.lib.units import cm
from reportlab.lib import colors
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.enums import TA_CENTER, TA_LEFT, TA_JUSTIFY
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame, Paragraph,
                                Spacer, Table, TableStyle, Image, PageBreak, Preformatted)

HERE = os.path.dirname(os.path.abspath(__file__))

C_CRIT  = "#B91C1C"
C_ALTA  = "#EA580C"
C_MEDIA = "#D97706"
C_BAIXA = "#2563EB"
C_INFO  = "#64748B"
C_FORTE = "#059669"
C_INK   = "#1f2937"
C_SUB   = "#6b7280"

def hx(c): return colors.HexColor(c)

def build_styles():
    ss = getSampleStyleSheet()
    out = {}
    out["h1"] = ParagraphStyle("h1", parent=ss["Title"], fontSize=22, leading=27,
                               textColor=hx(C_INK), alignment=TA_LEFT, spaceAfter=8)
    out["h2"] = ParagraphStyle("h2", parent=ss["Heading2"], fontSize=14, leading=18,
                               textColor=hx(C_ALTA), spaceBefore=12, spaceAfter=6)
    out["h3"] = ParagraphStyle("h3", parent=ss["Heading3"], fontSize=11.5, leading=15,
                               textColor=hx(C_INK), spaceBefore=8, spaceAfter=4)
    out["body"] = ParagraphStyle("body", parent=ss["BodyText"], fontSize=9.5, leading=13.5,
                                 textColor=hx(C_INK), alignment=TA_JUSTIFY, spaceAfter=5)
    out["small"] = ParagraphStyle("small", parent=out["body"], fontSize=8, leading=11,
                                  textColor=hx(C_SUB))
    out["mono"] = ParagraphStyle("mono", parent=ss["Code"], fontSize=7.0, leading=9.2,
                                 textColor=hx(C_INK), backColor=colors.HexColor("#f4f4f5"),
                                 borderPadding=5, spaceAfter=4)
    out["cover_title"] = ParagraphStyle("cover_title", parent=ss["Title"], fontSize=28,
                                        leading=34, textColor=hx(C_INK), alignment=TA_LEFT)
    out["cover_sub"] = ParagraphStyle("cover_sub", parent=ss["Title"], fontSize=13,
                                      leading=18, textColor=hx(C_ALTA), alignment=TA_LEFT)
    out["cover_body"] = ParagraphStyle("cover_body", parent=out["body"], fontSize=10,
                                       leading=15, textColor=hx(C_INK))
    out["td"] = ParagraphStyle("td", parent=out["body"], fontSize=8, leading=10.5,
                               alignment=TA_LEFT, spaceAfter=0)
    out["tdc"] = ParagraphStyle("tdc", parent=out["td"], alignment=TA_CENTER)
    return out

PROJETO = "SIMUT"
VERSAO_AUDITADA = "v2.3.6-beta (commit 4586733)"
DATA = datetime.date.today().strftime("%d/%m/%Y")

FINDINGS = [
 dict(id="ACH-01", cat="3. IDOR", sev="ALTA", loc="src/WebManager_Files.cpp:85-129",
  title="DELETE /api/delete aceita qualquer caminho — apaga /config (credenciais) sem a guarda isSecretFsPath",
  desc=("O handler de exclusao valida o caminho apenas contra <b>isProtectedFsPath</b> "
        "(que protege somente os README.txt de pastas). Diferente do download (linha 59, que rejeita "
        "<i>..</i>, <i>%</i> e <b>isSecretFsPath</b>), aqui nao ha rejeicao de traversal nem da pasta "
        "<b>/config</b>. Um usuario com PERM_FILE_DELETE (0x80) consegue apagar <b>/config/system.bin</b> "
        "(loja de credenciais + hashes), <b>/config/web_key.pem</b> e <b>/config/web_cert.pem</b>."),
  evid="""void WebManager::handleDelete( ) {
  ... if (!(perms & PERM_FILE_DELETE)) { 403 }
  String path = _server->arg("file");
  if (isProtectedFsPath(path)) { 403 }   // so README.txt
  ...
  LittleFS.remove(path);                 // sem isSecretFsPath, sem ".."/%
}""",
  imp=("Quebra do invariante documentado (AUTHORIZATION.md / SECURITY.md §9.2): /config so e acessivel via "
       "/api/backup com PERM_FULL_ADMIN. Apagar system.bin forca factory-reset (perda de config, usuarios, "
       "WiFi, telemetria) e apagar a chave TLS faz downgrade silencioso para HTTP. Requer uma conta com "
       "PERM_FILE_DELETE (nao e padrao, mas e concedivel via UI por um admin web)."),
  fix=("Aplicar a mesma guarda do handleDownload: rejeitar <b>path.indexOf('..')</b>, "
       "<b>path.indexOf('%')</b> e <b>isSecretFsPath(path)</b> antes de LittleFS.remove.")),

 dict(id="ACH-02", cat="3. IDOR", sev="ALTA", loc="src/WebManager_Files.cpp:359-437",
  title="UPLOAD /api/upload permite gravar sob /config — sobrescreve system.bin / planta certificado TLS",
  desc=("A montagem do caminho final de upload (<b>uploadDir</b> + nome do arquivo) nao aplica "
        "<b>isSecretFsPath</b>. <b>isSafeUploadFilename</b> permite o caractere <i>/</i>, e o campo "
        "<b>uploadDir</b> e aceito como <i>/config</i>. So <b>/calib.csv</b> e remapeado (linha 432). Um "
        "usuario com PERM_FILE_UPLOAD (0x40) pode sobrescrever <b>/config/system.bin</b> (corrompe a config "
        "-> factory-reset no proximo boot) ou gravar <b>web_cert.pem</b>/<b>web_key.pem</b> proprios (passa a "
        "servir TLS com certificado do atacante apos reboot, viabilizando MITM do trafego administrativo)."),
  evid="""String targetDir = "/";
if (_server->hasArg("uploadDir")) { targetDir = _server->arg("uploadDir"); ... }
String finalPath = (targetDir == "/") ? filename
                      : targetDir + "/" + baseName;
if (finalPath == "/calib.csv") finalPath = "/calib.tmp";  // unico caso especial
_uploadFile = LittleFS.open(finalPath, "w");""",
  imp="Escrita arbitraria na pasta de segredos do dispositivo, sob uma permissao de 'upload de arquivos'.",
  fix=("Rejeitar <b>isSecretFsPath(finalPath)</b> e tambem <b>isSecretFsPath(targetDir)</b> em "
       "UPLOAD_FILE_START, marcando <b>_uploadRejected</b>, da mesma forma que o calib.csv ja e protegido.")),

 dict(id="ACH-03", cat="3. IDOR", sev="MEDIA", loc="src/ota/restore.cpp:27-36",
  title="RESTORE /api/restore?op=apply valida '..' mas nao /config — .bkp malicioso grava em /config",
  desc=("<b>path_is_safe</b> rejeita apenas caminhos que nao comecam com '/' e a sequencia '..'. Nao ha "
        "checagem de <b>isSecretFsPath</b>, entao um backup .bkp forjado pode nomear <b>/config/system.bin</b> "
        "como destino. O vinculo por <b>chip_id</b> nao e uma barreira real: o chip_id e o mesmo valor exposto "
        "como <b>serial</b> em <b>GET /api/config</b> (src/WebManager_Api.cpp:203-204, PERM_SYS_CONFIG)."),
  evid="""static bool path_is_safe(const char* p, uint16_t len) {
    if (len == 0 || p[0] != '/') return false;
    for (...) if (p[i] == 0) return false;
    for (i+1<len) if (p[i]=='.' && p[i+1]=='.') return false;
    return true;   // nao testa /config
}""",
  imp=("Combinacao PERM_FILE_UPLOAD + PERM_SYS_CONFIG permite sobrescrever a config via .bkp. Mesmo tema de "
       "ACH-01/02: caminhos de escrita nao respeitam a guarda de /config."),
  fix="Adicionar checagem de /config em path_is_safe (rejeitar prefixo DIR_CONFIG)."),

 dict(id="ACH-04", cat="3. IDOR", sev="BAIXA", loc="src/WebManager_Files.cpp:131-171",
  title="LIST /api/ls lista o conteudo de /config (nomes de arquivos sensiveis)",
  desc=("handleApiLs valida '..' e '%' mas nao <b>isSecretFsPath</b>, entao <b>?dir=/config</b> enumera os "
        "nomes dos arquivos da pasta de segredos (system.bin, web_cert.pem, web_key.pem). Divulgacao minima de "
        "informacao, mas revela exatamente o que ACH-01/02 permitem apagar/sobrescrever."),
  evid="""if (dirPath.indexOf("..") >= 0 || dirPath.indexOf('%') >= 0) { 400 }
...
Dir dir = LittleFS.openDir(dirPath);   // dirPath pode ser "/config" """,
  imp="Enumeracao dos nomes de arquivos sensiveis de /config.",
  fix="Recusar dirPath sob /config (isSecretFsPath) em handleApiLs."),

 dict(id="ACH-05", cat="4. Chaves expostas", sev="BAIXA",
  loc="tools/web_test_suite.py:57 · tools/pico_test_suite.py:301 · tools/alarm_mqtt_test.py:47 · tools/validate_top_pin_alarm.py:40 · tools/telemetry_bench/phase_mqtt.py:239",
  title="Credenciais de bancada/teste hardcoded em scripts (Wt3st!suite#2026, Pico!Test123, Alarm!Test2026, benchuser/benchsecret)",
  desc=("Diversos scripts em <b>tools/</b> trazem senhas literais de contas de bancada. Elas estao "
        "documentadas como test-only em <b>tools/.secretscan-allow</b> e o gate <b>scan_secrets.sh</b> as "
        "libera de proposito. Nao sao compiladas no firmware. O risco e o <b>reuso</b>: se um operador criar "
        "essas mesmas credenciais em um dispositivo real, qualquer leitor do repositorio publico "
        "(github.com/angeloINTJ/simut) tem o segredo."),
  evid="""tools/web_test_suite.py:57    TEST_PASS = 'Wt3st!suite#2026'
tools/pico_test_suite.py:301  API_PASS  = 'Pico!Test123'
tools/alarm_mqtt_test.py:47   TEST_PASS = "Alarm!Test2026"
tools/telemetry_bench/phase_mqtt.py:239  'm_user':'benchuser', 'm_pass':'benchsecret'""",
  imp="Exposicao de credenciais caso haja reuso em producao; senao, apenas higiene.",
  fix=("Manter o padrao ja adotado (rig_secrets.py gitignored / variaveis de ambiente SIMUT_WEB_USER/"
       "SIMUT_WEB_PASS) em todos os scripts e reduzir a allowlist a um numero minimo de senhas de teste.")),

 dict(id="ACH-06", cat="4. Chaves expostas", sev="BAIXA",
  loc="src/StorageManager.cpp:506-511 · src/StorageManager.cpp:556",
  title="Credenciais padrao documentadas: usuario viewer/viewer e PIN de display 1234",
  desc=("O firmware cria a conta <b>viewer</b> com senha <b>viewer</b> (PERM_DASHBOARD|PERM_HISTORY) e o PIN "
        "de display <b>1234</b>. Ambos tem flag de troca obrigatoria no primeiro uso (mustChangePassword / "
        "mustChangePin) e estao documentados em SECURITY.md §2. O PIN 1234 e deliberadamente trivial "
        "(protege so contra toque acidental)."),
  evid="""safeCopy(_currentConfig.users[1].username, "viewer", ...);
defaultViewerHash = hashPasswordV1("viewer", sha256("viewer"), salt);
safeCopy(_currentConfig.displayPin, "1234", ...);
setMustChangePin( );""",
  imp="Baixo: a troca e forcada no primeiro login/acesso; a senha do admin e aleatoria (nao hardcoded).",
  fix="Sem acao obrigatoria alem de manter as flags de troca e a documentacao."),

 dict(id="ACH-07", cat="5. XSS", sev="BAIXA", loc="WebUI.h:6984 · src/WebManager_Api.cpp:452-512",
  title="Stored XSS via pacote de idioma: applyLang injeta @WEBDICT (de .lng enviavel) em innerHTML sem sanitizar",
  desc=("O dicionario de UI e servido por <b>/api/lang</b> (sem autenticacao) a partir do bloco "
        "<b>@WEBDICT</b> do arquivo <b>.lng</b> ativo — que e enviavel via /files (PERM_FILE_UPLOAD). "
        "<b>applyLang</b> escreve esses valores com <b>el.innerHTML = text</b> sem passar por <b>escHtml</b>. "
        "Um operador com PERM_FILE_UPLOAD que suba um .lng malicioso e o torne ativo executa JavaScript no "
        "navegador de todos os usuarios (inclusive admin). O resto do frontend usa escHtml/escAttr "
        "corretamente (finding M-7)."),
  evid="""WebUI.h:6984  window.applyLang = function() { ... else el.innerHTML = text; };
             // text = dict[lang][key], vindo de /api/lang (@WEBDICT do .lng)""",
  imp="Execucao de script no contexto do admin, condicionada a um ator ja privilegiado (upload de .lng).",
  fix=("Aplicar escHtml(text) no applyLang, ou servir o dicionario de idioma a partir de uma fonte nao "
       "gravavel por usuario (pacote embutido em flash, como as paginas).")),

 dict(id="ACH-08", cat="4. Chaves expostas", sev="INFO", loc="src/NetworkManager.cpp:97-98",
  title="Access Point de setup aberto (sem senha) com SSID previsivel <nome>_SETUP",
  desc=("No modo AP de configuracao/recovery, o AP e criado sem senha (WiFi.softAP) e com SSID derivado do "
        "nome do dispositivo (padrao <b>simut_SETUP</b>). O web server continua exigindo login (senha admin "
        "aleatoria), e o AP expira em 15 min (AP_MODE_TIMEOUT_MS) retornando a STA quando ha SSID configurado. "
        "Superficie de setup/recovery, por design."),
  evid="""String apName = String(deviceName) + "_SETUP";
WiFi.softAP(apName.c_str( ));   // sem senha""",
  imp="Associacao aberta por vizinhos de RF; mitigado pelo login obrigatorio + timeout.",
  fix=("Opcional: adicionar senha WPA2 derivada do serial para o AP de setup, sem quebrar o fluxo de recovery.")),
]

SEV_ORDER = ["CRITICA", "ALTA", "MEDIA", "BAIXA", "INFO"]
SEV_LABEL = {"CRITICA":"Crítica","ALTA":"Alta","MEDIA":"Média","BAIXA":"Baixa","INFO":"Informativa"}
SEV_COLOR = {"CRITICA":C_CRIT,"ALTA":C_ALTA,"MEDIA":C_MEDIA,"BAIXA":C_BAIXA,"INFO":C_INFO}

def sev_count():
    c = {k:0 for k in SEV_ORDER}
    for f in FINDINGS: c[f["sev"]] += 1
    return c

def cat_count():
    cats = ["1. Isolamento/tenant", "2. Permissão no navegador", "3. IDOR", "4. Chaves expostas", "5. XSS"]
    c = {k:0 for k in cats}
    for f in FINDINGS: c[f["cat"]] += 1
    return c

def make_donut(path):
    sc = sev_count()
    items = [(SEV_LABEL[k], sc[k], SEV_COLOR[k]) for k in SEV_ORDER if sc[k] > 0]
    labels = [i[0] for i in items]; vals = [i[1] for i in items]; cols = [i[2] for i in items]
    total = sum(vals)
    fig, ax = plt.subplots(figsize=(4.7, 3.5), dpi=150)
    wedges, _ = ax.pie(vals, colors=cols, startangle=90, counterclock=False,
                       wedgeprops=dict(width=0.42, edgecolor="white"))
    for w, lab, v in zip(wedges, labels, vals):
        ang = (w.theta2 + w.theta1) / 2.0
        x = 1.16 * math.cos(math.radians(ang))
        y = 1.16 * math.sin(math.radians(ang))
        ax.text(x, y, lab + chr(10) + str(v), ha="center", va="center", fontsize=8.5,
                fontweight="bold", color="#111827")
    ax.text(0, 0, str(total), ha="center", va="center", fontsize=22, fontweight="bold", color=C_INK)
    ax.text(0, -0.20, "achados", ha="center", va="center", fontsize=8, color=C_SUB)
    ax.set(aspect="equal"); ax.axis("off")
    ax.set_xlim(-1.55, 1.55); ax.set_ylim(-1.55, 1.55)
    fig.tight_layout(pad=0.2)
    fig.savefig(path, transparent=True); plt.close(fig)

def make_bar(path):
    cc = cat_count()
    cats = list(cc.keys()); vals = [cc[k] for k in cats]
    short = ["1. Isolam./tenant", "2. Perm. navegador", "3. IDOR", "4. Chaves exp.", "5. XSS"]
    cols = [C_FORTE if v == 0 else C_ALTA for v in vals]
    fig, ax = plt.subplots(figsize=(6.6, 3.0), dpi=150)
    bars = ax.bar(short, vals, color=cols, width=0.6)
    for b, v in zip(bars, vals):
        ax.text(b.get_x()+b.get_width()/2, v+0.06, str(v), ha="center", va="bottom",
                fontsize=9, fontweight="bold", color=C_INK)
    ax.set_ylim(0, max(vals)+0.55)
    ax.spines["top"].set_visible(False); ax.spines["right"].set_visible(False)
    ax.tick_params(axis="x", labelsize=7.4); ax.tick_params(axis="y", labelsize=7.4)
    ax.grid(axis="y", alpha=0.25, linewidth=0.6); ax.set_axisbelow(True)
    fig.tight_layout(pad=0.4)
    fig.savefig(path, transparent=True); plt.close(fig)

class ReportDoc(BaseDocTemplate):
    def __init__(self, fn, **kw):
        super().__init__(fn, pagesize=A4, leftMargin=2*cm, rightMargin=2*cm,
                         topMargin=2.1*cm, bottomMargin=2*cm, **kw)
        frame = Frame(self.leftMargin, self.bottomMargin, self.width, self.height, id="f")
        self.addPageTemplates([PageTemplate(id="p", frames=[frame], onPage=self._deco)])
    def _deco(self, canv, doc):
        canv.saveState()
        canv.setFont("Helvetica", 7); canv.setFillColor(hx(C_SUB))
        canv.drawString(2*cm, A4[1]-1.35*cm, "Relatório de Auditoria de Segurança — SIMUT")
        canv.drawRightString(A4[0]-2*cm, A4[1]-1.35*cm, "Confidencial")
        canv.setStrokeColor(hx("#e5e7eb")); canv.setLineWidth(0.5)
        canv.line(2*cm, A4[1]-1.5*cm, A4[0]-2*cm, A4[1]-1.5*cm)
        canv.line(2*cm, 1.5*cm, A4[0]-2*cm, 1.5*cm)
        canv.drawString(2*cm, 1.05*cm, "SIMUT — auditoria de segurança")
        canv.drawRightString(A4[0]-2*cm, 1.05*cm, "Página %d" % doc.page)
        canv.restoreState()

def P(text, s): return Paragraph(text, s)

_CHIP = {"CRITICA":"Crítica","ALTA":"Alta","MEDIA":"Média","BAIXA":"Baixa","INFO":"Info"}

def sev_chip(sev):
    return ('<font size="7.5" backColor="%s" color="white"><b>&nbsp;%s&nbsp;</b></font>'
            % (SEV_COLOR[sev], _CHIP[sev]))

def build():
    S = build_styles()
    story = []

    story.append(Spacer(1, 2.0*cm))
    story.append(P("Relatório de Auditoria de Segurança", S["cover_title"]))
    story.append(Spacer(1, 0.25*cm))
    story.append(P("SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria", S["cover_sub"]))
    story.append(Spacer(1, 1.6*cm))
    for label, val in [
        ("Data", DATA),
        ("Versão auditada", VERSAO_AUDITADA),
        ("Escopo", "Firmware embarcado (C++/Arduino) + Web UI + OTA/MQTT/Telemetria + CI/Docker + histórico git"),
        ("Metodologia", "Cinco categorias mapeadas para a stack: isolamento de inquilino → RBAC por bitmask de permissão por sessão; permissão no navegador → cruzamento dos gates do frontend (WebUI.h) com os handlers do backend; IDOR → rotas por path/arquivo (download/delete/ls/upload/mkdir/restore); chaves expostas → código + configs + CI + Docker + histórico git; XSS → sinks innerHTML/HTML dinâmico no frontend e backend."),
    ]:
        story.append(P("<b>%s:</b> %s" % (label, val), S["cover_body"]))
        story.append(Spacer(1, 0.25*cm))
    story.append(PageBreak())

    story.append(P("Resumo executivo", S["h1"]))
    sc = sev_count(); tot = sum(sc.values())
    story.append(P(
        "Foram identificados <b>%d achados</b> verificados no código real: "
        "<b>%d alta</b>, <b>%d média</b>, <b>%d baixa</b> e <b>%d informativa</b> (nenhuma crítica). "
        "O principal tema é a <b>guarda da pasta /config</b> (loja de credenciais e chaves TLS) aplicada "
        "apenas à leitura (<i>/download</i>) e ausente nos caminhos de escrita e exclusão — <i>/api/delete</i>, "
        "<i>/api/upload</i> e <i>/api/restore?op=apply</i>. O restante da superfície é notavelmente robusto: "
        "RBAC por bitmask em todas as rotas, modelo de dois níveis de privilégio, lockout exponencial de login, "
        "sanitização de caminhos, escape de HTML no frontend e gate de segredos no CI."
        % (tot, sc["ALTA"], sc["MEDIA"], sc["BAIXA"], sc["INFO"]), S["body"]))

    donut_png = os.path.join(HERE, "grafico_severidade.png")
    bar_png = os.path.join(HERE, "grafico_categoria.png")
    make_donut(donut_png); make_bar(bar_png)
    chart_tbl = Table([
        [Image(donut_png, width=7.4*cm, height=5.51*cm),
         Image(bar_png, width=9.2*cm, height=4.18*cm)]
    ], colWidths=[8.2*cm, 9.8*cm])
    chart_tbl.setStyle(TableStyle([
        ("VALIGN",(0,0),(-1,-1),"MIDDLE"), ("ALIGN",(0,0),(-1,-1),"CENTER"),
        ("LEFTPADDING",(0,0),(-1,-1),2), ("RIGHTPADDING",(0,0),(-1,-1),2),
    ]))
    story.append(chart_tbl)
    story.append(Spacer(1, 0.15*cm))
    story.append(P(
        "<b>Paleta:</b> crítica <font color='#B91C1C'>■</font> · alta <font color='#EA580C'>■</font> · "
        "média <font color='#D97706'>■</font> · baixa <font color='#2563EB'>■</font> · "
        "informativa <font color='#64748B'>■</font> · ponto forte <font color='#059669'>■</font>", S["small"]))
    story.append(PageBreak())

    story.append(P("Pontos fortes e pontos fracos", S["h1"]))
    story.append(P("Pontos fortes (o que está protegido, com evidência)", S["h2"]))
    for head, txt in [
        ("Autorização em todas as rotas.", "Todos os handlers de <b>src/WebManager_*.cpp</b> chamam "
         "<b>getAuthPerms()</b> e testam o bit correto; a matriz é documentada em docs/AUTHORIZATION.md e "
         "enforçada por tools/check_authz.py no CI (nenhuma rota sem gate, exceto as allowlistadas com razão)."),
        ("Dois níveis de privilégio.", "/api/commit_all recusa perms acima de PERM_ALL_BITS (0x03FF); "
         "/api/backup, /api/restore?op=stage e /api/ota/apply exigem <b>perms == PERM_FULL_ADMIN</b>. "
         "Web admin não lê backup nem grava firmware (WebManager_Commit.cpp:1079; WebManager_Ota.cpp)."),
        ("Login endurecido.", "Nonce CSRF consumido atomicamente, lockout exponencial por IP com slots "
         "sticky, senhas HMAC-SHA256 com salt aleatório + pepper do chip (v1), migração legada e "
         "secureCompare em tempo constante (WebManager_Auth.cpp / StorageManager.cpp)."),
        ("Sanitização de caminho (leitura).", "/download, /api/ls, /api/upload e /api/mkdir rejeitam '..', "
         "'%' e caracteres hostis; isSafeUploadFilename/isSafeDirPath com allowlist; /download recusa "
         "/config via isSecretFsPath (WebManager_Files.cpp:59)."),
        ("Anti-XSS.", "Frontend usa escHtml/escAttr em todos os templates com dado de operador; backend usa "
         "jsonEscape/jsonEscapeFilename; nomes validados por isValidName (WebUI.h:6992/7007; M-7)."),
        ("Gate de segredos + CI.", "tools/scan_secrets.sh (roda no GitHub Actions) bloqueia .pem/.key/.bkp, "
         "chaves privadas por conteúdo e credenciais literais; fuzz (libFuzzer) e cppcheck em jobs separados."),
        ("Config protegida em repouso.", "Campos sensíveis com XOR keystream (SHA-256(chip_id+domínio)), "
         "CRC32 + dual-bank, senha inicial do admin apenas em RAM e zerada após uso."),
        ("Histórico git limpo.", "Nenhuma chave privada, .bkp ou credencial real commitada (scan em 895 "
         "commits); gate de segredos passa limpo."),
    ]:
        story.append(P("<font color='#059669'>✔</font> <b>%s</b> %s" % (head, txt), S["body"]))
    story.append(Spacer(1, 0.3*cm))
    story.append(P("Pontos fracos (riscos centrais)", S["h2"]))
    for txt in [
        "Escrita/exclusão sobre <b>/config</b> sem guarda (ACH-01..04): a proteção da loja de credenciais cobre leitura, mas não mutação.",
        "Confiança no canal: o login envia SHA-256 da senha (pass-the-hash) e, sem TLS provisionado, cookies/payloads trafegam em claro em rede não confiável (reconhecido em SECURITY.md como fora de escopo).",
        "Sem assinatura de firmware: OTA valida apenas CRC/size, não autentica a origem do binário (responsabilidade do operador).",
        "Credenciais padrão (viewer/viewer, PIN 1234) e AP de setup aberto — mitigados por troca forçada e timeout, mas exigem disciplina do operador.",
    ]:
        story.append(P("<font color='#B91C1C'>■</font> %s" % txt, S["body"]))
    story.append(PageBreak())

    story.append(P("Achados detalhados por categoria", S["h1"]))
    story.append(P("Severidade | Arquivo:linha | Descrição. Os trechos completos de código, impacto e "
                   "correção estão nas issues (seção final).", S["small"]))
    data = [["Sev.", "Categoria", "Arquivo:linha", "Descrição"]]
    for f in FINDINGS:
        data.append([
            Paragraph(sev_chip(f["sev"]), S["tdc"]),
            Paragraph(f["cat"], S["td"]),
            Paragraph("<font face='Courier' size='7'>%s</font>" % f["loc"], S["td"]),
            Paragraph("<b>%s</b><br/>%s" % (f["title"], f["desc"]), S["td"]),
        ])
    tbl = Table(data, colWidths=[1.55*cm, 2.15*cm, 3.35*cm, 10.95*cm], repeatRows=1)
    tbl.setStyle(TableStyle([
        ("BACKGROUND",(0,0),(-1,0), hx(C_INK)), ("TEXTCOLOR",(0,0),(-1,0), colors.white),
        ("FONTNAME",(0,0),(-1,0),"Helvetica-Bold"), ("FONTSIZE",(0,0),(-1,0),8),
        ("VALIGN",(0,0),(-1,-1),"TOP"),
        ("GRID",(0,0),(-1,-1),0.4, hx("#d1d5db")),
        ("ROWBACKGROUNDS",(0,1),(-1,-1),[colors.white, colors.HexColor("#fafafa")]),
        ("TOPPADDING",(0,0),(-1,-1),4), ("BOTTOMPADDING",(0,0),(-1,-1),4),
        ("LEFTPADDING",(0,0),(-1,-1),4), ("RIGHTPADDING",(0,0),(-1,-1),4),
    ]))
    story.append(tbl)
    story.append(PageBreak())

    story.append(P("Recomendações priorizadas", S["h1"]))
    for tag, txt in [
        ("P1", "Aplicar a guarda de /config (isSecretFsPath + rejeição de '..'/'%') em /api/delete, /api/upload e path_is_safe do restore, e recusar a listagem de /config em /api/ls. É uma correção local, de alto valor, e fecha o único cluster de alta severidade."),
        ("P2", "Cobrir o gap com um teste de regressão em tools/check_authz.py ou novo script: para cada rota de mutação de arquivo, assertar que /config é recusado (delete/upload/restore)."),
        ("P3", "Escapar o texto do dicionário de idioma (escHtml) em applyLang, ou servir o dicionário de fonte não gravável (flash), eliminando o stored-XSS residual via .lng."),
        ("P4", "Substituir as credenciais de bancada literais por rig_secrets.py/ambiente em todos os scripts de tools/ e enxugar a allowlist .secretscan-allow."),
        ("P5", "Avaliar assinar o firmware OTA (ou ao menos documentar um hash de release) e, opcionalmente, proteger o AP de setup com WPA2 derivada do serial."),
        ("P6", "Reforçar a orientação ao operador sobre rede isolada/VPN, já que o transporte sem TLS e o pass-the-hash do login são limitações assumidas."),
    ]:
        story.append(P("<b>[%s]</b> %s" % (tag, txt), S["body"]))
    story.append(PageBreak())

    story.append(P("Issues para o GitHub", S["h1"]))
    story.append(P("Cada bloco abaixo é uma issue completa em Markdown, pronta para copiar e colar.", S["small"]))
    story.append(Spacer(1, 0.2*cm))

    issues = [
        dict(title="[Segurança] Caminhos de escrita/exclusão de /config não aplicam a guarda isSecretFsPath (delete, upload, restore, ls)",
             labels="security, severity: high",
             problem=("As rotas de mutação de arquivos não protegem a pasta **/config** (loja de credenciais: "
                      "system.bin com hashes de senha e wifi/mqtt/telemetria, além das chaves TLS "
                      "web_cert.pem/web_key.pem). A guarda **isSecretFsPath** existe, mas só é aplicada na "
                      "LEITURA (**/download**). Isso quebra o invariante documentado em AUTHORIZATION.md e "
                      "SECURITY.md §9.2 (acesso a /config restrito a PERM_FULL_ADMIN via /api/backup)."),
             evid="""- **ACH-01** src/WebManager_Files.cpp:85-129 — handleDelete valida apenas isProtectedFsPath (README.txt) e chama LittleFS.remove(path); sem isSecretFsPath nem rejeição de .. ou %. Requer PERM_FILE_DELETE.
- **ACH-02** src/WebManager_Files.cpp:359-437 — handleUploadData monta finalPath a partir de uploadDir sem checar /config; só /calib.csv é remapeado (linha 432). Requer PERM_FILE_UPLOAD.
- **ACH-03** src/ota/restore.cpp:27-36 — path_is_safe rejeita .. mas não /config; o chip_id de vínculo é exposto como serial em src/WebManager_Api.cpp:203-204.
- **ACH-04** src/WebManager_Files.cpp:131-171 — handleApiLs lista /config.""",
             impact=("Apagar /config/system.bin força factory-reset (perda de config/usuários/WiFi/telemetria); "
                     "apagar/substituir web_key.pem faz downgrade silencioso para HTTP ou passa a servir TLS com "
                     "certificado do atacante (MITM do tráfego admin). Escrita de system.bin corrompe a config."),
             fix=("Aplicar a mesma guarda de /download (linha 59) em todos os caminhos de mutação: rejeitar "
                  "isSecretFsPath(path) + .. e % em handleDelete, em handleUploadData (finalPath e uploadDir, "
                  "marcando _uploadRejected), em path_is_safe e em handleApiLs."),
             accept="""- [ ] POST /api/delete file=/config/system.bin → 403 (hoje: apaga).
- [ ] POST /api/upload com uploadDir=/config → rejeitado.
- [ ] POST /api/restore?op=apply com entrada /config/... → PATH_INVALID.
- [ ] GET /api/ls?dir=/config → recusado.
- [ ] Teste de regressão no CI cobrindo os quatro casos."""),
        dict(title="[Segurança] Credenciais de bancada hardcoded em tools/ (reuso possível em produção)",
             labels="security, severity: low",
             problem=("Scripts de bancada em tools/ contêm senhas literais de contas de teste "
                      "(Wt3st!suite#2026, Pico!Test123, Alarm!Test2026, benchuser/benchsecret), hoje liberadas "
                      "em tools/.secretscan-allow. Não embarcam no firmware, mas o repositório é público: se um "
                      "operador reusar essas credenciais num dispositivo real, o segredo já é público por construção."),
             evid="""- tools/web_test_suite.py:57  TEST_PASS = 'Wt3st!suite#2026'
- tools/pico_test_suite.py:301  API_PASS = 'Pico!Test123'
- tools/alarm_mqtt_test.py:47  TEST_PASS = "Alarm!Test2026"
- tools/telemetry_bench/phase_mqtt.py:239  'm_user':'benchuser','m_pass':'benchsecret'""",
             impact="Comprometimento de conta/device caso haja reuso das credenciais de teste em produção.",
             fix=("Migrar todos os scripts para rig_secrets.py (gitignored) ou variáveis de ambiente "
                  "SIMUT_WEB_USER/SIMUT_WEB_PASS, e reduzir a allowlist ao mínimo. Manter o gate scan_secrets.sh."),
             accept="""- [ ] Nenhum script em tools/ lê senha literal de conta web; todos usam rig_secrets/env.
- [ ] ./tools/scan_secrets.sh continua limpo.
- [ ] Documentação de bancada explica como criar rig_secrets.py."""),
        dict(title="[Segurança] Stored XSS via pacote de idioma (.lng → applyLang innerHTML sem sanitização)",
             labels="security, severity: low",
             problem=("applyLang (WebUI.h:6984) escreve o dicionário de UI (dict[lang][key]) com "
                      "el.innerHTML = text sem passar por escHtml. O dicionário vem de /api/lang (sem "
                      "autenticação), servido do bloco @WEBDICT do arquivo .lng ativo — enviável via /files por "
                      "um usuário com PERM_FILE_UPLOAD. Um .lng malicioso ativado executa JS no navegador de "
                      "todos os usuários, incluindo o admin."),
             evid="""- WebUI.h:6984  window.applyLang = ... else el.innerHTML = text;
- src/WebManager_Api.cpp:452-512  handleApiLang serve o @WEBDICT sem auth.""",
             impact="Execução de script no contexto administrativo, condicionada a um ator já privilegiado (upload de .lng).",
             fix=("Aplicar escHtml(text) no applyLang, ou servir o dicionário a partir de fonte não gravável "
                  "(pacote embutido em flash, como as páginas), mantendo o restante do escape (M-7)."),
             accept="""- [ ] Um .lng com <img src=x onerror=...> num valor de chave existente não executa.
- [ ] Teste automatizado de sanitização do dicionário."""),
        dict(title="[Segurança] Hardening de defaults e superfície de setup (viewer/viewer, PIN 1234, AP aberto)",
             labels="security, severity: low",
             problem=("A conta viewer nasce com senha viewer e o PIN de display é 1234 (ambos com troca "
                      "forçada no primeiro uso, documentados em SECURITY.md §2). O AP de setup/recovery é criado "
                      "aberto, com SSID previsível <nome>_SETUP. São escolhas deliberadas e mitigadas (troca "
                      "forçada, timeout de 15 min, login obrigatório), registradas aqui como higiene de hardening."),
             evid="""- src/StorageManager.cpp:506-511 (viewer/viewer)
- src/StorageManager.cpp:556 (PIN 1234 + setMustChangePin)
- src/NetworkManager.cpp:97-98 (WiFi.softAP sem senha)""",
             impact="Baixo, dado o controle de troca obrigatória e o login; risco residual se o operador não trocar.",
             fix="Opcional: WPA2 derivada do serial para o AP de setup; manter e testar as flags de troca forçada.",
             accept="""- [ ] Avaliar WPA2 no AP sem quebrar o fluxo de recovery.
- [ ] Confirmar que viewer/viewer e PIN 1234 continuam bloqueando navegação até a troca."""),
    ]

    n = 1
    NL = chr(10)
    for it in issues:
        body = NL.join([
            "**Descrição do problema / por que é explorável**", "",
            it["problem"], "",
            "**Evidência (arquivo:linha + trecho)**", "",
            it["evid"], "",
            "**Impacto**", "",
            it["impact"], "",
            "**Sugestão de correção**", "",
            it["fix"], "",
            "**Critérios de aceite**", "",
            it["accept"],
        ])
        md = ("--- ISSUE %d ---" % n) + NL + ("# %s" % it["title"]) + NL + ("> **Labels sugeridas:** %s" % it["labels"]) + NL + NL + body + NL + ("--- FIM ISSUE %d ---" % n)
        story.append(Preformatted(md, S["mono"]))
        story.append(Spacer(1, 0.35*cm))
        n += 1

    out = os.path.join(HERE, "relatorio-auditoria-seguranca.pdf")
    doc = ReportDoc(out, title="Relatório de Auditoria de Segurança — SIMUT",
                    author="Auditoria de Segurança")
    doc.build(story)
    return out

if __name__ == "__main__":
    out = build()
    print("PDF gerado:", out)
