# Investigação F-OTA-BOOTLOOP — boot pós-apply travado até power cycle

> Iniciado: 2026-05-07
> Branch: `feature/ota-self-flash` @ `v3.43.16`
> Sintoma: após OTA apply destrutivo + `applier_reboot`, SIMUT enumera USB
> CDC mas Web (porta 80) e CLI (Serial USB) ficam mudos. Apenas
> `power cycle` físico (USB unplug+replug) destrava. Reset por GPIO RUN
> (mão pico_hand `HOLD RESET 10s`) **não basta**.
>
> Reproduzido em v3.43.14, v3.43.15, v3.43.16.
>
> v3.43.10/11 (sem Fase 9) reportaram apply OK em HW — possível que seja
> regressão da Fase 9 OU mesmo Bug 2 que foi parcialmente corrigido.

## Hipóteses iniciais

1. **Estado periférico não limpo por watchdog_reboot:**
   - CYW43 wifi state em SRAM persiste — pode ter IRQ pendente
   - SIO mailbox (multicore) em estado stale entre Core0/Core1
   - USB CDC controller em estado intermediário
   - BTstack TLV em flash região reservada (sector 233 ~ 0xE9000)

2. **Ordem de tear-down do orchestrator incorreta:**
   - WiFi.end() pode não desligar tudo (IRQ residual)
   - LittleFS.end() pode deixar mutex preso
   - save_and_disable_interrupts() pode deixar algum periph em estado ruim

3. **Lockout multicore mal-resolvido:**
   - Antes do applier rodar, Core 1 está em multicore_lockout. O reboot
     via `applier_reboot()` deve resetar TODOS os cores via PSM.
   - Mas se a sequência PSM->wdsel + watchdog não é feita exatamente
     certo, Core 1 pode ficar em estado pré-locked após reset.

4. **Linker Symbol pos vs apply path:**
   - Algum símbolo crítico (heartbeat, watchdog feed, etc) numa região
     que foi reescrita pelo applier deixa estado inválido até power cycle.

5. **USB CDC bug específico do RP2040:**
   - Comum em fóruns: USB CDC após `watchdog_reboot()` enumera mas não
     responde a host até replug físico.

## Plan de investigação

### Fase A: Coleta de evidências (sem mudança de código)
- [ ] Re-flashar v3.43.16 limpo
- [ ] Configurar WiFi via CLI
- [ ] Disparar OTA apply
- [ ] **Imediatamente** após apply rodar, capturar Serial cru por 5+ minutos
- [ ] Capturar `picotool save` do flash pós-apply (com mão BOOTSEL) para
      comparar com flash pós power-cycle
- [ ] Analisar diferenças

### Fase B: Instrumentação (logging Serial detalhado)
- [ ] Adicionar `Serial.printf("[BOOT] step N: ...\n"); Serial.flush();`
      antes e depois de cada etapa crítica em `AppManager::setup`
- [ ] Adicionar logs em `_displayMgr->begin()`, `_storageMgr->begin()`,
      `LogManager::begin()`, `NetworkManager` init, `WebManager` init
- [ ] Também adicionar em `StorageManager::begin` (mountFS, mkdir, snapshot
      restore, loadConfiguration)
- [ ] Build+flash via picotool, dispara OTA, captura serial

### Fase C: Análise comparativa
- [ ] Comparar boot path normal (USB flash) vs pós-apply (watchdog
      reboot)
- [ ] Identificar exatamente em qual linha trava
- [ ] Correlacionar com hipóteses

### Fase D: Fix
- [ ] Implementar fix focado na causa raiz identificada
- [ ] Validar via test F9 v2 múltiplos ciclos consecutivos sem power
      cycle físico

---

## Notas de pesquisa (ordem cronológica)

### 2026-05-07 — Achado #1: divergência crítica no WDSEL mask

Comparando `src/ota/applier.cpp::applier_reboot()` com `pico-sdk
hardware_watchdog/watchdog.c::_watchdog_enable()` (referência canônica):

**SDK (linha 53):**
```c
hw_set_bits(&psm_hw->wdsel,
    PSM_WDSEL_BITS & ~(PSM_WDSEL_ROSC_BITS | PSM_WDSEL_XOSC_BITS));
```
Reseta tudo EXCETO ROSC e XOSC. Máscara efetiva: `0x1fffc`.

**Nosso applier_reboot (linha 155):**
```c
*(volatile uint32_t*)(PSM_BASE_ADDR + PSM_WDSEL_OFFSET) = PSM_WDSEL_ALL;
// PSM_WDSEL_ALL = 0xFFFFFFFFu → efetivamente 0x1ffff (17 bits válidos)
```
Reseta TUDO incluindo ROSC e XOSC. Máscara efetiva: `0x1ffff`.

