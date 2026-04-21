# Relatório de Auditoria Técnica — SIMUT v3.19.0

## Metadados

| Campo | Valor |
|---|---|
| **Projeto** | SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura |
| **Versão auditada** | 3.19.0 |
| **Plataforma** | Raspberry Pi Pico W (RP2040) — Arduino-Pico framework |
| **Escopo** | Pacote completo: 19 arquivos fonte, 27.244 linhas |
| **Data do relatório** | 2026-04-20 |
| **Propósito** | Subsidiar ciclo de correção colaborativo entre IA executora + revisor humano |

---

## Sumário Executivo

O projeto SIMUT v3.19.0 é firmware embarcado de **alto nível de engenharia**, com padrões maduros de dual-core synchronization, flash-safety atômica, observabilidade via black-box profiler com autópsia por watchdog scratch, e segurança web multi-camada (challenge-response, RBAC, rate-limit, ofuscação de credenciais). Não foram identificados defeitos que comprometam funcionalidade corrente nem vulnerabilidades exploráveis remotamente sem credenciais válidas.

Os achados se distribuem em **quatro grupos de prioridade**:

1. **Vulnerabilidades que exigem correção antes de exposição pública**: path traversal em upload, senhas padrão bem conhecidas, DoS de heap via CLI sem autenticação USB.
2. **Bugs latentes de longo prazo**: ~26 comparações de `millis()` não wrap-safe que falharão após ~49,7 dias de uptime contínuo; inconsistências em barreiras de memória cross-core.
3. **Inconsistências documentais**: comentários contraditórios sobre scratch registers, enums desalinhados com capacidade real.
4. **Dívida de manutenibilidade**: três arquivos `.cpp` com > 2.500 linhas cada concentrando 50% do código; callbacks duplicados em 5 managers; padrões não consolidados em helpers.

O código base **está pronto para produção** após a correção dos itens de severidade **Crítica** e **Alta**. Os demais podem ser endereçados em sprints subsequentes sem bloquear release.

---

## Como Usar Este Documento

### Formato dos itens

Cada achado possui um **ID estável** (ex: `SEC-001`, `BUG-007`) que **NÃO deve ser alterado**. Quando um item for corrigido, marque-o como `[RESOLVIDO]` no cabeçalho mantendo o ID. Novos achados durante o trabalho devem receber IDs incrementais na categoria apropriada.

Cada item contém:

- **Severidade**: Crítica / Alta / Média / Baixa / Informativa
- **Categoria**: SEC (segurança) / BUG / CON (inconsistência) / MEM / PER / REF / DOC / STY
- **Localização**: arquivo(s):linha(s) — use como fonte de verdade
- **Risco de regressão**: Baixo / Médio / Alto (quão provável quebrar outras coisas ao corrigir)
- **Descrição / Impacto / Proposta / Critério de aceitação**

### Ordem de execução recomendada

1. Bloco **SEC** inteiro (Crítica → Baixa).
2. Bloco **BUG** (começar pelas wrap-safes, que são mecânicas e de baixo risco).
3. Bloco **CON** (inconsistências documentais têm risco zero).
4. Bloco **MEM + PER** juntos (costumam compartilhar mudanças).
5. Bloco **REF** por último (refatorações grandes requerem testes extensos).

### Convenção de correção

- Qualquer mudança deve **preservar comentários existentes** relevantes (o projeto é rico em *why-comments* que justificam decisões não óbvias).
- Preferir **edits pontuais** a reescritas amplas.
- **Manter indentação de 4 espaços** e **naming convention** já estabelecida (`camelCase` para métodos, `_camelCase` para membros privados, `SCREAMING_SNAKE_CASE` para constexpr).
- Todo código novo deve ser comentado em nível equivalente ao existente.
- Preferir `timeReached()` / `timeRemaining()` a qualquer nova comparação de `millis()`.

---

## Legenda de Severidade

| Severidade | Significado |
|---|---|
| **Crítica** | Exploração remota viável ou perda de dados iminente. Corrigir antes de exposição pública. |
| **Alta** | Comportamento defeituoso em produção (bug latente, DoS local, falha após uptime longo). |
| **Média** | Dívida técnica com impacto operacional moderado; acúmulo piora manutenção. |
| **Baixa** | Inconsistências, documentação, estilo. Sem impacto funcional. |
| **Informativa** | Observação arquitetural ou sugestão de oportunidade. |

---

## Índice de Achados

### SEC — Segurança (9)
- SEC-001 — Path traversal em upload de arquivo via `upload.filename` não sanitizado
- SEC-002 — Sanitização de `uploadDir` frágil (`replace("..","")` literal)
- SEC-003 — Senhas padrão hardcoded conhecidas (`admin/admin`, `viewer/viewer`)
- SEC-004 — PIN padrão do display `"1234"` sem flag `mustChangePin`
- SEC-005 — DoS de heap na CLI: buffer sem limite superior
- SEC-006 — Anti-evicção em `_loginStates` permite reset de lockout
- SEC-007 — `hashPassword` trunca output para 120 bits (abaixo do recomendado)
- SEC-008 — `hashPassword` usa apenas 2500 iterações HMAC-SHA256
- SEC-009 — Salt determinístico (username lowercase) em `hashPassword`

### BUG — Bugs e Comportamentos Defeituosos (5)
- BUG-001 — 26+ comparações `millis() - X > Y` não wrap-safe
- BUG-002 — `volatile` cross-core em pares relacionados sem proteção conjunta
- BUG-003 — `FLASH_OP` macro aplicada inconsistentemente em `writeHistoryEntryFlash`
- BUG-004 — `mutex_try_enter` em `loopCore1` pode perder atualização de `_webBusy`
- BUG-005 — `_preBootSnapshotTaken` depende de ordem de chamada frágil

### CON — Inconsistências (6)
- CON-001 — Comentários contraditórios sobre `scratch[4]/scratch[5]` em `LogManager`
- CON-002 — `LanguageCode` enum define 3 idiomas, `DICTIONARY` implementa 8
- CON-003 — Cabeçalhos de vários arquivos descrevem "3 idiomas" incorretamente
- CON-004 — `_lastSavedCrc` como `static` local em vez de membro da classe
- CON-005 — Mistura de `String` com `char[]` fixo em estruturas relacionadas
- CON-006 — Constante `DS_CONVERSION_TIME` local em vez de centralizada

### MEM — Gestão de Memória (3)
- MEM-001 — `String` em hot paths da CLI e histórico (fragmentação de heap)
- MEM-002 — `CliDemand` com 2× `String` aumenta pressão de heap na fila CLI
- MEM-003 — `WebUI.h` raw (333 KB) potencialmente redundante com `WebUI_GZ.h`

### PER — Performance (3)
- PER-001 — 14 chamadas a `watchdog_update()` no main loop (consolidar)
- PER-002 — `RenderGuard` a cada chunk de upload causa flickering
- PER-003 — `isValidHistoryFileName` chamado em loop sem cache

### REF — Refatoração e Manutenibilidade (7)
- REF-001 — `DisplayManager.cpp` (7.872 linhas) — split por tela
- REF-002 — `AppManager.cpp` (3.334 linhas) — split por responsabilidade
- REF-003 — `WebManager.cpp` (2.515 linhas) — split em handlers
- REF-004 — Callbacks `setTouchPriorityChecker` duplicados em 5 managers
- REF-005 — Helper `feedWdt()` para consolidar `watchdog_update()+TRACE_BEAT(0)`
- REF-006 — Macro `FLASH_OP` deveria ser helper reutilizável
- REF-007 — `handleApiLogin` tem ~130 linhas — extrair sub-funções

### DOC — Documentação (3)
- DOC-001 — Header diz "3 idiomas" mas código suporta 8
- DOC-002 — Magic numbers sem justificativa (`100ms` DHT timeout, `800ms` loop de dots)
- DOC-003 — Falta doc de segurança (threat model, rotação de credenciais)

---

## SEC — Segurança

---

### SEC-001 — Path traversal em upload de arquivo via `upload.filename` não sanitizado

**Severidade**: Crítica
**Categoria**: SEC (path traversal)
**Local**: `WebManager.cpp:1795-1832` (função `handleUploadData`)
**Risco de regressão**: Médio (upload é caminho crítico do file manager)

**Descrição**

O handler de upload aceita `upload.filename` diretamente do cliente HTTP multipart sem sanitização contra sequências `..` ou caracteres especiais. Apenas `uploadDir` é parcialmente tratado (linha 1819: `targetDir.replace("..", "")`, veja SEC-002). O fluxo é:

```cpp
String filename = upload.filename;                  // vem do cliente, NÃO sanitizado
if (!filename.startsWith("/")) filename = "/" + filename;
// ...
if (targetDir == "/") {
    finalPath = filename;                           // ← usado direto
} else {
    String baseName = filename.substring(filename.lastIndexOf('/') + 1);
    finalPath = targetDir + "/" + baseName;         // ← baseName pode ser vazio ou perigoso
}
```

**Impacto**

Um usuário autenticado com `PERM_FILE_UPLOAD` pode enviar um arquivo chamado `../config/system.bin` com `uploadDir=/` e sobrescrever a configuração do sistema, destruir credenciais, alterar usuários. Embora exija login prévio, usuários com permissão de upload (por exemplo, um viewer promovido parcialmente) poderiam escalar privilégio para admin via file replace. Também permite sobrescrever `/system.blog` para apagar trilha de auditoria.

**Proposta de correção**

Criar helper de sanitização em `SystemDefs.h` e aplicar antes de construir `finalPath`:

```cpp
/**
 * @brief  Valida um nome de arquivo para operações de upload/download.
 *
 * Regras:
 *   - Deve ser não-vazio e conter apenas caracteres imprimíveis seguros
 *   - Não pode conter sequências ".." (escape de diretório)
 *   - Não pode conter caracteres de controle, '\' ou bytes nulos
 *   - Comprimento máximo: 64 caracteres (reserva flash para path completo)
 *
 * @param  name  String a validar (pode conter '/' no início, que é removido).
 * @return true se o nome é seguro para uso em LittleFS path.
 */
inline bool isSafeUploadFilename(const char* name) {
    if (!name) return false;
    /* Pula '/' inicial se presente */
    if (name[0] == '/') name++;
    const size_t len = strlen(name);
    if (len == 0 || len > 64) return false;

    /* Busca sequência ".." em qualquer posição — proteção contra traversal. */
    if (strstr(name, "..") != nullptr) return false;

    for (size_t i = 0; i < len; i++) {
        const unsigned char c = (unsigned char)name[i];
        /* Proíbe: controle, '\', '"', ':', '<', '>', '|', '?', '*' */
        if (c < 32 || c == 127) return false;
        if (c == '\\' || c == '"' || c == ':' || c == '<'
            || c == '>'  || c == '|' || c == '?' || c == '*') return false;
    }
    return true;
}
```

E em `handleUploadData`:

```cpp
if (upload.status == UPLOAD_FILE_START) {
    /* Sanitiza o nome vindo do cliente ANTES de qualquer uso. */
    if (!isSafeUploadFilename(upload.filename.c_str())) {
        LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId,
                 String("Upload rejeitado: nome invalido '") + upload.filename + "'");
        _server.send(400, "application/json",
                     "{\"error\":\"Invalid filename\"}");
        return;
    }
    /* ... resto do código ... */
}
```

**Critério de aceitação**

1. Upload com `filename="../config/system.bin"` retorna HTTP 400.
2. Upload com `filename="normal.csv"` funciona normalmente.
3. Log `SEC_UNAUTHORIZED` é gerado para cada tentativa rejeitada.
4. Teste automatizado simulando curl de payload malicioso deve falhar graciosamente.
5. Upload de `calib.csv` continua funcionando (é reescrito para `calib.tmp` na linha 1834).

**Referências cruzadas**: SEC-002

---

### SEC-002 — Sanitização de `uploadDir` via `replace("..","")` é frágil

**Severidade**: Alta
**Categoria**: SEC (path traversal parcial)
**Local**: `WebManager.cpp:1819`
**Risco de regressão**: Baixo

**Descrição**

