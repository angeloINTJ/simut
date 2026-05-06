# Plano de Implementação — Sistema de Atualização OTA para Raspberry Pi Pico W

> **Documento dirigido a uma IA implementadora (Claude Code, Opus 4.7) sob supervisão humana.**  
> A IA tem acesso ao código-fonte do projeto. O humano executa todos os testes em hardware, autoriza transições entre fases e aprova mudanças destrutivas. **Nenhuma fase deve ser iniciada sem aprovação explícita do humano da fase anterior.**

---

## 0. Sumário Executivo

Implementar um mecanismo de atualização OTA (Over-The-Air) para Raspberry Pi Pico W que opera dentro de uma flash de 2 MB com **um único slot de aplicação de 1 MB** (a app atual ocupa ~99% desse slot). A solução utiliza:

1. **Self-flashing sem bootloader separado** — a aplicação carrega o aplicador de update em SRAM e se sobrescreve.
2. **Compressão gzip do firmware** — o binário (~1015 KB) é comprimido (~500–700 KB) antes do upload, cabendo na área de staging.
3. **Backup obrigatório do estado** — antes de qualquer destruição, o usuário baixa um arquivo de backup que contém todas as configurações, atrelado ao ID único do chip.
4. **Validação pré-destrutiva rigorosa** — o staging é integralmente verificado (CRC, descompressão dry-run) antes que qualquer setor da flash seja apagado.
5. **Recuperação documentada via BOOTSEL** — falhas catastróficas (ex.: queda de energia durante apagamento) caem para procedimento manual via USB. **Não há promessa de recuperação automática para falhas pós-destruição.**

A LittleFS é **destruída a cada update** e recriada após o boot. As configurações são repopuladas pelo navegador (que mantém cópia em IndexedDB durante o processo) ou via upload manual do arquivo de backup.

---

## 1. Contexto e Premissas a Verificar

### 1.1 Hardware-alvo
- **Placa:** Raspberry Pi Pico W (RP2040 + CYW43439, 2 MB QSPI flash, 264 KB SRAM).
- **Particionamento atual:** 1 MB para sketch + 1 MB para LittleFS (configurado via Tools → Flash Size na Arduino IDE).

