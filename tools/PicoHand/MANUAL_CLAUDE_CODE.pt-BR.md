# Manual de operação — `pico_hand` para Claude Code

Este manual descreve como o **Claude Code** deve usar a ferramenta
`pico_hand` (mão robótica que aciona BOOTSEL/RESET de outro Pico via
serial USB) durante pipelines de teste automatizado.

A premissa é simples: sempre que o Pico **alvo** travar a ponto de não aceitar
mais flash via `picotool`, a `pico_hand` é o caminho automático para forçá-lo
de volta ao modo BOOTSEL **sem intervenção humana**.

[English](MANUAL_CLAUDE_CODE.md)

---

## 0. Estado da bancada — verificado em 23/07/2026

Tudo abaixo foi exercitado contra o dispositivo real, não lido do código.

| Linha | GPIO | Estado |
|-------|------|--------|
| RESET | GP0 | ✅ **Funciona de ponta a ponta.** O uptime do alvo caiu de 134 s para 19 s com `hand RESET`. |
| BOOTSEL | GP1 | ✅ **Funciona de ponta a ponta.** O alvo entrou em BOOTSEL e foi gravado sem ninguém encostar nele. |

A receita completa de recuperação (§4.2) foi executada do início ao fim: o
`hand BOOTSEL` retornou `OK`, a porta CDC do alvo sumiu, o `picotool info`
enxergou o dispositivo, o `picotool load` gravou a imagem e reiniciou, e o alvo
voltou respondendo `Firmware: 1.5.2-rc4`. Zero intervenção humana.

**Fiação que importa.** O RESET mantém resistor de 100 Ω em série e funciona
através dele. O BOOTSEL precisa de ligação direta — 100 Ω nessa linha impedem o
funcionamento, pelo motivo da §7.1. Chegar aqui exigiu dois diagnósticos
errados, ambos registrados na §7.1 para não se repetirem.

Para gravação de rotina, o `pio run -t upload` continua sendo o caminho mais
simples: faz o próprio reset por toque de 1200 bps e dispensa o dispositivo. A
mão é o que te salva quando o alvo trava a ponto de não atender USB.

---

## 1. Pré-requisitos (verificar uma vez)

1. O firmware `pico_hand.ino` está gravado no Pico controlador (a "mão").
2. Mão e alvo estão conectados ao mesmo computador via USB.
3. A fiação física entre mão e alvo está feita (ver `pico_hand/README.pt-BR.md`).
4. O usuário pode acessar `/dev/ttyACM*` (geralmente exige estar no grupo `dialout`).
5. O wrapper `pico_hand.sh` está acessível (caminho conhecido pelo agente).

> **Nunca** presuma que mão e alvo estão em portas fixas. A ordem de enumeração
> USB pode mudar a cada reset ou reconexão.

---

## 2. Identificação das portas — antes de tudo

### Mais rápido e confiável: perguntar ao udev

A mão é um **Pico** comum; o alvo SIMUT é um **Pico W**. Os descritores USB
dizem isso diretamente, sem tráfego serial e sem risco de mandar comando para a
placa errada:

```bash
for p in /dev/ttyACM*; do
  udevadm info -q property -n "$p" | grep -q 'ID_MODEL=Pico_W' && echo "$p alvo" && continue
  udevadm info -q property -n "$p" | grep -q 'ID_MODEL=Pico'   && echo "$p mao"
done
```

Nesta bancada: mão `ID_SERIAL_SHORT=E660C062131E3E27`, alvo
`E6642815E34C1824`. Números de série são estáveis entre reenumerações, então
são a âncora mais segura se você precisar fixar uma placa.

### Ou use o wrapper

```bash
source /caminho/para/pico_hand.sh
hand_init
# stderr: "[pico_hand] hand detected at: /dev/ttyACM1"
# saída 0 = sucesso, 1 = não encontrada
```

O `hand_init` sonda cada porta com `PING` e fica com a que responder `PONG`. A
variável `PICO_HAND_PORT` é exportada e reutilizada pelas chamadas seguintes.

### Se quiser porta fixa (ex.: regra udev)

```bash
export PICO_HAND_PORT=/dev/pico_hand   # symlink persistente criado por udev
hand RESET
```

---

## 3. Referência de comandos

Doze comandos. Cada resposta tem uma linha, exceto `HELP` e `PULSE_TEST`.

