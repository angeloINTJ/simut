#!/usr/bin/env python3
"""Walks every screen of the functional study build (pico_wiretft_angulo) on the
bench, by touch, in the light and the dark theme, and keeps a capture and the
frame's state of every step.

    python3 tools/tft_ui/roteiro_ui.py --host 192.168.3.40:8080 --out <pasta> [--temas claro,escuro]

Touches are POST /api/touch at the centre of a target the board itself lists
(GET /api/ui), never at coordinates copied from a layout. Each step says which
mode it expects on the glass afterwards and fails when another one is there.

What it changes on the device, and puts back:
  - a test account (teste_ui, an 8-digit PIN, the panel's alarm bits, system
    settings and account management) created over the web before the walk and
    deleted at the end; both changes reboot the device;
  - the theme (cfg.themeIndex), set per walk and restored;
  - nothing else is saved: every editor is left with Cancel or Back, and the
    PIN policy, the display alignment's Save (it wipes the touch calibration)
    and the clock's Save (it rewrites this boot's history stamps) are never
    pressed.

Credentials come from ~/.simut-bench.env, never printed. The report is
<out>/relatorio.json; the captures are <out>/<tema>/<nn>_<passo>.png.
"""
import argparse
import json
import os
import pathlib
import random
import sys
import time
import traceback

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from bancada_ui import Bancada, TestFail  # noqa: E402

CONTA = 'teste_ui'
# PERM_SYS_CONFIG | PERM_USER_MGR | PERM_PANEL_ALARM_ANY (SystemDefs_Records.h):
# every Settings item of the wired build opens for it.
PERMS = 0x0008 | 0x0100 | 0x1C00

# UiMode values (SystemDefs_Records.h, in order).
MODOS = ['DASHBOARD', 'GRAPH_LOADING', 'GRAPH_VIEW', 'GRAPH_DETAIL', 'AUTH', 'SETTINGS_MAIN', 'SETTINGS_THEMES',
         'SETTINGS_ALARMS', 'SETTINGS_ALARM_EDIT', 'SETTINGS_LANG', 'SETTINGS_PASSWORD', 'SETTINGS_TOUCH_CAL',
         'SETTINGS_TOUCH_SENS', 'SETTINGS_SOUNDS', 'SETTINGS_LICENSE', 'SETTINGS_STATUS', 'SETTINGS_DISPLAY_OFFSET',
         'ALARM_ACTION', 'CALENDAR', 'CONFIRM_MUTE_ALL', 'SETTINGS_ALARM_SENSOR', 'SETTINGS_MAINT', 'SETTINGS_USERS',
         'SETTINGS_USER_EDIT', 'SETTINGS_USER_CONFIRM_DEL', 'PANEL_MESSAGE', 'AUTH_USER', 'SETTINGS_PIN_POLICY',
         'CONFIRM_AP', 'SET_CLOCK', 'OTA_UPDATE']
M = {n: i for i, n in enumerate(MODOS)}


