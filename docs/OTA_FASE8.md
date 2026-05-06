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

## Anexo A — Bug 2 boot intermitente (v3.43.4 → v3.43.11)

Histórico do diagnóstico do bug "boot pós-apply intermitente":

### Sintoma observado
- Em ~70% dos applies, device USB enumera (`2e8a:f00a`) mas firmware
  não responde por mais de 2 minutos. CLI silencioso, web não responde,
  1200bps reset trick falha (USB CDC handler não responde).
- Em ~30% dos applies, boot completa em ~60 s.
- Padrão: "lucky" boots tendem a ser primeiros após picotool flash;
  applies subsequentes mais propensos a hang.

### Diagnósticos errados
- v3.43.4: hipótese inicial era hard fault em flash app slot.
  Fix: substituí pico-sdk watchdog_update/reboot por inlines MMIO.
  Correto, mas insuficiente.
- v3.43.5: hipótese era WDT bit errado (1u<<30 ENABLE em vez de
  1u<<31 TRIGGER). Fix correto, mas só resolveu o erase loop crash,
  não o boot intermitente.
- v3.43.6-9: hipóteses sobre sector 0 (boot2) corruption. Fix correto,
  mas independente do Bug 2.
- v3.43.10: assumiu Bug 2 era "falso positivo" baseado em 1 boot OK.
  Refutado: re-teste mostrou intermitência clara.

### Diagnóstico final (v3.43.11)

**Root cause**: `applier_reboot` usava SCB SYSRESETREQ via AIRCR
(0xE000ED0C). SCB reset só atinge cores M0+ (CPU registers, NVIC,
SysTick), **não** atinge peripherals SIO, RESETS, BUSCTRL, ou multicore
mailbox. Esses ficam em estado pré-reset.

Após reset, arduino-pico's `_displayMgr->startCore1()` chama
`multicore_launch_core1()` que envia handshake via SIO mailbox para
Core 1. Se mailbox tem dados stale do antes-do-reset, Core 1 boot ROM
recebe input inesperado → diverge da sequência expected → core 1
hang silencioso. Core 0 main loop espera Core 1 ready (ou worse, vê
fake-ready) → hang.

Verificado via análise de pico-sdk: a função `watchdog_reboot()` da
SDK faz reset MUITO mais completo:
1. Configura `psm_hw->wdsel` com bits para todos os peripherals
   selecionáveis (incluindo SIO via PROC0/PROC1 bits).
2. Habilita watchdog com TIME pequeno + TRIGGER imediato.
3. Watchdog fire → PSM reset cycle → todos os peripherals voltam
   pra estado pós-power-on.

### Fix v3.43.11

`applier_reboot` re-implementado inline replicando watchdog_reboot
com MMIO puro (sem dependência de SDK em flash slot):

```cpp
static inline void __not_in_flash_func(applier_reboot)() {
    /* PSM->wdsel = todos os bits → reset completo on watchdog fire */
    *(volatile uint32_t*)(PSM_BASE_ADDR + PSM_WDSEL_OFFSET) = PSM_WDSEL_ALL;

    /* Clear watchdog ENABLE (CLR alias atomic) */
    *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_CLR_ALIAS +
                          WATCHDOG_CTRL_OFFSET) = WATCHDOG_CTRL_ENABLE;

    /* Clear scratch[4]: boot ROM checa este magic; 0 = normal boot */
    *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_SCRATCH4_OFFSET) = 0;

    /* LOAD = 10 ms × 2 ticks/μs (12 MHz/6) */
    *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_LOAD_OFFSET) = 20000u;

    /* SET ENABLE | TRIGGER atomic (SET alias) */
    *(volatile uint32_t*)(WATCHDOG_BASE_ADDR + WATCHDOG_SET_ALIAS +
                          WATCHDOG_CTRL_OFFSET) =
        WATCHDOG_CTRL_ENABLE | WATCHDOG_CTRL_TRIG;

    __asm volatile("dsb");
    while (1) { __asm volatile("nop"); }
}
```

### Validação pendente

A validação da v3.43.11 precisa de:
1. Power cycle físico do device (atual está hung em estado de
   v3.43.10, USB CDC não responde reset).
2. Reflash v3.43.11 via picotool.
3. Configurar WiFi via CLI.
4. Rodar OTA apply pelo menos 5 vezes consecutivas.
5. Verificar boot OK em 100% dos casos.
6. Medir tempo médio de boot pós-apply (esperado: ~60 s, similar à
   v3.43.10 mas determinístico em vez de probabilístico).

---

**Autor**: Ângelo Moisés Alves (com co-autoria Claude Opus 4.7).
**Última atualização**: 2026-05-06.