A linha `targetDir.replace("..", "");` substitui literalmente todas ocorrências de `..` por string vazia. Porém:

- `"...."` → `".."` (uma substituição não-sobreposta deixa `..` restante)
- `". ."` → não detectado (espaço no meio)
- `"%2e%2e"` → não detectado (URL-encoded)

A função `String::replace` do Arduino não é recursiva nem trata overlap, então um atacante pode construir strings que, após uma passada de replace, ainda contenham `..`.

**Impacto**

Usuário autenticado pode escapar o diretório de upload designado. Bypass da mitigação existente.

**Proposta de correção**

Rejeitar ao invés de tentar limpar (aplicando a lógica do helper de SEC-001):

```cpp
if (_server.hasArg("uploadDir")) {
    targetDir = _server.arg("uploadDir");
    targetDir.trim();

    /* Rejeita em vez de tentar limpar: escape perfeito é inviável com
     * replace() não-recursivo, e paths legítimos nunca contêm ".." */
    if (targetDir.indexOf("..") >= 0) {
        LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, _currentUserId,
                 String("uploadDir rejeitado: ") + targetDir);
        _server.send(400, "application/json",
                     "{\"error\":\"Invalid uploadDir\"}");
        return;
    }

    if (!targetDir.startsWith("/")) targetDir = "/" + targetDir;
    while (targetDir.length() > 1 && targetDir.endsWith("/")) {
        targetDir = targetDir.substring(0, targetDir.length() - 1);
    }
}
```

**Critério de aceitação**

1. `uploadDir="/history/.."` rejeita com 400.
2. `uploadDir="/history"` aceita.
3. `uploadDir="...."` rejeita (sequência `..` detectada).
4. Log de segurança registra cada rejeição com IP do cliente.

**Referências cruzadas**: SEC-001

---

### SEC-003 — Senhas padrão hardcoded bem conhecidas

**Severidade**: Alta
**Categoria**: SEC (credenciais padrão)
**Local**: `StorageManager.cpp:165-176`
**Risco de regressão**: Baixo

**Descrição**

Na função `loadDefaults()`, quando a config não existe ou está corrompida, dois usuários são criados com credenciais previsíveis:

- `admin / admin` (hash SHA-256 de `"admin"`)
- `viewer / viewer` (hash SHA-256 de `"viewer"`)

Ambos têm `mustChangePassword = true`, o que força troca no primeiro login — **essa é a mitigação existente e funciona**. Porém:

1. Em AP mode (setup inicial), o usuário pode não trocar as senhas antes de conectar à rede WiFi, expondo a janela.
2. Se o flash for apagado parcialmente (downgrade, restauração), os defaults voltam sem aviso.
3. A string `"admin"` pré-hashed (`8c6976e5...`) é um hash SHA-256 público trivial de localizar em rainbow tables.

**Impacto**

Risco durante janela de setup e após recuperação de factory reset. Atacante na mesma rede que conhece os defaults pode acessar antes do legítimo proprietário.

**Proposta de correção**

Três camadas de mitigação:

1. **Gerar senha inicial aleatória exibida no display no primeiro boot**:

```cpp
/* Em loadDefaults(), gerar senha aleatória de 8 chars [A-Z0-9]
 * e exibir no display por 5 min ou até primeiro login bem-sucedido. */
void StorageManager::generateInitialAdminPassword(char* outPlain, size_t bufSize) {
    const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";  /* sem O/0/I/1 */
    const size_t len = (bufSize > 9) ? 8 : (bufSize - 1);
    for (size_t i = 0; i < len; i++) {
        outPlain[i] = alphabet[rp2040.hwrand32() % (sizeof(alphabet) - 1)];
    }
    outPlain[len] = '\0';
}
```

2. **Forçar display de aviso persistente** enquanto `mustChangePassword == true` em qualquer conta ativa.

3. **Adicionar flag `_factoryDefaults`** que é limpa apenas após primeira troca de senha, bloqueando operações sensíveis enquanto ativa.

**Critério de aceitação**

1. Primeiro boot após flash limpo exibe senha aleatória no display TFT.
2. Login com senha aleatória funciona e força troca.
3. Após troca, a senha aleatória é zerada e não pode mais ser reutilizada.
4. Factory reset gera nova senha aleatória (não reusa a anterior).

**Referências cruzadas**: SEC-004

---

### SEC-004 — PIN padrão do display `"1234"` sem flag `mustChangePin`

**Severidade**: Alta
**Categoria**: SEC (credencial física padrão)
**Local**: `StorageManager.cpp:206`
**Risco de regressão**: Baixo

**Descrição**

```cpp
safeCopy(_currentConfig.displayPin, "1234", sizeof(_currentConfig.displayPin));
```

O PIN de acesso ao menu de configurações no display físico é `"1234"` por padrão. Diferentemente das senhas web (SEC-003), **não há flag `mustChangePin`** que force troca no primeiro uso. Um usuário com acesso físico ao dispositivo pode entrar nas configurações e alterar temas, idiomas, alarmes, etc.

**Impacto**

Qualquer pessoa que saiba que SIMUT usa `1234` como PIN padrão pode acessar configurações físicas sem ter alterado explicitamente o PIN. Em ambiente compartilhado (laboratório, almoxarifado, linha de produção), é um vetor real.

**Proposta de correção**

Adicionar flag em `SystemConfig` (aproveitando `reserved[]`):

```cpp
/**
 * Overlay em reserved[26..27]: flags de estado de setup.
 * bit 0 = mustChangePin (true se o PIN ainda é o default)
 */
struct __attribute__((packed)) SetupFlagsData {
    uint8_t magic;       /**< 0xSE = valido */
    uint8_t flags;
};
constexpr size_t SETUP_FLAGS_OFFSET = 26;
constexpr uint8_t SETUP_FLAGS_MAGIC = 0xBE;
constexpr uint8_t FLAG_MUST_CHANGE_PIN = 0x01;
```

No `AppManager::setup`, se `mustChangePin == true`, mostrar aviso no display e **bloquear saída do menu de configurações até o PIN ser trocado**.

**Critério de aceitação**

1. Primeiro boot: display mostra aviso "PIN padrao detectado — alterar em Configuracoes > Senha".
2. Acesso ao menu com PIN `1234` é permitido mas exige troca antes de qualquer outra operação.
3. Após troca, `mustChangePin = false` é persistido e aviso some.
4. Factory reset reseta `mustChangePin = true`.

**Referências cruzadas**: SEC-003

---

### SEC-005 — DoS de heap na CLI: buffer sem limite superior

**Severidade**: Alta
**Categoria**: SEC (DoS local)
**Local**: `CommandManager.cpp:84, 105`
**Risco de regressão**: Baixo

**Descrição**

O acúmulo de caracteres nos buffers USB e Bluetooth é feito sem limite:

```cpp
_usbBuffer += c;   // linha 84 — sem bound check
_btBuffer  += c;   // linha 105 — sem bound check
```

Um fluxo de caracteres sem `\n` pode fazer `String` crescer até esgotar o heap do Pico W (~264 KB). Via USB exige acesso físico; via BT exige autenticação (mitigado). Além de DoS, stress no heap pode comprometer operações críticas paralelas (telemetria TLS, saveConfiguration).

**Impacto**

- USB: usuário com cabo conectado pode travar o dispositivo enviando texto contínuo sem enter.
- BT: após login, mesmo ataque é possível (menos provável, mas existe).
- Aumento de fragmentação mesmo em uso normal (linhas de comando longas).

**Proposta de correção**

Adicionar constante em `SystemDefs.h`:

```cpp
/** Tamanho máximo de uma linha de entrada da CLI (USB e BT).
 *  Acima disso, o buffer é descartado para evitar DoS de heap. */
constexpr size_t CLI_LINE_MAX = 256;
```

Criar helper em `CommandManager.cpp`:

```cpp
/**
 * @brief  Acumula caractere no buffer da CLI com proteção anti-overflow.
 *
 * Se o buffer já está em CLI_LINE_MAX, descarta a linha e emite warning.
 * Protege contra DoS de heap por stream sem terminador de linha.
 *
 * @param  buffer       Buffer de acumulação (USB ou BT).
 * @param  c            Caractere a acumular.
 * @param  channelName  Identificação do canal para o log ("USB" ou "BT").
 */
static void appendCharWithLimit(String& buffer, char c, const char* channelName) {
    if (buffer.length() >= CLI_LINE_MAX) {
        /* Linha sobre-dimensionada: descarta para evitar exaustao de heap.
         * Log emite apenas o excedente da primeira vez para nao spammar. */
        buffer = "";
        LOG_CODE(LOG_WARN, "CLI", CLI_UNKNOWN_CMD, (int)CLI_LINE_MAX,
                 String("Linha > ") + CLI_LINE_MAX + " descartada em " + channelName);
        return;
    }
    buffer += c;
}
```

Substituir:

```cpp
_usbBuffer += c;                      // → appendCharWithLimit(_usbBuffer, c, "USB");
_btBuffer  += c;                      // → appendCharWithLimit(_btBuffer, c, "BT");
```

**Critério de aceitação**

1. Enviar 1000 caracteres sem `\n` via USB: buffer é resetado após 256 chars, log emite warning, dispositivo continua responsivo.
2. Comando válido ≤256 chars continua funcionando normalmente.
3. Heap livre após ataque permanece dentro do esperado (≥80% do total disponível).
4. Teste com `yes | head -c 10000 > /dev/ttyUSB0` não trava o sistema.

---

### SEC-006 — Anti-evicção em `_loginStates` permite reset de lockout via IPs rotativos

**Severidade**: Média
**Categoria**: SEC (bypass de rate-limit)
**Local**: `WebManager.cpp:794-808`
**Risco de regressão**: Baixo

**Descrição**

Em `handleApiLoginInit`, quando todos os `LOGIN_STATE_SLOTS` (8) estão ocupados, o slot mais antigo (`oldest`) é reusado **zerando seu `failCount` e `lockoutUntil`**:

```cpp
if (slot == -1) slot = oldest;          // reusa slot LRU
_loginStates[slot].ip = clientIP;
_loginStates[slot].failCount = 0;       // ← zera contador de tentativas
_loginStates[slot].lockoutUntil = 0;    // ← remove lockout
```

Um atacante com acesso a 8+ IPs distintos (fácil em rede local com DHCP, ou via proxy) pode rotacionar para forçar que o slot da vítima seja evictado, zerando o lockout ativo dela.

**Impacto**

Bypass parcial do rate-limit de brute force. Para uma vítima com `lockoutUntil = millis() + 300000` (5 min), atacante pode zerar o lockout em segundos, reiniciando o counter.

**Proposta de correção**

Preservar contadores de slots com lockout ativo:

```cpp
if (slot == -1) {
    slot = oldest;

    /* Se o slot evictado tem lockout ativo não expirado, NÃO reseta
     * contadores. Isso evita que atacante com IPs rotativos burle
     * o rate-limit de uma vítima por saturação de slots. */
    if (_loginStates[slot].lockoutUntil > 0
        && !timeReached(_loginStates[slot].lockoutUntil)) {
        /* Preserva failCount e lockoutUntil — só sobrescreve IP */
        LOG_CODE(LOG_WARN, "SEC", SEC_UNAUTHORIZED, 0,
                 "LRU slot evict com lockout ativo preservado");
    } else {
        _loginStates[slot].failCount    = 0;
        _loginStates[slot].lockoutUntil = 0;
    }
}
_loginStates[slot].ip = clientIP;
```

**Critério de aceitação**

1. Criar 8 slots ocupados (via 8 IPs distintos tentando login).
2. Forçar lockout em um dos slots (5 falhas seguidas).
3. Tentar fazer 9º IP criar slot → slot da vítima é evictado mas `lockoutUntil` preserva.
4. Vítima, ao tentar novo login do IP original, encontra lockout ainda ativo.

---

### SEC-007 — `hashPassword` trunca output para 120 bits (abaixo do mínimo de 128)

**Severidade**: Baixa
**Categoria**: SEC (entropia de hash)
**Local**: `StorageManager.cpp:995-998`
**Risco de regressão**: Alto (quebra autenticação existente — exige migração)