class Roteiro:
    def __init__(self, host, out):
        self.b = Bancada(host)
        self.out = pathlib.Path(out)
        self.out.mkdir(parents=True, exist_ok=True)
        self.passos = []
        self.tema = None
        self.n = 0
        self.pin = ''.join(random.choice('0123456789') for _ in range(8))

    # -- the record ---------------------------------------------------------
    def passo(self, nome, modo, cond=None, timeout=10.0, captura=True):
        """Waits for `modo` on the glass (and cond, if given), keeps its state
        and a capture, and records the step."""
        t0 = time.monotonic()
        reg = {'tema': self.tema, 'passo': nome, 'espera': MODOS[modo] if modo is not None else None}
        try:
            st = self.b.esperar(lambda s: (modo is None or s['modo'] == modo) and (cond is None or cond(s)),
                                timeout, nome)
            reg.update(ok=True, modo=MODOS[st['modo']], ms=st['ms'], pinturas=st['pinturas'],
                       alvos=len(st['alvos']), t=round(time.monotonic() - t0, 2))
            if captura:
                self.n += 1
                pasta = self.out / self.tema
                pasta.mkdir(exist_ok=True)
                arq = pasta / f'{self.n:03d}_{nome}.png'
                self.b.captura().save(arq)
                reg['captura'] = str(arq.relative_to(self.out))
            print(f'[{self.tema}] ok   {nome}: {reg["modo"]} ({reg["ms"]} ms)', flush=True)
            self.passos.append(reg)
            return st
        except Exception as e:  # noqa: BLE001  (a failed step is recorded and the walk goes on)
            reg.update(ok=False, erro=str(e))
            print(f'[{self.tema}] FAIL {nome}: {e}', flush=True)
            self.passos.append(reg)
            raise

    def salvar(self):
        ok = sum(1 for p in self.passos if p.get('ok'))
        (self.out / 'relatorio.json').write_text(json.dumps(
            {'passos': self.passos, 'ok': ok, 'falhas': len(self.passos) - ok}, ensure_ascii=False, indent=1))
        return ok, len(self.passos) - ok

    # -- the device ---------------------------------------------------------
    def status(self):
        r = self.b.web.get('/api/status')
        if r.status_code != 200:
            raise TestFail(f'/api/status HTTP {r.status_code}')
        return r.json()

    def usuarios(self):
        r = self.b.web.get('/api/users')
        if r.status_code != 200:
            raise TestFail(f'/api/users HTTP {r.status_code}')
        return r.json()

    def commit(self, payload):
        # commit_all answers 503 inside the 5 s window a touch opens.
        for _ in range(12):
            r = self.b.web.post('/api/commit_all', data={'_payload': json.dumps(payload)},
                                headers={'Content-Type': 'application/x-www-form-urlencoded'})
            if r.status_code != 503:
                return r
            time.sleep(1.0)
        return r

    def esperar_reinicio(self, uptime_antes, timeout=180.0):
        """A users change reboots the device: wait until it answers with a smaller uptime, then
        until its first frame is on the glass. Each failure is printed, not swallowed."""
        import bancada_ui
        t0 = time.monotonic()
        ultimo = ''
        while time.monotonic() - t0 < timeout:
            try:
                self.b.web.login(*bancada_ui.creds())
                up = self.status()['sys']['uptime']
                if up < uptime_antes:
                    print(f'  reiniciou: uptime {up} ms, {time.monotonic() - t0:.0f} s depois do pedido')
                    self.b.esperar(lambda s: s['modo'] == M['DASHBOARD'], 60, 'painel depois do reinício')
                    return up
                msg = f'ainda não reiniciou (uptime {up} ms)'
            except Exception as e:  # noqa: BLE001  (down while it reboots)
                msg = f'{type(e).__name__}: {str(e)[:80]}'
            if msg != ultimo:
                print(f'  {time.monotonic() - t0:5.1f} s: {msg}')
                ultimo = msg
            time.sleep(2.0)
        raise TestFail(f'o aparelho não voltou em {timeout:.0f} s')

    def esperar_conta(self, presente, timeout=60.0):
        """Until /api/users lists the test account (or no longer does). A users change used to
        reboot the device (panel_users_hw_test.py); on this build it does not (2026-10-14, uptime
        read before and after), so both are handled: the client logs in again if it has to."""
        import bancada_ui
        t0 = time.monotonic()
        while time.monotonic() - t0 < timeout:
            try:
                u = next((u for u in self.usuarios() if u['name'] == CONTA), None)
                if (u is not None) == presente and (not presente or u.get('pin')):
                    return u
            except Exception:  # noqa: BLE001  (down while it reboots)
                try:
                    self.b.web.login(*bancada_ui.creds())
                except Exception:  # noqa: BLE001
                    pass
            time.sleep(1.0)
        raise TestFail(f'{CONTA} {"não apareceu" if presente else "não sumiu"} em /api/users em {timeout:.0f} s')

    def criar_conta(self):
        r = self.commit({'users': {'actions': [{'type': 'add', 'name': CONTA, 'perms': PERMS, 'pin': self.pin}]}})
        if r.status_code != 200:
            raise TestFail(f'criar {CONTA}: HTTP {r.status_code} {r.text[:160]}')
        u = self.esperar_conta(True)
        self.b.esperar(lambda s: s['modo'] == M['DASHBOARD'], 60, 'painel depois de criar a conta')
        return u

    def apagar_conta(self):
        u = next((u for u in self.usuarios() if u['name'] == CONTA), None)
        if not u:
            return False
        r = self.commit({'users': {'actions': [{'type': 'del', 'id': u['id']}]}})
        if r.status_code != 200:
            raise TestFail(f'apagar {CONTA}: HTTP {r.status_code} {r.text[:160]}')
        self.esperar_conta(False)
        return True

    def keypad(self):
        r = self.b.web.get('/api/keypad')
        if r.status_code != 200:
            raise TestFail(f'/api/keypad HTTP {r.status_code}')
        return r.json()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--host', default=os.environ.get('SIMUT_HOST'))
    ap.add_argument('--out', required=True)
    ap.add_argument('--temas', default='claro,escuro')
    a = ap.parse_args()
    if not a.host:
        raise SystemExit('passe --host ip:porta (ou defina SIMUT_HOST)')
    rt = Roteiro(a.host, a.out)
    tema_antes = rt.status()['sys']['theme']
    print(f'tema antes: {tema_antes}')
    try:
        rt.criar_conta()
        for tema in a.temas.split(','):
            rt.tema = tema
            rt.b.tema({'claro': 0, 'escuro': 1}[tema])
            rt.passo('painel_tema', M['DASHBOARD'], lambda s, t=tema: s['tema'] == t, timeout=15)
            for etapa in ETAPAS:
                try:
                    etapa(rt)
                except Exception:  # noqa: BLE001
                    traceback.print_exc()
                    try:
                        voltar_ao_painel(rt)
                    except Exception:  # noqa: BLE001  (a board that is stuck stops the walk, not the clean-up)
                        traceback.print_exc()
                        raise SystemExit('a placa não voltou ao painel: o roteiro parou aqui')
    finally:
        try:
            rt.apagar_conta()
        finally:
            rt.b.tema(tema_antes)
            ok, falhas = rt.salvar()
            print(f'{ok} passos ok, {falhas} falhas; relatório em {rt.out / "relatorio.json"}')


