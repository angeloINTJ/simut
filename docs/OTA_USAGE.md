# Como usar OTA no SIMUT

> Guia prático para atualizar firmware do Pico W via web, sem precisar
> de cabo USB ou botão BOOTSEL.

## O que é

OTA (Over-The-Air) atualiza o firmware do Pico W remotamente via web —
sem precisar de cabo USB nem botão BOOTSEL.

## Pré-requisitos

- Device SIMUT já bootando com firmware **v3.42.0+** (qualquer versão recente)
- Wi-Fi configurado e device respondendo no IP da rede local
- Login admin ativo (senha conhecida)
- Build novo do firmware: `firmware.bin` em `.pio/build/pico_w_release/`
- Cabo USB conectado (apenas se algo der errado e precisar recovery via BOOTSEL)

## ⚠️ ATENÇÃO antes de começar

**O OTA reformata a LittleFS** — você perde:
- WiFi config (SSID, senha, modo IP)
- Senha admin (volta para OTP factory)
- Mapping dos sensores (slot N → sensor)
- Histórico de medidas (`/history/*`)
- Themes e idiomas customizados

**Fluxo recomendado**: backup antes → OTA → restaurar backup depois.

## Procedimento — opção 1: orquestrador automático

Forma mais fácil, com um comando:

```bash
cd /home/angelo/Documentos/SIMUT
./tools/ota_apply.py \
    --ip 192.168.3.195 \
    --user admin --pass 'SuaSenhaAtual' \
    --firmware .pio/build/pico_w_release/firmware.bin
```

O script automaticamente faz: login → backup .bkp → upload firmware →
commit → apply → espera boot.

Se admin estiver em factory state (recém-resetada), use também
`--new-pass 'NovaSenha'` para fazer chpass primeiro.

## Procedimento — opção 2: passo a passo manual

### 1. Build do firmware

```bash
cd /home/angelo/Documentos/SIMUT
~/.platformio/penv/bin/pio run
```

Confirma `[SUCCESS]` e gera `.pio/build/pico_w_release/firmware.bin`.

### 2. Login web + backup

Abra `http://<IP-do-pico>` no browser, faça login com admin.

Em **Files** → botão **Backup** → salva `.bkp` localmente.

### 3. Upload do firmware via curl ou web UI

Via curl (mais robusto):

```bash
# Pegue um nonce
NONCE=$(curl -s http://192.168.3.195/api/login_init | jq -r .nonce)
PASS_HASH=$(echo -n 'SuaSenha' | sha256sum | cut -d' ' -f1)

# Login (salva cookie)
curl -s http://192.168.3.195/api/login \
    -d "user=admin&pass=$PASS_HASH&nonce=$NONCE" \
    -c /tmp/simut.cookie

# Upload firmware com commit=1
curl -s http://192.168.3.195/api/restore?op=stage\&commit=1 \
    -F "file=@.pio/build/pico_w_release/firmware.bin" \
    -b /tmp/simut.cookie
```

Resposta esperada: `{"st":5,"bytes":...,"v":0,"committed":1}`.
- `st=5` = STAGED
- `v=0` = valid
- `committed=1` = metadata gravada

Tempo: ~30s para 947 KiB de firmware.

### 4. Disparar apply

```bash
curl -s http://192.168.3.195/api/ota/apply -b /tmp/simut.cookie -X POST
```

Resposta: `{"accepted":true,"mode":"apply"}`. Device reboota imediatamente.

### 5. Aguardar boot

**Tempo esperado**: 60-90 segundos. Durante esse tempo:
- Device USB CDC enumera mas CLI fica silencioso
- LFS auto-format em curso (~13s)
- Core 1 lockout recovery (~10s)
- Factory init (touch cal default, admin OTP regen, etc)

**Se passar de 3 minutos sem boot**: power cycle físico (desconectar +
reconectar USB). É o **Bug 2** documentado — relacionado a estado do
chip CYW43/TFT que precisa power-off real para resetar.

### 6. Capturar OTP do Serial USB

Após boot, conecte ao Serial USB e leia a senha admin one-time gerada:

