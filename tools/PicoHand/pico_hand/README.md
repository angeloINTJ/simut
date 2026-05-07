# pico_hand (versão IDE Arduino)

Firmware para um Raspberry Pi Pico funcionar como **"mão robótica"** que aciona
remotamente os botões **BOOTSEL** e **RESET** de outro Pico (o "alvo"), via
comandos enviados por uma porta serial USB CDC.

Pensado para destravar pipelines de teste automatizado (SIMUT) quando o alvo
trava antes de aceitar o próximo binário.

---

## Pré-requisitos da IDE Arduino

1. Instalar o core **arduino-pico** (Earle Philhower):
   - Em **Arquivo → Preferências → URLs adicionais para Gerenciadores de Placas**, adicione:
     ```
     https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
     ```
   - Em **Ferramentas → Placa → Gerenciador de Placas**, procure por `pico` e instale **"Raspberry Pi Pico/RP2040"**.

2. Selecionar:
   - **Placa**: `Raspberry Pi Pico` (ou `Pico W`, conforme o hardware da sua "mão").
   - **Porta**: a porta serial onde a "mão" está conectada.

> O comando `SELF_BOOTSEL` usa `rp2040.rebootToBootloader()`, que é específico
> deste core. Se você usar o core Mbed oficial da Arduino, esse comando precisa
> ser adaptado — todo o resto funciona sem mudança.

---

## Fiação

```
   Pico "mão" (este firmware)            Pico alvo
   ---------------------------           ---------
   GP0   ─────────────────────────────►  pad/lado quente do botão BOOTSEL
   GP1   ─────────────────────────────►  pad/lado quente do botão RUN (reset)
   GND   ─────────────────────────────►  GND  (obrigatório!)
```

> Os GPIOs operam em **open-drain emulado**: viram `OUTPUT` em `LOW` quando
> "pressionados" e voltam a `INPUT` (alta impedância) quando "soltos". Isso
> evita brigar com o pull-up do Pico alvo e elimina o risco de curto se o
> botão físico for pressionado simultaneamente. **Nunca** o GPIO é levado
> a nível alto.

Quer outros pinos? Mude `PIN_BOOTSEL` / `PIN_RESET` no topo de `pico_hand.ino`.

---

## Compilação e gravação

1. Abra `pico_hand/pico_hand.ino` na IDE Arduino.
2. Coloque o Pico que vai virar a "mão" em modo BOOTSEL (segura BOOTSEL e plugue o USB).
3. Clique em **Carregar** (`Ctrl+U`). Da segunda vez em diante, a placa já reaparece como porta serial e a IDE consegue gravar sem o BOOTSEL manual.

Para reflashar mais tarde sem mexer nos botões: envie `SELF_BOOTSEL` pela serial e a placa volta sozinha para o modo BOOTSEL.

---

## Protocolo serial

- USB CDC, 115200 8N1 (a velocidade é ignorada na CDC, mas use isso por hábito).
- Uma linha por comando, terminador `\n` ou `\r\n`.
- Resposta sempre começa com `OK`, `ERR` ou `PONG` (para `PING`).
- Comandos são *case-insensitive*.

| Comando             | Resposta                          | O que faz                                                      |
|---------------------|-----------------------------------|----------------------------------------------------------------|
| `PING`              | `PONG`                            | Teste de conectividade.                                        |
| `RESET`             | `OK RESET`                        | Pulsa o RUN do alvo (reset).                                   |
| `BOOTSEL`           | `OK BOOTSEL`                      | Sequência completa: segura BOOTSEL, pulsa RESET, libera tudo.  |
| `HOLD BOOTSEL`      | `OK HOLD BOOTSEL`                 | Mantém BOOTSEL pressionado indefinidamente.                    |
| `HOLD RESET`        | `OK HOLD RESET`                   | Mantém RESET pressionado indefinidamente.                      |
| `RELEASE BOOTSEL`   | `OK RELEASE BOOTSEL`              | Solta BOOTSEL.                                                 |
| `RELEASE RESET`     | `OK RELEASE RESET`                | Solta RESET.                                                   |
| `STATUS`            | `STATUS BOOTSEL=... RESET=...`    | Estado atual de cada linha (`PRESSED`/`RELEASED`).             |
| `PINOUT`            | `PINOUT BOOTSEL=GP.. RESET=GP..`  | Mostra os GPIOs em uso.                                        |
| `SELF_BOOTSEL`      | `OK SELF_BOOTSEL`                 | Põe a **própria mão** em BOOTSEL (útil para reflashar via cmd).|
| `HELP`              | lista textual                     | Lista todos os comandos.                                       |

> Atenção: `HOLD` sem `RELEASE` posterior deixa o botão pressionado até o
> próximo `RELEASE`, reset da mão ou desconexão. Em scripts, sempre garanta
> o pareamento (use `trap` no bash, `try/finally` em Python, etc.).

---

## Exemplos no bash

Assumindo que a "mão" apareceu como `/dev/ttyACM1` (o alvo costuma ser
`/dev/ttyACM0`):

```bash
HAND=/dev/ttyACM1

# Configura a porta uma vez (raw, sem eco, sem mexer em CR/LF).
stty -F "$HAND" 115200 raw -echo -echoe -echok -echoctl -echoke

# Helper: envia comando e lê uma linha de resposta.
hand() {
    echo "$1" > "$HAND"
    timeout 1 head -n 1 < "$HAND"
}

hand PING        # → PONG
hand RESET       # → OK RESET
hand BOOTSEL     # → OK BOOTSEL  (alvo cai em modo BOOTSEL e reaparece como RPI-RP2)
```

Fluxo típico de recovery dentro do seu script de teste:

```bash
if ! picotool info >/dev/null 2>&1; then
    echo "[hand] alvo não responde — forçando BOOTSEL"
    hand BOOTSEL
    sleep 1            # tempo para o RPI-RP2 montar
fi
picotool load -x firmware.uf2
```

---

## Estrutura do projeto

```
pico_hand/
└── pico_hand.ino   # sketch único, autocontido
```

A pasta precisa ter o **mesmo nome** do `.ino` — exigência da IDE Arduino.

---

## Notas de segurança elétrica

- Conecte o **GND** das duas placas. Sem isso, os "botões virtuais" não têm
  referência comum e o comportamento é imprevisível.
- Não há resistor de série obrigatório, mas um resistor de 470 Ω – 1 kΩ em
  série em cada linha é uma boa prática para limitar correntes em caso de
  acidente de fiação.
- O firmware **nunca** dirige as linhas para nível alto, então é seguro
  manter a mão conectada mesmo enquanto alguém pressiona os botões físicos
  do Pico alvo.