def voltar_ao_painel(rt):
    """After a failed step: the 30 s idle rule brings any screen back to the dashboard."""
    rt.b.esperar(lambda s: s['modo'] == M['DASHBOARD'], 40, 'volta ao painel por inatividade')


# ---------------------------------------------------------------------------
# Target types, by screen (the enums of src/ui/angulo/telas/*.h, 2026-10-14).
# ---------------------------------------------------------------------------
MSG_BOTAO, DLG_NAO, DLG_SIM = 1, 1, 2
PNL_CIMA, PNL_GRAF_CIMA, PNL_BAIXO, PNL_GRAF_BAIXO, PNL_NAV = 1, 2, 3, 4, 5
LST_VOLTAR, LST_LINHA, LST_ANT, LST_PROX = 1, 2, 3, 4            # menu, theme, language, alarms, status, licence
SA_VOLTAR, SA_LIMITES, SA_ALARMES, SA_MANUT = 1, 2, 3, 4
QUEM_VOLTAR, QUEM_LINHA, QUEM_ANT, QUEM_PROX = 1, 2, 3, 4
PIN_TECLA, PIN_POPUP, PIN_APAGAR, PIN_ENTRAR, PIN_CANCELAR = 1, 2, 3, 4, 5
TXT_TECLA, TXT_SHIFT, TXT_CAMADA, TXT_ESPACO, TXT_APAGAR, TXT_VER, TXT_SALVAR, TXT_CANCELAR = 1, 2, 3, 4, 5, 6, 7, 8
USR_VOLTAR, USR_NOVA, USR_LINHA, USR_ANT, USR_PROX = 1, 2, 3, 4, 5
CTA_VOLTAR, CTA_INTERRUPTOR, CTA_DEFINIR_PIN, CTA_EXCLUIR, CTA_SALVAR, CTA_CANCELAR = 1, 2, 3, 4, 5, 6
GRF_VOLTAR, GRF_DIA, GRF_PERIODO, GRF_ANT, GRF_SEG, GRF_AREA = 1, 2, 3, 4, 5, 6
DET_VOLTAR, DET_ANT, DET_PROX = 1, 2, 3
CAL_VOLTAR, CAL_MES_ANT, CAL_MES_SEG, CAL_DIA, CAL_HOJE = 1, 2, 3, 4, 5
LIM_VOLTAR, LIM_MENOS, LIM_MAIS, LIM_ANT, LIM_PROX, LIM_SALVAR, LIM_CANCELAR = 1, 2, 3, 4, 5, 6, 7
MAN_VOLTAR, MAN_MENOS, MAN_MAIS, MAN_INICIAR, MAN_ENCERRAR, MAN_CANCELAR = 1, 2, 3, 4, 5, 6
SONS_VOLTAR, SONS_INTERRUPTOR, SONS_LINHA, SONS_MENOS, SONS_MAIS, SONS_ANT, SONS_PROX, SONS_SALVAR = 1, 2, 3, 4, 5, 6, 7, 8
MEL_VOLTAR, MEL_ITEM, MEL_USAR, MEL_CANCELAR = 1, 2, 3, 4
POL_VOLTAR, POL_MENOS, POL_MAIS, POL_TECLADO, POL_CARACTERES, POL_SALVAR, POL_CANCELAR = 1, 2, 3, 4, 5, 6, 7
REL_MAIS, REL_MENOS, REL_VOLTAR, REL_PULAR, REL_SALVAR = 1, 2, 3, 4, 5
ALI_VOLTAR, ALI_CIMA, ALI_BAIXO, ALI_ESQ, ALI_DIR, ALI_REDEF, ALI_SALVAR = 1, 2, 3, 4, 5, 6, 7
SOM_TOQUE, SOM_MUDO = 0, 6
NAV_CFG, NAV_PAGINA = -2, -3

