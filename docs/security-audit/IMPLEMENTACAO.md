# Implementação das correções — Auditoria de Segurança

Registro das correções aplicadas sobre o relatório
`relatorio-auditoria-seguranca.pdf` (8 achados, ACH-01..08, versão auditada
v2.3.6-beta). Correções aplicadas sobre a linha v2.3.8-beta.

## Resumo

| Achado | Severidade | Decisão | Correção |
|---|---|---|---|
| ACH-01 — delete de `/config` | Alta | **Corrigido** | `handleDelete` rejeita `..`/`%`/`isSecretFsPath` |
| ACH-02 — upload sob `/config` | Alta | **Corrigido** | `handleUploadData` rejeita `isSecretFsPath(finalPath)` |
| ACH-03 — restore grava em `/config` | Média | **Corrigido (permissão)** | `op=apply` → `PERM_FULL_ADMIN` |
| ACH-04 — `ls` de `/config` | Baixa | **Corrigido** | `handleApiLs` rejeita `isSecretFsDir(dirPath)` |
| ACH-05 — credenciais de bancada | Baixa | **Adiado** | opcional (higiene) |
| ACH-06 — viewer/viewer, PIN 1234 | Baixa | **Sem ação** | por design |
| ACH-07 — XSS via `.lng` | Baixa | **Corrigido** | `applyLang` usa `escHtml` |
| ACH-08 — AP de setup aberto | Info | **Sem ação** | por design |

## Detalhes

### ACH-01/02/04 — guarda de `/config` (cluster principal)

- `src/WebManager_Files.cpp`: `handleDelete`, `handleUploadData` e
  `handleApiLs` agora aplicam a guarda `isSecretFsPath`/`isSecretFsDir`
  (antes só `handleDownload`).
- `src/FsSecretPath.h`: novo helper `isSecretFsDir` cobre o diretório exato
  `/config` (o `isSecretFsPath` só casa com `/config/...`).
- Testes nativos: `test_secret_dir_blocks_bare_and_nested`,
  `test_secret_dir_allows_legit_dirs`.

### ACH-03 — correção por permissão (não por path)

O relatório sugeria rejeitar `/config` em `path_is_safe` (restore). Isso foi
avaliado e **descartado**: o `.bkp` é um dump do FS inteiro
(`ota/backup.cpp` faz `walk_dir("/")` sem excluir `/config`), portanto o
restore legítimo grava `/config/system.bin` por design. Rejeitar `/config`
em `path_is_safe` quebraria o restore legítimo com `PATH_INVALID`.

A correção correta é elevar `POST /api/restore?op=apply` de
`PERM_FILE_UPLOAD` para `PERM_FULL_ADMIN` (simétrico ao `/api/backup`), em
`src/WebManager_Ota.cpp` (upload handler + finish handler). Um não-admin
agora recebe `403` ao tentar aplicar um `.bkp`.

### ACH-07 — XSS

`WebUI.h`: as três funções `applyLang` (shell autenticado, login e FCP)
escapam o valor vindo do dicionário (`/api/lang` → `@WEBDICT` do `.lng`)
via `escHtml` antes de `innerHTML`. O `data-en` embutido e o
`placeholder` não são escapados (não são sinks HTML).

## Teste de regressão (CI)

- `tools/check_fsguard.py` (novo, roda em `.github/workflows/build.yml`):
  asserta a guarda `/config` em delete/upload/ls e que o gate de `apply` não
  usa `PERM_FILE_UPLOAD`.
- `test/test_validators/test_main.cpp`: vetores de `isSecretFsDir`.

## Verificação

- `pio run -e pico_w_release`: SUCCESS (Flash 97.3%)
- `pio test -e native`: 121/121 PASSED
- `tools/check_authz.py`: clean (56 rotas)
- `tools/check_fsguard.py`: clean

## Documentação atualizada

- `docs/AUTHORIZATION.md`: `op=apply` movido para o tier `== PERM_FULL_ADMIN`.
- `SECURITY.md`: threat model, endpoints com permissão e §9.2 (procedimento
  de restauração de config) atualizados.