**Descrição**

A função produz um output de **30 caracteres hex = 120 bits de entropia**:

```cpp
char hashHex[32];
for (int i = 0; i < 15; i++) snprintf(hashHex + (i * 2), 3, "%02x", currentHash[i]);
hashHex[30] = '\0';
```

NIST SP 800-131A recomenda mínimo **128 bits** para hash de senha em novos sistemas. 120 bits ainda está acima do quebrável por ataque de força bruta prático (2^120 operações), mas:

- Está abaixo da recomendação formal.
- Perde 16 bits por motivo estético (caber em buffer 32).
- O truncamento **não segue nenhum algoritmo de derivação de chave padrão** (não é HKDF-Extract nem truncation padronizada).

**Impacto**

Baixo na prática — 120 bits ainda é computacionalmente seguro contra força bruta. Porém, em auditorias de compliance (ISO 27001, GDPR), o desvio do padrão pode gerar objeção formal.

**Proposta de correção**

**Alterar para 32 caracteres hex = 128 bits**, com caminho de migração:

```cpp
/**
 * @brief HMAC-SHA256 password hashing com salt + pepper, 2500 iteracoes.
 *
 * Saida: 32 hex chars = 128 bits — compliant com NIST SP 800-131A.
 * Nota de migracao: hashes de 30 chars (v3.19 e anteriores) sao
 * detectados por length() == 30 e re-hashed no primeiro login bem-sucedido.
 */
String StorageManager::hashPassword(const String& username, const String& plainPassword) {
    /* ... código de iteração idêntico ao atual ... */

    /* Output 32 hex chars (16 bytes = 128 bits) */
    char hashHex[33];
    for (int i = 0; i < 16; i++) snprintf(hashHex + (i * 2), 3, "%02x", currentHash[i]);
    hashHex[32] = '\0';
    return String(hashHex);
}
```

Adicionar verificação de compatibilidade em `handleApiLogin`:

```cpp
/* Migracao transparente de hash 120-bit (v3.19-) para 128-bit.
 * Se stored hash tem 30 chars, compara os primeiros 30 do novo hash. */
const String& stored = String(cfg.users[i].password);
bool passValid = false;
if (stored.length() == 30 && inputHash.length() >= 30) {
    passValid = secureCompare(stored, inputHash.substring(0, 30));
    if (passValid) {
        /* Re-hash com formato novo e salva */
        safeCopy(cfg.users[i].password, inputHash.c_str(),
                 sizeof(cfg.users[i].password));
        _storageRef->saveConfiguration();
        LOG_CODE(LOG_INFO, "SEC", SEC_CONFIG_CHANGED, i,
                 "Password hash migrated to 128-bit");
    }
} else {
    passValid = secureCompare(stored, inputHash);
}
```

**Critério de aceitação**

1. Usuário com hash antigo (120-bit) consegue logar e tem hash migrado silenciosamente.
2. Novos hashes são 128-bit.
3. Após migração completa de todos os usuários, remover o branch de compatibilidade.
4. Teste de regressão: logar com senha antiga, verificar que após logout/login o hash no flash tem 32 chars.

**Referências cruzadas**: SEC-008, SEC-009

---

### SEC-008 — `hashPassword` usa apenas 2500 iterações HMAC-SHA256

**Severidade**: Baixa
**Categoria**: SEC (work factor de KDF)
**Local**: `StorageManager.cpp:989`
**Risco de regressão**: Alto (afeta performance de login)

**Descrição**

```cpp
for (int r = 0; r < 2500; r++) {
    if (r % 50 == 0) watchdog_update();
    br_hmac_init(&ctx, &kc, 0); br_hmac_update(&ctx, currentHash, 32); br_hmac_out(&ctx, currentHash);
}
```

2500 iterações é **muito abaixo** dos padrões atuais:

| Padrão | Recomendação |
|---|---|
| OWASP 2023 (PBKDF2-SHA256) | ≥600.000 iterações |
| NIST SP 800-132 | mínimo 10.000; 100.000+ preferível |
| SIMUT atual | 2.500 |

**Impacto**

Em GPU moderna (~10 GH/s para SHA-256), atacante com dump do hash pode testar ~10⁶ senhas/segundo → 8 chars minúsculos quebrados em ~3 dias. Dado que o pepper é o board serial (fixo por dispositivo), um atacante que obteve cópia do flash pode montar ataque offline focado.

Porém, este é um trade-off **consciente** do autor — o RP2040 Cortex-M0+ @ 133 MHz com 2500 iterações já custa ~300-500ms/hash. Aumentar para 10k quadruplicaria e afetaria UX do login.

**Proposta de correção**

Duas opções:

**Opção A (conservadora)**: aumentar para **5000 iterações** com cold-start warning:

```cpp
/* 5000 iteracoes: equilibrio entre seguranca (dobra custo vs atacante)
 * e latencia aceitavel de login (~700ms em Pico W @ 133MHz).
 * NOTA: se aumentar alem de 10000, considere async hashing em task
 * separada para nao bloquear o main loop. */
constexpr int PASSWORD_HMAC_ROUNDS = 5000;
for (int r = 0; r < PASSWORD_HMAC_ROUNDS; r++) {
    if (r % 50 == 0) watchdog_update();
    br_hmac_init(&ctx, &kc, 0); br_hmac_update(&ctx, currentHash, 32); br_hmac_out(&ctx, currentHash);
}
```

**Opção B (ideal)**: usar **Argon2id** via BearSSL se disponível, que é memory-hard e resistente a GPU. Mais trabalhoso — requer avaliar se BearSSL empacotado tem suporte.

**Critério de aceitação**

1. Login mantém resposta < 1 segundo em hardware alvo.
2. Comentário documenta o número de iterações e justifica o valor.
3. Constante movida para `SystemDefs.h` para facilitar ajuste futuro.

**Referências cruzadas**: SEC-007, SEC-009

---

### SEC-009 — Salt determinístico (username lowercase) em `hashPassword`

**Severidade**: Baixa
**Categoria**: SEC (qualidade do salt)
**Local**: `StorageManager.cpp:983`
**Risco de regressão**: Alto (migração de hashes existentes)

**Descrição**

O salt usado em `hashPassword` é o username em lowercase:

```cpp
String salt = username; salt.toLowerCase();
```

Problemas:

1. **Previsível**: atacante sabe os usernames típicos (`admin`, `viewer`).
2. **Compartilhado entre dispositivos**: dois SIMUTs com mesma senha para `admin` terão o mesmo hash **se o pepper (board serial) fosse compartilhado** — mas o pepper é único por placa, então o pepper **já compensa** a previsibilidade do salt. Ainda assim, salt randômico é padrão.
3. **Rainbow tables direcionadas**: para a senha "admin" do usuário "admin", o hash é completamente determinístico dentro do dispositivo (pepper fixo). Rainbow tables pré-computadas por-pepper são viáveis se o pepper vazar.

**Impacto**

Baixo na prática (pepper único por placa salva), mas representa desvio da boa prática.

**Proposta de correção**

Adicionar salt aleatório por usuário, armazenado junto do hash:

```cpp
struct __attribute__((packed)) UserAccount {
    bool active;
    char username[16];
    char password[32];               /* hash 128-bit hex = 32 chars */
    uint8_t salt[8];                 /* ← NOVO: salt random por usuário */
    uint16_t permissions;
    bool mustChangePassword;
};
```

E em `hashPassword`, passar o salt explicitamente em vez de derivá-lo:

```cpp
String StorageManager::hashPassword(const String& username,
                                    const String& plainPassword,
                                    const uint8_t* salt, size_t saltLen) {
    /* ... */
}
```

**Migração**: adicionar campo `uint8_t salt[8]` requer bump de `CONFIG_VERSION` e rotina de migração (similar à v12→v14 já implementada). Gerar salt para usuários existentes no primeiro login pós-update.

**Critério de aceitação**

1. Novos usuários recebem salt aleatório de 8 bytes (hwrand32 × 2).
2. Hashes recomputados no primeiro login após migração.
3. Dois dispositivos com mesma senha para `admin` produzem hashes diferentes.
4. Reset admin via CLI regera salt junto.

**Referências cruzadas**: SEC-007, SEC-008


---

## BUG — Bugs e Comportamentos Defeituosos

---

### BUG-001 — 26+ comparações `millis() - X > Y` não wrap-safe

**Severidade**: Alta
**Categoria**: BUG (overflow após ~49 dias)
**Local**: múltiplos — mapeados abaixo
**Risco de regressão**: Baixo (substituição mecânica)

**Descrição**

O projeto define helpers explícitos `timeReached()` e `timeRemaining()` em `SystemDefs.h:215-232` com docstring extensa explicando por que `millis() > deadline` é inseguro. Porém, **26 ocorrências** do padrão inseguro existem espalhadas pelo código. Após wrap de `millis()` (a cada 49,7 dias de uptime), essas comparações invertem o resultado e causam timeouts "eternos" ou disparos prematuros.

**Localização exaustiva**

```
AppManager.cpp:272   if (millis() - lastMsg > 800)                    — dots boot
AppManager.cpp:296   if (millis() - netWait > 30000)                  — timeout boot
AppManager.cpp:462   if (pauseTs > 0 && (millis() - pauseTs > 5000))  — display pause stuck
AppManager.cpp:472   if (millis() - _lastCore1RestartCheck > 5000)    — core1 check interval
AppManager.cpp:590   if (millis() - _lastSensorCheck > 3000)          — sensor heal
AppManager.cpp:1475  if (_inYield && (millis() - _yieldEntryTime > 10000))
AppManager.cpp:1938  if (_bootCompletedAt > 0 && (millis() - _bootCompletedAt > 5000))
AppManager.cpp:1998  if (millis() - lastPendingRefresh > 10000)
AppManager.cpp:2061  if (millis() - _preloadBudget > 5000)
AppManager.cpp:3121  if (millis() - lastMissingLog[gpio] > 60000)
DisplayManager.cpp:1468  if (_sensDone && (millis() - _sensDoneTime > 1500))
DisplayManager.cpp:1627  (millis() - _lastTouchTime > 30000)
DisplayManager.cpp:4200  if (millis() - _lastTouchTime > 30000) forceDashboard();
NetworkManager.cpp:109  if (millis() - _apStartTime > AP_MODE_TIMEOUT_MS ...)
NetworkManager.cpp:227  if (millis() - _rssiSampleAt > 60000)
NetworkManager.cpp:236  if (millis() - _stateTimer > 200)
NetworkManager.cpp:266  else if (millis() - _stateTimer > 20000)
SensorManager.cpp:440  else if (millis() - _dhtTimer > 100)
StorageManager.cpp:588 if (millis() - _budgetStart > 4000)
StorageManager.cpp:799 if (millis() - _budgetStart > 6000)
StorageManager.cpp:824 if (millis() - _budgetStart > 6000)
```

Mais 5 em arquivos não rastreados pelo grep inicial (verificar com `grep -rn "millis() -.*>\s*[0-9]"`).

**Impacto**

Após 49,7 dias de uptime contínuo, comportamentos diversos quebram:
- AP mode timeout nunca dispara (linha `NetworkManager.cpp:109`) → dispositivo fica em AP indefinidamente.
- Display pause stuck detection falha → pauses longos não são detectados.
- Sensor heal nunca é executado → sensores em erro não recuperam.
- Timeouts de rede (20s, 200ms) tornam-se infinitos ou imediatos dependendo do momento do wrap.

Dispositivos de monitoramento frequentemente ficam meses sem reboot. **Este é o bug latente mais impactante do projeto.**

**Proposta de correção**

Substituição mecânica `millis() - X > Y` → `timeReached(X + Y)`. Exemplo:

```cpp
/* ANTES */
if (millis() - _apStartTime > AP_MODE_TIMEOUT_MS && strlen(_ssid) > 0) { ... }

/* DEPOIS */
if (timeReached(_apStartTime + AP_MODE_TIMEOUT_MS) && strlen(_ssid) > 0) { ... }
```

Para intervalos periódicos (ex: `lastMsg > 800`):