# The Settings rows the test account sees on the wired build (SettingsMenu.h
# MENU_ORDER without MENU_SETUP_AP, every bit but PERM_NET_CONFIG).
MENU = ['LIMITES', 'SONS', 'STATUS', 'DATA_HORA', 'PIN_PROPRIO', 'USUARIOS', 'SEGURANCA_PIN', 'IDIOMA', 'TEMA',
        'CALIBRAR', 'ALINHAR', 'LICENCA']
# The text keyboard's layers (IdentidadeTexto.cpp) as [row][column].
CAMADAS = [['qwertyuiop', 'asdfghjkl@', 'zxcvbnm.-_'], ['QWERTYUIOP', 'ASDFGHJKL#', 'ZXCVBNM!?;'],
           ['1234567890', '@#$%&*-+=~', '()[]{}/\\^_']]


def tem(st, tipo, indice=None):
    return any(a[4] == tipo and (indice is None or a[5] == indice) for a in st['alvos'])


def tocar(rt, tipo, indice=None, ms=100):
    return rt.b.tocar_alvo(tipo, indice, ms)


def presente(s):
    """The chart's window ends now: GET /api/ui "janela" against the device's "agora"."""
    return abs(s['agora'] - s['janela'][1]) <= 120


def no_passado(s):
    return s['agora'] - s['janela'][1] > 3600


def mudou(st0):
    """A condition: the frame on the glass is another one than st0's."""
    return lambda s: s['hash'] != st0['hash'] or s['pinturas'] != st0['pinturas']


def ir_ao_painel(rt):
    """Back to the dashboard: the back targets first, the 30 s idle rule last."""
    for _ in range(6):
        st = rt.b.estado()
        if st['modo'] == M['DASHBOARD']:
            return st
        if tem(st, 1):
            tocar(rt, 1)
            time.sleep(0.5)
        else:
            break
    return rt.b.esperar(lambda s: s['modo'] == M['DASHBOARD'], 45, 'volta ao painel')


def lista_ate(rt, indice, tipo_linha, tipo_ant, tipo_prox):
    """Pages a list until the row `indice` (its position in the whole list) is on the glass."""
    for _ in range(8):
        st = rt.b.estado()
        if tem(st, tipo_linha, indice):
            return st
        vis = [a[5] for a in st['alvos'] if a[4] == tipo_linha]
        if vis and indice < min(vis) and tem(st, tipo_ant):
            tocar(rt, tipo_ant)
        elif tem(st, tipo_prox):
            tocar(rt, tipo_prox)
        else:
            break
        rt.b.esperar(mudou(st), 5, 'página')
    raise TestFail(f'a linha {indice} não apareceu')


def digitar_pin(rt, pin):
    """Each character on the key that has it now (GET /api/keypad: the cards are drawn anew after each touch)."""
    for ch in pin:
        kp = rt.keypad()
        faces = kp['faces']
        k = next((i for i, f in enumerate(faces) if ch in f), None)
        if k is None:
            raise TestFail(f'nenhuma tecla tem {ch!r}: {faces}')
        st0 = rt.b.estado()
        tocar(rt, PIN_TECLA, k)
        if kp['kb'] == 'groups':
            st = rt.b.esperar(lambda s: tem(s, PIN_POPUP), 5, 'popup do grupo')
            tocar(rt, PIN_POPUP, faces[k].replace(' ', '').index(ch))
        else:
            rt.b.esperar(mudou(st0), 5, 'ponto do PIN')


