# Plano de correção — Auditoria de segurança de 07/09/2026

Base: `feature/simut-air` @ `694ef65` (main `d64bacd`, firmware `2.3.9-beta`).
Relatório: artefato "Auditoria SIMUT 07/09/2026" (8 achados V-01..V-08, 21 regressões
conferidas, 0 reabertas). Este plano cobre **todos** os achados e as três observações
que merecem ação (O-1..O-3). Nada aqui foi executado ainda.

Regras do projeto que o plano obedece:

- Toda mudança em handler web, CLI, upload ou persistência passa pelas 6 falhas de
  `docs/diretrizes_seguranca_vibecoding.md` antes de fechar.
- Portões de CI continuam verdes: `scan_secrets`, `check_authz`, `check_fsguard`,
  `gen_logcodes --check`, testes nativos, fuzz, cppcheck. Portão novo só com controle
  positivo (provar que ele FALHA no caso que deve pegar).
- Flash é escasso: folga em 07/09 ≈ 15,1 KB (release), 13,1 KB (alpha), ~24 KB (Air).
  Medir por **símbolo** antes e depois de cada fase; o `-A` engana por alinhamento de 4096.
- Toda correção de firmware é validada no ferro com A/B (firmware anterior × novo) e
  controle positivo, antes de ser declarada fechada.
- `git add` por caminho (a árvore tem trabalho de KiCad do Ângelo em `PCB_test/`).
- Commits em inglês; `SECURITY.md` revisado na mesma PR de qualquer mudança de segurança.

---

## 0. Ordem e fases

| Fase | Itens | Imagens | Firmware? | Bancada? | Decisão pendente | Esforço |
|---|---|---|---|---|---|---|
| 0 — Sem firmware (primeiro) | V-02, V-08, V-07a | — | não | não | D-2 (expurgo, recomendado NÃO) | 2–3 h + rotação |
| 1 — Rádio e energia | V-01a, V-03, V-06 | alpha, Air | sim | sim (BT + suíte Air) | — | ~1 dia |
| 2 — Serialização e validação | V-04, O-2, O-1 | todas | sim | sim (script de casos) | D-5 (O-1) | 4–6 h |
| 3 — Desenho | V-05, V-01b, D-6 | todas / alpha+Air | sim | sim | D-1, D-3, D-4, D-6 | 4–8 h |
| 4 — Docs e release | V-07, IMPLEMENTACAO, CHANGELOGs, release | — | não | 3 OTAs | D-8 (número da versão) | 4–6 h |

Dependências: Fase 0 antes de tudo (o CI da Fase 8 passa a cobrir alpha/Air, que é onde
as Fases 1–3 mexem). Fase 1 antes de publicar qualquer alpha nova ou Air em campo.
Fase 3 depende das decisões da seção 6.

---

## 1. Fase 0 — sem firmware

### V-02 · Senha do rig no repositório e ponto cego do scanner

**Ordem obrigatória: rotacionar antes de limpar.** Limpar primeiro e rotacionar depois
deixa uma janela em que o valor vazado ainda abre o rig.

1. **Rotacionar a senha do rig** (Ângelo), para um valor sem relação com o vazado:
   `conf user pass admin <nova>` + `write memory` na serial, ou pela página `/users`.
   Guardar a nova em `~/.simut-bench.env` (`chmod 600`), fora do repositório:
   ```bash
   export SIMUT_WEB_USER=admin
   export SIMUT_WEB_PASS='<nova>'
   ```
   e `source ~/.simut-bench.env` antes de qualquer script de bancada (a suíte Air já
   exige essas variáveis — `AGENTS.md`).
2. **Tirar os quatro literais** (todos passam a ler do ambiente; falhar com mensagem
   clara quando faltar, nunca com um default):
   - `tools/rig_validate_history_clock.py:45` → `ADMIN = ("admin", os.environ["SIMUT_WEB_PASS"])`
   - `tools/telemetry_bench/verify_huge1mb.py:18` e `verify_mq_rst.py:17` → remover o
     `setdefault`; `os.environ["SIMUT_WEB_PASS"]` com `KeyError` explicado.
   - `tools/air_test_suite.py:1638` → vetor de teste neutro
     (`sha256_frontend('Str1ngDeTeste')`); o teste só confere latin-1 × UTF-8.