| Comando | Quando o agente deve usar | Resposta real |
|---------|---------------------------|---------------|
| `PING` | Teste de saúde antes de iniciar | `PONG` |
| `RESET` | Reiniciar o alvo sem entrar em BOOTSEL | `OK RESET`, ou `ERR RESET VFY:<falha>` |
| `BOOTSEL` | Forçar o alvo em BOOTSEL (recuperação) | `OK BOOTSEL`, ou `ERR BOOTSEL VFY:<falha>` |
| `HOLD BOOTSEL` \| `HOLD RESET` | Manter uma linha acionada | `OK HOLD <nome>` |
| `RELEASE BOOTSEL` \| `RELEASE RESET` | Encerrar o `HOLD` correspondente | `OK RELEASE <nome>` |
| `STATUS` | Confirmar que ambas as linhas estão liberadas | `STATUS BOOTSEL=RELEASED RESET=RELEASED VFY:BOOTSEL_ACT=HIGH VFY:RESET_ACT=HIGH` |
| `PINOUT` | Confirmar quais GPIOs estão em uso | `PINOUT BOOTSEL=GP1 RESET=GP0 LED=GP25` |
| `VERIFY` | Ler o analisador lógico (ver §5) | `VFY BOOTSEL=OK RESET=OK HB=2us E:… A:…` |
| `VERIFY CLEAR` | Zerar contadores de falha antes de medir | `OK VFY CLEAR` |
| `PULSE_TEST <linha> <ms> <n>` | Pulsos cronometrados, com largura obtida | multilinha, termina em `DONE PULSE_TEST` |
| `DEBUG ON` \| `OFF` \| `STATUS` | Log verboso — ver o aviso na §7.2 | `OK DEBUG ON` / `OK DEBUG OFF` |
| `SELF_BOOTSEL` | **Apenas** para regravar o firmware da própria mão | `OK SELF_BOOTSEL` (a porta some) |
| `HELP` | Não usar em automação (saída multilinha) | lista textual |

As larguras de pulso são respeitadas com precisão — um `PULSE_TEST RESET 100 3`
reportou `actual=100ms` nos três pulsos.

### Códigos de saída do wrapper

| Código | Significado |
|--------|-------------|
| `0` | Resposta é de sucesso para aquele comando |
| `1` | Resposta começa com `ERR`, ou não foi reconhecida |
| `2` | Timeout — sem resposta dentro de `PICO_HAND_TIMEOUT` |
| `3` | Mão não inicializada / porta sumiu |

Sucesso não é só `OK`/`PONG`: comandos de consulta respondem com o próprio
cabeçalho, então `STATUS`, `PINOUT`, `VFY` e `DONE` também contam como sucesso.
Uma versão anterior do wrapper tratava tudo isso como falha, o que fazia todo
comando de diagnóstico retornar 1.

---

## 4. Receitas

### 4.1 Teste de saúde no início do pipeline

```bash
source /caminho/para/pico_hand.sh

if ! hand_init; then
    echo "FATAL: mao indisponivel" >&2
    exit 1
fi

if ! hand PING > /dev/null; then
    echo "FATAL: mao nao responde ao PING" >&2
    exit 1
fi
```

### 4.2 Gravação com recuperação automática

A receita central, **verificada de ponta a ponta nesta bancada**: BOOTSEL
forçado pela mão, imagem gravada pelo picotool, alvo de volta reportando a
versão nova, sem ninguém envolvido.

```bash
flash_with_recovery() {
    local uf2="$1"

    # Tentativa 1: caminho normal. O uploader do pio faz o próprio reset por
    # toque de 1200 bps, então funciona em qualquer alvo que ainda atenda USB.
    if picotool load -x "$uf2" 2>/dev/null; then
        return 0
    fi

    echo "[flash] picotool falhou — acionando pico_hand para BOOTSEL forcado"
    hand BOOTSEL || return 1

    sleep 1.5      # espera o RPI-RP2 enumerar
    picotool load -x "$uf2"
}
```

### 4.3 Reset simples entre casos de teste

```bash
hand RESET && sleep 6    # SIMUT leva ~5 s para subir e reconectar no Wi-Fi
```

Verificado: uptime caiu de 134 s para 19 s, batendo com os ~17 s de tempo real
decorridos desde o pulso.

### 4.4 Garantia de estado limpo (uso defensivo)

```bash
trap hand_release_all EXIT
hand_release_all   # libera tudo na partida
```