def entrar_config(rt, capturar=True):
    """Dashboard → gear → the test account → its PIN → Settings."""
    st = ir_ao_painel(rt)
    for _ in range(4):
        nav = st['painel']['nav']
        if NAV_CFG in nav:
            break
        if NAV_PAGINA not in nav:
            raise TestFail(f'nem engrenagem nem página na barra: {nav}')
        tocar(rt, PNL_NAV, nav.index(NAV_PAGINA))
        st = rt.b.esperar(mudou(st), 5, 'página da barra')
    tocar(rt, PNL_NAV, st['painel']['nav'].index(NAV_CFG))
    rt.passo('quem_esta_no_painel', M['AUTH_USER'], captura=capturar)
    com_pin = [u for u in sorted(rt.usuarios(), key=lambda u: u['id']) if u.get('pin')]
    pos = next(i for i, u in enumerate(com_pin) if u['name'] == CONTA)
    lista_ate(rt, pos, QUEM_LINHA, QUEM_ANT, QUEM_PROX)
    tocar(rt, QUEM_LINHA, pos)
    rt.passo('pin_vazio', M['AUTH'], captura=capturar)
    digitar_pin(rt, rt.pin)
    if capturar:
        rt.passo('pin_digitado', M['AUTH'])
    tocar(rt, PIN_ENTRAR)
    st = rt.b.esperar(lambda s: s['modo'] in (M['SETTINGS_MAIN'], M['AUTH']), 10, 'resposta do PIN')
    if st['modo'] == M['AUTH']:
        # A forced PIN change (the account came marked): define a new one, twice.
        novo = ''.join(random.choice('0123456789') for _ in range(8))
        rt.passo('pin_troca_forcada', M['AUTH'])
        for _ in range(2):
            digitar_pin(rt, novo)
            tocar(rt, PIN_ENTRAR)
            time.sleep(0.8)
        rt.pin = novo
        st = rt.b.estado()
        if st['modo'] == M['PANEL_MESSAGE']:
            tocar(rt, MSG_BOTAO)
    return rt.passo('configuracoes_p1', M['SETTINGS_MAIN'], captura=capturar)


def menu(rt, nome):
    pos = MENU.index(nome)
    lista_ate(rt, pos, LST_LINHA, LST_ANT, LST_PROX)
    tocar(rt, LST_LINHA, pos)


def voltar_menu(rt, tipo=1):
    tocar(rt, tipo)
    return rt.passo('configuracoes', M['SETTINGS_MAIN'], captura=False)


# ---------------------------------------------------------------------------
# The walk
# ---------------------------------------------------------------------------
def etapa_painel(rt):
    st = rt.passo('painel', M['DASHBOARD'])
    p0 = st['painel']
    tocar(rt, PNL_BAIXO)
    st = rt.passo('painel_minmax_baixo', M['DASHBOARD'], lambda s: s['painel']['minmax'][1] == 1)
    tocar(rt, PNL_GRAF_BAIXO)
    rt.passo('grafico', M['GRAPH_VIEW'], presente, timeout=20)
    st = rt.b.estado()
    # The chart opens on 1 h (EVT_OPEN_GRAPH param 0) and its lit chip is inert:
    # another period is the one that changes the screen.
    tocar(rt, GRF_PERIODO, 3)
    rt.passo('grafico_24h', M['GRAPH_VIEW'], mudou(st), timeout=20)
    st = rt.b.estado()
    tocar(rt, GRF_ANT)
    rt.passo('grafico_anterior', M['GRAPH_VIEW'], mudou(st), timeout=20)
    if tem(rt.b.estado(), GRF_SEG):
        st = rt.b.estado()
        tocar(rt, GRF_SEG)
        rt.passo('grafico_seguinte', M['GRAPH_VIEW'], mudou(st), timeout=20)
    tocar(rt, GRF_AREA)
    st = rt.passo('detalhe', M['GRAPH_DETAIL'])
    if tem(st, DET_PROX):
        tocar(rt, DET_PROX)
        rt.passo('detalhe_p2', M['GRAPH_DETAIL'], mudou(st))
    tocar(rt, DET_VOLTAR)
    rt.passo('grafico_de_volta', M['GRAPH_VIEW'], captura=False)
    tocar(rt, GRF_DIA)
    st = rt.passo('calendario', M['CALENDAR'], timeout=20)
    hoje = time.localtime().tm_mday
    passados = sorted(a[5] for a in st['alvos'] if a[4] == CAL_DIA and a[5] < hoje)
    if passados:
        # A day before today, then Today: the chart comes back to the present.
        tocar(rt, CAL_DIA, passados[-1])
        rt.passo('grafico_do_dia', M['GRAPH_VIEW'], no_passado, timeout=20)
        tocar(rt, GRF_DIA)
        rt.passo('calendario_de_novo', M['CALENDAR'], captura=False, timeout=20)
        tocar(rt, CAL_HOJE)
        rt.passo('grafico_hoje', M['GRAPH_VIEW'], presente, timeout=20)
        # That day again, out to the dashboard and the chart anew: the present too.
        # Nothing cleared the day's anchor before 2026-10-14 (image 9c3e0a25).
        tocar(rt, GRF_DIA)
        rt.passo('calendario_outra_vez', M['CALENDAR'], captura=False, timeout=20)
        tocar(rt, CAL_DIA, passados[-1])
        rt.passo('grafico_do_dia_outra_vez', M['GRAPH_VIEW'], no_passado, captura=False, timeout=20)
        tocar(rt, GRF_VOLTAR)
        st = rt.passo('painel_com_dia_escolhido', M['DASHBOARD'], captura=False)
        if not st['painel']['minmax'][1]:
            tocar(rt, PNL_BAIXO)
            rt.b.esperar(lambda s: s['modo'] == M['DASHBOARD'] and s['painel']['minmax'][1] == 1, 10, 'mín/máx de baixo')
        tocar(rt, PNL_GRAF_BAIXO)
        rt.passo('grafico_reaberto', M['GRAPH_VIEW'], presente, timeout=20)
    tocar(rt, GRF_VOLTAR)
    st = rt.passo('painel_de_volta', M['DASHBOARD'], captura=False)
    if st['painel']['minmax'][1]:
        tocar(rt, PNL_BAIXO)
    tocar(rt, PNL_CIMA)
    rt.passo('painel_minmax_cima', M['DASHBOARD'], lambda s: s['painel']['minmax'][0] == 1)
    tocar(rt, PNL_CIMA)
    st = rt.passo('painel_normal', M['DASHBOARD'], lambda s: s['painel']['minmax'] == [0, 0], captura=False)
    outro = next((i for i, s in enumerate(st['painel']['nav']) if s >= 0 and s != p0['sel']), None)
    if outro is not None:
        alvo = st['painel']['nav'][outro]
        tocar(rt, PNL_NAV, outro)
        st = rt.passo('painel_outro_sensor', M['DASHBOARD'], lambda s, a=alvo: s['painel']['sel'] == a)
        volta = st['painel']['nav'].index(p0['sel']) if p0['sel'] in st['painel']['nav'] else None
        if volta is not None:
            tocar(rt, PNL_NAV, volta)
            rt.passo('painel_sensor_original', M['DASHBOARD'], lambda s: s['painel']['sel'] == p0['sel'],
                     captura=False)