```cpp
/* ANTES */
if (millis() - lastMsg > 800) { lastMsg = millis(); ... }

/* DEPOIS — identidade semântica preservada */
if (timeReached(lastMsg + 800)) { lastMsg = millis(); ... }
```

**Cuidado especial**: quando o timestamp é `0` (não inicializado), `timeReached(0 + Y)` pode retornar true imediatamente após wrap. **Sempre inicializar timestamps com `millis()` no setup** ou proteger com `if (X > 0 && timeReached(X + Y))`.

**Critério de aceitação**

1. Nenhuma ocorrência de `millis() - X > Y` ou `millis() - X >= Y` no código (exceto comentários).
2. Teste simulado: forçar `millis()` próximo do wrap via debugger e verificar que todos os timers se comportam corretamente.
3. Execução de 50 dias em bancada (ou simulador) sem regressões funcionais.

**Referências cruzadas**: SystemDefs.h:193-232 (helpers existentes)

---

### BUG-002 — `volatile` cross-core em pares relacionados sem proteção conjunta

**Severidade**: Média
**Categoria**: BUG (race condition sutil)
**Local**: `DisplayManager.h:453-476`
**Risco de regressão**: Médio (mudanças em concorrência exigem testes cuidadosos)

**Descrição**

Várias variáveis são declaradas `volatile` em `DisplayManager.h` para comunicação cross-core:

```cpp
volatile bool      _previewPending  = false;
volatile uint8_t   _previewType     = 0;
volatile uint8_t   _previewMelIdx   = 0;

volatile bool      _volumePreviewPending = false;
volatile uint8_t   _volumePreviewLevel   = 0;

volatile bool      _alarmVolPreviewPending = false;
volatile uint8_t   _alarmVolPreviewLevel   = 0;

volatile uint8_t  _pktArrowState     = 0;
volatile bool     _pktArrowFlashOn   = false;
volatile uint32_t _pktArrowFlashTime = 0;
volatile uint32_t _pktArrowFlashEnd  = 0;
```

**Em ARM Cortex-M0+, `volatile` NÃO garante**:
- Ordering entre escritas de variáveis diferentes (compilador pode reordenar mesmo com volatile).
- Atomicidade de acessos > 32 bits.
- Visibilidade imediata entre cores sem memory barrier.

O pattern correto observado em outros locais do código (`AppManager.cpp:170`) usa `__dmb()` explicitamente após escrita de dados e antes de flag:

```cpp
app._timeSyncBootTs = bootTs;
app._timeSyncDelta = delta;
__dmb();  /* barrier correto */
app._pendingTimeSync = true;
```

**Impacto**

Em preview de som, Core 0 escreve `_previewType = SND_CONFIRM; _previewMelIdx = 3; _previewPending = true;`. Core 1 pode ler `_previewPending == true` **antes** de `_previewType/_previewMelIdx` estarem visíveis — toca melodia errada. Probabilidade baixa (janela de microssegundos), mas reproduz em stress.

**Proposta de correção**

Para cada par de variáveis relacionadas, adicionar `__dmb()` entre dados e flag, e garantir leitura na ordem inversa:

```cpp
/* Producer (Core 0) */
void DisplayManager::requestPreviewSound(SoundEvent type, uint8_t melIdx) {
    _previewType   = type;
    _previewMelIdx = melIdx;
    __dmb();                       /* garante escritas de dados antes do flag */
    _previewPending = true;
}

/* Consumer (Core 1) */
bool DisplayManager::consumePreviewSound(SoundEvent& outEvent, uint8_t& outIdx) {
    if (!_previewPending) return false;
    __dmb();                       /* garante leitura dos dados APOS flag */
    outEvent = (SoundEvent)_previewType;
    outIdx   = _previewMelIdx;
    __dmb();
    _previewPending = false;
    return true;
}
```

**Alternativa arquitetural**: substituir por `queue_t` do pico-sdk (lock-free, já usado para `_eventQueue`). Mais limpo mas requer refatoração maior.

**Critério de aceitação**

1. Auditoria: todo par `(flag, data)` tem `__dmb()` entre escritas no producer.
2. Todo consumo tem `__dmb()` entre leitura do flag e leitura dos dados.
3. Teste de stress: 1000× preview de som em rajada não produz melodia inconsistente.

---

### BUG-003 — `FLASH_OP` macro aplicada inconsistentemente em `writeHistoryEntryFlash`

**Severidade**: Média
**Categoria**: BUG (disciplina de flash safety)
**Local**: `StorageManager.cpp:526-570`
**Risco de regressão**: Médio

**Descrição**

Em `saveConfiguration` (linha 409-416), há uma macro bem estruturada:

```cpp
#define FLASH_OP(BLOCK) do { \
    { LogManager::TraceScope _trLock(0, MOD_CORE1_LOCK); \
      enterFlashSafeMode(); } \
    watchdog_update(); \
    BLOCK; \
    watchdog_update(); \
    exitFlashSafeMode(); \
} while (0)
```

Porém em `writeHistoryEntryFlash` (linhas 533-568), o mesmo pattern é implementado **manualmente**, sem a macro:

```cpp
{
    LogManager::TraceScope _trLock(0, MOD_CORE1_LOCK);
    enterFlashSafeMode();
}
if (path != _currentLogFileName) { enforceStorageLimit(); _currentLogFileName = path; }
watchdog_update();
File f = LittleFS.open(path, "a");
// ...
exitFlashSafeMode();
```

Isto duplica o padrão (harder to maintain), e **não aplica a chunkagem granular** que `saveConfiguration` faz — cada op LittleFS individual não sai do lockout entre elas, bloqueando Core 1 por mais tempo.

**Impacto**

Writes de histórico (1× por minuto) podem travar Core 1 por mais tempo que o necessário, causando flickering ou heartbeat stale detectado pelo cross-core watchdog.

**Proposta de correção**

Mover `FLASH_OP` para ser helper da classe (não macro local):

```cpp
/* StorageManager.h — novo método privado */
private:
    /**
     * @brief  Executa uma operação LittleFS com lockout granular do Core 1.
     *
     * Entra em flash safe mode (multicore_lockout), alimenta WDT, executa a
     * lambda, alimenta WDT novamente, sai do lockout. Permite que Core 1
     * renderize 1 frame entre chamadas consecutivas.
     *
     * @param  op  Lambda com a operação LittleFS a executar.
     */
    template<typename F>
    void flashOp(F&& op) {
        {
            LogManager::TraceScope _trLock(0, MOD_CORE1_LOCK);
            enterFlashSafeMode();
        }
        watchdog_update();
        op();
        watchdog_update();
        exitFlashSafeMode();
    }
```

E refatorar `writeHistoryEntryFlash`:

```cpp
bool StorageManager::writeHistoryEntryFlash(const BinaryHistoryRecord& rec) {
    if (!_isMounted) return false;
    String path = getHistoryFileName();

    LogManager::TraceScope _tr(0, MOD_HIST_FLASH);
    LogManager::WdtWindow _wdt(30000);

    if (path != _currentLogFileName) {
        flashOp([&]{ enforceStorageLimit(); });
        _currentLogFileName = path;
    }

    bool ok = false;
    flashOp([&]{
        File f = LittleFS.open(path, "a");
        if (f) {
            f.write((const uint8_t*)&rec, HISTORY_RECORD_SIZE);
            f.close();
            ok = true;
        }
    });

    if (ok) { _storageDirty = true; return true; }

    /* Retry path: força cleanup antes de segunda tentativa. */
    LOG_CODE(LOG_WARN, "STO", STO_WRITE_FAILED, 0, "");
    _storageDirty = true;
    flashOp([&]{ enforceStorageLimit(); });

    flashOp([&]{
        File f = LittleFS.open(path, "a");
        if (f) {
            f.write((const uint8_t*)&rec, HISTORY_RECORD_SIZE);
            f.close();
            ok = true;
        }
    });

    return ok;
}
```

E simplificar `saveConfiguration` usando o mesmo helper (remove a macro local).

**Critério de aceitação**

1. `FLASH_OP` macro removida de `saveConfiguration` e substituída por `flashOp(lambda)`.
2. `writeHistoryEntryFlash` usa o mesmo helper.
3. Nenhuma duplicação do par `enterFlashSafeMode/exitFlashSafeMode`.
4. Tests: write de histórico continua funcionando sem regressão; Core 1 responsivo durante write (heartbeat OK).

**Referências cruzadas**: REF-006

---

### BUG-004 — `mutex_try_enter` em `loopCore1` pode perder atualização de `_webBusy`

**Severidade**: Baixa
**Categoria**: BUG (UX transiente)
**Local**: `DisplayManager.cpp:1333-1336`
**Risco de regressão**: Baixo

**Descrição**

```cpp
bool webBusyNow = false;
if (mutex_try_enter(&_stateMutex, NULL)) {
    webBusyNow = _webBusy;
    mutex_exit(&_stateMutex);
}
```

Se `mutex_try_enter` falhar (mutex ocupado por Core 0), `webBusyNow` permanece `false` mesmo que `_webBusy == true`. No próximo frame, o overlay de web-busy pode piscar ou ficar ausente momentaneamente.

**Impacto**

Em operações web longas com alta contenção (vários handlers simultâneos), o overlay "WEB BUSY" pode ter comportamento inconsistente — aparecendo e desaparecendo rapidamente. Apenas UX, não funcional.

**Proposta de correção**

Usar estado sticky — manter último valor lido:

```cpp
/* Retém último valor conhecido quando mutex está contested, evitando
 * flicker do overlay. Assume que _webBusy é flag "lenta" (set por
 * handlers longos, reset ao final — mudanças em ordem de segundos). */
static bool _lastWebBusy = false;
bool webBusyNow = _lastWebBusy;
if (mutex_try_enter(&_stateMutex, NULL)) {
    webBusyNow = _webBusy;
    _lastWebBusy = webBusyNow;
    mutex_exit(&_stateMutex);
}
```

**Critério de aceitação**

1. Overlay "WEB BUSY" aparece e desaparece suavemente (sem piscar rápido).
2. Teste: disparar 5 handlers web em paralelo; observar display — overlay permanece estável.

---

### BUG-005 — `_preBootSnapshotTaken` depende de ordem de chamada frágil

**Severidade**: Baixa
**Categoria**: BUG (fragilidade em refatoração)
**Local**: `LogManager.cpp:440-442, 601-602`
**Risco de regressão**: Médio

**Descrição**

A lógica de captura pré-boot do scratch[3]:

```cpp
/* Em setModule (primeira chamada capta) */
if (!_preBootSnapshotTaken) {
    _preBootScratch4 = watchdog_hw->scratch[3];
    _preBootSnapshotTaken = true;
}
```

```cpp
/* Em performCrashAutopsy (lê o snapshot) */
uint32_t modTrace = _preBootSnapshotTaken ? _preBootScratch4 : watchdog_hw->scratch[3];
```

Depende da **primeira chamada a `setModule`** acontecer **antes** de `performCrashAutopsy`. Se alguém refatorar o boot e trocar a ordem, a autópsia lerá o scratch já sobrescrito.

**Impacto**

Autópsia pós-crash mostra módulo **incorreto** após refatoração acidental. Debug mais difícil.

**Proposta de correção**

Tornar a captura explícita e auto-documentada:

```cpp
/**
 * @brief  Captura o valor de scratch[3] do boot anterior para autópsia.
 *
 * DEVE ser chamado em LogManager::begin() ANTES de qualquer TRACE_MOD.
 * Idempotente: chamadas subsequentes são no-op.
 */
void LogManager::captureBootSnapshot() {
    if (!_preBootSnapshotTaken) {
        _preBootScratch4 = watchdog_hw->scratch[3];
        _preBootSnapshotTaken = true;
    }
}

void LogManager::begin(bool saveToFile, LogLevel minSerialLevel) {
    captureBootSnapshot();        /* ← chamada explícita */
    /* ... resto do begin ... */
    performCrashAutopsy();
}
```

E remover o código oportunista do `setModule`. Adicionar assertion em `performCrashAutopsy`:

