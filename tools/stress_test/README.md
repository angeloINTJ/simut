# SIMUT Stress Test Toolkit

Conjunto de ferramentas para fazer stress test do SIMUT em hardware real:
encher o filesystem com história sintética, drenar via telemetria, exportar
via CSV, e restaurar o estado original.

## Componentes

| Arquivo | Descrição |
|---|---|
| `lib_simut_api.sh` | Helpers compartilhados (login challenge SHA256, ls/get/put/delete/mkdir, walk recursivo, Serial CLI helper) |
| `generate_history_v2.py` | Gera arquivos `YYYYMMDD.bin` em codec V2 (header SIM2 + delta+anchor) com dados realistas (temp/hum senoidal + ruído) |
| `backup_fs.sh` | Walk recursivo + download de cada arquivo via `/download` + manifest com tamanhos |
| `restore_fs.sh` | Restaura do backup, opcionalmente deletando arquivos extras (`--delete-extras`). Tolera divergência de tamanho em logs/cursor (crescem naturalmente). |
| `run_stress_test.sh` | Orchestrator end-to-end (backup → generate → upload → reset cursor → reboot → drain → CSV → restore) |

## Pré-requisitos

- `bash 4+`, `curl`, `openssl`, `iconv`, `python3`
- Acesso de rede ao device (porta 80)
- Usuário com perms `FILE_READ` + `FILE_UPLOAD` + `FILE_DELETE`
- (opcional) `/dev/ttyACM0` livre para o reboot via Serial — caso contrário,
  `run_stress_test.sh` ainda funciona mas o reset de cache do cursor não
  ocorre (cursor antigo bloqueia o drain dos records sintéticos)

## Variáveis de ambiente

```bash
export SIMUT_IP=192.168.3.195
export SIMUT_USER=admin
export SIMUT_PASS='admin'           # plaintext, será SHA256-hashed (Latin-1)
```

## Uso típico

### Stress test completo (30 dias)

```bash
./run_stress_test.sh --days 30 --records-per-day 1440 --drain-timeout 1800
```

Pipeline:

1. **Backup** — full FS dump em `/tmp/stress_report_<ts>/backup/` + manifest
2. **Generate** — N daily V2 files em `/tmp/stress_report_<ts>/generated/`
3. **Upload** — POST sequencial cada arquivo para `/history/`
4. **Reset cursor + reboot** — `/api/delete /config/t_cursor.bin` + Serial
   `reload confirm` (invalida cache RAM); aguarda device voltar
5. **Drain** — pool `/api/status` a cada 30s, log para `drain.csv`. Termina
   quando `pending=0` + `telSent` estável por 2 ciclos OU timeout
6. **CSV** — chama `/api/export/history.bin?from=&to=` em chunks de 31 dias,
   valida HTTP 200 + magic `SIMX`
7. **Restore** — re-upload do backup com `--delete-extras` (remove os
   stress files de `/history/`)

### Apenas backup (insurance antes de operação manual)

```bash
./backup_fs.sh /tmp/my_backup
# ... operação manual ...
./restore_fs.sh /tmp/my_backup
```

### Apenas generate (sem rodar o teste)

```bash
python3 generate_history_v2.py --days 30 --output-dir /tmp/hist
```

### Smoke test rápido (5 min)

```bash
./run_stress_test.sh --days 5 --records-per-day 1440 --drain-timeout 600
```

## Capacidade do FS

- LittleFS: 1 048 576 B total
- Margem segura para stress: ~850 KB (`--max-bytes 850000` no generator)
- Cada arquivo V2: ~22.6 KB / 1440 records (60s interval)
- **Cap prático: ~37 dias × 1440 records**

Se exceder, generator para e avisa via stderr.

## Capturas (`/tmp/stress_report_<ts>/`)

```
backup/                 # snapshot pré-teste
  manifest.txt          # /caminho|tam_remoto|tam_local por linha
  history/, config/, ...  # arquivos preservando estrutura

generated/              # arquivos sintéticos V2
  20260402.bin
  20260403.bin
  ...

added_files.txt         # tracking dos arquivos que adicionamos
drain.csv               # epoch,uptime,telSent,telFailed,telBytes,pending,deltaSent
csv_chunk_*.simx        # respostas raw de cada chunk de export
stress.log              # log completo via tee
```

## Limitações conhecidas

- **Cache de cursor RAM-only**: `StorageManager::getLastSentTimestamp()`
  cacheia o último valor enviado. Reset só via reboot (que o script faz).
  Se Serial estiver ocupado e reboot falhar, drain não vai pegar os
  records sintéticos do passado — só os novos do device.
- **Telemetria fallback de 30 dias**: se gerar mais de 30 dias, só os
  últimos 30 entram no batch da telemetria. CSV exporta tudo
  (não usa cursor, vai por range).
- **Rate limit no /api/upload e /api/delete**: 200ms server-side; o lib
  tem `SIMUT_API_RATE_DELAY=0.5` por padrão, ajustável via env var.

## Próximos passos / melhorias futuras

- `tel reset` CLI command no firmware: invalidaria cache RAM diretamente,
  evitando a dependência de reboot via Serial. Ver discussão em F-TEL-V2READER.
- Rodar via CI: precisa device físico ou emulador. Não é trivial.
- Relatório HTML auto-gerado a partir do `drain.csv`.