def etapa_configuracoes(rt):
    entrar_config(rt)
    for n in (2, 3):
        st = rt.b.estado()
        tocar(rt, LST_PROX)
        rt.passo(f'configuracoes_p{n}', M['SETTINGS_MAIN'], mudou(st))

    # Alarm limits, one sensor, its limits, its maintenance window.
    menu(rt, 'LIMITES')
    rt.passo('alarmes', M['SETTINGS_ALARMS'])
    tocar(rt, LST_LINHA, 0)
    rt.passo('sensor', M['SETTINGS_ALARM_SENSOR'])
    tocar(rt, SA_LIMITES)
    st = rt.passo('limites', M['SETTINGS_ALARM_EDIT'])
    tocar(rt, LIM_MAIS, 1)
    rt.passo('limites_mais', M['SETTINGS_ALARM_EDIT'], mudou(st))
    tocar(rt, LIM_CANCELAR)
    rt.passo('sensor_de_volta', M['SETTINGS_ALARM_SENSOR'], captura=False)
    tocar(rt, SA_MANUT)
    st = rt.passo('manutencao', M['SETTINGS_MAINT'])
    tocar(rt, MAN_INICIAR)
    rt.passo('sensor_em_manutencao', M['SETTINGS_ALARM_SENSOR'], timeout=10)
    tocar(rt, SA_MANUT)
    rt.passo('manutencao_aberta', M['SETTINGS_MAINT'])
    tocar(rt, MAN_ENCERRAR)
    rt.passo('sensor_sem_manutencao', M['SETTINGS_ALARM_SENSOR'], captura=False, timeout=10)
    # The alarms switch is not pressed: turning a sensor's alarms on fires a real
    # alarm when its reading is out of range or in error (on the rig, 2026-10-14,
    # GELADEIRA reads an error), with the buzzer and the telemetry record.
    tocar(rt, SA_VOLTAR)
    rt.passo('alarmes_de_volta', M['SETTINGS_ALARMS'], captura=False)
    voltar_menu(rt, LST_VOLTAR)

    # Sounds: three pages, a volume and back, a melody, the mute confirmation; nothing saved.
    menu(rt, 'SONS')
    st = rt.passo('sons_p1', M['SETTINGS_SOUNDS'])
    tocar(rt, SONS_PROX)
    st = rt.passo('sons_p2', M['SETTINGS_SOUNDS'], mudou(st))
    if tem(st, SONS_INTERRUPTOR, SOM_MUDO):
        tocar(rt, SONS_INTERRUPTOR, SOM_MUDO)
        rt.passo('mudo_confirmar', M['CONFIRM_MUTE_ALL'])
        tocar(rt, DLG_NAO)
        st = rt.passo('sons_de_volta', M['SETTINGS_SOUNDS'], captura=False)
    tocar(rt, SONS_PROX)
    st = rt.passo('sons_p3', M['SETTINGS_SOUNDS'], mudou(st))
    tocar(rt, SONS_MENOS, 0)
    st = rt.passo('sons_volume_menos', M['SETTINGS_SOUNDS'], mudou(st))
    tocar(rt, SONS_MAIS, 0)   # the preview reaches the speaker in RAM: put it back
    st = rt.passo('sons_volume_de_volta', M['SETTINGS_SOUNDS'], mudou(st), captura=False)
    tocar(rt, SONS_ANT)
    st = rt.b.esperar(mudou(st), 5, 'sons p2')
    tocar(rt, SONS_ANT)
    st = rt.b.esperar(mudou(st), 5, 'sons p1')
    linhas = [a[5] for a in st['alvos'] if a[4] == SONS_LINHA]
    if linhas:
        tocar(rt, SONS_LINHA, linhas[0])
        st = rt.passo('melodia', M['SETTINGS_SOUNDS'], lambda s: tem(s, MEL_USAR))
        tocar(rt, MEL_ITEM, 1)
        rt.passo('melodia_escolhida', M['SETTINGS_SOUNDS'], mudou(st))
        tocar(rt, MEL_CANCELAR)
        rt.passo('sons_sem_melodia', M['SETTINGS_SOUNDS'], lambda s: tem(s, SONS_SALVAR), captura=False)
    voltar_menu(rt, SONS_VOLTAR)

    # System status, four pages.
    menu(rt, 'STATUS')
    st = rt.passo('status_p1', M['SETTINGS_STATUS'])
    for n in (2, 3, 4):
        tocar(rt, LST_PROX)
        st = rt.passo(f'status_p{n}', M['SETTINGS_STATUS'], mudou(st))
    voltar_menu(rt, LST_VOLTAR)

    # Date and time: one step up, then back without saving.
    menu(rt, 'DATA_HORA')
    st = rt.passo('data_hora', M['SET_CLOCK'])
    tocar(rt, REL_MAIS, 0)
    rt.passo('data_hora_mais', M['SET_CLOCK'], mudou(st))
    voltar_menu(rt, REL_VOLTAR)

    # The account's own PIN, defined anew twice.
    menu(rt, 'PIN_PROPRIO')
    rt.passo('meu_pin', M['AUTH'])
    novo = ''.join(random.choice('0123456789') for _ in range(8))
    digitar_pin(rt, novo)
    tocar(rt, PIN_ENTRAR)
    rt.passo('meu_pin_confirmar', M['AUTH'], lambda s: True)
    digitar_pin(rt, novo)
    tocar(rt, PIN_ENTRAR)
    rt.passo('meu_pin_salvo', M['PANEL_MESSAGE'], timeout=10)
    rt.pin = novo
    voltar_menu(rt, MSG_BOTAO)

    # Accounts: one opened and left; a new one created by the panel and deleted by it.
    menu(rt, 'USUARIOS')
    st = rt.passo('usuarios', M['SETTINGS_USERS'])
    tocar(rt, USR_LINHA, [a[5] for a in st['alvos'] if a[4] == USR_LINHA][0])
    st = rt.passo('conta', M['SETTINGS_USER_EDIT'])
    tocar(rt, CTA_INTERRUPTOR, 0)
    rt.passo('conta_interruptor', M['SETTINGS_USER_EDIT'], mudou(st))
    tocar(rt, CTA_CANCELAR)
    rt.passo('usuarios_de_volta', M['SETTINGS_USERS'], captura=False)
    tocar(rt, USR_NOVA)
    rt.passo('nova_conta_nome', M['SETTINGS_PASSWORD'])
    for ch in 'ui':
        tocar(rt, TXT_TECLA, CAMADAS[0][0].index(ch))
    st = rt.b.estado()
    tocar(rt, TXT_CAMADA, 2)
    st = rt.b.esperar(mudou(st), 5, 'camada 123')
    tocar(rt, TXT_TECLA, CAMADAS[2][0].index('2'))
    rt.passo('nova_conta_digitada', M['SETTINGS_PASSWORD'], mudou(st))
    tocar(rt, TXT_SALVAR)
    rt.passo('nova_conta_permissoes', M['SETTINGS_USER_EDIT'])
    tocar(rt, CTA_SALVAR)
    rt.passo('nova_conta_pin', M['AUTH'])
    pin2 = ''.join(random.choice('0123456789') for _ in range(8))
    for _ in range(2):
        digitar_pin(rt, pin2)
        tocar(rt, PIN_ENTRAR)
        time.sleep(0.8)
    rt.passo('nova_conta_salva', M['PANEL_MESSAGE'], timeout=10)
    tocar(rt, MSG_BOTAO)
    st = rt.passo('usuarios_com_ui2', M['SETTINGS_USERS'])
    ui2 = next(u for u in rt.usuarios() if u['name'] == 'ui2')
    contas = [u for u in sorted(rt.usuarios(), key=lambda u: u['id']) if u['id'] != 0]
    pos = next(i for i, u in enumerate(contas) if u['id'] == ui2['id'])
    lista_ate(rt, pos, USR_LINHA, USR_ANT, USR_PROX)
    tocar(rt, USR_LINHA, pos)
    rt.passo('ui2', M['SETTINGS_USER_EDIT'], captura=False)
    tocar(rt, CTA_EXCLUIR)
    rt.passo('excluir_ui2', M['SETTINGS_USER_CONFIRM_DEL'])
    tocar(rt, DLG_SIM)
    rt.passo('ui2_excluida', M['PANEL_MESSAGE'], timeout=10)
    tocar(rt, MSG_BOTAO)
    rt.passo('usuarios_sem_ui2', M['SETTINGS_USERS'], captura=False)
    voltar_menu(rt, USR_VOLTAR)

    # PIN policy: changed on screen, never saved (it would mark every account).
    menu(rt, 'SEGURANCA_PIN')
    st = rt.passo('seguranca_do_pin', M['SETTINGS_PIN_POLICY'])
    tocar(rt, POL_MAIS)
    rt.passo('seguranca_do_pin_mais', M['SETTINGS_PIN_POLICY'], mudou(st))
    voltar_menu(rt, POL_CANCELAR)

    # Language, licence, display alignment (back restores the saved offset).
    menu(rt, 'IDIOMA')
    rt.passo('idioma', M['SETTINGS_LANG'])
    voltar_menu(rt, LST_VOLTAR)
    menu(rt, 'LICENCA')
    st = rt.passo('licenca_p1', M['SETTINGS_LICENSE'])
    tocar(rt, LST_PROX)
    rt.passo('licenca_p2', M['SETTINGS_LICENSE'], mudou(st))
    voltar_menu(rt, LST_VOLTAR)
    menu(rt, 'ALINHAR')
    st = rt.passo('alinhar', M['SETTINGS_DISPLAY_OFFSET'])
    tocar(rt, ALI_DIR)
    rt.passo('alinhar_direita', M['SETTINGS_DISPLAY_OFFSET'], mudou(st))
    voltar_menu(rt, ALI_VOLTAR)

    # Theme, applied by the panel: the walk's own theme, so nothing changes but the path.
    menu(rt, 'TEMA')
    rt.passo('tema', M['SETTINGS_THEMES'])
    tocar(rt, LST_LINHA, {'claro': 0, 'escuro': 1}[rt.tema])
    rt.passo('tema_aplicado', M['DASHBOARD'], lambda s: s['tema'] == rt.tema, timeout=15)


def etapa_sensibilidade(rt):
    """The touch sensitivity: injected touches carry no pressure, so it is only seen and cancelled.
    The screen has no idle return of its own while a press is down; Cancel takes any touch."""
    entrar_config(rt, capturar=False)
    menu(rt, 'CALIBRAR')
    rt.passo('sensibilidade', M['SETTINGS_TOUCH_SENS'])
    time.sleep(0.5)   # the menu's tap is still down for its 100 ms hold: let it go first
    tocar(rt, 1)
    rt.passo('sensibilidade_cancelada', M['SETTINGS_MAIN'], captura=False)
    ir_ao_painel(rt)


ETAPAS = [etapa_painel, etapa_configuracoes, etapa_sensibilidade]

if __name__ == '__main__':
    sys.exit(main())