### 1.2 Toolchain
- **IDE:** Arduino IDE 2.x (Ubuntu).
- **Core:** [arduino-pico](https://github.com/earlephilhower/arduino-pico) (Earl Philhower).
- **Editor para implementação:** Claude Code com API Opus 4.7 (`claude-opus-4-7`), executado dentro da pasta do projeto.
- **Bibliotecas esperadas no projeto atual:** `WiFi`, `WebServer` (ou `ESPAsyncWebServer`), `LittleFS`. **A IA implementadora deve verificar e documentar quais estão em uso na Fase 0.**

### 1.3 Premissas sobre o estado atual do projeto
A IA implementadora **deve assumir que NÃO conhece** os seguintes pontos e os documentará durante a Fase 0:

- Estrutura de diretórios do sketch (caminho do `.ino` principal, módulos, libs locais).
- Quais arquivos vivem na LittleFS (configs, temas, traduções, assets web).
- Esquema atual de versionamento (existe um `version.h`? `#define FIRMWARE_VERSION`?).
- Qual servidor web está em uso e como suas rotas estão organizadas.
- Como a interface web atual gerencia upload de arquivos para a LittleFS.
- Se há uso de **dois cores** (`setup1`, `loop1`) — afeta o aplicador em SRAM.

### 1.4 Verificações obrigatórias antes de iniciar a Fase 1

A IA implementadora produzirá `BASELINE.md` com respostas a:

1. Tamanho do `.bin` atual com flags de produção (`-Os -ffunction-sections -fdata-sections -Wl,--gc-sections`).
2. Tamanho da `.bin` resultante após adicionar **stub vazio** das libs gzip (`uzlib`) — para garantir que ainda cabe nos 1 MB.
3. Versão exata do core arduino-pico em uso.
4. Conteúdo atual da LittleFS, listado por nome e tamanho.
5. Endereços efetivos das partições (sketch, FS) em flash, conforme configurados na IDE.

---

## 2. Decisões Arquiteturais (ADR)

### ADR-001 — Self-flashing sem bootloader separado
**Decisão:** A aplicação contém o aplicador de update embutido. Em tempo de update, copia o aplicador para SRAM e salta para lá.  
**Alternativas rejeitadas:**
- *Bootloader prefixado em flash:* não cabe (firmware atual usa 99% do slot de 1 MB).
- *Dual-bank A/B:* exigiria 2 MB só de código.

### ADR-002 — LittleFS sacrificada a cada update
**Decisão:** A área da LittleFS é integralmente apagada para servir de staging. Após o update, é reformatada vazia.  
**Mitigação:** Backup obrigatório antes do update + restore automatizado pelo navegador.  
**Alternativa rejeitada:** redimensionar partição em runtime — risco e complexidade desproporcionais.

### ADR-003 — Backup atrelado ao ID único do chip
**Decisão:** O arquivo de backup contém o ID único do RP2040 (lido via `flash_get_unique_id()`) e só pode ser restaurado no mesmo dispositivo.  
**Razão:** evita restauração cruzada acidental que destruiria configurações em outro equipamento.

### ADR-004 — Compressão gzip do firmware
**Decisão:** O cliente envia `.bin.gz`; a aplicação descomprime durante o self-flash.  
**Razão:** firmware bruto de ~1015 KB excede o slot de staging útil; gzip tipicamente resulta em 50–65% do tamanho original.  
**Biblioteca:** `uzlib` (~2 KB de código, sem alocação dinâmica).

### ADR-005 — Navegador como persistência temporária de configs
**Decisão:** Durante o update, o JS da página de admin mantém cópia das configurações em IndexedDB. Após reboot, faz upload de volta.  
**Razão:** evita reservar região protegida em flash (espaço escasso) e elimina necessidade de área não-volátil dedicada.  
**Lifeline:** o arquivo de backup baixado é a salvaguarda caso a aba do navegador seja fechada.

---

## 3. Layout de Memória Flash

```
Endereço (flash absoluto)        Tamanho      Função
──────────────────────────────  ──────────  ─────────────────────────────────────
0x10000000 ─ 0x100FFFFF         1024 KB     Slot da aplicação (firmware ativo)
                                            └─ inclui boot2 nos primeiros 256 B
0x10100000 ─ 0x101FEFFF         1020 KB     Área dual-uso:
                                            └─ Modo normal: LittleFS
                                            └─ Modo update: staging do .bin.gz
0x101FF000 ─ 0x101FFFFF            4 KB     Setor de metadata/flag de update
                                            (setor único, alinhado em 4 KB)
```

Constantes a definir em `ota_layout.h`:
```cpp
// Endereços relativos a XIP_BASE (0x10000000)
#define OTA_APP_OFFSET           (0u)
#define OTA_APP_MAX_SIZE         (1024u * 1024u)
#define OTA_STAGING_OFFSET       (1024u * 1024u)
#define OTA_STAGING_MAX_SIZE     (1020u * 1024u)
#define OTA_METADATA_OFFSET      (OTA_STAGING_OFFSET + OTA_STAGING_MAX_SIZE)
#define OTA_METADATA_SIZE        (4u * 1024u)
```

---

## 4. Fluxo End-to-End

```
USUÁRIO                NAVEGADOR                  PICO W
   │                       │                         │
   │ Abre /admin            │                         │
   │ ─────────────────────▶ │                         │
   │                       │ GET /api/configs        │
   │                       │ ──────────────────────▶ │
   │                       │ ◀────────────────────── │ JSON com tudo
   │                       │ Salva em IndexedDB      │
   │                       │                         │
   │ Clica "Backup"         │                         │
   │ ─────────────────────▶ │ GET /api/backup         │
   │                       │ ──────────────────────▶ │
   │ Download .bkp          │ ◀────────────────────── │ stream binário
   │ ◀───────────────────── │                         │
   │                       │                         │
   │ Seleciona .bin.gz      │                         │
   │ Clica "Atualizar"      │                         │
   │ ─────────────────────▶ │                         │
   │                       │ POST /api/firmware/begin│
   │                       │ ──────────────────────▶ │ Lê configs, unmount LFS,
   │                       │                         │ apaga staging
   │                       │ ◀────────────────────── │ ACK
   │                       │ POST chunks (8 KB cada) │
   │                       │ ──────────────────────▶ │ Grava em staging,
   │                       │                         │ atualiza CRC
   │                       │ ◀────────────────────── │ ACK por chunk
   │                       │                         │
   │                       │ POST /api/firmware/commit
   │                       │ ──────────────────────▶ │ Pré-validação:
   │                       │                         │  - CRC do staging
   │                       │                         │  - Magic do .bin.gz
   │                       │                         │  - Dry-run gzip
   │                       │                         │  - CRC do output
   │                       │ ◀────────────────────── │ OK / FALHA detalhada
   │                       │                         │
   │                       │ POST /api/firmware/apply│
   │                       │ ──────────────────────▶ │ Grava metadata,
   │                       │                         │ copia aplicador p/ SRAM,
   │                       │                         │ desabilita IRQs,
   │                       │                         │ salta p/ aplicador
   │                       │ ◀╳╳ desconexão ╳╳╳╳╳╳   │ [Aplicador roda em SRAM:
   │                       │                         │  apaga slot, decomp,
   │                       │                         │  grava, watchdog reset]
   │                       │                         │
   │                       │ Polling em IP do Pico   │ [novo firmware sobe,
   │                       │ (timeout 60s)           │  detecta flag fresh-boot,
   │                       │ ──────────────────────▶ │  reformata LFS]
   │                       │ ◀────────────────────── │ HTTP 200 / build novo
   │                       │                         │
   │                       │ POST /api/restore       │
   │                       │ (configs do IndexedDB)  │
   │                       │ ──────────────────────▶ │ Grava na LFS
   │                       │ ◀────────────────────── │ ACK
   │                       │                         │
   │ ◀───── Update concluído ─────────────────────── │
```

---

## 5. Especificação de Formatos de Dados

Todos os campos multi-byte são **little-endian** (nativo do RP2040).

### 5.1 Arquivo de backup (`.bkp`)

Cabeçalho fixo (40 bytes) seguido de payload variável:

```cpp
// Layout binário do arquivo .bkp — versão 1
struct __attribute__((packed)) BackupHeader {
    uint32_t magic;            // 0x31504B42 = "BKP1" em ASCII little-endian
    uint16_t schema_version;   // Inicia em 1; incrementa a cada quebra de formato
    uint16_t reserved0;        // Mantém alinhamento; preencher com 0
    uint8_t  chip_id[8];       // RP2040 unique ID via flash_get_unique_id()
    uint32_t firmware_version; // Versão do firmware que gerou o backup
    uint32_t timestamp;        // Unix epoch UTC; 0 se não houver RTC
    uint32_t payload_size;     // Bytes de payload após este cabeçalho
    uint32_t payload_crc32;    // CRC32 do payload (poly 0xEDB88320, init 0xFFFFFFFF)
    uint32_t header_crc32;     // CRC32 dos 36 bytes anteriores deste struct
};
// sizeof(BackupHeader) == 40 (verificar em compile-time com static_assert)
```

**Payload:** sequência de entradas TLV (Tipo-Tamanho-Valor):

```cpp
struct __attribute__((packed)) BackupEntry {
    uint16_t path_length;      // Tamanho do path (sem nul-terminator)
    uint32_t content_length;   // Tamanho do conteúdo do arquivo
    // char  path[path_length];      // Path do arquivo na LittleFS (ex: "/config.json")
    // uint8_t content[content_length]; // Conteúdo bruto do arquivo
};
```

A IA implementadora **deve gerar** um script Python `tools/verify_backup.py` que valida um arquivo `.bkp` (magic, CRCs, lista de arquivos). Esse script é entregue como artefato da Fase 1.

### 5.2 Payload do upload de firmware

O cliente envia um POST `/api/firmware/begin` com JSON:

```json
{
    "compressed_size": 524288,
    "uncompressed_size": 1038732,
    "compressed_crc32": "0xDEADBEEF",
    "uncompressed_crc32": "0xCAFEBABE",
    "build_version": "1.4.2",
    "build_timestamp": 1714867200
}
```

Em seguida, chunks binários (8 KB cada) via POST `/api/firmware/chunk?offset=N`. O Pico verifica que `offset` é contíguo e dentro de `compressed_size`.

Finalização: POST `/api/firmware/commit` (sem body). O Pico responde:
- `200 OK` com JSON `{"status":"validated"}` se passar todas as checagens.
- `400` ou `422` com JSON descrevendo qual checagem falhou.

### 5.3 Setor de metadata (4 KB em `OTA_METADATA_OFFSET`)

O setor inteiro é apagado (preenchido com `0xFF`) e apenas a primeira página (256 B) recebe gravação:

```cpp
struct __attribute__((packed)) UpdateMetadata {
    uint32_t magic;             // 0xA5C3F00D quando há update pendente válido
    uint32_t state;             // 1=COMMITTED, 2=APPLYING, 3=POST_BOOT, 4=COMPLETED
    uint32_t compressed_size;   // Bytes em staging
    uint32_t uncompressed_size; // Bytes esperados após decompressão
    uint32_t compressed_crc32;
    uint32_t uncompressed_crc32;
    uint32_t attempts;          // Tentativas de aplicar (anti-loop, máx 3)
    uint32_t reserved[57];      // Padding até 256 B; preencher com 0xFFFFFFFF
};
// sizeof(UpdateMetadata) == 256
```

---

## 6. Plano em Fases

> **Regra de ouro:** cada fase termina com critérios de aceitação **objetivos e testáveis pelo humano**. A IA implementadora **não** avança para a próxima fase sem confirmação explícita do supervisor humano de que os critérios foram atendidos em hardware real.

---

### Fase 0 — Descoberta e Baseline

**Objetivo:** estabelecer o estado atual do projeto e do binário antes de qualquer alteração funcional.

**Pré-requisitos:** acesso à pasta do projeto.

**Tarefas:**
1. Criar branch git `feature/ota-self-flash`.
2. Listar e documentar a árvore de arquivos do projeto em `BASELINE.md`.
3. Identificar o servidor web em uso e mapear as rotas existentes.
4. Listar todos os arquivos atualmente armazenados na LittleFS (ler de `data/` no sketch ou via interface web).
5. Compilar o projeto atual com flags de tamanho mínimo:
   ```
   -Os -ffunction-sections -fdata-sections -Wl,--gc-sections
   ```
   e registrar o tamanho do `.bin` resultante.
6. Adicionar `uzlib` ao projeto como **stub não-funcional** (apenas inclusões e símbolos vazios), recompilar e medir o delta de tamanho.
7. Documentar a versão do core arduino-pico e a configuração de Flash Size selecionada na IDE.
8. Verificar se o projeto usa `setup1`/`loop1` (segundo core).

**Artefatos produzidos:**
- `BASELINE.md` na raiz do projeto.
- Branch `feature/ota-self-flash` criada e pusheada.

**Critérios de aceitação:**
- [ ] `BASELINE.md` existe e responde a todas as perguntas da seção 1.4.
- [ ] Tamanho do `.bin` com `-Os` documentado e **comprovadamente abaixo de 1 MB**.
- [ ] Adição do stub do `uzlib` ainda mantém o `.bin` abaixo de 1 MB.
- [ ] O firmware compilado **funciona normalmente** em hardware (regressão zero).

**Teste em hardware obrigatório:** sim — flash do binário com `-Os` e validação de funcionamento normal. Se o `-Os` quebrar alguma funcionalidade, **parar e reportar** antes de prosseguir.

**Pontos de aprovação humana:** revisão do `BASELINE.md` + confirmação de que firmware com `-Os` continua funcional.

---

### Fase 1 — Formato e Geração de Backup

**Objetivo:** produzir arquivos `.bkp` válidos e baixáveis pela interface web.

**Pré-requisitos:** Fase 0 aprovada.

**Tarefas:**
1. Criar `src/ota/backup_format.h` com a definição de `BackupHeader` e `BackupEntry` exatamente conforme seção 5.1.
2. Adicionar `static_assert(sizeof(BackupHeader) == 40)` no header.
3. Implementar `src/ota/backup.cpp` com:
   - `bool ota_backup_stream(Print& out, uint32_t firmware_version);` — gera o backup em streaming, lendo todos os arquivos da LittleFS e calculando CRC à medida que escreve. **Não usar buffer em RAM para o arquivo todo.**
   - Função interna de CRC32 (poly 0xEDB88320) reutilizável em outras fases.
4. Adicionar rota `GET /api/backup` no servidor web que:
   - Define `Content-Type: application/octet-stream`.
   - Define `Content-Disposition: attachment; filename="backup_<chip_id>_<timestamp>.bkp"`.
   - Chama `ota_backup_stream()` para o response.
5. Adicionar botão "Baixar Backup" na página de admin existente.
6. Criar `tools/verify_backup.py` (script Python 3, sem dependências externas) que:
   - Lê um arquivo `.bkp` e valida magic, schema_version, header_crc32, payload_crc32.
   - Lista os arquivos contidos com seus tamanhos.
   - Retorna exit code 0 em sucesso, 1 em falha de validação.

**Artefatos produzidos:**
- `src/ota/backup_format.h`
- `src/ota/backup.cpp`
- Rota web `GET /api/backup`
- Botão UI "Baixar Backup"
- `tools/verify_backup.py`

**Critérios de aceitação:**
- [ ] Clicar no botão "Baixar Backup" inicia o download de um `.bkp`.
- [ ] `python3 tools/verify_backup.py backup_*.bkp` retorna sucesso.
- [ ] Os arquivos listados pelo script correspondem ao conteúdo da LittleFS.
- [ ] O `chip_id` no header corresponde ao retornado por `flash_get_unique_id()` (verificar via log serial).

**Teste em hardware obrigatório:** sim.

**Pontos de aprovação humana:** confirmação de que o backup é gerado, baixado, e que `verify_backup.py` valida o arquivo em pelo menos 3 execuções consecutivas com conteúdos diferentes na LittleFS.

---

### Fase 2 — Validação e Restore de Backup

**Objetivo:** restaurar arquivos da LittleFS a partir de um `.bkp`, com validação rigorosa.

**Pré-requisitos:** Fase 1 aprovada.

**Tarefas:**
1. Em `src/ota/backup.cpp`, implementar:
   - `BackupValidationResult ota_backup_validate(Stream& in);` — lê o arquivo, valida magic, schema, CRC do header, CRC do payload, e que `chip_id` bate com o do dispositivo.
   - `bool ota_backup_restore(Stream& in, bool overwrite_existing);` — escreve cada arquivo na LittleFS. Estratégia atômica: gravar em `<path>.tmp`, renomear ao final.
2. Adicionar rotas:
   - `POST /api/restore/validate` — recebe upload, retorna JSON com lista de arquivos que serão restaurados, sem escrever nada.
   - `POST /api/restore/apply` — aplica de fato.
3. Adicionar UI: botão "Restaurar Backup" → seleção de arquivo → preview da validação → confirmação → aplicação.
4. Implementar política de migração de schema: se `schema_version` do backup < versão atual, executar handlers de migração (deixar uma tabela vazia preparada para uso futuro).

**Artefatos produzidos:**
- Funções `ota_backup_validate` / `ota_backup_restore`.
- Rotas `POST /api/restore/validate` e `POST /api/restore/apply`.
- UI de restore com preview.

**Critérios de aceitação:**
- [ ] Restaurar um backup recém-gerado no mesmo dispositivo funciona; arquivos retornam ao estado original.
- [ ] Tentativa de restaurar backup gerado em **outro Pico W** é rejeitada com mensagem clara ("chip ID mismatch").
- [ ] Backup com payload CRC corrompido (testar manualmente alterando 1 byte) é rejeitado.
- [ ] Backup com schema_version mais antiga é aceito (executando migração trivial).
- [ ] Em caso de falha durante restore, arquivos `*.tmp` são removidos e arquivos originais permanecem intactos.

**Teste em hardware obrigatório:** sim — incluindo o teste com **dois Pico W diferentes** (o supervisor humano fornecerá ambos).

**Pontos de aprovação humana:** demonstração de que backup gerado em Pico A não restaura em Pico B.

---

### Fase 3 — Integração do Decompressor gzip

**Objetivo:** descompressor gzip funcional em streaming, sem alocação dinâmica.

**Pré-requisitos:** Fase 2 aprovada.

**Tarefas:**
1. Adicionar `uzlib` como biblioteca local em `lib/uzlib/` (vendoring; não usar gerenciador de bibliotecas para garantir reprodutibilidade).
2. Criar `src/ota/decompressor.h` com a API:
   ```cpp
   /**
    * Inicia uma sessão de descompressão gzip em streaming.
    *
    * @param ctx  Contexto opaco alocado pelo chamador.
    * @return true em sucesso.
    */
   bool ota_gunzip_begin(GunzipContext* ctx);

   /**
    * Alimenta bytes comprimidos. Pode ser chamada repetidas vezes.
    * O callback @p out_cb é invocado com pedaços de dados descomprimidos
    * conforme se tornam disponíveis.
    *
    * @return true em progresso normal; false em erro de formato.
    */
   bool ota_gunzip_feed(GunzipContext* ctx,
                        const uint8_t* in, size_t in_len,
                        bool (*out_cb)(const uint8_t*, size_t, void*),
                        void* user);

   /**
    * Finaliza e valida o trailer gzip (CRC, tamanho).
    */
   bool ota_gunzip_finish(GunzipContext* ctx);
   ```
3. Garantir que `GunzipContext` é alocável em stack ou estaticamente; **proibido `malloc` interno**.
4. Adicionar testes unitários em `test/decompressor_test/`:
   - Descomprimir blob conhecido pequeno (ex.: 1 KB) e comparar com original.
   - Descomprimir blob de ~500 KB e comparar.
   - Detectar corrupção: bit-flip aleatório → erro.
5. Documentar uso de RAM (window de 32 KB para deflate é o gargalo) e medir tamanho do código adicionado.

**Artefatos produzidos:**
- `lib/uzlib/` (código vendored).
- `src/ota/decompressor.{h,cpp}`.
- Testes em `test/decompressor_test/`.

**Critérios de aceitação:**
- [ ] Testes unitários passam no Pico real (rodando como sketch dedicado de teste).
- [ ] `.bin` total ainda abaixo de 1 MB com decompressor incluído.
- [ ] Uso de SRAM medido e documentado em `BASELINE.md` (espera-se ~33 KB).

**Teste em hardware obrigatório:** sim — sketch de teste rodado no Pico, log via serial.

**Pontos de aprovação humana:** revisão do log de testes + confirmação do tamanho do binário.

---

### Fase 4 — Definição e Acesso à Área de Staging

**Objetivo:** ler e escrever na região de staging via flash bruta, com a LittleFS desmontada.

**Pré-requisitos:** Fase 3 aprovada.

**Tarefas:**
1. Criar `src/ota/ota_layout.h` com as constantes da seção 3.
2. Criar `src/ota/staging.{h,cpp}` com:
   ```cpp
   /** Apaga toda a região de staging (1020 KB). Roda da SRAM. */
   bool ota_staging_erase_all();

   /** Grava @p data (múltiplo de FLASH_PAGE_SIZE = 256) em @p offset relativo ao staging. */
   bool ota_staging_write(uint32_t offset, const uint8_t* data, size_t len);

   /** Lê @p len bytes a partir de @p offset (acesso XIP, sem desabilitar IRQs). */
   void ota_staging_read(uint32_t offset, uint8_t* dst, size_t len);

   /** Lê uma única página alinhada para validação. */
   bool ota_staging_read_page(uint32_t page_index, uint8_t out[FLASH_PAGE_SIZE]);
   ```
3. Todas as funções de **escrita/apagamento** marcadas com `__not_in_flash_func(...)`.
4. Wrapper de alto nível `ota_staging_session_begin()` que:
   - Verifica que LittleFS está desmontada.
   - Apaga toda a área.
   - Inicializa estado interno para tracking de escrita sequencial.

**Artefatos produzidos:**
- `src/ota/ota_layout.h`
- `src/ota/staging.{h,cpp}`

**Critérios de aceitação:**
- [ ] Após `ota_staging_erase_all()`, leitura retorna `0xFF` em todos os bytes.
- [ ] `ota_staging_write` + `ota_staging_read` em padrões conhecidos (todos os zeros, padrão xadrez, sequência incremental) funcionam.
- [ ] Após operações de staging, **a aplicação continua rodando normalmente** (nenhum lockup, nenhum corrompimento da própria área de código).
- [ ] Tentativa de escrita não-alinhada em página é rejeitada com erro claro.

**Teste em hardware obrigatório:** sim. **Crítico:** este é o primeiro contato com flash. Se algo quebrar aqui, é fácil debugar; nas fases seguintes fica progressivamente mais difícil.

**Pontos de aprovação humana:** revisão dos logs de teste + verificação de que LittleFS, após remontagem, ainda funciona com seus arquivos originais (a área de staging não deve invadir a LittleFS quando esta está montada).

---

### Fase 5 — Upload de Firmware para Staging

**Objetivo:** receber `.bin.gz` via web e gravar em staging com integridade verificada.

**Pré-requisitos:** Fase 4 aprovada.

**Tarefas:**
1. Implementar máquina de estados de sessão de upload em `src/ota/upload_session.{h,cpp}`:
   - Estado IDLE → UPLOADING → COMMITTED.
   - `begin()` recebe metadata, salva configs em IndexedDB no cliente (responsabilidade do JS), desmonta LittleFS, apaga staging.
   - `chunk(offset, data, len)` valida contiguidade, atualiza CRC running, escreve em staging.
   - `commit()` finaliza CRC, compara com declarado.
2. Implementar rotas:
   - `POST /api/firmware/begin` — recebe JSON (seção 5.2), retorna `{"session_id":"..."}`.
   - `POST /api/firmware/chunk?session=<id>&offset=<n>` — recebe binário.
   - `POST /api/firmware/commit?session=<id>` — finaliza.
   - `POST /api/firmware/abort?session=<id>` — reformata LittleFS, restaura estado original.
3. Garantir que apenas **uma sessão simultânea** é permitida (mutex global).
4. Implementar timeout: sessão sem chunk recebido por 60s → abort automático.
5. Adicionar página `/admin/update` com:
   - Input file para `.bin.gz`.
   - Barra de progresso (XHR com `upload.onprogress`).
   - Display de erros legíveis.

**Artefatos produzidos:**
- `src/ota/upload_session.{h,cpp}`.
- 4 rotas web novas.
- Página `/admin/update`.

**Critérios de aceitação:**
- [ ] Upload de um `.bin.gz` válido completa com CRC correto.
- [ ] Upload com 1 byte modificado (corrompido) é detectado no commit.
- [ ] Upload abortado restaura LittleFS funcional (todos os arquivos pré-update presentes — assumindo backup foi feito).
- [ ] Sessão expirada (sem chunks por 60s) é abortada automaticamente.
- [ ] Tentar iniciar segunda sessão enquanto primeira ativa retorna erro 409 Conflict.

**Teste em hardware obrigatório:** sim, com upload real via Wi-Fi.

**Pontos de aprovação humana:** demonstração de upload bem-sucedido + ao menos uma simulação de falha (corrupção ou abort) com recuperação correta.

---

### Fase 6 — Pré-validação (Dry-Run)

**Objetivo:** garantir que o staging está íntegro **antes** de qualquer ação destrutiva.

**Pré-requisitos:** Fase 5 aprovada.

**Tarefas:**
1. Implementar `bool ota_validate_staging(ValidationReport& out);` que:
   - Verifica magic gzip nos primeiros bytes do staging (`0x1F 0x8B`).
   - Recalcula CRC do conteúdo comprimido e compara com metadata.
   - Executa **descompressão completa em dry-run** (descarta a saída, só calcula CRC).
   - Verifica que tamanho descomprimido bate com o declarado.
   - Verifica que CRC do descomprimido bate com o declarado.
   - Verifica que os primeiros 256 bytes do binário descomprimido **parecem um boot2 RP2040 válido** (a IA implementadora deve pesquisar a assinatura típica e implementar uma checagem heurística).
2. Integrar `ota_validate_staging` ao fluxo do `commit`: só retornar sucesso ao cliente se a validação completa passar.
3. Adicionar logs detalhados (sem expor segredos) sobre qual checagem falhou.

**Artefatos produzidos:**
- Função `ota_validate_staging` em `src/ota/validation.cpp`.
- Integração ao endpoint `/api/firmware/commit`.

**Critérios de aceitação:**
- [ ] Validação aceita um `.bin.gz` legítimo do projeto.
- [ ] Validação rejeita `.bin.gz` com 1 byte alterado em qualquer posição.
- [ ] Validação rejeita arquivo que não é gzip (ex.: zip, tar).
- [ ] Validação rejeita gzip de conteúdo aleatório (não-firmware).
- [ ] Logs identificam claramente qual etapa falhou.

**Teste em hardware obrigatório:** sim.

**Pontos de aprovação humana:** revisão dos logs de pelo menos 4 cenários (sucesso + 3 falhas distintas).

---

### Fase 7 — Aplicador em SRAM (FASE CRÍTICA)

**Objetivo:** apagar o slot da app e gravar o novo firmware descomprimido, rodando integralmente da SRAM.

**Pré-requisitos:** Fase 6 aprovada. **REVISÃO TÉCNICA HUMANA OBRIGATÓRIA antes de executar em hardware com firmware real.**

> ⚠️ **Esta fase pode brickar o dispositivo.** O humano deve ter um Pico W de teste dedicado e estar preparado para usar BOOTSEL + UF2 para recuperar.

**Tarefas:**
1. Criar `src/ota/applier.{h,cpp}` contendo:
   ```cpp
   /**
    * Aplica o update lido do staging.
    *
    * Esta função NÃO RETORNA EM CASO DE SUCESSO (faz watchdog reset).
    * Roda inteiramente da SRAM. Toda função chamada por ela DEVE estar
    * marcada com __not_in_flash_func.
    *
    * Pré-condições:
    *   - IRQs desabilitadas.
    *   - Core 1 parado via multicore_lockout_start_blocking().
    *   - Wi-Fi/CYW43 desligado.
    *   - LittleFS desmontada.
    *   - Metadata em flash com state=APPLYING.
    *
    * @return false apenas se erro detectado ANTES de iniciar a destruição.
    */
   bool __not_in_flash_func(ota_applier_run)(const UpdateMetadata* meta);
   ```
2. Implementação esperada (pseudo-código de alto nível):
   ```
   1. Ler primeira página do staging para SRAM.
   2. Inicializar contexto gunzip em SRAM.
   3. Apagar slot da app em incrementos de 4 KB (não tudo de uma vez,
      para reduzir janela vulnerável; mas note: a app sendo apagada
      JÁ está copiada para SRAM, então tudo bem).
   4. Loop: ler chunk do staging via XIP → alimentar gunzip → quando
      gunzip emite bytes, acumular em buffer de 256 B → quando cheio,
      gravar página no slot da app.
   5. Após último byte, validar CRC do que foi escrito (relendo via XIP).
   6. Se CRC OK: limpar metadata, watchdog_reboot.
   7. Se CRC FAIL: setar metadata.state=POST_BOOT_FAIL, watchdog_reboot
      (próximo boot do firmware antigo... que não existe mais. Aqui é
      onde cai para BOOTSEL).
   ```
3. **Toda função em cadeia de chamada** deve estar em SRAM:
   - `ota_applier_run` e tudo que ela chama: `__not_in_flash_func`.
   - `flash_range_erase`, `flash_range_program`: já são in-RAM no SDK.
   - Funções do gunzip: precisam ser marcadas. **Verificar `uzlib` — pode ser necessário um wrapper que copia partes essenciais para SRAM.**
4. Implementar o pré-update orquestrador em `src/ota/orchestrator.cpp`:
   ```cpp
   void ota_apply_pending_update() {
       UpdateMetadata meta = read_metadata();
       if (meta.magic != OTA_MAGIC || meta.state != STATE_COMMITTED) return;

       meta.state = STATE_APPLYING;
       meta.attempts++;
       write_metadata(&meta);  // Persiste antes de qualquer destruição

       // Desliga subsistemas
       WiFi.end();
       LittleFS.end();
       multicore_lockout_start_blocking();

       uint32_t irq_state = save_and_disable_interrupts();
       // Sem retorno daqui em caso de sucesso
       ota_applier_run(&meta);
       restore_interrupts(irq_state);
       // Se chegou aqui, algo deu errado ANTES da destruição
   }
   ```
5. Adicionar endpoint `POST /api/firmware/apply` que valida estado e chama `ota_apply_pending_update()`.

**Artefatos produzidos:**
- `src/ota/applier.{h,cpp}`.
- `src/ota/orchestrator.cpp`.
- Endpoint `/api/firmware/apply`.

**Critérios de aceitação:**

> Esta fase tem **dois subníveis** de aceitação. Não pular do primeiro para o segundo.

**7a — Aplicador "no-op" (sem destruição):**
- [ ] Implementar uma versão que copia para SRAM, salta, mas em vez de apagar a app, apenas pisca um LED em padrão conhecido por 5 segundos e dá reboot.
- [ ] Demonstra que a infraestrutura (cópia para SRAM, lockout do core 1, salto, reboot limpo) funciona.

**7b — Aplicador real (DESTRUTIVO):**
- [ ] Atualização de firmware **idêntico ao atual** (gerado, comprimido, aplicado) resulta em sistema funcional pós-boot.
- [ ] Atualização para firmware com **mudança visível** (ex.: versão hardcoded incrementada) resulta no novo firmware rodando.
- [ ] Em pelo menos 5 ciclos consecutivos de update, sistema permanece funcional.

**Teste em hardware obrigatório:** sim, **com dispositivo de teste dedicado**. Não usar dispositivo de produção.

**Pontos de aprovação humana:**
- Revisão completa do código de `applier.cpp` e `orchestrator.cpp` antes de qualquer execução.
- Aprovação separada para 7a e 7b.
- Documento `RECOVERY.md` (procedimento BOOTSEL) entregue antes do primeiro teste destrutivo.

---

### Fase 8 — Boot Pós-Update e Restore de Configs

**Objetivo:** firmware novo detecta primeiro boot pós-update, formata LittleFS, espera restore.

**Pré-requisitos:** Fase 7b aprovada.

**Tarefas:**
1. No `setup()` do firmware, **antes** de inicializar Wi-Fi/web, verificar metadata:
   - Se `state == STATE_APPLYING` ou `state == STATE_COMMITTED`: estamos no primeiro boot pós-update.
2. Em primeiro boot pós-update:
   - Reformatar LittleFS (`LittleFS.format(); LittleFS.begin();`).
   - Atualizar metadata para `state = STATE_POST_BOOT`.
   - Subir interface web em modo "aguardando restore" (página simplificada).
3. Adicionar rota `POST /api/post_update/restore` que aceita configs em formato JSON ou diretamente um `.bkp`.
4. Após restore bem-sucedido:
   - Atualizar metadata para `state = STATE_COMPLETED`.
   - Reiniciar normalmente (boot subsequente é normal).
5. JS do navegador: detecta versão nova do firmware (via endpoint `/api/version`) e dispara restore automático a partir do IndexedDB.

**Artefatos produzidos:**
- Lógica de detecção de pós-update em `src/main.cpp` (ou equivalente).
- Rota `/api/post_update/restore`.
- Página HTML simplificada para modo "aguardando restore".
- JS de auto-restore.

**Critérios de aceitação:**
- [ ] Após update, primeiro boot detecta o estado e formata LittleFS.
- [ ] Página web no primeiro boot mostra modo "aguardando restore" claramente.
- [ ] Auto-restore via JS popula a LittleFS com as configs originais.
- [ ] Após restore, reboot normal usa as configs corretamente.
- [ ] Se navegador não fizer restore em 10 minutos, dispositivo continua acessível para upload manual de `.bkp`.

**Teste em hardware obrigatório:** sim.

**Pontos de aprovação humana:** ciclo completo (backup → update → auto-restore) demonstrado em hardware.

---

### Fase 9 — Integração de UI End-to-End

**Objetivo:** UX coesa em um único botão "Atualizar Firmware".

**Pré-requisitos:** Fase 8 aprovada.

**Tarefas:**
1. Página `/admin/update` consolidada com fluxo único:
   - Verificação prévia: confirma com o usuário que entende o processo.
   - Download automático do backup (forçado, antes de prosseguir).
   - Pré-carregamento das configs em IndexedDB.
   - Seleção e validação local do `.bin.gz` (cliente verifica magic gzip antes de subir).
   - Upload com progresso.
   - Trigger do apply.
   - Polling de reconexão (timeout configurável, padrão 90s).
   - Restore automático.
   - Confirmação final com versão nova rodando.
2. Tratamento de erros em cada etapa com mensagens claras e ação sugerida.
3. Logs persistentes do processo no console do navegador para diagnóstico.

**Artefatos produzidos:**
- Página `/admin/update` finalizada.
- JS consolidado em `data/admin/update.js` (ou equivalente).

**Critérios de aceitação:**
- [ ] Usuário leigo consegue fazer um update completo seguindo apenas as instruções da tela.
- [ ] Cada erro possível tem mensagem específica e acionável.
- [ ] Cancelar a qualquer momento antes do `apply` deixa o sistema funcional.

**Teste em hardware obrigatório:** sim — preferencialmente com pessoa não-envolvida no projeto fazendo o update sob observação.

---

### Fase 10 — Documentação e Release

**Objetivo:** material para usuário final e para manutenção futura.

**Pré-requisitos:** Fase 9 aprovada.

**Tarefas:**
1. `docs/USER_GUIDE_UPDATE.md` — passo a passo ilustrado para o usuário final.
2. `docs/BOOTSEL_RECOVERY.md` — procedimento de recuperação via USB para casos catastróficos.
3. `docs/ARCHITECTURE.md` — visão geral técnica do sistema OTA, baseada neste plano.
4. `tools/build_release.sh` — script que compila com flags de produção, comprime o `.bin` para `.bin.gz`, calcula CRCs, gera arquivo de manifest.
5. CHANGELOG.md atualizado.
6. Merge da branch `feature/ota-self-flash` para `main` via PR com checklist completo.

**Critérios de aceitação:**
- [ ] Toda documentação revisada e mergeable.
- [ ] Script de release produz artefatos consistentes em duas execuções.
- [ ] PR aprovado pelo supervisor humano.

---

## 7. Riscos e Mitigações

| # | Risco | Probabilidade | Impacto | Mitigação |
|---|-------|---------------|---------|-----------|
| R1 | `.bin` final excede 1 MB após inclusão de uzlib + lógica OTA | Média | Bloqueante | Fase 0 mede stub vazio; aplicar `-Os --gc-sections`; remover features descartáveis se necessário |
| R2 | Função do gunzip não marcada com `__not_in_flash_func` causa crash durante apply | Alta | Brick | Inspeção manual de cada função na cadeia de chamada de `ota_applier_run`; teste 7a antes de 7b |
| R3 | Queda de energia durante apply | Baixa | Brick | Documentar BOOTSEL como recovery; sugerir UPS/bateria durante updates |
| R4 | Wi-Fi não desliga limpo, IRQ pendente causa lock | Média | Brick | `WiFi.end()` + `cyw43_arch_deinit()` antes de IRQs disable; teste 7a verifica isso |
| R5 | Core 1 acessa flash durante apply | Alta (se aplicável) | Brick | `multicore_lockout_start_blocking()` obrigatório; verificar uso de core 1 na Fase 0 |
| R6 | LittleFS corrompida não desmonta limpo, deixa estado inconsistente | Baixa | Update falha | `LittleFS.format()` no pós-boot é idempotente; backup é o seguro |
| R7 | Cliente fecha aba durante upload | Média | Update incompleto | Sessão expira em 60s; abort restaura LittleFS via reformat |
| R8 | Decompressão real diverge do dry-run (extremamente raro) | Muito baixa | Brick parcial | Aceitar como falha BOOTSEL; documentar |
| R9 | Schema de backup muda e quebra restore antigo | Média (longo prazo) | Perda de configs | `schema_version` + tabela de migrações desde Fase 2 |

---

## 8. Pontos de Decisão para o Supervisor Humano

A IA implementadora **deve parar e perguntar ao humano** nestes momentos:

1. **Início da Fase 1:** "O `BASELINE.md` está completo. Confirma que posso prosseguir para implementar a geração de backup?"
2. **Antes da Fase 4:** "Vou começar a tocar em flash bruta. Você tem um Pico W de teste dedicado e está pronto para reflashar via UF2 se necessário?"
3. **Antes da Fase 7a:** "Próxima fase modifica IRQs e core 1. Revisar applier.cpp comigo antes de testar?"
4. **Antes da Fase 7b (CRÍTICA):** "Próximo passo é destrutivo. Confirma:
   - (a) Você está usando dispositivo de teste, não produção?
   - (b) `RECOVERY.md` foi revisado?
   - (c) Tem o `.uf2` original salvo localmente?"
5. **Qualquer momento que uma compilação ultrapasse 1 MB:** parar e reportar antes de ajustar arquitetura.

---

## 9. Convenções de Código

- **Linguagem:** C++ no estilo do arduino-pico; evitar STL pesada (`std::string`, `std::vector` aceitáveis em rotas web; **proibidos** no aplicador em SRAM).
- **Indentação:** 4 espaços, sem tabs.
- **Naming:** `snake_case` para funções e variáveis; `PascalCase` para tipos.
- **Documentação:** todo header público (`.h`) tem doxygen-style comments em cada função pública.
- **Erros:** funções públicas retornam `bool` ou enum de erro; **nunca** `throw`.
- **Logging:** macros `OTA_LOGI/W/E` que mapeiam para `Serial.printf` em debug e viram no-op em release.
- **Constantes:** todas as constantes de layout em `ota_layout.h`. **Nenhum literal numérico hardcoded** em código de OTA.
- **Funções não devem ser perdidas em refatorações:** se uma função existe e é renomeada/movida, manter wrapper deprecated por 1 fase para detectar uso esquecido.

---

## 10. Glossário

| Termo | Significado |
|-------|-------------|
| **App slot** | Região de 1 MB onde reside o firmware ativo (`0x10000000–0x100FFFFF`). |
| **Staging** | Região de 1020 KB onde o `.bin.gz` é depositado durante upload. Compartilha endereço com a LittleFS, mas só uma das duas pode estar montada/usada por vez. |
| **Metadata** | Setor de 4 KB ao final da flash com o flag de update e CRCs. |
| **boot2** | Primeiros 256 bytes da flash; configura XIP. Faz parte do `.bin` produzido pelo arduino-pico. |
| **XIP** | Execute-In-Place. O código em flash é executado diretamente, sem cópia para RAM. Falha durante apagamento da flash. |
| **Self-flashing** | A própria aplicação (após copiar a rotina para SRAM) sobrescreve o app slot. |
| **Aplicador** | A rotina que roda em SRAM e executa: erase → decompress → write → reboot. |
| **Apply** | Ato de transferir staging → app slot. Ponto de não-retorno do update. |
| **Pré-destruição** | Estado do sistema antes que qualquer setor da flash seja apagado para o update. Erros aqui são 100% recuperáveis. |
| **Pós-destruição** | Estado a partir do primeiro `flash_range_erase` no app slot. Erros aqui requerem BOOTSEL. |
| **BOOTSEL** | Modo de recuperação USB do RP2040. Exige acesso físico ao botão. |
| **CRC32** | Polinômio 0xEDB88320, init 0xFFFFFFFF, xor-out 0xFFFFFFFF (idêntico ao usado por gzip e zlib). |

---

**Fim do plano.** A IA implementadora deve confirmar leitura completa deste documento antes de iniciar a Fase 0, e referenciar a fase em todo commit (ex.: `[Fase 1] add backup format header`).
