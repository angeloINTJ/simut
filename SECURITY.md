# Segurança do SIMUT

Este documento descreve o modelo de ameaça, as defesas implementadas e os
procedimentos operacionais de segurança do firmware SIMUT. Deve ser
mantido em sincronia com o código — **qualquer mudança de segurança no
projeto exige revisão deste arquivo**.

Contexto: SIMUT é um firmware para Raspberry Pi Pico W (RP2040) que
gerencia sensores de temperatura/umidade, expõe dashboard e gestão via
HTTP, tem CLI via USB e Bluetooth, e envia telemetria para um servidor
remoto. É projetado para operar em rede local confiável (LAN industrial
ou domótica); não é hardened contra rede pública adversária.

---

## 1. Modelo de ameaça

### Protegido

- **Integridade da configuração** contra corrupção acidental (CRC32 +
  dual-bank backup, `StorageManager`).
- **Credenciais de usuário** em flash: hash HMAC-SHA256 com salt por
  usuário + pepper derivado do serial do chip. Senhas plaintext nunca
  são gravadas.
- **Path traversal em upload** (`handleApiUpload` valida filename e
  uploadDir — SEC-001/SEC-002).
- **DoS por buffer de CLI** (`CLI_LINE_MAX=256` descarta streams sem
  `\n` — SEC-005).
- **Forma de JSON em `/api/ls`** contra bytes de controle que quebrariam
  o listing (WEB-001 — `jsonEscapeFilename`).
- **Brute force de login** (rate limiting + lockout exponencial por
  slot IP).
- **Crash autopsy** após HW watchdog (`LogManager::performCrashAutopsy` —
  F13.1) — telemetria forense do último freeze.

### Não protegido (fora de escopo)

- **Rede pública/hostil**: HTTP puro sem TLS. Senhas SHA-256 no payload
  do cliente, mas exposição de cookies e payloads em rede não confiável.
  Use VPN ou rede isolada.
- **MitM na LAN**: sem autenticação mútua TLS. Um atacante no caminho
  pode forjar respostas ou interceptar cookies.
- **Ataque físico**: acesso ao UART USB dá CLI privilegiado sem auth
  (por design — recuperação via console). Acesso ao pino BOOTSEL permite
  flash de firmware arbitrário.
- **DMA/side-channel**: chip RP2040 sem secure enclave; qualquer código
  rodando tem acesso total a flash e RAM.
- **Disponibilidade sob DDoS**: rate-limiting é por-conexão, não resiste
  a flood coordenado.

---

## 2. Credenciais & rotação

### Admin (conta web)

- **Factory defaults**: usuário `admin` com senha random 8 caracteres do
  alfabeto `[A-Z2-9]` (32 símbolos, sem `O`/`0`/`I`/`1`). Entropia
  ≈ 2^40 — suficiente contra brute-force casual; trocar imediatamente
  após primeiro acesso. A senha é gerada por `rp2040.hwrand32()` (ROSC,
  hardware). Ver SEC-003/F12.3.
- **Exposição da senha factory**: impressa **uma vez** no Serial USB no
  boot factory (banner `SEC-003: FACTORY DEFAULTS ATIVADO`). Nunca
  persistida — zerada da RAM assim que o admin troca a senha ou carrega
  config não-factory.
- **Flag `mustChangePassword`**: bloqueia navegação até troca no 1º
  login web.
- **Rotação**: via UI `/users` (admin edita própria senha) ou CLI
  `conf user pass <user> <newpass>`. Rotação periódica recomendada
  conforme política do operador.
- **Reset sem trocar factory**: `conf system admin reset confirm` (CLI)
  regenera senha random e exibe no console — útil se o admin esquecer
  a senha.

### PIN do display físico

- **Default**: `1234` — propositalmente trivial, protege apenas contra
  mexidas acidentais no display.
- **Flag `FLAG_MUST_CHANGE_PIN`** (SEC-004/F12.4): força troca no 1º
  acesso ao menu de config do display. Overlay em
  `SystemConfig.reserved[26..27]`.

### Bluetooth CLI

- Autenticação via **PIN do display**. Acesso BT sem auth só permite
  `help` e `language`. Após auth, CLI completa — mesmos privilégios do
  USB CLI.

### Viewer (conta read-only)

- Usuário `viewer` criado em factory com permissões limitadas
  (`PERM_DASHBOARD | PERM_HISTORY`). Senha default pública (`viewer`),
  documentada como tal, com `mustChangePassword=true` pra forçar troca
  mesmo pra conta de leitura.

---

## 3. Armazenamento de secretos

- **Hashes de senha** — atualmente em transição (F15):
  - **Legado** (`hashVersion=0`, v14 e anteriores): HMAC-SHA256 × 2500
    rounds com **salt = username.toLowerCase()** (determinístico) +
    pepper derivado do board serial. Output truncado a 30 hex chars
    (120 bits). Dois devices com mesmo user+pass geram hashes diferentes
    via pepper, mas dentro de um device o salt é previsível.
  - **v1** (`hashVersion=1`, F15.2.c em diante): HMAC-SHA256 × 5000
    rounds com **salt random por usuário** (8 bytes via `hwrand32`) +
    mesmo pepper. Output 32 hex chars (128 bits, atende NIST mínimo).
    Gerado ao criar/mudar senha ou em factory reset.
  - **Schema v15** (F15.2.a, v3.24.4): `UserAccount` ganhou campos
    `salt[8]` e `hashVersion` para suportar os dois esquemas em
    paralelo. Migração transparente: configs v13/v14 são lidas,
    users ficam marcados `hashVersion=0` (legado), e serão
    auto-upgradados para v1 no próximo login válido (F15.2.c).
