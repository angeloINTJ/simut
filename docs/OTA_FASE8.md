# OTA Fase 8 — Preservação de user data durante apply

> Documento entregue como parte da Fase 8 do plano OTA, posterior à
> validação da Fase 7b (apply destrutivo) em 2026-05-06.

---

## Problema

A área de staging do OTA (`OTA_STAGING_OFFSET = 0x100FF000`, 1024 KiB)
**compartilha a mesma partição de flash que o LittleFS**. Durante o
upload do firmware via `/api/restore?op=stage`, todo o conteúdo da
LittleFS é apagado e sobrescrito com os bytes do firmware.

Após `/api/ota/apply`, o boot detecta que o LFS está corrompido (sem
superblock válido), formata vazio e o device sobe em **factory state**:

- Admin password regenerada (SEC-003 OTP visível só no Serial USB)
- WiFi config perdida (SSID/pass/IP mode)
- Touch calibration perdida
- Device name perdido (volta para "simut")
- Sensor mapping perdido
- Histórico (`/history/*`) perdido
- Themes/lang customizados perdidos
- Log do sistema perdido

Para uso em produção, isso é inaceitável.

## Decisão de design

**Escolhida**: orquestração do lado do cliente (script `tools/ota_apply.py`).

**Não escolhida**: backup automático server-side em região protegida do
flash. Motivo: requer remapear `ota_layout.h` para criar uma nova
partição "backup-pinned", reduzindo o tamanho máximo do firmware ou
do staging. O firmware atual (969 KiB) já está em 91.6% da capacidade do
slot de 1020 KiB; perder espaço não é viável.

## Fluxo

```
Cliente (host com USB+IP)         Device SIMUT
──────────────────────────────    ─────────────────────────────────
1. POST /api/login_init         → 200 OK (nonce)
2. POST /api/login              → 200 OK (cookie SIMUTSESS)
3. GET  /api/backup             → 200 OK (BKP1 .bkp, ~5-100 KiB)
   Salva em /tmp/simut-pre-ota-<ts>.bkp
4. POST /api/restore?op=stage   → 200 OK (st=5, v=0, committed=1)
   &commit=1 + multipart firmware    Apaga LFS, escreve firmware,
                                     marca metadata.state=COMMITTED
5. POST /api/ota/apply          → 202 Accepted
                                     Marca metadata.state=APPLYING,
                                     orchestrator faz teardown,
                                     applier SRAM faz erase+program
                                     do app slot, reboot.
6. (espera ~60-90 s)              Boot: AppManager_Boot detecta
                                     state=APPLYING, clear metadata,
                                     mountFS auto-format,
                                     Core 1 lockout recovery 10s,
                                     factory init (SEC-003 OTP regen).
7. GET  /api/login_init         → 200 OK (após boot completo)
8. POST /api/login_chpass       → 200 OK (chpass do OTP factory
   user=admin                         para senha normal escolhida
   oldpass=sha256(OTP_FACTORY)        pelo cliente)
   newpass=sha256(NewPass)
9. POST /api/login              → 200 OK
10. POST /api/restore?op=apply  → 200 OK (rota Fase 2)
    multipart .bkp do passo 3        Restaura system.bin + history +
                                     /web/ + /lang/ + /themes/ + sensores
11. POST /api/ota/apply         → (não — nenhuma mudança de firmware
    NÃO chamar aqui                   pendente; metadata já COMPLETED)
12. (re-login se sessão caiu)
13. GET  /api/system/info       → confirma deviceName + WiFi voltam
```

### Limitação: SEC-003 OTP só visível via USB Serial

Para o passo 7-8, o cliente precisa saber a one-time password gerada
pelo SEC-003 no boot pós-apply. Hoje essa OTP é impressa **apenas no
Serial USB CDC** (linha "SEC-003: FACTORY DEFAULTS ATIVADO Senha ADMIN
inicial: XXXXXXXX"), não exposta via web.

**Justificativa**: defesa em camadas — em factory state o device exige
acesso físico (USB) para configurar a primeira senha. Expor OTP via web
violaria essa camada.

**Workaround do cliente**: o orquestrador pode ler o serial USB enquanto
espera o boot completar e capturar a OTP automaticamente. Isso só funciona
se o cliente estiver no mesmo host que o device (cabo USB local).

Para uso remoto (cliente em rede separada), uma extensão futura seria:
- Pre-apply: cliente envia hash de senha futura ao server via novo
  endpoint `/api/ota/preserve_password`
- Server salva hash em região protegida do metadata sector (4 KiB tem
  margem para isso)
- Pós-apply boot: AppManager_Boot lê metadata, restaura admin user com
  esse hash em vez de gerar OTP factory
- Cliente faz login direto com a senha pré-acordada

## Status

| Etapa | Status | Validação |
|-------|--------|-----------|
| 1-5 (apply) | ✅ Implementado | Validado HW 2026-05-06 |
| 6 (boot wait) | ✅ Implementado | Validado HW 2026-05-06 |
| 7-9 (chpass + login pós-apply) | ⚙ Implementado, OTP via Serial | Pendente teste E2E |
| 10 (restore) | ⚙ Implementado | Reusa fluxo Fase 2 já validado |
| 11-13 (verify) | 📋 Pendente | — |

## Ferramenta

`tools/ota_apply.py` — orquestra etapas 1-6, etapas 7-10 ainda
pendentes de captura automática da OTP. Exemplo:

```bash
./tools/ota_apply.py \
    --ip 192.168.3.195 \
    --user admin --pass 'AdminPass' \
    --firmware .pio/build/pico_w_release/firmware.bin \
    --no-restore
```

## Riscos e mitigações

| Risco | Probabilidade | Impacto | Mitigação |
|-------|---------------|---------|-----------|
| Backup falha antes do apply | Baixa | Alto (user data perdido sem recurso) | Cliente verifica HTTP 200 + magic BKP1 antes de chamar apply |
| Boot pós-apply não completa em 180 s | Baixa | Alto (manual recovery) | Documento de recovery em `docs/RECOVERY.md` |
| Restore parcial (interrompido) | Média | Médio (LFS inconsistente) | Fase 2 OTA já tem rollback parcial (mas não atomic) |
| Backup .bkp corrompido em rede | Baixa | Médio (validate detecta CRC fail) | Validate rota Fase 2 verifica magic + CRC antes de apply |

## Próximos passos (Fase 9)

1. Validar end-to-end E2E em HW (etapas 1-13 completas).
2. Implementar captura automática de OTP via Serial USB no orquestrador.
3. Considerar endpoint `/api/ota/preserve_password` para uso remoto.
4. Documentar tempo médio de cada etapa para SLA de OTA.
5. Adicionar test suite automatizada (`tools/test_device_full.py`).

---

**Autor**: Ângelo Moisés Alves (com co-autoria Claude Opus 4.7).
**Última atualização**: 2026-05-06.
