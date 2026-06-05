# Manual de operação — `pico_hand` para Claude Code

Este manual descreve como o **Claude Code** deve usar a ferramenta
`pico_hand` (mão robótica que aciona BOOTSEL/RESET de outro Pico via
serial USB) durante pipelines de teste automatizado.

A premissa é simples: sempre que o Pico **alvo** travar a ponto de não aceitar
mais flash via `picotool`, a `pico_hand` é o caminho automático para forçá-lo
de volta ao modo BOOTSEL **sem intervenção humana**.

---

## 1. Pré-requisitos (verificar uma vez)

1. O firmware `pico_hand.ino` está gravado no Pico controlador (a "mão").
2. Mão e alvo estão conectados ao mesmo computador via USB.
3. A fiação física entre mão e alvo está feita (ver `README.md`).
4. O usuário pode acessar `/dev/ttyACM*` (geralmente exige estar no grupo `dialout`).
5. O wrapper `pico_hand.sh` está acessível (caminho conhecido pelo agente).

> **Nunca** assuma que mão e alvo são portas fixas. A ordem de enumeração
> USB pode mudar a cada reset ou re-plugue.

---

## 2. Identificação da porta — antes de qualquer coisa

A mão **responde** a `PING` com `PONG`. O alvo **não** — ou está rodando
firmware do usuário que não fala esse protocolo, ou está em modo BOOTSEL
(que não aparece como serial). Use isso para distinguir.

### Forma recomendada: usar o wrapper

```bash
source /caminho/para/pico_hand.sh
hand_init
# stderr: "[pico_hand] mao detectada em: /dev/ttyACM1"
# exit 0 = sucesso, 1 = nao achou
```

A variável `PICO_HAND_PORT` fica exportada após `hand_init` e é reutilizada
por todas as chamadas seguintes de `hand`.

### Se quiser fixar a porta (ex.: udev rule)

```bash
export PICO_HAND_PORT=/dev/pico_hand   # link persistente criado por udev
hand RESET
```

`hand_detect_port` respeita `PICO_HAND_PORT` se já estiver setada e existir.

---

## 3. Tabela de comandos — referência rápida

| Comando         | Quando o agente deve usar                                     | Resposta esperada                |
|-----------------|---------------------------------------------------------------|----------------------------------|
| `PING`          | Health check antes de iniciar testes                          | `PONG`                           |
| `RESET`         | Reiniciar alvo sem entrar em BOOTSEL (ex.: rerodar firmware já flashado) | `OK RESET`            |
| `BOOTSEL`       | Forçar alvo para modo BOOTSEL (recovery; antes de novo flash) | `OK BOOTSEL`                     |
| `HOLD BOOTSEL`  | Início de sequência manual (raro; preferir `BOOTSEL`)         | `OK HOLD BOOTSEL`                |
| `HOLD RESET`    | Manter alvo em reset (ex.: para inspecionar circuito)         | `OK HOLD RESET`                  |
| `RELEASE BOOTSEL` / `RELEASE RESET` | Encerrar `HOLD` correspondente            | `OK RELEASE …`                   |
| `STATUS`        | Diagnóstico: confirmar que linhas estão soltas                | `STATUS BOOTSEL=… RESET=…`       |
| `PINOUT`        | Diagnóstico: confirmar GPIOs em uso                           | `PINOUT BOOTSEL=GP14 RESET=GP15 …` |
| `SELF_BOOTSEL`  | **Apenas** para reflashar o firmware da própria mão           | `OK SELF_BOOTSEL` (a porta some) |
| `HELP`          | Não usar em automação (saída multi-linha)                     | lista textual                    |

### Exit codes do wrapper `hand`

| Código | Significado                                  |
|--------|----------------------------------------------|
| `0`    | Resposta começa com `OK` ou `PONG`           |
| `1`    | Resposta começa com `ERR` ou inesperada      |
| `2`    | Timeout — sem resposta no prazo              |
| `3`    | Mão não inicializada / porta sumiu           |

---

## 4. Receitas

### 4.1 Health check no início do pipeline

```bash
source /caminho/para/pico_hand.sh

if ! hand_init; then
    echo "FATAL: mao indisponivel" >&2
    exit 1
fi

if ! hand PING > /dev/null; then
    echo "FATAL: mao nao responde a PING" >&2
    exit 1
fi
```

### 4.2 Flash com recovery automático

Esta é **a receita central** do uso pretendido. Tente flash normal; se
falhar, force BOOTSEL pela mão e tente de novo.