- **Campos sensíveis em flash** (WiFi pass, telemetry API key):
  ofuscação XOR com keystream SHA-256(chipID + domain) antes da
  gravação. **Não é criptografia forte** — é defesa em profundidade
  contra dumps triviais do flash. Adversário com code-execution no chip
  extrai facilmente.
- **RAM-only secrets**: `_initialAdminPassword` (plaintext da senha
  factory) só em RAM; zerado quando admin troca senha ou config válida
  é carregada.
- **Nonces de login** (`LoginState.nonce`): 64 chars hex de
  SHA-256(entropia hardware × 4). Válidos por `NONCE_LIFETIME_MS = 60s`.
  Consumidos atomicamente após uso.

---

## 4. Rate limiting & lockout

### Login web

- Estado por **IP client** em `_loginStates[LOGIN_STATE_SLOTS=8]`
  (LRU evict por `lastActivity`, mas **apenas entre slots evictáveis** —
  ver SEC-006 abaixo).
- **Backoff exponencial**: `(1 << failCount) × 1s`, cap 300s. Reseta a 0
  após login bem-sucedido.
- **Nonce expirado** conta como falha (mesmo backoff).
- **Log de falha**: `LOG_WARN SEC SEC_LOGIN_FAIL` com motivo (nonce
  inválido, nonce expirado, credencial inválida).
- **SEC-006/F15.1**: o algoritmo de LRU evict ignora slots sob lockout
  ativo (`lockoutUntil > now`). Slots trancados ficam "sticky" até a
  penalidade expirar — impede atacante de escapar do backoff cyclando
  por 8+ IPs diferentes até o slot lockado virar LRU. Se os 8 slots
  estiverem trancados simultaneamente (edge case), `/api/login_init`
  responde **HTTP 429** com `Retry-After` em segundos.

### CLI

- **USB/BT buffer bound**: `CLI_LINE_MAX = 256`. Linhas maiores
  descartadas + `LOG_WARN CLI CLI_UNKNOWN_CMD`. Previne DoS tipo
  `yes | cat > /dev/ttyACM0` (SEC-005/F12.5).

### Touch priority

- Durante interação no display, handlers web pesados respondem 503
  (`TouchPriority::isActive()` → `rejectIfTouchPriority` — REF-004).
  Não é rate-limit, é prioridade de UX, mas também reduz janela para
  ataques de CPU-exaustion.

---

## 5. Auditoria

### Logs persistentes

- Arquivos binários `/system.blog` (atual) e `/system.old.blog`
  (rotação), `MAX_RECORDS_PER_FILE = 800`. Cada record tem timestamp,
  core, level, tag, code, context.
- Leitura via web `/history` → aba Logs, CLI `show system log`, ou
  download direto `/download?file=/system.blog`.
- Clear: `clear log confirm` (CLI) ou UI — ação auditada
  (`LOG_CODE(LOG_WARN, "SEC", SYS_REBOOT_USER, ...)` dispara no fluxo).

### Endpoint de status

- `GET /api/sec_status` (perm: `PERM_SYS_CONFIG`) expõe tentativas
  ativas, lockouts, failCount, idade de slot — úteis para triagem.

### Logs imutáveis durante touch

- Se user está interagindo no display, logs são **bufferizados em RAM**
  e flushados ao soltar o touch. Previne perda de evidência em
  crash-during-interaction.

---

## 6. Resposta a incidente

Se houver suspeita de comprometimento:

1. **Triagem remota**:
   - `GET /api/sec_status` (se login ainda funciona) — quantos slots
     ativos, failCounts, lockouts recentes.
   - `GET /api/logs` ou `show system log` — procurar
     `SEC_LOGIN_FAIL`, `SEC_CONFIG_CHANGED`, `SYS_REBOOT_USER`
     inesperados, `STO_*` indicando gravações não solicitadas.

2. **Contenção**:
   - Desconectar da rede (power cycle ou remover WiFi).
   - Se acesso físico disponível: USB CLI continua disponível mesmo com
     rede down.

3. **Revogação**:
   - Rotacionar admin: `conf user pass admin <novaSenha>` + `write
     memory` + `reload confirm`.
   - Revogar outros users: `conf user del <nome>`.
   - Se comprometimento extenso: `conf system factory confirm` (apaga
     TODA config).

4. **Preservação**:
   - Baixe `/system.blog` e `/system.old.blog` antes do factory reset —
     são apagados junto com tudo. Arquivos em `/history/` também.

5. **Recuperação**:
   - Após factory reset, reconfigure do zero. Senha admin aparece
     **uma vez** no Serial USB (conectar para capturar).