```
SEC-003: FACTORY DEFAULTS ATIVADO
Senha ADMIN inicial: ABCD1234
Trocar no primeiro login (forcado).
```

### 7. Reconfigurar WiFi via Serial CLI

```
conf system ssid SuaRede
conf system pass SuaSenhaWiFi
conf ip dhcp
write memory
reload confirm
```

### 8. Restaurar backup

Login web com a OTP, fazer chpass, depois em **Files** → **Restore**
→ selecionar o `.bkp` baixado no passo 2.

Resposta esperada: `{"st":0,"chip":"...","fc":N,"fsm":0}`.
- `st=0` = OK
- `fc` = files counted

Device reaplica config + history + sensors. Não precisa reload —
restore aplica direto.

## Verificação pós-OTA

Via CLI ou web:

```bash
# CLI: confirma versão nova
SIMUT> show system info
 Firmware:  v3.43.11   ← versão nova

# Web: confirma config restaurada
curl -s http://192.168.3.195/api/perms -b /tmp/simut.cookie
# {"user":"admin","perms":65535,"version":"v3.43.11"}
```

## Recovery se OTA falhar

### Cenário A: device em BOOTSEL (USB ID `2e8a:0003`)

Boot2/firmware corrompido. Reflash via picotool:

```bash
# Tem firmware backup local? Use ele
picotool load -f -x ~/firmware-rollback-simut/firmware-v3.43.9.uf2

# Ou flash o build atual
picotool load -f -x .pio/build/pico_w_release/firmware.uf2
```

### Cenário B: device em app mode (USB ID `2e8a:f00a`) mas firmware silent (Bug 2)

1. Tente 1200bps trick para ir pra BOOTSEL:
   ```bash
   ./.venv/bin/python3 -c "
   import serial,time
   s=serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00',1200)
   time.sleep(0.5); s.close()
   "
   ```

2. Se 1200bps falhar (mesmo USB hung): **power cycle físico** —
   desconecte USB por 30s+, reconecte.

3. Se device entrar em BOOTSEL ou voltar em app mode: siga cenário A
   ou aguarde boot completo.

### Cenário C: tudo travado, BOOTSEL inacessível

Apertar e segurar botão BOOTSEL no Pico W enquanto pluga o cabo USB.
Device entra em BOOTSEL forçado. Reflash via picotool.

## Limites técnicos

| Item | Valor |
|------|-------|
| Tamanho máximo do firmware | 1020 KiB (slot da app) |
| Tamanho atual v3.43.11 | 947 KiB (91.6% do limite) |
| Margem livre | ~85 KiB |
| Tempo upload | ~32s para 947 KiB |
| Tempo apply (erase+write) | ~13s |
| Tempo boot pós-apply | 60-90s típico |
| Tentativas de apply (anti-loop) | 3 max |

## FAQ rápido

**P: Preciso fazer backup todo OTA?**
R: Sim. A partição LFS é apagada durante o stage upload. Sem backup,
perde tudo.

**P: Posso fazer OTA do firmware compactado (.bin.gz)?**
R: O servidor aceita só RAW (.bin). Build do PIO já gera .bin direto.

**P: Quantas vezes posso aplicar OTA?**
R: Sem limite teórico. Flash QSPI Pico W aguenta ~100k erase cycles
por sector. Cada apply faz ~255 erases.

**P: Funciona via internet (não só LAN)?**
R: Tecnicamente sim se o device tem IP público + porta 80 forwarded.
Mas não há HTTPS — credentials viajam em claro. Use só em LAN ou via VPN.

**P: O backup .bkp serve para outro Pico W?**
R: Não. O backup é atrelado ao chip_id (RP2040 unique ID 64-bit).
Restore em chip diferente retorna erro `st=6 chip mismatch`.

---

## Documentação relacionada

- `docs/OTA_FASE8.md` — fluxo completo + diagnóstico do Bug 2 boot intermitente
- `docs/RECOVERY.md` — procedimentos de recovery via BOOTSEL
- `docs/test_reports/` — relatórios de validação em hardware

**Última atualização**: 2026-05-06 (firmware v3.43.11).