```bash
flash_with_recovery() {
    local uf2="$1"

    # Tentativa 1: alvo já está em BOOTSEL ou aceita reset USB?
    if picotool load -x "$uf2" 2>/dev/null; then
        return 0
    fi

    echo "[flash] picotool falhou — invocando pico_hand para BOOTSEL forcado"
    hand BOOTSEL || return 1

    # Aguarda o RPI-RP2 enumerar (montagem USB mass storage).
    sleep 1.5

    # Tentativa 2: agora deve funcionar.
    picotool load -x "$uf2"
}
```

### 4.3 Reset simples entre casos de teste

```bash
hand RESET && sleep 0.5    # tempo para o firmware do alvo subir
```

### 4.4 Garantia de estado limpo (uso defensivo)

Sempre que um script começar, soltar ambas as linhas garante que nenhum
`HOLD` órfão (de uma execução anterior interrompida) deixou o alvo
travado:

```bash
trap hand_release_all EXIT
hand_release_all   # solta tudo no inicio
```

### 4.5 Diagnóstico quando algo está estranho

```bash
hand PING       # mao viva?
hand STATUS     # alguma linha presa em PRESSED?
hand PINOUT     # GPIOs configurados batem com a fiacao?
```

---

## 5. Fluxograma de decisão (recovery)

```
Upload do firmware no alvo
        |
        v
   picotool load
        |
   sucesso? ----- SIM ---> terminou
        |
       NAO
        |
        v
   hand PING
        |
   PONG? -------- NAO ---> abortar: mao indisponivel, requer humano
        |
       SIM
        |
        v
   hand BOOTSEL
        |
   OK? ---------- NAO ---> abortar: fiacao? alvo morto?
        |
       SIM
        |
        v
   sleep 1.5s   (espera RPI-RP2 montar)
        |
        v
   picotool load (segunda tentativa)
        |
   sucesso? ----- SIM ---> terminou
        |
       NAO
        |
        v
   abortar: alvo provavelmente em hard fault de hardware
```

---

## 6. Armadilhas a evitar

1. **Não confundir as portas.** Mandar `BOOTSEL` para a porta errada coloca
   o **alvo errado** em recovery. Sempre use `hand_init` ou confirme com
   `hand PING`.

2. **Não chamar `SELF_BOOTSEL` em automação.** Esse comando coloca a
   *própria mão* em modo BOOTSEL — a serial some e o pipeline trava.
   `SELF_BOOTSEL` é exclusivamente para reflashar o firmware da mão.

3. **`HOLD` exige `RELEASE` pareado.** Se o script morrer entre os dois,
   o alvo fica trancado em reset/BOOTSEL para sempre. Sempre use:
   ```bash
   trap hand_release_all EXIT
   ```

4. **Tempo de enumeração USB.** Após `hand BOOTSEL`, o alvo precisa de
   ~1–2 segundos para reaparecer como dispositivo `RPI-RP2`. Não tente
   `picotool` imediatamente — adicione `sleep 1.5` ou faça polling.

5. **Buffer da serial pode ter lixo.** A primeira leitura após conectar
   pode trazer fragmentos de respostas antigas. O wrapper já drena o
   buffer durante a detecção; em uso prolongado, prefira sempre usar
   `hand` (que lê uma linha por vez) em vez de `cat /dev/ttyACM*`.

6. **Não abrir múltiplos consumidores na porta.** Um `cat /dev/ttyACM1`
   rodando em background "rouba" as respostas de `hand`. Use uma
   ferramenta de cada vez.

7. **Permissões.** Em sistemas onde o usuário não está no grupo `dialout`,
   `stty -F /dev/ttyACM*` falha silenciosamente. Verifique com:
   ```bash
   groups | grep -q dialout || echo "FALTA: usuario fora do grupo dialout"
   ```

---

## 7. Resumo: o que o Claude Code deve fazer

1. No início de qualquer pipeline que envolva flash de Pico:
   ```bash
   source /caminho/para/pico_hand.sh
   hand_init || exit 1
   trap hand_release_all EXIT
   ```

2. Ao flashar firmware, usar `flash_with_recovery` (receita 4.2) em vez
   de `picotool load` direto.

3. Entre casos de teste que precisam de estado limpo, usar `hand RESET`.

4. **Nunca** chamar `SELF_BOOTSEL` em automação.

5. Em qualquer falha: confirmar via `hand PING` se a mão segue viva
   antes de declarar problema no alvo.