---

## 7. Factory reset

### Via CLI (não-destrutivo para firmware)

```
conf system factory confirm
```

Efeito:
- `StorageManager::resetToFactory()` — aplica `loadDefaults` + save +
  reboot limpo.
- Overlays em `reserved[]` recriados com defaults (SetupFlags
  `MUST_CHANGE_PIN`, NetworkTime `dns_auto/ntp_enabled`).
- Users revogados (admin random, viewer default), sensores desmapeados,
  cache de telemetria zerado na próxima carga.
- **Preserva**: `/history/*.bin` (dados de sensores coletados),
  `/system.blog` (logs). Para apagar, delete via `/files` ou
  `clear log confirm` antes.

### Via wipe de flash (destrutivo total)

1. Pressionar BOOTSEL + reboot → modo UF2.
2. Flashear um `.uf2` limpo ou ferramenta de clear do Pico SDK.
3. Reflashar SIMUT.

Apaga 100% do flash: código, config, histórico, logs.

---

## 8. Superfície de ataque

### Portas/protocolos expostos

- **HTTP**: porta 80 (ou valor em `WebConfigData.port` — 1..65535).
  Autenticado via cookie de sessão após login.
- **mDNS**: `<deviceName>.local` (default `simut.local`) — apenas para
  descoberta; não expõe endpoints adicionais.
- **Bluetooth SPP**: `SIMUT_CLI`. Sem pareamento PIN (depende da stack
  do cliente); aplicação-layer auth via PIN do display.
- **USB CDC**: serial sempre disponível, sem auth (requer acesso físico).
- **NTP**: tráfego outbound UDP/123.
- **Telemetria**: HTTP/HTTPS ou MQTT/MQTTS outbound para servidor
  configurado (`cfg.telServer`).

### Endpoints com permissão

Permissões (bitmask): `PERM_DASHBOARD`, `PERM_HISTORY`, `PERM_LOGS`,
`PERM_SYS_CONFIG`, `PERM_NET_CONFIG`, `PERM_FILE_READ`,
`PERM_FILE_UPLOAD`, `PERM_FILE_DELETE`, `PERM_USER_MGR`.

Endpoints destrutivos exigem perm específica + touch-priority check (503
se user está no display): `/api/commit_all`, `/api/delete`,
`/api/mkdir`, `/api/clear_logs`, `/api/reset_touch_cal`,
`/api/force_chpass`.

Ação imediata (sem reboot): `/api/set_time` — seta RTC manual, requer
`PERM_SYS_CONFIG`.

---

## 9. Updates

- **Sem OTA**: atualização só via BOOTSEL + UF2 (requer acesso físico).
  Decisão explícita — remove vetor de update malicioso remoto em troca
  de friction operacional.
- **Integridade do firmware**: UF2 não tem assinatura; operador é
  responsável por baixar binário de canal confiável.
- **Rollback**: flashear UF2 anterior restaura. Config em `/config/`
  sobrevive ao reflash se a imagem nova tiver o mesmo layout de flash;
  se mudar partição LittleFS, pode apagar tudo.

---

## 10. Disclosure policy

Suspeitou de vulnerabilidade? Reporte **privadamente** antes de
publicar:

- Abrir issue privado no repositório do projeto (se o repo estiver em
  GitHub/GitLab, use o canal de security advisory).
- Ou contatar o mantenedor diretamente via email (preencher canal
  conforme repositório).

Evite publicar em fóruns/issues públicos até coordenação — dispositivos
em produção podem ser comprometidos durante janela de disclosure.

---

## Histórico de mudanças de segurança

Commits/sub-fases relevantes:

- **F12.1** (SEC-001, commit `d91b9e8`) — Path traversal em upload.
- **F12.2** (SEC-002, `d91b9e8`) — UploadDir sanitização.
- **F12.3** (SEC-003, `d91b9e8`) — Senha admin random em factory.
- **F12.4** (SEC-004, `d91b9e8`) — PIN display com `mustChangePin`.
- **F12.5** (SEC-005, `2543ed2`) — DoS CLI buffer bounded.
- **F-NET-TIME.1b** (`6e239f1`) — CLI `conf system factory` exposta.
- **F-NET-TIME.5a** (`95ea5e4`) — Cursor telemetria auto-reset (previne
  silent drop pós manual time).
- **F14/WEB-001** (`1826a85`) — Escape JSON em `/api/ls`.
- **F14/CON-005a** (`40795d2`) — `LoginState.nonce` sem heap alloc.
- **F15.1/SEC-006** (v3.24.3) — LRU evict de `_loginStates` ignora slots
  sob lockout ativo; `/api/login_init` responde 429 se os 8 slots
  estiverem trancados.
- **F15.2.a/SEC-007..009** (v3.24.4) — schema bump v14→v15: `UserAccount`
  ganhou `salt[8]` e `hashVersion` (zero impacto comportamental; prepara
  F15.2.c). Migrações v13→v15 e v12→v15 expandem `UserAccount` de 52→62
  bytes transparentemente.

Para detalhes técnicos, ver `STABILITY_PLAN.md` e commits individuais.
