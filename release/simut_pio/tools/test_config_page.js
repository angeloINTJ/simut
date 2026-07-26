/* Exercises the shipped /config loader against the three failure paths.
   Stubs just enough DOM for the script to define and run its functions. */
const fs = require('fs');
const vm = require('vm');

const SRC = fs.readFileSync(__dirname + '/cfg_new.js', 'utf8');

function makeEl(id) {
  return {
    id, value: '', checked: false, textContent: '', disabled: false,
    style: {}, classList: { add(){}, remove(){}, toggle(){} },
    querySelectorAll: () => [],
    addEventListener(){}, appendChild(){}, insertAdjacentHTML(){},
    getAttribute: () => null, setAttribute(){}, removeAttribute(){},
    children: [], innerHTML: '',
  };
}

function buildSandbox(fetchImpl) {
  const els = {};
  const formControls = ['name','tz','log','ntp_enabled','man_date','man_time','res','s_int',
    'h_int','t_transport','t_sec','t_srv','t_port','t_path','t_key','m_topic','m_cid',
    'm_user','m_qos','m_retain','m_ka','t_int','t_bat','t_mode','t_glob','t_line','t_sep'];
  const get = id => (els[id] || (els[id] = makeEl(id)));

  const form = makeEl('sysForm');
  form.querySelectorAll = () => formControls.map(get);
  els['sysForm'] = form;

  const doc = {
    getElementById: id => get(id),
    querySelectorAll: () => [],
    querySelector: () => null,
    createElement: () => makeEl('tmp'),
    addEventListener(){},
    body: makeEl('body'),
    documentElement: makeEl('html'),
  };

  const win = {
    t: (k, fb) => fb,
    showToast(){}, escHtml: s => String(s),
    fetchSafe: fetchImpl,
    location: { pathname: '/config', href: '/config' },
    addEventListener(){}, setTimeout, clearTimeout, console,
  };
  win.window = win;

  /* Real Pending lives in /lang.js (window.Pending); the config page only
     uses getSection/set/refreshUI. Empty section = "nothing staged". */
  win.Pending = {
    getSection: () => ({}), set(){}, refreshUI(){}, init(){}, clear(){},
  };

  const sandbox = {
    document: doc, window: win, console, setTimeout, clearTimeout,
    Pending: win.Pending,
    fetch: fetchImpl, fetchSafe: fetchImpl,
    sessionStorage: { _d:{}, getItem(k){return this._d[k]||null;}, setItem(k,v){this._d[k]=v;}, removeItem(k){delete this._d[k];} },
    localStorage: { getItem: () => null, setItem(){}, removeItem(){} },
    location: win.location, navigator: { language: 'en' }, Date, Math, JSON, String, Number,
    Array, Object, Promise, AbortController: class { constructor(){ this.signal = {}; } abort(){} },
  };
  sandbox.globalThis = sandbox;
  return { sandbox, els, get };
}

function run(name, fetchImpl, checks) {
  const { sandbox, get } = buildSandbox(fetchImpl);
  const ctx = vm.createContext(sandbox);
  try {
    vm.runInContext(SRC, ctx, { timeout: 5000 });
  } catch (e) {
    console.log(`  [${name}] script load threw: ${e.message}`);
  }
  const loadConfig = ctx.loadConfig || (ctx.window && ctx.window.loadConfig);
  if (typeof loadConfig !== 'function') {
    console.log(`  [${name}] FAIL: loadConfig not defined`);
    return false;
  }
  return loadConfig().then(() => checks(get)).then(ok => {
    console.log(`  [${name}] ${ok ? 'PASS' : 'FAIL'}`);
    return ok;
  }).catch(e => { console.log(`  [${name}] FAIL (threw): ${e.message}`); return false; });
}

const GOOD = {
  name: 'simut-bench', tz: -3, log: true, ntp_enabled: true, now_epoch: 1784847000,
  res: 12, s_int: 5000, h_int: 1, t_transport: 0, t_sec: false, t_srv: 'srv.example',
  t_port: 8080, t_path: '/api/v1/telemetry', t_key: 'abcd***', m_topic: 'simut/t',
  m_cid: 'cid', m_user: 'u', m_qos: 1, m_retain: false, m_ka: 60, t_int: 300000,
  t_bat: 10, t_mode: 0, t_glob: '', t_line: '', t_sep: '', serial: 'E664',
};

const resp = (status, bodyFn) => Promise.resolve({
  ok: status >= 200 && status < 300, status,
  json: bodyFn,
});

console.log('Cenarios do /config:');

const scenarios = [
  ['200 OK -> campos preenchidos e form habilitado',
    () => resp(200, () => Promise.resolve(GOOD)),
    get => get('name').value === 'simut-bench'
        && get('t_srv').value === 'srv.example'
        && get('cfg_load_err').style.display === 'none'
        && get('name').disabled === false],

  ['403 -> banner de permissao, form desabilitado',
    () => resp(403, () => Promise.resolve({ error: 'Forbidden' })),
    get => get('cfg_load_err').style.display === ''
        && get('name').disabled === true
        && /permission/i.test(get('cfg_load_err_msg').textContent)],

  ['JSON truncado 3x -> banner de falha, form desabilitado',
    () => resp(200, () => Promise.reject(new SyntaxError('Unexpected end of JSON input'))),
    get => get('cfg_load_err').style.display === ''
        && get('name').disabled === true
        && /Could not load/i.test(get('cfg_load_err_msg').textContent)],

  ['503 depois 200 -> recupera sozinho',
    (() => { let n = 0; return () => (n++ === 0)
        ? resp(503, () => Promise.reject(new Error('busy')))
        : resp(200, () => Promise.resolve(GOOD)); })(),
    get => get('name').value === 'simut-bench'
        && get('cfg_load_err').style.display === 'none'
        && get('name').disabled === false],
];

(async () => {
  let all = true;
  for (const [name, f, check] of scenarios) all = (await run(name, f, check)) && all;
  console.log(all ? '\nTODOS OS CENARIOS PASSARAM' : '\nHOUVE FALHA');
  process.exit(all ? 0 : 1);
})();