3. **Fechar o scanner** (`tools/scan_secrets.sh`, depois da etapa 3):
   ```bash
   # 3b. credenciais posicionais: setdefault('X_PASS','v'), ("admin","v"), PASS = "v"
   hits2=$(git grep -I -n -E \
     "(PASS|PASSWORD|SENHA|SECRET|TOKEN|API_?KEY)['\"][[:space:]]*,[[:space:]]*['\"][A-Za-z0-9][^'\"]{5,}['\"]|\(\"admin\"[[:space:]]*,[[:space:]]*\"[^\"]{5,}\"\)" -- . || true)
   # filtrar pela allowlist igual à etapa 3; somar a $hits
   # 3c. valores reais da bancada, em arquivo PRIVADO (nunca no repo)
   DENY="${SIMUT_SECRET_DENYLIST:-$HOME/.simut-secrets-deny}"
   if [ -f "$DENY" ]; then
     while IFS= read -r v; do
       [ -z "$v" ] && continue
       if git grep -I -n -F -- "$v" -- . ; then echo "SECRET GATE — bench value in tracked file"; fail=1; fi
     done < "$DENY"
   fi
   ```
   Pôr no `~/.simut-secrets-deny` a senha antiga E a nova, o SSID doméstico e a senha do
   Wi-Fi. No CI o arquivo não existe e a etapa é pulada (correto: só o desenvolvedor
   conhece os valores).
4. **`.gitignore`**: acrescentar `*.pem`, `*.key`, `*.p12`, `*.pfx`, `*.jks`, `.env`,
   `*.env` (hoje só `*.bkp`; a etapa 1 do scanner já barra o tipo, isto é a segunda camada).
5. **Higiene de docs**: `docs/MANUAL.pt-BR.html:842` e
   `docs/analysis/SIMUT_AIR_PLANO_FIX.md:750` → `<SSID da bancada>`. O `.md` do manual
   não tem o valor; o `.html` rastreado é que tem — editar direto.
6. **Controle positivo, nesta ordem**: (a) com os literais AINDA no lugar, rodar o
   scanner novo → **tem de acusar as 4 linhas**; (b) aplicar os passos 2 e 5 → `clean`;
   (c) `git grep -c '<valor antigo>'` = 0 em arquivos rastreados; (d) rig aceita a senha
   nova e a suíte roda com o ambiente.
7. **Expurgo do histórico** (D-2): opcional. A senha antiga ficou em 5 commits; após a
   rotação o valor é inerte. O expurgo reescreve 800+ commits, invalida todo SHA citado
   em docs, CHANGELOG e memórias, e não alcança `refs/pull` do GitHub (lição de 16/08).
   Recomendação: **não expurgar**; registrar a rotação em `IMPLEMENTACAO`.

Commit sugerido: `chore(security): bench credentials come from the environment; widen the secret gate`.
Aceite: scanner acusa antes e limpa depois; `tools/scan_secrets.sh` no CI verde;
`grep` do valor antigo vazio; rotação registrada.

### V-08 · CI sem alpha e Air

`.github/workflows/build.yml`:

```yaml
on:
  push:
    branches: [main, 'feature/**']
  pull_request:            # todas as PRs, não só as para main
...
      - name: Build firmware
        run: pio run -e pico_w_release -e pico_w_alpha -e pico_w_air
      - name: Air consistency gate
        run: python3 tools/check_air_consistency.py
      - name: Run unit tests (AirConfig)
        run: pio test -e native_air
      - name: Run unit tests (AlarmQueue)
        run: pio test -e native_alarmqueue
```

O passo "Apply framework overrides" (`build.yml:66`) já precede o build e vale para os
três envs. Custo: ~2× o tempo do job. Aceite: uma execução verde numa PR aberta a
partir de `feature/simut-air`; `check_air_consistency.py` já está C1–C8 limpo.

Commit: `ci: build the alpha and Air images and run their native tests`.

### V-07a · Verdade imediata no SECURITY.md

Correção mínima agora (a revisão completa é a Fase 4), porque o arquivo é público e
afirma o contrário do que a imagem alpha publicada faz:

- `SECURITY.md:12-13`, `:111-116`, `:389-392`: o Bluetooth SPP está **compilado nas
  imagens alpha e Air**; autenticação = **senha do admin web** (não PIN do display);
  "sem limite de tentativas até a correção V-01" (frase temporária, removida na Fase 4).
