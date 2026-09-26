# Economia por recurso — quanto cada chave devolve quando desligada

**Estado:** Living · **Levantado em:** 2026-09-26 contra `main` `dc7c7c5`,
medindo os seis ambientes de firmware com `tools/measure_savings.py` ·
**Pergunta:** ao desligar um driver ou qualquer funcionalidade, quanto de flash
e de RAM a imagem realmente devolve — e o que hoje **não** devolve o que deveria?

## Por que este documento existe

O modelo de recursos ([MODELO_DE_RECURSOS.md](MODELO_DE_RECURSOS.md)) promete
builds com recursos chaveáveis e **impacto previsível**. Previsível só se for
medido. Este levantamento mede, recurso a recurso, a diferença entre a imagem
com o recurso ligado e a mesma imagem com **só aquele recurso** desligado — e
`tools/feature_savings.json` guarda o piso dessa economia, que o portão
`measure_savings.py --check` impede de regredir. Se alguém acrescenta código
que, preso a um recurso, deixa de sumir quando o recurso é desligado, a economia
cai e o portão reprova: o recurso **vazou** para o caminho sempre-ligado.

A medida é o `used` que o próprio PlatformIO reporta (flash = `.text`+`.rodata`
do ELF; RAM = `.data`+`.bss` estáticos). O que importa aqui é a **diferença**
entre base e variante, não o número absoluto.

## O que cada recurso devolve (2026-09-26)

Ordenado pela economia de flash. `base` é um perfil onde o recurso está ligado;
a variante é o mesmo perfil com só ele desligado, derivada pelo modelo
(`tools/features.toml` → `gen_features.compose`), para que um recurso com
`off_exclude` (bluetooth, som) realmente **tire o `.cpp`** do build.

| Recurso | Base | Flash devolvido | RAM estática devolvida |
|---|---|---:|---:|
| `bluetooth` | `pico_w_alpha` | 140 132 B | 19 348 B |
| `cli_full` | `pico_w_test` | 48 232 B | 0 B |
| `web_https` (TLS) | `pico_w_release` | 24 520 B | 24 B |
| `sensor_bme280` | `pico_w_release` | 17 724 B | 220 B |
| `mdns` | `pico_w_release` | 16 576 B | 168 B |
| `sensor_ds18b20` | `pico_w_release` | 5 432 B | 64 B |
| `sensor_dht22` | `pico_w_release` | 3 400 B | 0 B |
| `concurrency_asserts` | `pico_w_asserts` | 2 176 B | 0 B |
| `license_stub` | `pico_w_test` | −1 728 B | 0 B |

Os três achados que a medição trouxe:

1. **A RAM estática quase não se move ao desligar.** `bluetooth` é a exceção
   (−19 348 B: a pilha BT e o `liblwip-bt`). Os sensores devolvem 0–220 B de
   RAM porque suas instâncias vivem no heap; o que sai é código (flash), não
   `.bss`. `web_https` devolve 24 520 B de flash mas só 24 B de RAM — os buffers
   TLS são de heap (`setBufferSizes` em runtime), invisíveis ao `used`. Ou seja:
   **a alavanca de RAM ao desligar é o bluetooth; as demais são alavancas de
   flash.** Quem precisa de RAM não a ganha desligando um sensor.

2. **`license_stub` é "ligar para economizar".** O stub *remove* o texto da
   licença; desligá-lo o *adiciona* (+1 728 B). Fica fora do piso "desligar
   economiza" — não é um recurso que se desliga para poupar.

3. **O buzzer não é chaveável isolado — está preso ao SIMUT_AIR.** `SoundManager.h`
   só vira no-op sob `#if SIMUT_AIR`; não há macro do buzzer. Excluir
   `SoundManager.cpp` fora do Air não linka (o `CommandManager` chama os métodos
   reais). Por isso `sound_buzzer` e `air` aparecem **acoplados** e ficam fora do
   piso: numa build TFT/normal não dá para largar o buzzer e poupar seus bytes.
   Corrigir isso — dar um macro ao buzzer (`SIMUT_SOUND_BUZZER`) e destravar o
   no-op também sob ele — é o próximo passo de "economia ao máximo em qualquer
   funcionalidade", num PR de firmware dedicado.

Os temas (`SIMUT_THEMES_*`) não entram: vêm **comentados** (desligados) em todo
perfil, custo zero. São "ligar para gastar", não "desligar para poupar".

## Como rodar

```bash
python3 tools/measure_savings.py              # a tabela acima
python3 tools/measure_savings.py --only air   # só um recurso (rápido)
python3 tools/measure_savings.py --update      # grava o piso no ledger
python3 tools/measure_savings.py --check       # reprova se a economia caiu
```

O `--check` reconstrói base e variante de cada recurso do ledger e falha se a
economia de flash ficou **abaixo** do piso (a RAM entra com tolerância de 64 B,
ruído de alinhamento do linker). É um ratchet, como o orçamento de flash e a
marca d'água do sprawl: a economia só sobe; abaixá-la exige `--update` e
justificativa no commit.

**Custo em CI.** O portão constrói ~13 imagens (base + variante por recurso), o
que não cabe no job `gates` (livre de framework). Pertence a um job próprio,
agendado (noturno) ou disparado à mão — ver `.github/workflows/savings.yml`.
Um `--check --only <recurso>` é barato e serve para conferir um recurso após
mexer no que ele guarda.