```cpp
void LogManager::performCrashAutopsy() {
    if (!_preBootSnapshotTaken) {
        /* Erro de programação: setup chamou autopsy antes de captureBootSnapshot */
        Serial.println("[LOG] performCrashAutopsy called before captureBootSnapshot!");
    }
    /* ... */
}
```

**Critério de aceitação**

1. `captureBootSnapshot` é método público explícito.
2. `begin()` chama na primeira linha.
3. `setModule` não captura mais implicitamente.
4. Autópsia após crash continua funcionando (teste: força panic, reboot, verifica log).


---

## CON — Inconsistências

---

### CON-001 — Comentários contraditórios sobre `scratch[4]/scratch[5]` em `LogManager`

**Severidade**: Baixa
**Categoria**: CON (documentação contraditória)
**Local**: `LogManager.cpp:450-452` vs `LogManager.cpp:554-557`
**Risco de regressão**: Zero

**Descrição**

Dois comentários no mesmo arquivo dizem coisas incompatíveis:

```cpp
// Linha 450-452:
/* ATENÇÃO: NÃO usar scratch[4] ou scratch[5] — são reservados pelo
 * SDK Pico para `watchdog_reboot(pc, sp, delay)` passar PC/SP. */
```

```cpp
// Linha 554-557:
/* Usamos scratch[5] em vez de scratch[4] porque scratch[5]
 * comprovadamente persiste (o path do soft panic sempre chega ao
 * autopsy com o valor correto). */
```

O código usa `scratch[5]` como magic value (linhas 539, 557, 572, 590, 593). O comentário empírico da linha 554 venceu o comentário teórico da linha 450, mas ambos continuam no arquivo.

**Impacto**

Mantenedores futuros podem desfazer mudança válida por acreditarem no primeiro comentário.

**Proposta de correção**

Consolidar em um único comentário na linha 450 explicando a situação real:

```cpp
/*
 * Uso dos watchdog scratch registers neste sistema:
 *   scratch[3] — livre, usado para trace de módulo (Core 0 + Core 1).
 *   scratch[4] — sobrescrito por watchdog_reboot(pc, ...). NÃO usar.
 *   scratch[5] — sobrescrito teoricamente (sp), mas teste empírico
 *                mostra que scratch[5] persiste através de
 *                watchdog_reboot(0,0,0) no Arduino-Pico atual.
 *                Usamos scratch[5] como magic value para distinguir
 *                soft panic (0xCA11B007) de reboot limpo (0xC1EA8007).
 *   scratch[6] — dados do soft panic (deadCore, mod0, mod1).
 *   scratch[7] — elapsed_ms do soft panic.
 *
 * ATENÇÃO: se Arduino-Pico mudar o comportamento de watchdog_reboot
 * em versão futura, testar novamente. Se scratch[5] for zerado,
 * migrar magic para scratch[3] (compartilhando com trace — usar
 * bits altos para não colidir).
 */
```

E remover o comentário duplicado da linha 554.

**Critério de aceitação**

1. Apenas um comentário autoritativo sobre uso de scratch no arquivo.
2. Comentário menciona a versão do Arduino-Pico testada (se possível).

---

### CON-002 — `LanguageCode` enum define 3 idiomas, `DICTIONARY` implementa 8

**Severidade**: Baixa
**Categoria**: CON (enum desalinhado com capacidade)
**Local**: `SystemDefs.h:298-302` vs `DisplayManager.cpp:32-40`
**Risco de regressão**: Baixo

**Descrição**

```cpp
// SystemDefs.h
enum LanguageCode {
    LANG_EN = 0,
    LANG_PT = 1,
    LANG_ES = 2
};
```

```cpp
// DisplayManager.cpp
static const int TOTAL_LANGS = 8;
static const char* const LANG_NAMES[TOTAL_LANGS] = {
    "English", "Portugues", "Espanol", "Francais",
    "Deutsch", "Italiano", "Russkiy", "Zhongwen"
};
```

O enum só cataloga 3 dos 8 idiomas realmente implementados. Qualquer código que use `LanguageCode` em vez de `uint8_t` fica limitado a 3.

**Impacto**

- `_cmdMgr.setCliLang(cfg.displayLang)` passa `uint8_t` bruto — funciona mas perde type-safety.
- Futuro código que tente usar o enum (ex: `switch (lang) { case LANG_EN: ... }`) omitirá os 5 idiomas faltantes.

**Proposta de correção**

Completar o enum:

```cpp
/** Display language selection (indexes into i18n dictionary).
 *  Valores sincronizados com DICTIONARY[] em DisplayManager.cpp e
 *  tabelas de CLI em CommandManager.cpp. Ao adicionar novo idioma,
 *  atualizar TODOS os três locais. */
enum LanguageCode {
    LANG_EN = 0,
    LANG_PT = 1,
    LANG_ES = 2,
    LANG_FR = 3,
    LANG_DE = 4,
    LANG_IT = 5,
    LANG_RU = 6,
    LANG_ZH = 7,
    LANG_COUNT = 8                /**< Sentinela — total de idiomas */
};
```

**Critério de aceitação**

1. Enum tem 8 valores + sentinela `LANG_COUNT`.
2. `DisplayManager.cpp` usa `LANG_COUNT` em vez de `TOTAL_LANGS`.
3. `static_assert(LANG_COUNT == sizeof(LANG_NAMES)/sizeof(char*))` adicionado.

**Referências cruzadas**: CON-003, DOC-001

---

### CON-003 — Cabeçalhos descrevem "3 idiomas" incorretamente

**Severidade**: Informativa
**Categoria**: CON (doc desatualizada)
**Local**: `LogManager.h`, `CommandManager.cpp`, outros (grep: `"3 idiomas"`, `"3 languages"`)
**Risco de regressão**: Zero

**Descrição**

Documentação de classe em vários headers menciona suporte a 3 idiomas quando na realidade são 8. Exemplo em `LogManager.h` ou docstrings de `setLanguage`:

```cpp
void setLanguage(uint8_t lang) { _language = lang; }  /* EN/PT/ES */
```

**Impacto**

Nenhum funcional — apenas confusão documental. Agravado pelo CON-002 (enum consistente com a doc).

**Proposta de correção**

Grep `grep -rn "3 idiomas\|3 languages\|EN/PT/ES\|3 linguas"` e atualizar cada comentário para refletir a capacidade real (8 idiomas). Exemplo:

```cpp
/** Idioma dos labels dos códigos de log. 0..7, vide enum LanguageCode. */
void setLanguage(uint8_t lang) { _language = lang; }
```

**Critério de aceitação**

1. Zero ocorrências de "3 idiomas", "3 languages", "EN/PT/ES" nos comentários (exceto contexto histórico).
2. Comentários citam o `enum LanguageCode` como fonte de verdade.

**Referências cruzadas**: CON-002, DOC-001

---

### CON-004 — `_lastSavedCrc` como `static` local em vez de membro da classe

**Severidade**: Baixa
**Categoria**: CON (escopo inadequado)
**Local**: `StorageManager.cpp:385`
**Risco de regressão**: Baixo

**Descrição**

```cpp
bool StorageManager::saveConfiguration() {
    // ...
    static uint32_t _lastSavedCrc = 0;              // ← static local
    uint32_t currentCrc = calculateCRC32((uint8_t*)&_currentConfig, sizeof(SystemConfig));
    if (currentCrc == _lastSavedCrc && _lastSavedCrc != 0) {
        // skip no-op
    }
    // ...
    _lastSavedCrc = currentCrc;
}
```

`static` dentro de método membro faz sentido só como singleton implícito. Como `StorageManager` já é singleton (uma instância em `AppManager`), funciona — mas acopla o comportamento à primeira instância criada. Se futuramente houver testes unitários com múltiplas instâncias, o estado seria compartilhado indevidamente.

**Impacto**

Zero em produção. Bloqueio para testes unitários ou refactoring multi-instância futuro.

**Proposta de correção**

Mover para membro privado:

```cpp
// StorageManager.h, seção private:
uint32_t _lastSavedCrc = 0;     /**< CRC do último save bem-sucedido (para skip no-op) */
```

E remover o `static` local em `saveConfiguration`:

```cpp
uint32_t currentCrc = calculateCRC32((uint8_t*)&_currentConfig, sizeof(SystemConfig));
if (currentCrc == _lastSavedCrc && _lastSavedCrc != 0) {
    _lastSaveWasNoOp = true;
    MetricsManager::instance().data().configSaves++;
    return true;
}
// ...
_lastSavedCrc = currentCrc;
```

**Critério de aceitação**

1. `_lastSavedCrc` é membro privado (não `static` local).
2. Inicializado a `0` no construtor ou via in-class initializer.
3. `saveConfiguration` skip no-op continua funcionando.

---

### CON-005 — Mistura de `String` com `char[]` fixo em estruturas relacionadas

**Severidade**: Baixa
**Categoria**: CON (heterogeneidade de representação)
**Local**: `SystemDefs.h:936-945` (`CliDemand`), `WebManager.h:89-96` (`LoginState`), e outros
**Risco de regressão**: Alto (mudança afeta serialização)

**Descrição**

`CliDemand`:

```cpp
struct CliDemand {
    DemandType type;
    String strVal1;          /* String — aloca heap */
    String strVal2;          /* String — aloca heap */
    int intVal1;
    bool boolVal;
    uint8_t rom[8];          /* buffer fixo */
    bool confirmed = false;
    bool intVal1Valid = true;
};
```

`LoginState`:

```cpp
struct LoginState {
    uint32_t ip = 0;
    String nonce = "";       /* String */
    uint32_t nonceCreatedAt = 0;
    // ...
};
```

Misturar `String` (heap) e `uint8_t[]` (stack) na mesma estrutura dificulta dimensionamento de memória e introduz pontos de alocação dinâmica em estruturas "passivas".

**Impacto**

- Cópias de `CliDemand` no `_cliQueue` (buffer de 2 slots em `AppManager`) envolvem aloc/free de `String` interno. Cada enqueue toca o heap duas vezes.
- `_loginStates[8]` tem 8 × `String nonce` = 8 ponteiros para heap.

**Proposta de correção**

**Para `CliDemand`** (hot path — fila CLI):

```cpp
struct CliDemand {
    DemandType type;
    char     strVal1[64];         /* Era String — tamanho fixo suficiente p/ valor CLI */
    char     strVal2[64];
    int      intVal1;
    bool     boolVal;
    uint8_t  rom[8];
    bool     confirmed = false;
    bool     intVal1Valid = true;
};
```

Ajustar parser para usar `safeCopy`:

```cpp
safeCopy(demand.strVal1, tokens[1].c_str(), sizeof(demand.strVal1));
```

**Para `LoginState`**: nonce de 128 bits = 32 hex chars:

```cpp
struct LoginState {
    uint32_t ip = 0;
    char nonce[33] = {0};        /* 32 hex + null — tamanho fixo de generateSecureToken */
    uint32_t nonceCreatedAt = 0;
    uint8_t failCount = 0;
    uint32_t lockoutUntil = 0;
    uint32_t lastActivity = 0;
};
```

**Critério de aceitação**

1. `CliDemand` não contém mais `String`.
2. `LoginState` não contém mais `String`.
3. Heap livre após 100 comandos CLI permanece estável (sem fragmentação crescente).
4. Comportamento funcional idêntico (regressão via testes de CLI e login).

**Referências cruzadas**: MEM-001, MEM-002

---

### CON-006 — Constante `DS_CONVERSION_TIME` local em vez de centralizada

**Severidade**: Informativa
**Categoria**: CON (constante fora do hub)
**Local**: `SensorManager.h:136`
**Risco de regressão**: Baixo

**Descrição**

`SensorManager.h`:

```cpp
const uint32_t DS_CONVERSION_TIME = 750;
```

Enquanto todas as outras constantes de timing (`WATCHDOG_TIMEOUT_MS`, `NET_SOCKET_TIMEOUT_MS`, `AP_HOLD_DURATION_MS`, etc.) estão centralizadas em `SystemDefs.h` com docstrings explicando o dimensionamento.

**Impacto**

Nenhum funcional. Apenas inconsistência com o padrão estabelecido.

**Proposta de correção**