### 4.5 Diagnóstico quando algo está estranho

```bash
hand PING       # mao viva?
hand STATUS     # alguma linha presa em PRESSED?
hand PINOUT     # os GPIOs batem com a fiacao?
hand VERIFY     # o que o analisador logico realmente ve?
```

---

## 5. Lendo o analisador lógico

O Core 1 amostra as duas linhas a 10 kHz e compara com o que o Core 0 diz estar
acionando. O `VERIFY` reporta:

```
VFY BOOTSEL=OK RESET=OK HB=2us E:BOOTSEL=HIGH E:RESET=HIGH A:BOOTSEL=HIGH A:RESET=HIGH
```

- `BOOTSEL=` / `RESET=` — código de falha: `OK`, `STUCK_HIGH` (circuito aberto),
  `STUCK_LOW` (curto para GND), `GLITCH`, `NO_VERIFIER`. Uma falha vem seguida
  de `(contagem,idade_us)`.
- `HB=` — idade do heartbeat do Core 1. Poucos µs é saudável; perto de
  1 000 000 µs significa que o núcleo verificador morreu e os códigos estão velhos.
- `E:` — nível esperado (o que o Core 0 está acionando).
- `A:` — nível real lido de volta no pino.

Uma divergência precisa persistir 5 ms para ser sinalizada, o que é justamente
o que torna as leituras da §7.1 significativas em vez de ruído.

---

## 6. Fluxograma de decisão (recuperação)

```
Gravar firmware no alvo
        │
        ▼
   pio run -t upload      (toque de 1200 bps — dispensa o dispositivo)
        │
   sucesso? ──── SIM ──▶ pronto
        │
       NAO
        │
        ▼
   hand PING
        │
   PONG? ──────── NAO ──▶ aborta: mao indisponivel, precisa de humano
        │
       SIM
        │
        ▼
   hand BOOTSEL
        │
   OK? ────────── NAO ──▶ ver §7.1: resistor em serie na linha? mau contato?
        │
       SIM
        │
        ▼
   sleep 1.5s   (espera montar RPI-RP2)
        │
        ▼
   picotool load (segunda tentativa)
        │
   sucesso? ──── SIM ──▶ pronto
        │
       NAO
        │
        ▼
   aborta: alvo provavelmente em hard fault de hardware
```

---

## 7. Armadilhas a evitar

### 7.1 A linha de BOOTSEL não é um nó passivo de botão

O pad de BOOTSEL do alvo é o QSPI_SS, o chip-select da flash — não um nó quieto
que só um botão toca. Duas consequências, ambas medidas aqui.

**O verificador não consegue validá-la com o alvo rodando.** Um RP2040 em
execução aciona o QSPI_SS constantemente, então a mão lê a linha alternando
entre HIGH e LOW sem nada pressionado — três `VERIFY` consecutivos retornaram
`A:BOOTSEL=HIGH`, depois `LOW`, depois `HIGH`. Durante o boot o alvo lê flash em
rajadas longas, segurando a linha baixa além do limiar de 5 ms, e é por isso que
`hand BOOTSEL` reporta `ERR BOOTSEL VFY:BOOTSEL_STUCK_LOW` logo após o próprio
pulso de reset. **Essa falha é artefato da medição, não prova de curto.** A
oscilação é, em si, boa notícia: significa que o fio está num nó vivo, não solto.

**O BOOTSEL precisa de ligação direta; o RESET tolera resistor em série.** Os
dois pinos do alvo são eletricamente diferentes, e a bancada provou isso nos
dois sentidos:

- **RUN** é entrada de alta impedância com pull-up interno fraco. 100 Ω para GND
  vence com folga — e é por isso que o `hand RESET` funciona através do
  resistor, verificado e não presumido.
- **QSPI_SS** é saída push-pull acionada ativamente pelo alvo. Puxá-la através
  de 100 Ω contra um driver de impedância de saída na casa de dezenas de ohms
  forma um divisor que nunca chega a nível lógico baixo, e o BOOTSEL falha
  silenciosamente. O botão que ela emula é um curto direto para GND, 0 Ω por
  projeto. Com o resistor removido, o BOOTSEL funcionou na primeira tentativa.

### O teste de segurar, e quando ele mente

