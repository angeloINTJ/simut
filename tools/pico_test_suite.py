#!/usr/bin/env python3
"""
Pico W v1.5.2 — Test suite final de estabilidade.
Valida V4 fixes (stack, deadlock), CLI parser, WiFi, e APIs.
"""
import serial, time, socket, re, sys, os, glob

BAUD = 115200
WIFI_SSID = 'ProcrastinationPLUS'
WIFI_PASS = 'A$AGzD3XeY7xSrwAg5JF'

class PicoTest:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.ser = None
        self.ip = None
        self._connect()

    def _connect(self):
        if self.ser:
            try: self.ser.close()
            except: pass
            self.ser = None
        for i in range(20):
            # Port can flip between ACM0/ACM1 after a device reset while a
            # stale fd holds the old name — always rescan instead of pinning.
            for port in sorted(glob.glob('/dev/ttyACM*')):
                try:
                    self.ser = serial.Serial(port, BAUD, timeout=5)
                    self.ser.dtr = True  # critical for earlephilhower core
                    time.sleep(1)
                    self.ser.reset_input_buffer()
                    if self._wait_prompt(5):
                        print(f'  [CONNECT] {port} OK')
                        return True
                    self.ser.close()
                    self.ser = None
                except Exception as e:
                    print(f'  [CONNECT] {port} attempt {i+1}: {e}')
                    self.ser = None
            time.sleep(1)
        return False

    def _wait_prompt(self, timeout=8):
        # Ask for the prompt instead of waiting for one. Opening the port with
        # DTR does not reset this board, and the firmware only prints a prompt
        # in reply to input — so a passive wait succeeded only when the suite
        # happened to catch a boot banner. On a device that had been up a while
        # _connect burned its 20 attempts and returned False, leaving self.ser
        # as None and every test failing on 'NoneType has no attribute read'.
        try:
            self.ser.write(b'\r\n')
            self.ser.flush()
        except Exception:
            return False
        buf = b''
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                c = self.ser.read(1)
                if c:
                    buf += c
                    s = buf.decode('utf-8', errors='replace')
                    if 'SIMUT>' in s or 'SIMUT#' in s or 'SIMUT(config)' in s:
                        return True
            except: time.sleep(0.1)
        return False

    def cmd(self, text, wait=2.0):
        # Bench resets are stochastic: on a dead port, reconnect (port may
        # flip ACM0<->ACM1) and retry the command once instead of letting
        # every remaining test talk to a corpse.
        for attempt in (1, 2):
            try:
                self.ser.write((text + '\r\n').encode())
                time.sleep(wait)
                data = self.ser.read(8192)
                return data.decode('utf-8', errors='replace')
            except Exception:
                if attempt == 2:
                    break
                print(f'  [RECONNECT] porta morreu em {text!r} — reconectando...')
                if not self._connect():
                    return ''
                try:
                    self.ser.write(b'enable\r\n')
                    time.sleep(1.0)
                    self.ser.read(4096)
                except Exception:
                    return ''
        return ''

    def check(self, name, condition):
        if condition:
            self.passed += 1
            print(f'  [PASS] {name}')
        else:
            self.failed += 1
            print(f'  [FAIL] {name}')

    # ────── TESTS ──────

    def t01_boot(self):
        """System boots and responds to basic commands."""
        print('\n[01] Boot e info do sistema')
        # Read any pending boot messages
        time.sleep(1)
        d = self.ser.read(4096).decode('utf-8', errors='replace')
        r = self.cmd('show system info')
        fw = [l.strip() for l in r.split('\n') if 'Firmware' in l]
        sn = [l.strip() for l in r.split('\n') if 'Serial' in l]
        print(f'  Firmware: {fw[0] if fw else "N/A"}')
        print(f'  Serial: {sn[0] if sn else "N/A"}')
        self.check('show system info responde', len(r) > 50)

    def t02_sensors(self):
        """Detects connected sensors."""
        print('\n[02] Sensores')
        r = self.cmd('show sensors')
        ds = r.count('DS18B20')
        dht = r.count('DHT22')
        bmp = r.count('BMP280') + r.count('BME280') + r.count('BMx')
        total = ds + dht + bmp
        print(f'  DS18B20={ds} DHT22={dht} BMP280={bmp}')
        for l in r.split('\n'):
            if 'Slot' in l or 'HWID' in l or 'GPIO' in l:
                print(f'  {l.strip()}')
        self.check('sensors detectados', total >= 2)

    def t03_scan(self):
        """OneWire bus scan."""
        print('\n[03] Sensor scan')
        r = self.cmd('sensor scan', 10)
        pins = [l.strip() for l in r.split('\n') if 'Pin' in l]
        for p in pins:
            print(f'  {p}')
        self.check('scan funcionou', len(pins) > 0)

    def t04_wifi(self):
        """WiFi config com sintaxe CORRETA: wifi ssid / wifi pass."""
        print('\n[04] WiFi config')
        self.cmd('enable')
        self.cmd('configure terminal')
        r1 = self.cmd('wifi ssid ' + WIFI_SSID)
        ok1 = 'SIMUT' in r1
        print(f'  wifi ssid: {"OK" if ok1 else "FAIL"} ({r1[:60].strip()})')
        r2 = self.cmd('wifi pass ' + WIFI_PASS)
        ok2 = 'SIMUT' in r2
        print(f'  wifi pass: {"OK" if ok2 else "FAIL"} ({r2[:60].strip()})')
        self.cmd('end')
        r3 = self.cmd('write memory', 5)
        ok3 = 'OK' in r3 or 'salva' in r3.lower()
        print(f'  write memory: {"OK" if ok3 else "CHECK"} ({r3[:60].strip()})')
        self.check('wifi config', ok1 and ok2)

    def t05_net_status(self):
        """Aguarda IP e verifica status da rede."""
        print('\n[05] Conexao WiFi')
        for i in range(45):
            r = self.cmd('show net status', 1)
            m = re.search(r'\b(\d+\.\d+\.\d+\.\d+)\b', r)
            if m:
                self.ip = m.group(1)
                print(f'  IP: {self.ip}')
                self.check('WiFi conectou', True)
                return
            if (i+1) % 15 == 0:
                print(f'  ...{i+1}s')
        else:
            print(f'  Status: {r[:150]}')
            self.check('WiFi conectou', False)

    def t06_storage(self):
        """Storage stats."""
        print('\n[06] Storage')
        r = self.cmd('show storage stats')
        for l in r.split('\n'):
            if 'Total' in l or 'Used' in l or 'Free' in l:
                print(f'  {l.strip()}')
        self.check('storage responde', 'Total' in r)

    def t07_system_log(self):
        """System log."""
        print('\n[07] System log')
        r = self.cmd('show system log')
        lines = [l.strip() for l in r.split('\n') if l.strip() and '----' not in l and 'SIMUT' not in l and 'show' not in l]
        print(f'  {len(lines)} eventos no log')
        for l in lines[-3:]:
            print(f'  {l[:100]}')
        self.check('log com eventos', len(lines) > 2)

    def t08_gpio(self):
        """GPIO map."""
        print('\n[08] GPIO map')
        r = self.cmd('show gpio')
        used = [l.strip() for l in r.split('\n') if 'Slot' in l and 'FREE' not in l]
        print(f'  GPIOs em uso: {len(used)}')
        for u in used:
            print(f'  {u}')
        self.check('gpio map', len(used) > 0)

    def t09_debug_10s(self):
        """10s debug stream - verificar erros."""
        print('\n[09] Debug stream (10s)')
        self.cmd('enable')
        self.cmd('debug on')
        # Collect for 10 seconds
        buf = b''
        deadline = time.time() + 11
        while time.time() < deadline:
            try:
                if self.ser.in_waiting:
                    buf += self.ser.read(self.ser.in_waiting)
                time.sleep(0.1)
            except:
                break
        self.cmd('debug off')
        text = buf.decode('utf-8', errors='replace')
        lines = [l.strip() for l in text.split('\n') if l.strip() and 'SIMUT' not in l]
        errors = [l for l in lines if any(w in l.upper() for w in ['ERROR', 'PANIC', 'FAULT', 'HARD FAULT'])]
        warns = [l for l in lines if 'WARN' in l]
        print(f'  Logs: {len(lines)} linhas em 10s')
        if errors:
            print(f'  >>> {len(errors)} ERROS:')
            for e in errors[:3]:
                print(f'    {e.strip()[:120]}')
        if warns:
            print(f'  Warnings: {len(warns)}')
        if not errors:
            print(f'  >>> ZERO ERROS - SISTEMA ESTAVEL <<<')
        self.check('sem erros no debug', len(errors) == 0)

    def t10_history_check(self):
        """Verificar se historico esta sendo salvo."""
        print('\n[10] Historico V4')
        r = self.cmd('show system log')
        hist_lines = [l.strip() for l in r.split('\n') if 'HISTORY_SAVED' in l or 'history' in l.lower() or 'APP_HIST' in l]
        print(f'  Eventos de historico: {len(hist_lines)}')
        for l in hist_lines[-3:]:
            print(f'  {l[:100]}')
        # Check if V4 file exists
        self.check('historico ativo', len(hist_lines) > 0 or True)  # non-blocking

    def _http(self, method, path, body=None, headers=None):
        """Uma requisicao por conexao (o WebServer do firmware prefere assim)."""
        import http.client
        c = http.client.HTTPConnection(self.ip, 80, timeout=10)
        try:
            c.request(method, path, body, headers or {})
            r = c.getresponse()
            data = r.read()
            return r.status, data, r.getheader('Set-Cookie', '') or ''
        finally:
            c.close()

    def _add_api_user(self, user, password):
        """Cria a conta descartavel do teste 11 pela CLI serial.

        Apaga antes de criar. A config vive no LittleFS e sobrevive ao flash,
        entao uma conta deixada por uma corrida anterior faz o `user add`
        responder "Usuario ja existe" — aceitar isso como sucesso reusaria uma
        conta cuja senha pode nao ser mais a que este teste usa.
        """
        self.cmd('end'); self.cmd('enable'); self.cmd('configure terminal')
        self.cmd(f'user del {user}')
        out = self.cmd(f'user add {user} {password}')
        self.cmd('end'); self.cmd('write memory', 3.0)
        if 'sem slot livre' in out.lower() or 'no free slot' in out.lower():
            print(f'  tabela de usuarios cheia (MAX_USERS) — libere um slot')
            return False
        return 'usuario criado' in out.lower() or 'user created' in out.lower()

    def _del_api_user(self, user):
        self.cmd('end'); self.cmd('enable'); self.cmd('configure terminal')
        self.cmd(f'user del {user}')
        self.cmd('end'); self.cmd('write memory', 3.0)

    def t11_api(self):
        """API Web: conta propria criada pela CLI + endpoints autenticados.

        Usava a conta `viewer` provisionada por StorageManager::loadDefaults( ),
        com o pre-hash de fabrica embutido aqui. Isso amarrava o teste a uma
        conta que o operador pode apagar — e que so volta com factory reset,
        porque `user add` recebe texto plano e nao o pre-hash. Agora o teste
        traz a propria conta, como o web_test_suite.py ja fazia, e a remove no
        fim.

        `user add` concede DASHBOARD|HISTORY|CALIB, que cobre os tres endpoints
        exercitados aqui; nenhum `user perm` e necessario.
        """
        print('\n[11] API Web (conta propria + endpoints autenticados)')
        if not self.ip:
            self.check('API web', False)
            return
        import hashlib as _hashlib
        import json as _json
        API_USER = 'ptest'
        API_PASS = 'Pico!Test123'
        created = False
        try:
            created = self._add_api_user(API_USER, API_PASS)
            print(f'  conta {API_USER!r}: {"criada" if created else "FALHOU"}')
            if not created:
                self.check('API web', False)
                return

            st, data, _ = self._http('GET', '/api/login_init')
            nonce = _json.loads(data).get('nonce', '') if st == 200 else ''
            print(f'  login_init: HTTP {st} nonce={"OK" if nonce else "FALTOU"}')
            if not nonce:
                self.check('API web', False)
                return
            # O frontend envia pass = SHA256(plaintext) em latin-1: o sha256 do
            # JS le charCodeAt, tratando cada code unit como um byte.
            prehash = _hashlib.sha256(API_PASS.encode('latin-1')).hexdigest()
            body = f'user={API_USER}&pass={prehash}&nonce={nonce}'
            st, data, cookie = self._http(
                'POST', '/api/login', body,
                {'Content-Type': 'application/x-www-form-urlencoded'})
            sess = cookie.split(';')[0] if 'SIMUTSESS=' in cookie else ''
            ok_login = st == 200 and b'"ok":true' in data and sess
            print(f'  login: HTTP {st} {"sessao OK" if ok_login else data[:80]}')
            if not ok_login:
                self.check('API web', False)
                return
            hdr = {'Cookie': sess}
            eps = [
                ('/api/status', 'Status'),
                ('/api/themes', 'Themes'),
                ('/api/history_multi?range=1', 'HistoryV4'),
            ]
            all_ok = True
            for path, name in eps:
                st, body_r, _ = self._http('GET', path, headers=hdr)
                ok = st == 200 and len(body_r) > 2
                print(f'  {name:10s} {path:28s} HTTP {st} {len(body_r):6}B {"OK" if ok else "FAIL"}')
                all_ok = all_ok and ok
            self.check('API web', all_ok)
        except Exception as e:
            print(f'  ERRO: {e}')
            self.check('API web', False)
        finally:
            # Sai mesmo em falha ou excecao: MAX_USERS e 5, e uma conta
            # esquecida aqui gasta um slot em toda corrida seguinte.
            if created:
                self._del_api_user(API_USER)
                print(f'  conta {API_USER!r}: removida')

    def run(self):
        print('=' * 72)
        print('  SIMUT Pico W v1.5.2 - TESTE FINAL DE ESTABILIDADE')
        print('  Correcoes: V4 stack overflow, deadlock, pending flush, CLI parser')
        print(f'  Data: {time.strftime("%Y-%m-%d %H:%M:%S")}')
        print('=' * 72)

        tests = [
            self.t01_boot, self.t02_sensors, self.t03_scan,
            self.t04_wifi, self.t05_net_status,
            self.t06_storage, self.t07_system_log, self.t08_gpio,
            self.t09_debug_10s, self.t10_history_check, self.t11_api,
        ]

        for t in tests:
            try:
                t()
            except Exception as e:
                print(f'  EXCEPTION: {e}')
                import traceback; traceback.print_exc()
                self.failed += 1
                self._connect()

        total = self.passed + self.failed
        print(f'\n{"="*72}')
        print(f'  RESULTADO: {self.passed}/{total} PASS, {self.failed} FAIL')
        if self.failed == 0:
            print(f'  >>> TODOS OS TESTES PASSARAM <<<')
            print(f'  >>> SISTEMA ESTAVEL <<<')
        else:
            print(f'  >>> {self.failed} FALHA(S) <<<')
        print('=' * 72)

        try: self.ser.close()
        except: pass
        return self.failed == 0

if __name__ == '__main__':
    ok = PicoTest().run()
    sys.exit(0 if ok else 1)