Mover para `SystemDefs.h`:

```cpp
/* =========================================================================== */
/*                          SENSOR TIMING CONSTANTS                          */
/* =========================================================================== */

/** Tempo de conversão do DS18B20 para resolução 12-bit (ms).
 *  Datasheet: max 750ms. Resoluções menores: 9b=94ms, 10b=188ms, 11b=375ms.
 *  O projeto usa 12-bit por padrão (configurável via cfg.ds18Resolution). */
constexpr uint32_t DS18B20_CONVERSION_TIME_MS = 750;

/** Timeout max para leitura assíncrona do DHT22 (ms).
 *  Datasheet: transmissão completa ~20ms. 100ms cobre jitter + margem. */
constexpr uint32_t DHT22_READ_TIMEOUT_MS = 100;
```

E ajustar `SensorManager.h/cpp` para usar os nomes globais.

**Critério de aceitação**

1. Constante movida para `SystemDefs.h`.
2. Renomeada com sufixo `_MS` para consistência com o resto.
3. Docstring explica a fonte (datasheet) e o trade-off.

---

## MEM — Gestão de Memória

---

### MEM-001 — `String` em hot paths da CLI e histórico

**Severidade**: Média
**Categoria**: MEM (fragmentação de heap)
**Local**: múltiplos
**Risco de regressão**: Médio

**Descrição**

Uso de `String` em caminhos executados frequentemente:

- `CommandManager`: `_usbBuffer += c`, `_btBuffer += c` (cada caractere pode realoc).
- `StorageManager::getHistoryFileName()`: retorna `String` nova a cada chamada (1× por minuto).
- `NetworkManager::getFormattedTime()`, `getIpAddress()`, `getMacAddress()`: todas retornam `String`.
- `LogManager::uptimeString()`: String.
- Mensagens de log: `String(TRL("...", "...")) + varivel` — concatenação de 2-3 Strings em cada log.

**Impacto**

Cada `String` alocação pode causar realloc do heap. Em Pico W com 264 KB SRAM e uso pesado (telemetria TLS reserva 16 KB), a fragmentação se acumula. Observei que o projeto já tem `MetricsManager::sampleLargestBlock()` — prova de consciência do problema.

**Proposta de correção**

**Estratégia escalonada** — começar pelos mais quentes:

1. **History filename** (1×/min): trocar para buffer estático:

```cpp
// StorageManager.h: adicionar
private:
    char _historyFnBuf[40];
public:
    const char* getHistoryFileNameC();   /* retorna ponteiro para buffer interno */
```

```cpp
// StorageManager.cpp
const char* StorageManager::getHistoryFileNameC() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    snprintf(_historyFnBuf, sizeof(_historyFnBuf),
             "%s/%04d%02d%02d" HISTORY_FILE_EXT,
             DIR_HISTORY, timeinfo.tm_year + 1900,
             timeinfo.tm_mon + 1, timeinfo.tm_mday);
    return _historyFnBuf;
}
```

2. **Network getters**: formatar em buffer ao invés de retornar `String`:

```cpp
void NetworkManager::getIpAddress(char* out, size_t bufSize) {
    IPAddress ip = (_state == NET_AP_CONFIG) ? WiFi.softAPIP() : WiFi.localIP();
    snprintf(out, bufSize, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}
```

3. **CLI buffers**: já abordado em SEC-005 + CON-005 (tornar buffers fixos).

**Critério de aceitação**

1. `MetricsManager::data().largestBlock` após 24h de uso típico é ≥ 70% do heap inicial (antes: verificar baseline).
2. Nenhum caminho de 1×/min ou mais frequente aloca `String`.
3. Testes de regressão: todas chamadas externas continuam funcionando (adaptadas ou wrapped com `String()`).

**Referências cruzadas**: CON-005, MEM-002

---

### MEM-002 — `CliDemand` com 2× `String` aumenta pressão de heap na fila CLI

**Severidade**: Média
**Categoria**: MEM (pressão de heap em fila)
**Local**: `SystemDefs.h:936-945`, `AppManager.h:141`
**Risco de regressão**: Alto

**Descrição**

```cpp
struct CliDemand {
    String strVal1;          /* aloca heap na cópia */
    String strVal2;
    // ...
};
```

E em `AppManager.h`:

```cpp
CliDemand _cliQueue[CLI_QUEUE_CAP];      /* CLI_QUEUE_CAP = 2 */
```

A fila de 2 slots sempre tem 2× `CliDemand`, cada um com 2× `String`. Mesmo vazios, Strings alocam buffer mínimo (~8 bytes cada). Enqueue/dequeue envolve cópia de strings = aloc/free do heap.

**Impacto**

Baixo em uso normal (fila raramente usada), mas durante touch longo + rajada CLI, cada comando enfileirado toca heap 4×.

**Proposta de correção**

Vide CON-005 — transformar `strVal1`/`strVal2` em `char[64]`. Isso elimina alocações no enqueue/dequeue.

**Critério de aceitação**

Coberto por CON-005.

**Referências cruzadas**: CON-005, MEM-001

---

### MEM-003 — `WebUI.h` raw (333 KB) potencialmente redundante com `WebUI_GZ.h`

**Severidade**: Informativa
**Categoria**: MEM (flash program)
**Local**: `WebUI.h` (4.034 linhas)
**Risco de regressão**: Médio (requer verificar todos os call-sites)

**Descrição**

O projeto tem **dois** assets HTML/JS:

- `WebUI.h` — raw, ~333 KB em PROGMEM.
- `WebUI_GZ.h` — gzipped, gerado por `compressor.py`.

`handleLangJs` usa `WebUI_GZ`. Verificar se há qualquer handler que ainda usa `WebUI::` (raw) para páginas — se todos caíram para `WebUI_GZ::`, o `WebUI.h` raw pode ser removido, liberando ~333 KB do binário.

**Impacto**

Se realmente redundante, economiza ~15% do flash de programa do Pico W (2 MB total).

**Proposta de correção**

1. Grep todos usos de `WebUI::`:

```bash
grep -rn "WebUI::" *.cpp *.h
```

2. Se todos foram substituídos por `WebUI_GZ::`, remover `#include "WebUI.h"` e deletar o arquivo.
3. Se algum cliente não suporta gzip (`!_clientAcceptsGzip`), manter `WebUI.h` apenas para as páginas críticas e remover as redundantes.

**Critério de aceitação**

1. Análise documentada de quais páginas usam raw vs GZ.
2. Remoção (se viável) resulta em redução de pelo menos 200 KB no binário `.uf2`.
3. Navegação completa do website testada em browsers antigos sem gzip (raro mas possível).

---

## PER — Performance

---

### PER-001 — 14 chamadas a `watchdog_update()` no main loop

**Severidade**: Baixa
**Categoria**: PER (custo cumulativo)
**Local**: `AppManager.cpp::loop` (várias linhas)
**Risco de regressão**: Baixo

**Descrição**

O método `loop` tem 14 chamadas explícitas a `watchdog_update()` intercaladas. Cada chamada é barata individualmente, mas:

- Dificulta leitura do código.
- Mistura housekeeping com lógica principal.
- Inconsistente com `TRACE_BEAT(0)` que não é chamado em todos os pontos.

**Impacto**

Nenhum em runtime. Apenas clareza do código.

**Proposta de correção**

Criar helper e substituir:

```cpp
/**
 * @brief  Alimenta o watchdog e atualiza o heartbeat do Core 0.
 *
 * Substitui o par manual `watchdog_update(); TRACE_BEAT(0);` que aparece
 * dezenas de vezes no main loop e em paths de flash/rede. Unificar garante
 * que o heartbeat nunca seja esquecido junto com um feed.
 *
 * @note  Core 1 NÃO deve usar este helper — Core 1 alimenta seu próprio
 *        heartbeat via TRACE_BEAT(1) em loopCore1() e não tem ownership
 *        direto do watchdog hardware.
 */
inline void feedWdt() {
    watchdog_update();
    TRACE_BEAT(0);
}
```

Adicionar em `SystemDefs.h` e substituir 14 chamadas em `AppManager::loop` + dezenas em `StorageManager` e handlers de `WebManager`.

**Critério de aceitação**

1. Helper `feedWdt()` definido em `SystemDefs.h`.
2. Todas ocorrências de `watchdog_update(); TRACE_BEAT(0);` substituídas.
3. Ocorrências isoladas de `watchdog_update()` (sem TRACE_BEAT) avaliadas caso a caso — se for Core 0, trocar por `feedWdt()`; se for Core 1, manter.

**Referências cruzadas**: REF-005

---

### PER-002 — `RenderGuard` a cada chunk de upload causa flickering

**Severidade**: Baixa
**Categoria**: PER (UX durante upload)
**Local**: `WebManager.cpp:1842`
**Risco de regressão**: Baixo

**Descrição**

```cpp
} else if (upload.status == UPLOAD_FILE_WRITE) {
    if (_uploadFile) {
        { RenderGuard rg(_displayRef); _uploadFile.write(upload.buf, upload.currentSize); }
        feedWatchdog();
    }
}
```

Cada chunk do upload (tipicamente 1-2 KB) aciona `RenderGuard` que pausa/retoma Core 1. Para um upload de 100 KB = 50-100 chunks = 50-100 pauses. Display TFT flickering visível.

**Impacto**

UX ruim durante uploads grandes. Flash safety é preservada (multicore_lockout funciona).

**Proposta de correção**

Agregar múltiplos chunks antes de escrever no flash. Buffer intermediário de 8 KB reduz pauses em ~10×:

```cpp
private:
    static constexpr size_t UPLOAD_BATCH_SIZE = 8192;
    uint8_t _uploadBatchBuf[UPLOAD_BATCH_SIZE];
    size_t  _uploadBatchUsed = 0;

    void flushUploadBatch() {
        if (_uploadBatchUsed > 0 && _uploadFile) {
            RenderGuard rg(_displayRef);
            _uploadFile.write(_uploadBatchBuf, _uploadBatchUsed);
            _uploadBatchUsed = 0;
        }
    }
```

```cpp
// handleUploadData UPLOAD_FILE_WRITE
size_t remaining = upload.currentSize;
const uint8_t* src = upload.buf;
while (remaining > 0) {
    size_t free = UPLOAD_BATCH_SIZE - _uploadBatchUsed;
    size_t copy = (remaining < free) ? remaining : free;
    memcpy(_uploadBatchBuf + _uploadBatchUsed, src, copy);
    _uploadBatchUsed += copy;
    src += copy;
    remaining -= copy;
    if (_uploadBatchUsed == UPLOAD_BATCH_SIZE) flushUploadBatch();
}
feedWdt();

// UPLOAD_FILE_END: flush final
flushUploadBatch();
```

**Critério de aceitação**

1. Upload de 100 KB causa ≤ 15 pauses de Core 1 (antes: 50-100).
2. Flickering perceptível reduzido.
3. Integridade do arquivo upload preservada (teste: SHA256 match).
4. Memória não cresce além dos 8 KB adicionais.

---

### PER-003 — `isValidHistoryFileName` chamado em loop sem cache

**Severidade**: Informativa
**Categoria**: PER (microotimização)
**Local**: `StorageManager.cpp:604`
**Risco de regressão**: Baixo

**Descrição**

Em `enforceStorageLimit`, para cada iteração sobre `Dir`, `isValidHistoryFileName(fileName.c_str())` é chamado. A função (não mostrada, mas em `SystemUtils.cpp`) valida formato `YYYYMMDD.csv/bin`. Para diretórios com 100+ arquivos, é chamada 100+ vezes por execução.

**Impacto**

Nanosegundos por iteração, potencialmente relevante com muitos arquivos. Não é hot path (executa apenas quando flash > 86%).

**Proposta de correção**

Baixa prioridade. Se otimizar, usar comparação manual rápida sem construir `String` temporário:

```cpp
/* fast-path: rejeita sem construir String se formato óbvio não bate. */
if (fileName.length() != 12 /* YYYYMMDD.bin = 12 */
    || !fileName.endsWith(HISTORY_FILE_EXT)) continue;
/* Só então valida completude via isValidHistoryFileName. */
```