Segurar o BOOTSEL acionado com o alvo rodando deveria derrubá-lo: com ligação
direta, puxar o QSPI_SS para baixo corrompe toda busca de instrução. É a única
forma de provar que o fio está conectado, porque o `VERIFY` não consegue (ver
acima). Mas tem uma precondição fácil de esquecer.

**Com resistor em série na linha, o teste não significa nada.** A linha nunca
chega a nível baixo de verdade, então o alvo segue rodando esteja o fio
conectado ou não. Concluir "desconectado" por esse caminho é errado.

**Com ligação direta, o teste é decisivo** — e se paga. Uma execução já sem o
resistor mostrou o alvo sobrevivendo ao hold, com uptime avançando de 54 s para
72 s. Isso indicou corretamente mau contato, o que se confirmou: o fio foi
reencaixado e o BOOTSEL passou a funcionar imediatamente.

Ou seja: verifique primeiro se há resistência em série, depois confie no teste
de segurar.

### 7.2 O modo DEBUG muda qual é a primeira linha de resposta

Com `DEBUG ON`, a mão ecoa cada linha recebida antes de responder:

```
[DBG t=869359] RX line: 'PING'
PONG
```

Qualquer leitor que pegue a primeira linha recebe o eco de debug em vez da
resposta. Isso quebrava o `hand_init` por completo — ele procurava `PONG`, via
`[DBG …]` e reportava a mão como ausente. O wrapper agora pula linhas `[DBG`,
mas qualquer código que leia a porta diretamente precisa fazer o mesmo.

### 7.3 Nunca mantenha a porta aberta com dois leitores

Um segundo consumidor rouba as respostas. Não é hipotético: o monitor serial do
Arduino IDE estava com `/dev/ttyACM1` aberto durante esta sessão e toda tentativa
de conexão falhou com `Errno 16, Device or resource busy`. Cheque antes de
culpar o dispositivo:

```bash
fuser -v /dev/ttyACM1
```

### 7.4 Um descritor de arquivo por troca, não dois

Escrever com `echo > porta` e ler com `head < porta` abre e fecha o dispositivo
duas vezes. A mão responde no intervalo entre o fechamento do handle de escrita
e a abertura do de leitura, sem nenhum leitor conectado, e a resposta se perde —
medido aqui como string vazia em toda tentativa, enquanto um descritor único
mantido aberto retornou `PONG`. O wrapper agora faz:

```bash
exec 3<>"$porta"
printf 'PING\n' >&3
read -r -t 2 resp <&3
exec 3>&-
```

### 7.5 Nunca chame `SELF_BOOTSEL` em automação

Isso coloca a *própria mão* em BOOTSEL — a porta serial some e o pipeline
trava. Existe exclusivamente para regravar o firmware da mão.

### 7.6 `HOLD` exige `RELEASE` pareado

Se o script morrer entre os dois, o alvo fica travado em reset ou com BOOTSEL
acionado. Sempre:

```bash
trap hand_release_all EXIT
```

### 7.7 Tempo de enumeração USB

Depois de `hand BOOTSEL`, o alvo leva ~1–2 s para reaparecer como `RPI-RP2`.
Depois de `hand RESET`, o SIMUT leva ~5 s para subir e reconectar no Wi-Fi.

### 7.8 Permissões

```bash
groups | grep -q dialout || echo "FALTANDO: usuario fora do grupo dialout"
```

---

## 8. A ponte serial

O `Serial2` da mão (UART1, GP4 TX / GP5 RX) é encaminhado de forma transparente
para o USB. Ligue-o à UART de debug do alvo e a mão repassa esse tráfego pela
própria porta CDC, de modo que uma única conexão USB carrega tanto o controle do
dispositivo quanto o console do alvo. As linhas da ponte se intercalam com as
respostas de comando, então um parser precisa tolerar texto não solicitado —
mais uma razão para casar por prefixos conhecidos em vez de presumir que a
próxima linha é a sua resposta.

---

## 9. Resumo: o que o Claude Code deve fazer

1. No início de qualquer pipeline que envolva gravar Pico:
   ```bash
   source /caminho/para/pico_hand.sh
   hand_init || exit 1
   trap hand_release_all EXIT
   ```
2. Para gravar, prefira `pio run -e <env> -t upload`. Ele faz o próprio reset por
   toque de 1200 bps e dispensa o dispositivo. Recorra à mão só quando isso falhar.