- `SECURITY.md:26`: versão suportada = a tag mais recente publicada (hoje `v2.3.9-beta`).

Commit: `docs(security): describe the Bluetooth surface of the alpha and Air images as built`.

---

## 2. Fase 1 — rádio e energia (alpha + Air)

### V-01a · Lockout no CLI Bluetooth, recuperações só por USB, política de senha na CLI

**A. Lockout exponencial** (`src/BluetoothManager.{h,cpp}`), espelhando a web
(`(1 << failCount) × 1 s`, teto 300 s):

```cpp
// SystemDefs_Network.h, ao lado de BT_AUTH_BUFFER_MAX
constexpr uint8_t  BT_FAIL_CAP        = 12;
constexpr uint32_t BT_LOCKOUT_MAX_MS  = 300000;
inline uint32_t btLockoutMs(uint8_t failCount) {           // puro: testável no nativo
    uint32_t ms = 1000UL << (failCount > BT_FAIL_CAP ? BT_FAIL_CAP : failCount);
    return ms > BT_LOCKOUT_MAX_MS ? BT_LOCKOUT_MAX_MS : ms;
}
// BluetoothManager.h
uint8_t  _failCount = 0;  uint32_t _lockedUntil = 0;  bool _lockNoticeSent = false;
// BluetoothManager::update(), antes do laço de leitura (ramo não autenticado)
if (_lockedUntil && (int32_t)(millis() - _lockedUntil) < 0) {
    if (!_lockNoticeSent) { SerialBT.println(pt ? "Bloqueado. Tente mais tarde." : "Locked. Try later."); _lockNoticeSent = true; }
    while (SerialBT.available()) SerialBT.read();           // drena e ignora
    return;
}
// ramo de falha
_failCount = (_failCount < BT_FAIL_CAP) ? _failCount + 1 : BT_FAIL_CAP;
_lockedUntil = millis() + btLockoutMs(_failCount);  _lockNoticeSent = false;
LOG_CODE(LOG_WARN, "SEC", SEC_BT_LOCKOUT, (int)(btLockoutMs(_failCount) / 1000), TRL("BT lockout"));
// sucesso: _failCount = 0; _lockedUntil = 0;
```

O estado vive na RAM e sobrevive a desconectar/reconectar o RFCOMM (é isso que fecha o
laço do atacante). Código de log novo `SEC_BT_LOCKOUT` → `SystemDefs_Logging.h` +
`tools/logcodes.tsv` (portão `gen_logcodes.py --check`).

**B. Recuperações só pela USB** (`AppManager_Commands.cpp`, no `switch` de
`executeCommand`): `CMD_FACTORY_RESET`, `CMD_FORMAT_FS`, `CMD_RESET_ADMIN`,
`CMD_HTTPS_OFF` → se `_cmdMgr->wasLastInputFromBt()` (já existe,
`CommandManager.h:103`), `printError("USB only")` + `LOG_CODE(SEC_UNAUTHORIZED)`.
Quem autenticou por BT já tem a senha do admin; não precisa de `admin reset`. `ap`
continua permitido por BT (é o caso de uso documentado) — decisão D-4.

**C. Política de senha também na CLI** (`AppManager_CmdHandlers.cpp:632-640`,
`cmdHandleUserPass`): acrescentar `passwordPolicyOk(cmd.strVal2)` (≥ 8, letra e
dígito, já existe em `SystemDefs_Validate.h:232`). Sobre HTTP a web continua sem
política (só vê o SHA-256); documentar no SECURITY.md que a política vale sobre HTTPS
e na CLI.

**Testes.** Nativo: `btLockoutMs` em `test_validators` (1→2 s, 2→4 s, 8→256 s,
9+→300 s, 12+ estável). Bancada: script `tools/bt_auth_test.py` com socket RFCOMM
nativo do Python (`socket.AF_BLUETOOTH`, `BTPROTO_RFCOMM`; requer adaptador BT na
máquina — senão, app de terminal serial no celular e cronômetro):

- A (firmware atual): 20 senhas erradas em < 10 s, todas respondidas "Access denied".
- B (novo): após a 1ª falha, silêncio de 2 s; após a 3ª, ≥ 8 s; após a 9ª, 300 s;
  desconectar e reconectar **não** zera; senha certa após o bloqueio entra; log mostra
  `SEC_BT_LOCKOUT` com o ctx em segundos; logout por 5 min ocioso inalterado.