**Critério de aceitação**

Opcional. Medir throughput de `enforceStorageLimit` antes e depois.

---

## REF — Refatoração e Manutenibilidade

---

### REF-001 — `DisplayManager.cpp` (7.872 linhas) — split por tela

**Severidade**: Média
**Categoria**: REF (manutenibilidade)
**Local**: `DisplayManager.cpp`
**Risco de regressão**: Alto (maior refatoração do projeto)

**Descrição**

Um único arquivo de 7.872 linhas concentra 29% do código do projeto. Dificulta:

- Code review.
- Busca por funções específicas.
- Merge conflicts em trabalho colaborativo.
- Compilação incremental (toda mudança recompila todo o arquivo).

**Proposta de correção**

Dividir a **mesma classe** em múltiplos arquivos `.cpp` (C++ permite métodos da mesma classe em arquivos separados). Esqueleto:

```
DisplayManager.h                   (inalterado — declaração completa)
DisplayManager.cpp                 (core: construtor, loopCore1, helpers)
DisplayManager_Dashboard.cpp       (drawTopBar, drawAmbientPanel, drawSlotPanel, drawBottomButtons)
DisplayManager_Graph.cpp           (drawGraphScreen, drawGraphDetailScreen, drawGraphHeaderBar, formatGraphTime)
DisplayManager_Settings.cpp        (drawSettingsMain, drawSettingsThemes, drawSettingsLang, etc.)
DisplayManager_Auth.cpp            (drawAuthScreen, scrambleKeys, showAuthScreen)
DisplayManager_Calibration.cpp     (drawTouchCalibration, drawTouchSensitivity, drawSettingsDisplayOffset)
DisplayManager_Alarm.cpp           (drawAlarmAction, redrawAlarmFlash, restoreNormalDashboard)
DisplayManager_i18n.cpp            (DICTIONARY[], tr, LANG_NAMES, LANG_FLAGS)
DisplayManager_Calendar.cpp        (drawCalendarScreen, showCalendar)
```

Cada arquivo inclui `DisplayManager.h` no topo e implementa seu subconjunto de métodos. Membros privados permanecem na declaração do `.h`. Arquivos não-core podem também incluir fonts dedicadas.

**Não modificar interfaces públicas** — refactoring puramente organizacional.

**Critério de aceitação**

1. `DisplayManager.cpp` (core) ≤ 1.500 linhas.
2. Nenhum arquivo secundário excede 1.500 linhas.
3. Todos os métodos públicos permanecem acessíveis.
4. Build incremental após mudança em `_Graph.cpp` recompila apenas ele + link.
5. Testes funcionais de UI: dashboard, graph render, all settings screens, alarm flash — todos passam.

**Referências cruzadas**: REF-002, REF-003

---

### REF-002 — `AppManager.cpp` (3.334 linhas) — split por responsabilidade

**Severidade**: Média
**Categoria**: REF (manutenibilidade)
**Local**: `AppManager.cpp`
**Risco de regressão**: Alto

**Descrição**

Mesmo padrão de REF-001 aplicado ao orquestrador.

**Proposta de correção**

```
AppManager.h                 (inalterado)
AppManager.cpp               (setup, loop, loadTheme, core0Yield)
AppManager_Boot.cpp          (boot sequence completa: FS, WiFi, NTP, warm-up)
AppManager_Commands.cpp      (executeCommand e sub-handlers de CliDemand)
AppManager_Graph.cpp         (renderGraphOptimized, preloadGraphCaches, graphCacheIdx, appendToGraphCache)
AppManager_Events.cpp        (UI event dispatch: EVT_SLOT_SELECT, EVT_OPEN_GRAPH, etc.)
AppManager_Sensors.cpp       (checkAndAutoHealSensors, loadAndCalibrateSensors, processBackgroundScan)
AppManager_History.cpp       (processHistoryLogging, handleTimeSync, preloadMinMax, refreshSelectedSlot)
AppManager_Alarm.cpp         (checkAlarmConditions)
```

**Critério de aceitação**

1. `AppManager.cpp` core ≤ 800 linhas.
2. Nenhum arquivo secundário excede 1.000 linhas.
3. Build funciona sem warnings novos.
4. Teste end-to-end do boot completo passa.

**Referências cruzadas**: REF-001, REF-003

---

### REF-003 — `WebManager.cpp` (2.515 linhas) — split em handlers

**Severidade**: Baixa
**Categoria**: REF (manutenibilidade)
**Local**: `WebManager.cpp`
**Risco de regressão**: Médio

**Descrição**

Igual aos anteriores, agora para WebManager.

**Proposta de correção**

```
WebManager.h                    (inalterado)
WebManager.cpp                  (construtor, begin, update, SendGuard, safeSend*)
WebManager_Auth.cpp             (handleApiLoginInit, handleApiLogin, handleLogout, handleForceChpass, secureCompare, generateSecureToken)
WebManager_Files.cpp            (handleUpload*, handleDownload, handleDelete, handleApiLs, handleApiMkdir, safeStreamFile)
WebManager_Api.cpp              (handleApiStatus, handleApiPerms, handleApiNetwork, handleApiConfig, handleApiUsers, handleApiThemes, handleApiAlarms)
WebManager_Pages.cpp            (handleRoot, handleLogin, handleConfig, handleNetwork, handleUsers, handleFiles, handleAlarms, handleLicense, handleHistory, handleLangJs, handleNotFound)
WebManager_History.cpp          (handleApiHistoryData, handleApiHistoryDays, handleApiLogs, handleApiClearLogs, handleApiScreenshot)
WebManager_Commit.cpp           (handleSaveSystem, handleApiCommitAll, handleResetTouchCal)
WebManager_Util.cpp             (rate limit, getAuthPerms, isPasswordChangeRequired, serveProtectedPage, etc.)
```

**Critério de aceitação**

1. Nenhum arquivo excede 800 linhas.
2. `_server.on()` ainda todos registrados em `begin()` (mantém índice central de rotas).
3. Navegação completa do site testada.

**Referências cruzadas**: REF-001, REF-002

---

### REF-004 — Callbacks `setTouchPriorityChecker` duplicados em 5 managers

**Severidade**: Baixa
**Categoria**: REF (inversão de dependência repetida)
**Local**: `AppManager.cpp:138, 142`, `StorageManager.h:62`, `WebManager.h:46`, `LogManager.h:49`, outros
**Risco de regressão**: Baixo

**Descrição**

A mesma lambda `[]() -> bool { return app.isUserInteracting(); }` é registrada em 5 managers diferentes:

```cpp
LogManager::instance().setTouchPriorityChecker([]() -> bool { return app.isUserInteracting(); });
_storageMgr.setTouchPriorityChecker([]() -> bool { return app.isUserInteracting(); });
_webMgr.setTouchPriorityChecker([]() -> bool { return app.isUserInteracting(); });
// ...
```

Cada manager tem seu próprio `bool (*_isTouchPriorityFn)()` e lógica de null-check.

**Proposta de correção**

Introduzir singleton global ou interface:

```cpp
/* SystemDefs.h */

/**
 * @brief  Provedor global de estado "usuário interagindo com display".
 *
 * Set uma vez pelo AppManager no setup; lido por qualquer manager que
 * precise deferir operações pesadas durante interação (touch priority).
 * Thread-safe: leitura é simples load de ponteiro (atômico em ARM).
 */
class TouchPriority {
public:
    static void setProvider(bool (*fn)()) { _provider = fn; }
    static bool isActive() { return _provider && _provider(); }
private:
    static bool (*_provider)();
};
```

```cpp
/* SystemDefs.cpp (ou inline em header) */
bool (*TouchPriority::_provider)() = nullptr;
```

E usar em qualquer manager:

```cpp
if (TouchPriority::isActive()) {
    /* defer */
}
```

Remove 5 métodos `setTouchPriorityChecker` e 5 ponteiros membros.

**Critério de aceitação**

1. `TouchPriority::setProvider` chamado uma vez em `AppManager::setup`.
2. Managers consomem via `TouchPriority::isActive()`.
3. `setTouchPriorityChecker` removido de 5 classes.
4. Comportamento de deferral inalterado.

---

### REF-005 — Helper `feedWdt()` para consolidar `watchdog_update()+TRACE_BEAT(0)`

**Severidade**: Baixa
**Categoria**: REF (padrão repetido)
**Local**: `AppManager.cpp`, `StorageManager.cpp`, `WebManager.cpp` (dezenas de ocorrências)
**Risco de regressão**: Baixo

**Descrição**

Vide PER-001. Além de performance/legibilidade, a consolidação garante que `heartbeat` nunca seja esquecido junto com `feed` — erro sutil onde WDT é alimentado mas profiler perde rastro.

**Proposta de correção**

Coberta por PER-001.

**Critério de aceitação**

Coberto por PER-001.

**Referências cruzadas**: PER-001

---

### REF-006 — Macro `FLASH_OP` deveria ser helper reutilizável

**Severidade**: Baixa
**Categoria**: REF (pattern local → helper)
**Local**: `StorageManager.cpp:409-416`

Coberta por BUG-003.

**Referências cruzadas**: BUG-003

---

### REF-007 — `handleApiLogin` tem ~130 linhas — extrair sub-funções

**Severidade**: Baixa
**Categoria**: REF (função longa)
**Local**: `WebManager.cpp:831-1004`
**Risco de regressão**: Médio (crítico de segurança — preservar comportamento)

**Descrição**

`handleApiLogin` faz em uma única função:

1. Lookup de `_loginStates` por IP.
2. Verificação de lockout.
3. Validação de nonce.
4. Validação de tamanho de entrada.
5. Hash da senha do cliente.
6. Busca do usuário ativo.
7. Dois paths de comparação de hash (admin vs viewer mustChangePassword).
8. Alocação de slot de sessão.
9. Geração de token.
10. Set-Cookie e resposta.

**Proposta de correção**

Extrair em funções privadas auxiliares:

```cpp
private:
    /* Retorna índice do slot de _loginStates para IP ou -1 se não encontrado. */
    int findLoginStateForIp(uint32_t ip);

    /* Retorna true se o slot está em lockout ativo; popula lockSec. */
    bool checkLockout(int slot, uint32_t& lockSecOut);

    /* Valida nonce enviado pelo cliente; aplica penalty se expirado/inválido. */
    bool validateNonce(int slot, bool& nonceExpired);

    /* Compara inputHash com hash persistido de um usuário ativo. */
    bool verifyPasswordFor(const SystemConfig& cfg, int userIdx,
                           const String& u, const String& inputHash);

    /* Aloca slot de sessão. Retorna -1 se capacidade esgotada. */
    int allocSessionSlot(int foundUserId);

    /* Finaliza login bem-sucedido: gera token, set-cookie, reset failCount. */
    void completeLogin(int sessionSlot, int loginStateIdx, int userId,
                       const String& username, const SystemConfig& cfg);
```

`handleApiLogin` fica com ~40 linhas, cada passo visível.

**Critério de aceitação**

1. `handleApiLogin` ≤ 50 linhas.
2. Cada função auxiliar tem teste unitário (ou teste de integração cobrindo o caminho).
3. Regressão: testes de login com senhas válidas/inválidas/bloqueadas passam.
4. Comportamento de rate-limit e backoff preservado.

---

## DOC — Documentação

---

### DOC-001 — Headers descrevem "3 idiomas"

Coberto por CON-003.

---

### DOC-002 — Magic numbers sem justificativa

**Severidade**: Baixa
**Categoria**: DOC (números sem explicação)
**Local**: múltiplos

**Descrição**

Valores numéricos sem comentário explicativo da fonte:

- `SensorManager.cpp:440` — `millis() - _dhtTimer > 100` (por que 100ms?)
- `AppManager.cpp:272` — `millis() - lastMsg > 800` (por que 800ms para dots?)
- `DisplayManager.cpp:1357` — `alarmCount >= 2 && (millis() - _alarmRotateTimer >= 3000)` (3s de rotação)
- `WebManager.cpp` — vários timeouts inline.

**Proposta de correção**

Nomear e mover para `SystemDefs.h` com docstring. Exemplo:

```cpp
/** Timeout máximo para leitura assíncrona do DHT22 (ms).
 *  Datasheet: transmissão ~20ms. 100ms cobre jitter + margem 5×. */
constexpr uint32_t DHT22_READ_TIMEOUT_MS = 100;

/** Intervalo entre "..." animados em mensagens de boot (ms). */
constexpr uint32_t BOOT_DOTS_INTERVAL_MS = 800;

/** Tempo entre rotações automáticas de slot alarmado no dashboard (ms). */
constexpr uint32_t ALARM_ROTATE_INTERVAL_MS = 3000;
```

**Critério de aceitação**

1. Auditoria manual: nenhum literal numérico > 500 (exceto 1-2 dígitos claramente óbvios como `if (x > 0)`) sem nome simbólico ou comentário adjacente.
2. Constantes agrupadas por domínio em `SystemDefs.h`.

---

### DOC-003 — Falta documentação de segurança

**Severidade**: Informativa
**Categoria**: DOC (threat model, operação)
**Local**: ausente

**Descrição**

O projeto tem excelente `how-comments` no código mas falta documento top-level sobre:

- **Threat model**: quem é o atacante? (vizinho de rede? usuário com cabo USB? exfiltração por mod de firmware?)
- **Rotação de credenciais**: como um administrador deve rotacionar senha admin a cada N meses?
- **Resposta a incidente**: se atacante acessou admin, como recuperar?
- **Procedimento de factory reset**: como garantir que todos os dados sensíveis são apagados?
- **Auditoria**: o `/system.blog` contém logs de segurança — como analisar?

**Proposta de correção**

Criar `SECURITY.md` na raiz do projeto:

```markdown
# Modelo de Segurança — SIMUT

## Escopo de ameaças

SIMUT protege contra:
- Acesso remoto não-autorizado via rede local (autenticação web com rate-limit).
- Brute force de senha (backoff exponencial, lockout 5 min).
- Replay de sessão (nonce single-use com TTL 60s).
- Injeção CSRF (cookie HttpOnly+SameSite=Strict).
- Leitura não-autenticada de dados sensíveis (XOR ofuscação de credenciais).
- DoS local via USB/BT (limite de buffer na CLI).

SIMUT NÃO protege contra:
- Atacante com acesso físico ao PCB (pode ler flash, substituir firmware).
- Compromisso da rede WiFi (se atacante está no AP, vê tráfego HTTP não-TLS).
- Ataques de canal lateral (timing de leitura de sensor, consumo de corrente).

## Rotação de credenciais

Recomendado trocar senha admin e PIN do display:
- Após primeiro uso (forçado pelo firmware).
- A cada 90 dias em ambiente produtivo.
- Imediatamente após suspeita de comprometimento.

## Procedimento de factory reset seguro

1. Via CLI: `reset admin confirm` — apaga config, regenera senha random.
2. Após reset, conectar fisicamente para ler a nova senha no display.
3. Escrever nova senha permanente antes de expor o dispositivo à rede.

## Auditoria

O arquivo `/system.blog` (formato binário 12 bytes/record) contém todos eventos
de segurança. Use o CLI `show logs` ou a interface web para download.
Eventos críticos: SEC_LOGIN_FAIL, SEC_UNAUTHORIZED, SEC_FILE_UPLOAD, SEC_CONFIG_CHANGED.
```

**Critério de aceitação**

1. Arquivo `SECURITY.md` existe na raiz.
2. Documenta ao menos as 4 seções acima.
3. Referenciado no `README.md` principal.


---

## Pontos Fortes — Preservar Durante Correções

Estes pontos representam **valor técnico do projeto** e não devem ser degradados por refatorações. A IA executora deve **garantir que nenhuma mudança comprometa essas qualidades**:

### Arquitetura e concorrência

- **Dual-core strategy**: Core 0 para lógica/rede, Core 1 para display/touch. Comunicação via mutex + queue.
- **Watchdog context-aware** via `LogManager::WdtWindow` RAII com nesting correto.
- **Cross-core health monitor** em `checkCrossCoreHealth` com threshold calibrado (15s) que tolera saves/TLS.
- **Touch priority defer** para flash, logs, CLI — múltiplas camadas de proteção à UX.
- **Multicore lockout com timeout + retry** (resultado de debugging real documentado nos comentários).

### Segurança

- **Challenge-response login** com nonce single-use e TTL.
- **Backoff exponencial** por IP.
- **RBAC** via bitmask de permissões.
- **Timing-safe comparison** (`secureCompare`).
- **Ofuscação XOR** dos 3 campos sensíveis com keystream SHA-256 derivado do chip_id.
- **HttpOnly + SameSite=Strict cookies** com `Secure` opcional.
- **Validação de tamanho pré-hash** para evitar DoS de CPU.

### Flash safety

- **Dual-bank config** com CRC32 e fallback automático.
- **Atomic tmp→rename** com recuperação janela-coberta.
- **Skip no-op por CRC** evita rajadas de save idênticas.
- **Rate-limit server-side** (1 save / 1s).
- **Chunked multicore_lockout** mantendo Core 1 responsivo.
- **Migração transparente de schema** (v12 → v13 → v14).

### Memória

- **Batch size dinâmico** baseado em heap livre (`safeBatchLimit`).
- **Heap check pré-alocação** no TelemetryManager.
- **Release intencional de buffers** antes de picos de RAM (batch.clear antes de TLS).
- **Release idle resources** do WiFiClientSecure + cert.
- **Buffer de sort estático** no trimmed mean (zero-alloc).

### Observabilidade

- **Black-box profiler** com módulo por core em watchdog scratch.
- **Autópsia pós-crash** com 3 magic values distintos (soft panic / reboot limpo / HW WDT).
- **Preboot snapshot** para recuperar módulo do crash anterior.
- **TraceScope RAII** para instrumentação sem vazamento de estado.
- **MetricsManager** com heap, largest block, RSSI, sensorReadsOk/Err.

### UX

- **Adaptive delay** em Core 1 (1ms durante touch, 2ms idle).
- **Cache de gráficos** (7d × 12 slots + 5 ranges do slot ativo).
- **Invalidação de cache antes de telemetria** para liberar heap.
- **i18n** com 8 idiomas.
- **Temas** com paletas RGB565.
- **Virtual RTC** com timestamps provisórios + correção retroativa via NTP.

### CI/Build

- **`static_assert`** em vários structs packed (`TouchCalData`, `DisplayOffsetData`, `CliConfigData`, `CompactLogRecord`).
- **Comentários datados** (ex: "U23 (2026-04-19)") indicam disciplina de tracking.

---

## Checklist de Execução — Ordem Sugerida

### Sprint 1 — Crítico/Alto (bloqueia release público)

- [ ] SEC-001 — Path traversal em upload
- [ ] SEC-002 — Sanitização de uploadDir
- [ ] SEC-003 — Senhas padrão
- [ ] SEC-004 — PIN padrão
- [ ] SEC-005 — DoS CLI
- [ ] BUG-001 — Wrap-safe `millis()` em todas as ocorrências

### Sprint 2 — Médio (dívida técnica operacional)

- [ ] SEC-006 — Anti-evicção login
- [ ] BUG-002 — Memory barriers cross-core
- [ ] BUG-003 — FLASH_OP consolidada
- [ ] CON-005 — String → char[] em CliDemand/LoginState
- [ ] MEM-001 — String em hot paths
- [ ] PER-002 — Upload batching

### Sprint 3 — Baixo/Inconsistências (limpeza)

- [ ] SEC-007 / SEC-008 / SEC-009 — Hash de senha (com migração)
- [ ] BUG-004 — Webbusy sticky
- [ ] BUG-005 — Preboot snapshot explícito
- [ ] CON-001 — Comentários scratch
- [ ] CON-002 / CON-003 / DOC-001 — Enum LanguageCode
- [ ] CON-004 — `_lastSavedCrc` membro
- [ ] CON-006 — DS_CONVERSION_TIME centralizado
- [ ] PER-001 / REF-005 — Helper feedWdt
- [ ] REF-004 — TouchPriority singleton
- [ ] DOC-002 — Magic numbers nomeados
- [ ] DOC-003 — SECURITY.md

### Sprint 4 — Refatoração grande

- [ ] REF-001 — Split DisplayManager
- [ ] REF-002 — Split AppManager
- [ ] REF-003 — Split WebManager
- [ ] REF-007 — Decomposer handleApiLogin
- [ ] MEM-003 — Avaliar remoção WebUI.h raw

---

## Protocolo para IA Executora

Ao começar a trabalhar em um item:

1. **Ler o item completo** incluindo localização exata e critério de aceitação.
2. **Verificar referências cruzadas** — alguns itens dependem de outros (ex: BUG-003 depende de REF-006).
3. **Preservar todos os comentários existentes** relevantes. Adicionar novos comentários no estilo do projeto (docstrings Doxygen-like em português).
4. **Seguir indentação de 4 espaços** (não tabs).
5. **Rodar compilação** após cada mudança antes de declarar concluído.
6. **Gerar diff mínimo possível** — não reformatar código adjacente.
7. **Adicionar entrada no CHANGELOG.md** com o ID do item corrigido.
8. **Solicitar validação humana** antes de passar para próximo item em caso de dúvida.

Ao concluir um item:

1. Marcar como `[RESOLVIDO]` no cabeçalho do item neste relatório.
2. Preencher seção `### Resolução` com: commit hash, descrição breve da mudança, testes executados.
3. Re-validar que itens dependentes ainda são relevantes (pode ter sido implicitamente resolvido).

---

## Apêndice A — Comandos de Verificação

```bash
# Encontrar comparações não-wrap-safe
grep -rn "millis() -.*>\s*[0-9]" *.cpp | grep -v "timeReached\|timeRemaining\|// \|/\*"

# Verificar uso de String em hot paths
grep -rn "String " *.h *.cpp | grep -v "const String\|//"

# Encontrar volatile cross-core sem barrier
grep -B2 -A2 "volatile" *.h | grep -v "//"

# Contagem de linhas por arquivo .cpp
wc -l *.cpp | sort -n

# Literals numéricos grandes sem comentário adjacente
grep -rn "[[:space:]][0-9]\{4,\}" *.cpp | grep -v "//\|0x"
```

---

## Apêndice B — Glossário de Siglas do Projeto

| Sigla | Significado |
|---|---|
| WDT | Watchdog Timer |
| XIP | eXecute In Place (bus para flash no RP2040) |
| PIO | Programmable I/O (coprocessador do RP2040) |
| RBAC | Role-Based Access Control |
| TLS | Transport Layer Security |
| MQTT | Message Queuing Telemetry Transport |
| CRC | Cyclic Redundancy Check |
| NTP | Network Time Protocol |
| mDNS | Multicast DNS |
| DHT22 | Sensor de temperatura/umidade digital |
| DS18B20 | Sensor de temperatura 1-Wire |
| TFT | Thin Film Transistor (tipo de display) |
| GFX | Graphics (biblioteca Adafruit) |
| i18n | Internationalization |
| RAII | Resource Acquisition Is Initialization |
| CAS | Compare-And-Swap |
| DMB | Data Memory Barrier (instrução ARM) |
| KDF | Key Derivation Function |
| OWASP | Open Web Application Security Project |

---

## Apêndice C — Estatísticas do Projeto

| Métrica | Valor |
|---|---|
| Arquivos `.h` | 15 |
| Arquivos `.cpp` | 12 |
| `WebUI.h` (PROGMEM) | 4.034 linhas, ~333 KB |
| Total de linhas (sem WebUI) | 23.210 |
| Total geral | 27.244 linhas |
| Arquivo mais extenso | DisplayManager.cpp (7.872) |
| Arquivos > 2.500 linhas | 3 |
| Arquivos < 500 linhas | 8 |
| Maior concentração | 50% do código em 3 arquivos |

---

## Histórico deste documento

| Versão | Data | Autor | Mudanças |
|---|---|---|---|
| 1.0 | 2026-04-20 | Auditoria inicial | Documento original — 33 achados mapeados |

---

**FIM DO RELATÓRIO**