3. Entre casos de teste que precisem de estado limpo, use `hand RESET` + `sleep 6`.
4. **Nunca** chame `SELF_BOOTSEL` em automação.
5. Em qualquer falha, confirme com `hand PING` que a mão está viva antes de
   culpar o alvo — e com `hand VERIFY` antes de culpar a fiação.
6. Lembre que o `VERIFY` não valida a linha de BOOTSEL com o alvo rodando
   (§7.1), e que o teste de segurar também não vale enquanto houver resistor
   em série.

---

## 10. Uso com o SIMUT Air (build que hiberna) — 06/09/2026

O alvo com a imagem `pico_w_air` **some do USB quando dorme** (solta o pull-up
do D+ de propósito) e reenumera a cada wake. Três consequências para a mão:

1. **Alvo ausente não é alvo morto.** Antes de acionar a mão, espere um
   intervalo de histórico (`air status` mostra `wake=`) — o aparelho volta
   sozinho. A suíte `tools/air_test_suite.py` faz essa espera.
2. **`hand RESET` dá boot frio (M0).** O pulso é no pino RUN, que é reset
   global do chip: o mapa de scratch do firmware (`src/LogManager.cpp:605`)
   registra que esses registradores são zerados por "power cycle / physical
   reset", e o marcador de hibernação do Air vive justamente em `scratch[0]`.
   Ou seja, o alvo volta em modo operacional, sem precisar de `air stop`.
   (Uma revisão anterior desta seção afirmava o contrário; a suíte
   `tools/air_test_suite.py` agora mede o modo pós-reset em T02.)
3. **O RESET não prova nada sobre o wake.** Como ele restaura o ROSC e os
   clocks de fábrica, recupera o alvo mesmo que o item F01 do plano seja real
   (ROSC desligado antes de dormir e não religado no wake). A única prova de
   que o caminho do sono funciona é o alvo **reenumerar sozinho** dentro de
   `wakeSec` + margem. Se nem o RESET trouxer o alvo de volta, o problema é
   alimentação ou cabo, não firmware.

## 11. Canal PROBE — cronômetro do ciclo (06/09/2026)

A mão ganhou um terceiro canal: **`PROBE`, entrada em GP2** (pino físico 4),
ligada ao **GP16 do alvo**, que o firmware do SIMUT Air mantém em nível alto
enquanto está acordado e baixo enquanto dorme. O Core 1 já amostrava a 10 kHz
para o `VERIFY`, então a sonda pega carona nesse mesmo laço e guarda só as
**transições**, com carimbo de `micros()`, num anel de 64 bordas.

| Comando | Resposta |
|---|---|
| `PROBE STATUS` | `PROBE pin=GP2 level=HIGH edges=2/64 dropped=0 armed=YES` |
| `PROBE START` | `OK PROBE START` — limpa o anel e arma |
| `PROBE READ` | uma linha `EDGE <i> <H\|L> <us>` por borda, terminando em `DONE PROBE edges=<n> dropped=<n>` |

**Por que ele vale mais que o USB.** A enumeração USB atrasa cerca de um segundo
em relação ao boot, e esse segundo cai inteiro dentro da janela que se quer
medir; a serial é pior ainda, porque todo comando reseta o timer de inatividade
do alvo. A sonda não toca em nada. Medida de 06/09, ciclo completo:
sono 120,715 s, acordado 29,455 s, sono seguinte 89,413 s.

⚠️ **`micros()` dá a volta a cada ~71 minutos.** Leia diferenças, nunca valores
absolutos, e mantenha a janela de medição bem dentro disso. A suíte já trata a
volta.

⚠️ **GP4/GP5 continuam sendo a ponte serial.** A sonda foi para GP2 justamente
para não desativá-la.

⚠️ **Regravar a mão reinicia o alvo.** Foi observado em 06/09: depois de copiar
o `.uf2` para o volume `RPI-RP2`, o alvo apareceu com uptime zerado, em boot
frio (M0). Contar com isso ao planejar uma bateria.

### Como regravar a mão

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipico --build-path /tmp/picohand_build \
    tools/PicoHand/pico_hand
cp /tmp/picohand_build/pico_hand.ino.uf2 /media/angelo/RPI-RP2/   # com a mão em BOOTSEL
```

Colocar a mão em BOOTSEL exige `SELF_BOOTSEL` (que faz a porta sumir — nunca em
automação) ou o botão físico. Em 06/09 o usuário fez isso à mão.
