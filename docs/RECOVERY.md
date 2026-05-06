# Recovery — Pico W brick após OTA

> Documento entregue como pré-requisito da Fase 7b do plano OTA. Ler ANTES
> de qualquer teste destrutivo. Mantenha `.uf2` da última versão estável
> salva localmente em pasta separada do projeto.

---

## Quando este documento é necessário

Use **uma e apenas uma** destas situações:

1. Após `POST /api/ota/apply` (sem `?test=1`) o device não volta a
   responder em ~30 s, ping falha, serial silente.
2. Boot loop visível (`SIMUT firmware vX.Y.Z` repetido no serial sem
   chegar em "[BOOT] AP detect").
3. Após queda de energia durante apply (raríssimo, mas plano §7 R3).
4. `picotool info` retorna sucesso para `2e8a:0003` (BOOTSEL) mas o
   device em modo aplicação (`2e8a:f00a`) não enumera CDC ou não
   responde `\r\n`.

NÃO USE este documento se:

- Boot OK mas Wi-Fi falhou (problema de rede, não de firmware — ver
  CLI `show net status`).
- Touch broken — não afeta boot a partir do v3.42.3 (default cal
  aplicado automaticamente).

---

## Pré-requisitos físicos

- Acesso ao **botão BOOTSEL** do Pico W. No Pico W oficial é o único
  botão da placa, do lado oposto ao USB.
- Cabo USB conectado ao host Linux.
- Permissão para gravar em `/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*`
  (membro do grupo `dialout`/`uucp`, ou rodar como sudo).
- `picotool` instalado em `/usr/local/bin/picotool` (já presente neste
  setup; também disponível em `~/.platformio/packages/tool-picotool-*/`).

---

## Procedimento BOOTSEL (sem precisar do firmware atual)

Este é o caminho **garantido** — não depende de software no device. É o
que você usa quando o firmware está completamente quebrado.

### Passos

1. Desconecte o cabo USB do Pico W.
2. **Pressione e segure o botão BOOTSEL.**
3. Conecte o cabo USB com o botão ainda pressionado.
4. Solte o botão. O Pico W deve aparecer como
   `Bus 003 Device NNN: ID 2e8a:0003 Raspberry Pi RP2 Boot` em
   `lsusb`.
5. Confirme:
   ```bash
   picotool info | head
   ```
   Deve listar o device em BOOTSEL.
6. Apague tudo (defensive — força LittleFS reformat no próximo boot):
   ```bash
   picotool erase
   ```
7. Grave o `.uf2` da última versão estável:
   ```bash
   picotool load -x /caminho/para/firmware.uf2
   ```
   `-x` reinicia o device automaticamente após gravar.
8. Aguarde ~30s, depois verifique:
   ```bash
   ls /dev/serial/by-id/                  # Deve mostrar Raspberry_Pi_Pico_W_*
   lsusb | grep 2e8a:f00a                 # Modo aplicação (não BOOTSEL)
   ```
9. Confirmar versão via serial:
   ```bash
   ./.venv/bin/python3 -c "
   import serial, time
   s = serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00', 115200, timeout=2)
   time.sleep(0.5); s.write(b'\r\nshow system info\r\n'); time.sleep(2)
   print(s.read_all().decode(errors='replace'))
   s.close()"
   ```

### Pós-recovery

Após o reflash, o device estará em **factory state** (LFS apagada). Será
necessário reconfigurar via CLI:

```
conf system ssid <SSID>
conf system pass <SENHA>
write memory
reload confirm
```

Senha admin web é regenerada automaticamente — buscar na saída de boot
serial (linha `SEC-003: FACTORY DEFAULTS ATIVADO` + senha de uso único).

---

## Trigger de BOOTSEL via software (1200 bps trick)

Quando o device **ainda responde a USB CDC** mas o firmware app está
travado (boot loop, mas USB descritor enumera):

```bash
./.venv/bin/python3 -c "
import serial, time
s = serial.Serial('/dev/serial/by-id/usb-Raspberry_Pi_Pico_W_*-if00', 1200, timeout=0.5)
time.sleep(0.1); s.close()
"
sleep 4
lsusb | grep 2e8a:0003
```

Convenção arduino native USB: abrir a porta a 1200 bps + close trigger
reset-to-BOOTSEL. Funciona enquanto o firmware tiver a USB stack viva
(antes do `WiFi.end` em `ota_apply_pending_update`, por exemplo).

Após confirmar BOOTSEL via lsusb, prosseguir como no procedimento
acima a partir do passo 5.

---

## O que **NÃO fazer**

- ❌ `picotool erase` enquanto o device está em modo APP — falha; só
  funciona em BOOTSEL.
- ❌ `picotool reboot` esperando que recupere device travado — só
  funciona se device responde.
- ❌ Reflashear sem `picotool erase` antes — staging area + metadata
  podem ter lixo do apply quebrado, causando comportamento estranho
  (ainda que o sketch slot esteja OK).
- ❌ Deletar `.uf2` da versão antiga antes de confirmar que a nova está
  estável em HW. Sempre tenha o **rollback** disponível.

---

## Diagnóstico — qual etapa do apply quebrou

Se possível recuperar o device, ler o serial do **primeiro boot pós-apply**
para identificar onde o orchestrator parou:

| Sintoma serial | Etapa | Causa provável |
|---|---|---|
| Sem nenhum log | Pré-orchestrator | Crash em `WiFi.end()` ou `LittleFS.end()` |
| Banner repetido sem `[BOOT] OTA post-apply` | Pré-applier | metadata write falhou |
| `[BOOT] OTA post-apply detected` aparece | Apply funcionou | Boot pós-update OK |
| `[BOOT] OTA post-apply detected: state=2 attempts=N` com N>1 | Loop | `OTA_MAX_APPLY_ATTEMPTS` será atingido |

Se atingir `OTA_MAX_APPLY_ATTEMPTS` (3), `ota_apply_pending_update`
retorna `MAX_ATTEMPTS` sem destruir — o device continua bootando o
firmware atual e expõe o status via `/api/ota/status` (futuro endpoint
da Fase 8).

---

## Versões mantidas como rollback

Mantenha **localmente** (não no repo, fora do alcance de erase
acidental):

- `firmware-v3.43.0.uf2` (Fase 6 fechada — sem aplicador real ainda).
- `firmware-v3.43.x-prefase7b.uf2` (último build antes do primeiro
  apply destrutivo).

Recomendado: copiar para `~/firmware-rollback-simut/` cada vez que uma
fase for marcada como ✅ HW validada.