- `system factory` por BT → "USB only"; pela USB → funciona.
- `conf user pass viewer abc` → recusado; `Abc12345` → aceito.

Flash: +200–300 B, só alpha e Air (o `BluetoothManager` não é compilado no release).
Commit: `fix(security): exponential lockout on the Bluetooth CLI, USB-only recovery commands, password policy on the CLI`.

### V-03 · Air acordado por requisição sem autenticação

Mudanças (`src/WebManager_Auth.cpp`, `src/WebManager_Send.cpp`):

1. `getAuthPerms()` (`:30-37`): mover `_activityCb()` para **dentro** do `if` que casa o
   cookie com uma sessão ativa.
2. `ensureLoginStateSlot()` (`:197-202`): orçamento por boot —
   ```cpp
   if (_activityCb && _preAuthExt < WEB_PREAUTH_MAX_EXT) { _preAuthExt++; _activityCb(); }
   ```
   `WEB_PREAUTH_MAX_EXT = 3` em `SystemDefs_Network.h` (a WebManager é genérica; só o Air
   instala o callback).
3. `handleApiLogin` (sucesso, após alocar a sessão): `_preAuthExt = 0; if (_activityCb) _activityCb();`.
   `/metrics` com Basic válido: chamar o callback no ramo de sucesso.
4. `safeSendN` (`Send.cpp:130-137`) e `safeSend_GZ` (`:326-331`): **remover** as
   chamadas. As páginas autenticadas passam por `getAuthPerms`; a de login e o
   `login_init` ficam sob o orçamento.
5. `AGENTS.md`: a nota "toda resposta web rearma o timer" passa a "toda resposta
   **autenticada**; pré-login tem orçamento de 3 extensões por boot".

Com `air idle` 300 s e 3 extensões, o operador tem até 15 min para terminar o login
depois do boot; um poll anônimo ganha no máximo isso, uma vez por boot.

**Testes** (`tools/air_test_suite.py`, seguindo o padrão do T12: ajustar `air idle`
temporariamente e restaurar no `finally`; nunca com aba do painel aberta — `ss -tn`):

- **T15 "anonymous poll does not hold M0"**: `air idle 40`; `GET /api/login_init` sem
  cookie a cada 15 s; o alvo tem de sumir do USB (dormir) até `(WEB_PREAUTH_MAX_EXT+1) × 40 + margem` s.
  Controle positivo: no firmware atual o mesmo laço segura o aparelho indefinidamente
  (foi medido em 06/09: 491 s contra 306 s).
- **T16 "authenticated poll holds M0"**: login; `GET /api/status` com cookie a cada 15 s
  por 2 × idle; o alvo continua enumerado; parar o poll → dorme em ≤ idle + margem.
- Manual: abrir `/login`, esperar 4 min, logar: entra e o aparelho fica acordado.
- T12 e T14 continuam passando.

Flash: ≈ 0 (duas chamadas removidas, um contador). Commit:
`fix(simut-air): only an authenticated request holds the device awake; pre-login traffic gets a bounded budget`.

### V-06 · Pinos do CYW43 aceitos pelo Air

`src/air/AirConfig.h:170-172`:

```cpp
inline bool airPinValid(uint8_t pin) {
  if (pin == PIN_UNUSED) return true;
  if (pin > 29) return false;
  switch (pin) { case 23: case 24: case 25: case 29: return false; }  /* CYW43 no Pico W */
  return true;
}
```

`airSanitise()` já usa `airPinValid`, então um `air.bin` forjado ou restaurado com 25
volta ao default no load. No handler `CMD_AIR_CHARGER` (`AppManager_Commands.cpp:909-935`):
recusar também um pino que seja `sensorPowerPin` ou esteja em `cfg.sensors[i].pins[]` de
um slot ativo; mensagem `air charger <0..22|26..28|off>`.

Testes nativos (`test/test_air_config`): `test_pin_denylist_cyw43` (23/24/25/29 falsos;
0–22, 26–28 e 255 verdadeiros), `test_sanitise_resets_denied_pin` (config válida com
`chargerPin=25` → após `airSanitise` = `AIR_CHARGER_PIN`), e o existente
`test_charger_pin_bounds` ajustado. Bancada: `air charger 25` → recusado; `air charger 17` → ok.