**Implicação:** ao resetar ROSC/XOSC durante watchdog reboot, os
oscillators físicos voltam ao estado boot. ROSC retoma frequência
default, XOSC requer re-startup do cristal externo. PLLs derivados
desses clocks (PLL_SYS, PLL_USB) podem ficar instáveis durante a
janela de reset. Em geral o `runtime_init_clocks` do SDK re-inicializa
os clocks no boot novo, mas se algum periférico (USB CDC?) começar a
operar antes desse re-init e pegar clocks inválidos, fica com PHY mal
inicializado — sintoma compatível com "USB enumera mas host não recebe
dados".

`PSM_WDSEL_BITS` (decomposição):
- Bit 0: ROSC
- Bit 1: XOSC  ← excluído pelo SDK
- Bit 2: CLOCKS
- Bit 3: RESETS
- Bit 4: BUSFABRIC
- Bit 5: ROM
- Bits 6-11: SRAM0-5
- Bit 12: XIP
- Bit 13: VREG_AND_CHIP_RESET
- Bit 14: SIO
- Bit 15: PROC0
- Bit 16: PROC1

ROSC (bit 0) é também excluído pelo SDK. ROSC é o oscillator interno
do RP2040; mantê-lo ativo durante o reset garante que mesmo se XOSC
tiver problemas no re-startup, há uma fonte de clock funcionando para
o boot.

### Próximo passo: testar fix do WDSEL

Mudar applier.cpp para usar `PSM_WDSEL_BITS & ~(ROSC | XOSC)` igual SDK.

Build v3.43.17, flash limpo, OTA cycle, ver se boot pós-apply completa
sem power cycle.

### 2026-05-07 — Achado #2: WiFi.end() do arduino-pico NÃO chama cyw43_arch_deinit

Path verificado em `arduino-pico/libraries/WiFi/src/WiFiClass.cpp`:

```cpp
void WiFiClass::end(void) {
    if (_wifiHWInitted) {
        disconnect();
    }
}
int WiFiClass::disconnect(bool wifi_off __unused) {
    if (_dhcpServer) {
        dhcp_server_deinit(_dhcpServer);
        free(_dhcpServer);
        _dhcpServer = nullptr;
    }
    if (_wifiHWInitted) {
        _wifiHWInitted = false;
        _wifi.end();   // CYW43::end()
    }
    return WL_DISCONNECTED;
}
```

E `CYW43::end()` em `lwIP_CYW43/src/utility/CYW43shim.cpp`:

```cpp
void CYW43::end() {
    _netif = nullptr;
    cyw43_wifi_leave(&cyw43_state, _itf);  // ← apenas sai da rede
}
```

**Não chama `cyw43_arch_deinit()`!** Que faria:
- `btstack_cyw43_deinit(context)` — BTstack libera state
- `cyw43_driver_deinit(context)` — manda comando ao chip CYW43 entrar
  em low-power com state limpo, libera driver SPI
- `lwip_nosys_deinit(context)` — libera lwIP
- `async_context_deinit(...)` — libera async context

**Implicação:** ao chamar apenas `WiFi.end()` antes do `watchdog_reboot`,
o chip CYW43 (módulo externo conectado por SPI) NÃO é resetado nem
colocado em estado conhecido. RP2040 resetado, mas chip CYW43 mantém
state em sua SRAM dele. No boot novo, `cyw43_arch_init` tenta
sincronizar com chip que pode estar em estado intermediário (com
buffer de comando incompleto, ou em rota a aguardar resposta).

Sintoma compatível: USB CDC sobe (porque RP2040 boot inicial roda),
mas Web/CLI ficam mudos (porque cyw43 mal-inicializado faz lwip
não rotear, e talvez algum lock interno deixa Core 1 / scheduler
parado).

**Power cycle físico desliga a alimentação do chip CYW43 (rota via
VBUS USB), garantindo state fresh no boot novo. Por isso resolve.**

### Fix #2: cyw43_arch_deinit() no orchestrator antes do reset

Em `src/ota/orchestrator.cpp::ota_apply_pending_update`:

```cpp
WiFi.end();
cyw43_arch_deinit();  // ← novo (v3.43.18)
LittleFS.end();
...
```

Aplicado em v3.43.18 (commit pendente). Combinado com fix #1 do WDSEL.

### 🚨 Achado #3 — `cyw43_arch_deinit()` é PERIGOSO antes de reboot

Após v3.43.18 disparar OTA apply (com cyw43_arch_deinit), o sistema
NUNCA mais bootou normalmente. Reproduzido empiricamente:

1. v3.43.18 flash via picotool, boot OK em 45s ✅
2. WiFi config + reset, HTTP up ✅
3. OTA apply v3.43.18 → v3.43.18 disparado
4. SIMUT trava (HTTP/CLI mudo)
5. Recovery: mão BOOTSEL + picotool erase -a (2 MiB) + load v3.43.19
   (sem fix #2) — **FALHA**, SIMUT continua mudo
6. Recovery: mão BOOTSEL + erase total + load v3.43.13 baseline
   (zero código da Fase 9) — **TAMBÉM FALHA**, SIMUT continua mudo

**Conclusão:** o `cyw43_arch_deinit()` **DESTRÓI o chip CYW43**
(módulo SPI externo, não-resetável por watchdog) em estado que persiste
através de:
- watchdog_reboot
- picotool erase -a (apaga flash inteira do RP2040)
- picotool load -x (re-flasha firmware)
- mão GPIO RUN reset
- Combinações de tudo acima

**Apenas power cycle físico** (USB unplug+replug, que corta a 3V3 do
módulo CYW43) recupera.

**Lições:**
1. `cyw43_arch_deinit()` ANTES de reset é uma combinação destrutiva.
   O deinit manda comandos SPI ao chip que o colocam em low-power /
   pre-init state, e quando o RP2040 é resetado abruptamente em
   seguida, o chip fica esperando comandos de continuação que nunca
   vêm. No boot novo, `cyw43_arch_init` esbarra no estado intermediário
   e trava (provavelmente em `cyw43_init` esperando ack do chip).

2. **arduino-pico `WiFi.end()` é intencionalmente menos agressivo** —
   chama apenas `cyw43_wifi_leave` (sai da rede mas mantém chip ativo).
   Isso é por design: deinit total quebra o cyw43 numa rota que requer
   power cycle.

3. F-OTA-BOOTLOOP é limitação fundamental do **hardware** Pico W,
   não bug do nosso firmware. Fixes de software têm escopo limitado:
   - WDSEL alinhado com SDK (não resetar ROSC/XOSC) ✅ (v3.43.17+, mantido)
   - cyw43_arch_deinit ❌ NÃO USAR (destroi o chip)
   - WiFi.end + LittleFS.end + IRQ off + watchdog_reboot — pratica padrão

### Fix #2 REVERTIDO em v3.43.19

`cyw43_arch_deinit` removido do orchestrator. Mantido só fix #1.
Aceito que power cycle físico pode ser necessário pós-OTA — limitação
de hardware do Pico W documentada, workaround conhecido.

### Estado da investigação (2026-05-07 ~01:13)

- v3.43.16 com Fase 9: snapshot capture+restore VALIDADO em HW (config
  preservada byte-a-byte através do apply destrutivo).
- F-OTA-BOOTLOOP: parcialmente caracterizado. Sintoma é falta do CYW43
  init pós-watchdog reboot. Power cycle físico resolve.
- Fix #1 (WDSEL): mantido. Pode ajudar em casos marginais mas não é
  fix completo.
- Fix #2 (cyw43_arch_deinit): NÃO USAR — quebra chip CYW43.

### Próximos caminhos (quando user voltar e fizer power cycle)

A. **Aceitar workaround**: documentar oficialmente que pós-OTA
   pode requerer power cycle físico em alguns casos. Adicionar
   instrução clara na UI ("se não voltar em 3 min, replugue o USB").

B. **Investigar reset alternativo**: pesquisar se há comando SPI
   que reseta o CYW43 antes do watchdog_reboot, sem deinit-completo.
   Talvez `cyw43_wifi_set_up(off)` + `cyw43_arch_disable_sta_mode`
   seja menos agressivo.

C. **Aceitar Fase 9 como funcional**: snapshot+restore funciona;
   bootloop é limitação inerente. v3.43.16 é o estado funcional.
   Versões 17/18/19 foram experimentos.



---

## Achado #4 — Bricks residuais pós fix #3 (~24%)

Após fix #3 (TRIGGER-only watchdog) em v3.43.21, brick rate caiu de
"100% reproduzível" pra **~24% residual** (4 bricks em 17 tentativas
válidas no loop20).

### Padrão observado (loop20 v3.43.21, 2026-05-07)

```
iter 1-4: PASS    (4 PASS)
iter 5:   BRICK   ← 1º brick após 4 iters
iter 6-12: PASS   (7 PASS — recuperou após picotool flash)
iter 13:  BRICK   ← 2º brick após 7 iters
iter 14:  FAIL falso (recovery script hung, não foi brick real)
iter 15:  BRICK   ← 3º brick após picotool recovery
iter 16-17: PASS  (2 PASS)
iter 18:  BRICK   ← 4º brick após 2 iters
iter 19-20: FAIL falsos (recovery hung)
```

Bricks reais: 5, 13, 15, 18. Espaçamento: 5, 8, 2, 3 iters.
Pattern: **acúmulo de estado** entre apply consecutivos.

### Hipóteses (ranqueadas)

1. **CYW43 module residual state (mais provável)**: chip externo
   conectado via SPI. Não é resetado por `watchdog_reboot` (não tem
   power cycle). Após N applies, estado interno (timers, registros,
   FSMs) acumula até init falhar silencioso na próxima vez. Fix
   candidato: drive `WL_REG_ON` pin LOW por 100ms antes do
   watchdog_reboot — power-cycles a CYW43 sem afetar RP2040. Requer
   identificar o pino correto (provavelmente `CYW43_PIN_WL_REG_ON`).

2. **LittleFS metadata accumulation**: cada format pós-apply pode
   deixar estado de wear-leveling que eventualmente confunde mount.
   Improvável (LittleFS é robusto a interrupções), mas possível.

3. **BTstack TLV (sector 251 do app slot)**: dados de pairing
   acumulam? Cada apply REWRITES isso (erase total do app slot pelo
   applier). Improvável.

4. **USB CDC enumeration drift**: cada watchdog_reboot dispara
   re-enumeração. Host kernel pode reusar device numbers stale.
   Improvável afetar firmware.

5. **Timing edge case no PSM reset**: alguns periféricos tem reset
   asíncrono. PSM_WDSEL_BITS reset em onda pode pegar peripheral
   em ponto desfavorável raramente. Pode explicar o caráter random.

### Próximo passo recomendado

Implementar fix #4: power-cycle do CYW43 via `WL_REG_ON` pin antes
do `watchdog_reboot`. Adicionar a `applier_reboot()` em
`src/ota/applier.cpp`:

```cpp
// Power-cycle CYW43 antes do reset. WL_REG_ON é o gate que controla
// 3V3 do chip externo via load switch. Drive low → chip desliga.
// Mantém RP2040 funcionando — single chip self-reset não funciona
// pra ele.
gpio_init(CYW43_DEFAULT_PIN_WL_REG_ON);
gpio_set_dir(CYW43_DEFAULT_PIN_WL_REG_ON, GPIO_OUT);
gpio_put(CYW43_DEFAULT_PIN_WL_REG_ON, 0);
busy_wait_ms(100);  // Tempo pra capacitores descarregarem
// (não precisa religar — watchdog reboot vai re-init)
// applier_reboot via watchdog/PSM ...
```

Validar com loop de 20 ciclos. Se brick rate cair pra <5%, fix
candidato a v4.0.0.


---

## Achado #5 — `safeReboot()` reproduz F-OTA-BOOTLOOP fora do path OTA

Reproduzido em 2026-05-07 21:20 testando alpha3:

1. picotool BOOTSEL+flash → boot 1 OK (WiFi conecta, login_init OK)
2. CLI `conf system ssid/pass/admin reset/user pass + write memory + reload confirm`
3. Boot 2 trava em F-OTA-BOOTLOOP residual (USB CDC enumera, CLI/HTTP mudos)

**Causa:** `LogManager::safeReboot()` chama `watchdog_enable(500, 1)` — mesma
mecânica que o bug do `applier_reboot` antes do fix #3. `watchdog_enable`
seta ENABLE+TRIGGER simultâneo, e ENABLE persiste pós-reset. Combinado
com pause_on_debug=1, pode deixar watchdog armado pós-boot inicial
disparando em loop.

```cpp
/* LogManager.cpp:636 — implementação atual */
watchdog_enable(500, 1);  // ENABLE + 500ms timeout, pause_on_debug=1
while (1) tight_loop_contents();
```

**Fix candidato (não aplicado ainda):** replicar o padrão do `applier_reboot`
(fix #3): TRIGGER apenas, LOAD=0xFFFFFF (8s), sem ENABLE persistente.
Aplicar a `safeReboot()` em LogManager.cpp e validar que `reload confirm`
não brica.

```cpp
/* Padrão proposto (estilo applier_reboot fix #3) */
markCleanReboot();
Serial.println("[SYS] Rebooting..."); Serial.flush();
delay(50); Serial.end(); delay(100);
*WATCHDOG_LOAD = 0xFFFFFF;
*PSM_WDSEL = PSM_WDSEL_RESET_MASK;
*WATCHDOG_CTRL = WATCHDOG_CTRL_TRIG;  // só TRIGGER
while (1) tight_loop_contents();
```

**Implicação importante:** se safeReboot tem o mesmo bug, ele pode estar
contribuindo para os "bricks residuais ~24%" mesmo no OTA flow:
- OTA apply usa `applier_reboot` (corrigido)
- MAS configurações pré-OTA usam `reload` em alguns paths (TBD investigar)

Validar essa hipótese antes do v4.0.0 e aplicar fix se necessário.