Flash: +40 B (Air). Commit: `fix(simut-air): refuse the CYW43 GPIOs as charger or sensor-power pins`.

---

## 3. Fase 2 — serialização e validação (todas as imagens)

### V-04 · JSON sem escape; SSID/senha sem validação na web; `@NAME` sem filtro

1. `src/WebManager_Api.cpp`
   - `/api/network` (`:60-80`): `jsonEscape()` em `wifiSsid`, `staticIp`, `staticMask`,
     `staticGateway`, `staticDns`, `dns2`, `ntpServer`. O buffer `json[640]` pode ficar
     curto com escapes (SSID 31 → 62; NTP 64 → 128): subir para 896 ou emitir em
     partes como o `/api/config`.
   - `/api/status` (`:658-659`) e `/api/alarms` (`:316-317`): trocar `.replace("\"", "\\\"")`
     por `jsonEscape()` (cobre `\` e controle).
   - `/api/perms` (`:38-41`): `jsonEscape(lc)`, `jsonEscape(ln)`.
2. `src/WebManager_Commit.cpp:1157-1158` (seção `net`): `isValidCfgString()` em `ssid`
   e `pass`, com `rejectField("net.ssid")` / `rejectField("net.pass")` — igual ao CLI
   (`AppManager_Commands.cpp:448, 464`).
3. `src/DisplayManager_LangParser.cpp:202-212` (`@NAME`) e `:213-223` (`@CODE`): copiar
   só bytes ≥ 32 diferentes de `"` e `\`. Extrair para um helper puro
   (`langIdentSanitize(const char*, size_t, char*, size_t)` em `SystemDefs_Validate.h`)
   para testar no nativo.
4. **O-2 hwId estrito** (mesma raiz): `isValidHwId()` em `SystemDefs_Validate.h` —
   1..15 chars em `[A-Za-z0-9_-]`; aplicar em `WebManager_Commit.cpp:583` e
   `AppManager_CmdHandlers.cpp:261`. Fecha de vez o `"id":"abc\"` e a corrupção do JSON
   e do cabeçalho CSV da telemetria (`TelemetryManager.cpp:1201-1232, 1725-1733`).
   Configs existentes não são tocadas (validação só na escrita).

**Testes.** Nativo (`test_validators`): `isValidHwId` (aceita `DHT2202`, `28FF0A1B`;
recusa `abc\`, `a"b`, `a;b`, vazio, 16 chars), `langIdentSanitize` (`Po"rt\ugu` →
`Portugu`). Bancada: `tools/json_escape_cases.py` com três casos, cada um `json.loads`
da resposta e restauração do valor original ao fim (o `commit_all` **reinicia** o
aparelho — o script espera o reboot como a suíte faz):

- PERM_NET_CONFIG grava SSID `a"b` → hoje `/api/network` quebra; depois: 400
  `net.ssid` (validação) e, se um SSID com `"` for legítimo, a resposta continua JSON.
- PERM_SYS_CONFIG grava hwId `X\` → hoje `/api/status` quebra para todos; depois: 400.
- PERM_FILE_UPLOAD sobe `.lng` com `@NAME Po"rt` e reinicia (o pack só carrega no
  boot) → hoje `/api/perms` quebra e a UI toda cai; depois: `langName` = `Port`.

Flash: +200–400 B em todas as imagens (folga do release ≈ 15 KB). Commit:
`fix(web): escape every string in the JSON APIs; validate SSID, hwId and language-pack identity`.

### O-1 · `/download` sem os bits de logs e histórico (decisão D-5)

`src/WebManager_Files.cpp:50-80` (`handleDownload`), após a guarda de `/config`:
`/history/...` exige `PERM_HISTORY`; `*.blog` exige `PERM_LOGS` (helper puro
`downloadPermFor(path)` testável). Atualizar a linha de `/download` em
`docs/AUTHORIZATION.md` e a frase do `SECURITY.md` §5. Flash: +80 B. Se D-5 = "documentar
apenas", registrar que `PERM_FILE_READ` implica ler logs e histórico.

---

## 4. Fase 3 — decisões de desenho

### V-05 · AP de setup aberto (D-3, D-4)

Recomendação: WPA2 com PSK por aparelho, derivada e exibida, sem novo campo de config:

```cpp
// NetworkManager::beginAP — PSK = 10 chars do alfabeto [A-HJ-NP-Z2-9] (o mesmo das senhas)
// derivados de SHA-256(chipId + "simut-ap-psk"); ~50 bits, estável por aparelho.
WiFi.softAP(apName.c_str(), apPsk);
```

Onde mostrar: linha no console serial junto do `SYS_AP_START`; alpha 16×2 na segunda
linha ("PSK XXXXXXXXXX"); TFT na tela de AP (chave de tradução nova → reconstruir os
packs com `build_lang_pack.py` + `check_lang_packs.py`; o `.lng` só recarrega no boot).
Manter o AP aberto só atrás de `-DSIMUT_AP_OPEN=1` (default 0) para bancada.

Testes: `ap` pela serial → console mostra a PSK → notebook entra com ela; `nmcli dev wifi list`
mostra WPA2; sem PSK, recusa. Controle positivo: firmware atual aparece aberto.
Flash: +150–250 B (SHA-256 e alfabeto já existem). Commit:
`feat(net): WPA2 on the setup access point with a per-device key shown on the console`.

### V-01b · Consentimento de pareamento Bluetooth (D-1)

| Opção | O que fecha | Custo |
|---|---|---|
| (a) patch no `SerialBT` do framework (`tools/arduino_pico_overrides/patches`): `gap_ssp_set_auto_accept(0)`, imprimir o código de 6 dígitos na USB e aceitar só após `bt confirm` | pareamento sem consentimento | ~1 dia com bancada; patch por versão do framework (`patch.sh`) |
| (b) janela de descoberta: `gap_discoverable_control(0)` N s após o boot; `bt pair <s>` pela USB reabre | varreduras casuais | ~2 h; um endereço já conhecido ainda conecta |
| (c) só o lockout do V-01a | força bruta online | já na Fase 1 |

Recomendação: (b) agora, (a) se o Air for a espaços públicos.

### D-6 · TLS estrito na telemetria

Hoje `t_sec` sem `/cert.pem` cai para `setInsecure()` com aviso e selo (M-8). Opção:
flag `t_strict` que recusa enviar sem certificado. Recomendação: **não agora** —
revisitar quando houver instalação real com telemetria HTTPS.

### O-3 · Servidores de bancada

`tools/air_telemetry_server.py:59-66` e `tools/telemetry_bench/server_{http,mqtt}.py:377`:
bind padrão `127.0.0.1` (`--bind 0.0.0.0` explícito), arquivo de log criado com modo
0600, cabeçalhos `Authorization`/`X-Api-Key` redigidos no log. ~30 min.

---

## 5. Fase 4 — docs, registro e release

1. `SECURITY.md` completo: tabela env × superfície (web, HTTPS, BT, AP, CLI
   completa/emergência, hibernação); §2 BT (senha do admin, lockout, comandos USB-only);
   §8 AP/portal cativo (PSK), Air (M0/M1, `air.bin`, CLI de emergência), `/telemetry`;
   versão suportada = tag mais recente; chip id e `.bkp` como risco aceito.
2. `docs/AUTHORIZATION.md`: linha `GET /telemetry` (PERM_SYS_CONFIG); `/download` se O-1.
   Opcional: `tools/check_authz.py --doc` que compara a lista viva com as tabelas do
   `.md` e falha se uma rota estiver fora da doc (mesma cultura de portão).
3. `docs/security-audit/IMPLEMENTACAO_2026-09-07.md`: tabela achado → decisão → commit →
   evidência de bancada, como a de 29/08.
4. `CHANGELOG.md` + `CHANGELOG.pt-BR.md` (Unreleased → versão), `README*` (nota de
   segurança do Air), `AGENTS.md` (rearme autenticado, lockout BT, pinos proibidos).
5. Release (fluxo de `release-tag-1-5-2-burned`): conferir tag + release + CHANGELOG
   antes de numerar; build **limpo** (18 warnings pré-existentes); 5 envs nativos;
   `scan_secrets` nos zips (já automático); 3 OTAs antes de publicar; **re-apontar o
   Latest**, hoje em `simut-pcb-v1.1`. Número: `v2.4.0-beta` (config v22 e Air são
   minor bump) — D-8.

---

## 6. Decisões pendentes

| # | Decisão | Recomendação |
|---|---|---|
| D-1 | Consentimento de pareamento BT: (a) patch SSP, (b) janela de descoberta, (c) só lockout | (b) agora; (a) se o Air for a campo público |
| D-2 | Expurgar `simu…V5x` do histórico | Não; rotação resolve; registrar em IMPLEMENTACAO |
| D-3 | PSK do AP: derivada do chip id e exibida × campo configurável × manter aberto | Derivada e exibida; aberto só com `SIMUT_AP_OPEN=1` |
| D-4 | `ap` continua permitido por Bluetooth | Sim, depois do V-01a |
| D-5 | `/download` exigir PERM_HISTORY/PERM_LOGS por caminho × só documentar | Gatear (+80 B) |
| D-6 | `t_strict` na telemetria | Não agora |
| D-7 | Orçamento pré-login `WEB_PREAUTH_MAX_EXT` | 3 |
| D-8 | Versão do release | `v2.4.0-beta` |

---

## 7. Matriz achado × teste × portão

| Achado | Teste nativo | Bancada / A-B | Portão de CI |
|---|---|---|---|
| V-01a | `btLockoutMs` | `tools/bt_auth_test.py` (A: ilimitado; B: 2 s → 300 s), USB-only, política CLI | `gen_logcodes --check` (código novo) |
| V-02 | — | scanner acusa antes / limpa depois; rig com senha nova | `scan_secrets.sh` (3b/3c) |
| V-03 | — | T15 anônimo dorme; T16 autenticado segura; T12/T14 | — |
| V-04 / O-2 | `isValidHwId`, `langIdentSanitize` | `tools/json_escape_cases.py` (3 casos) | fuzz dos validadores |
| V-05 | — | `ap` → WPA2 com PSK; A: aberto | `check_lang_packs` (chave nova) |
| V-06 | `test_pin_denylist_cyw43`, `test_sanitise_resets_denied_pin` | `air charger 25` recusado | `native_air` (V-08) |
| V-07 | — | — | `check_authz --doc` (opcional) |
| V-08 | — | — | run verde na branch |
| O-1 | `downloadPermFor` | conta FILE_READ sem LOGS: `/download?file=/system.blog` → 403 | `check_authz` |
| O-3 | — | servidor sobe em 127.0.0.1; log 0600 | — |

---

## 8. Riscos e armadilhas (das memórias do projeto)

- **Instrumento antes do firmware**: todo A/B com controle positivo; o aparelho medindo a
  si mesmo não vale como prova (F22/F23 mostraram).
- **Aba do painel aberta invalida qualquer medida de idle** (fix do F21 funcionando):
  `ss -tn | grep <ip>` antes de T15/T16.
- **`commit_all` reinicia o aparelho**: scripts de caso esperam o reboot; testes de bancada
  não reconfiguram permanentemente (padrão do T12: ajustar e restaurar no `finally`).
- **`help` não discrimina firmware** (vem do `.lng`); discriminador do V-01: mandar uma
  senha errada duas vezes e medir o silêncio.
- **Flash**: medir por símbolo; descontar renomeações de `CSWTCH`; o alpha tem a menor
  folga (13 KB) e recebe V-01, V-04 e V-05.
- **Minificador** só entra se `WebUI.h` (JS) mudar — V-04 é só servidor. Se mexer no JS,
  conferir a função no `WebUI_GZ.h` descomprimido.
- **`.lng` recarrega só no boot**; chave de tradução nova (V-05) exige reconstruir os
  packs e passar em `check_lang_packs.py`.
- **Patch no framework** (V-01b opção a) vive em `tools/arduino_pico_overrides/patches`
  e o CI o aplica em `build.yml:66`; `patch.sh` sempre após trocar a versão.
- **Código de log novo** → `logcodes.tsv` sincronizado, senão o CI cai.
- **Nunca `uploadfs`**; **nunca `git add -A`**; **nunca checkout do PR no labeler**.
- **Air M1 não tem web**: T15/T16 rodam em M0.

---

## 9. Definição de pronto

- Os 8 achados fechados ou aceitos por decisão D-n registrada; O-1..O-3 decididos.
- Testes nativos novos passando em `native`, `native_air`; suíte Air com T15/T16;
  scripts de caso executados com A/B e saída anexada em `IMPLEMENTACAO_2026-09-07.md`.
- CI verde na branch com alpha e Air construídos; `scan_secrets` acusando o caso
  posicional (controle positivo documentado).
- `SECURITY.md` e `AUTHORIZATION.md` batendo com o código; CHANGELOGs; release com
  Latest re-apontado.
