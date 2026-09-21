# De beta a stable — o que precisa ser verdade, e como isso se prova

**Estado:** v2.6.1-beta é a Latest (21/09/2026, `main` `19ca65a`).
**Este documento é Living.** Cada linha da matriz vira ✅ com a data e o
número, ou continua aberta. Nenhuma vira ✅ por argumento.

---

## 1. O que "stable" tem de significar aqui

Uma definição que dê para testar, senão a promoção é uma mudança de rótulo.
Para este produto, stable quer dizer três coisas:

1. **Não reinicia sozinho.** Todo reboot tem causa conhecida e registrada. Um
   `ctx` sem explicação é bloqueio, não ruído.
2. **Não perde medição.** O que o sensor leu chega ao histórico e à telemetria,
   ou o aparelho diz que não chegou.
3. **Volta sozinho.** Queda de rede, coletor morto, OTA interrompida, falta de
   energia — o aparelho retorna sem alguém ir até ele.

E uma quarta, que é do projeto e não do firmware:

4. **Toda afirmação publicada tem número ou marcação.** O manual já faz isso
   ("aritmética, não medição" na autonomia). Numa stable isso não pode
   regredir.

---

## 2. Onde a v2.6.1-beta já está

Isto não é para tranquilizar; é para não refazer o que já foi feito.

| o que | evidência | quando |
|---|---|---|
| Reconexão de Wi-Fi com AP sumindo e voltando | `wifi_outage_test.py`, 3 fases, log do próprio aparelho | 09/09, v2.4.1-beta |
| Varredura de redes, STA e **de dentro do AP** | `wifi_scan_hw_test.py --ap`, 18/18 | 21/09 |
| Painel: 32 contas, PIN, política | `panel_users_hw_test.py` 32/32 + `panel_fulltable_test.py` | 20/09 |
| Alarmes 2ª linha (HTTP/MQTT) | `alarm_hw_test.py`, `alarm_mqtt_test.py` | 23/08 |
| OTA | 24 ciclos, 3/3 na v2.2.12 | 19/08 |
| Soak 5 h sob coletor morto | 0 reboots, heap +114 B | — |
| Telemetria nos 4 transportes | dreno 204 reg/s, soak HTTPS 30 min 0 FTL | — |
| 6 imagens × 7 suítes nativas (384 casos) + 5 portões | CI em todo PR | contínuo |

---

## 3. O que falta — e o que disso bloqueia

| # | item | bloqueia? | por quê |
|---|---|:---:|---|
| B1 | **`ctx=209`/`ctx=455` do D-C1**: watchdog do Core 0 com trace vazio, reproduzido 2×2 em 20/09 por `panel_fulltable_test.py`, **não determinístico** | 🔴 **sim** | Fere o item 1 da definição. Um reset sem causa numa stable é o defeito que volta como "o aparelho reiniciou sozinho" sem nada para investigar |
| B2 | **Sem soak na imagem desta versão** | 🔴 **sim** | Os soaks que existem são de versões anteriores. Um release que ninguém deixou ligado por horas não é stable |
| B3 | **OTA nunca exercitada nesta imagem**, e o Air está a **5.076 B** do teto | 🔴 **sim** | Uma linha stable recebe correções; cada uma arrisca cruzar o degrau de 4 KiB e a imagem passa a **recusar atualização pelo ar** com o `used` ainda dizendo 21 kB de folga |
| B4 | **`ctx=205`** — reboot sob telemetria morta, aberto e **não reproduz** | ⚠️ não | As blindagens existem; fica onde está até reproduzir. Registrado, não esquecido |
| B5 | **Corrente real nunca medida** (os 407,8 mAh/dia são cálculo) | ⚠️ não | O manual **já diz** "aritmética, não medição". Não bloqueia porque nada é afirmado sem marcação — mas nenhuma afirmação de autonomia pode perder a marcação numa stable |
| B6 | `pico_w_test_https` a **3.308 B** do teto de OTA | ⚠️ não | Ambiente de bancada, não é imagem de produto. Vira bloqueio se alguém precisar dele no campo |
| B7 | Issue #118 — mDNS do Air | ⚠️ não | Backlog |

**Três bloqueios: B1, B2, B3.** Os outros quatro ficam registrados e não
impedem a promoção.

---

## 4. A matriz de testes

O que cada linha tem de produzir para virar ✅: um **número** ou um **log**,
nunca "rodou e pareceu bem".

### 4.1 O que roda nesta bancada, agora

| # | teste | como | tempo | passa quando |
|---|---|---|---:|---|
| T1 | Regressão funcional da imagem | `panel_users_hw_test.py`, `panel_fulltable_test.py`, `alarm_hw_test.py`, `wifi_scan_hw_test.py --ap`, `web_test_suite.py` | ~2 h | tudo verde na imagem **desta** versão |
| T2 | Caça ao B1 | `panel_fulltable_test.py` em repetição, com o log binário preservado entre corridas | 3–4 h | ou reproduz com `ctx` e trace utilizáveis, ou N corridas limpas dão a taxa |
| T3 | Soak | `soak_monitor.py`, ≥ 8 h, coletor vivo e coletor morto | 8 h+ | 0 reboots sem causa; deriva de heap medida e declarada |
| T4 | OTA nesta imagem | ciclo de stage+apply, ida e volta entre v2.6.0-beta e v2.6.1-beta, pela `:8080` | ~1 h | 3/3 nos dois sentidos, `/history` íntegro pelo `fsguard` |
| T5 | Queda de rede | `wifi_outage_test.py` nesta imagem | ~40 min | as 3 fases, com o log do aparelho como prova |
| T6 | Telemetria | `telemetry_bench` nos 4 transportes + dreno | ~1 h | 0 FTL; vazão registrada |
| T7 | Relógio e histórico | `rig_validate_history_clock.py` | ~20 min | 3/3 |

### 4.2 O que esta bancada **não** consegue

| # | teste | o que falta | sem isso |
|---|---|---|---|
| N1 | Corrente real (B5) | INA219 ou multímetro em série | a autonomia continua marcada como cálculo — e **tem de continuar marcada** |
| N2 | Queda de energia real, repetida | tomada comandável | o caminho "volta sozinho" fica provado só por reset do PicoHand, que não é o mesmo |
| N3 | Temperatura e umidade fora da bancada | câmara | nenhuma afirmação de faixa ambiental pode ir ao manual |

### 4.3 Ordem sugerida

T1 → T4 → T5 → T7 (um dia de bancada, tudo curto e conclusivo) → T2 e T3 em
paralelo (T3 é tempo de relógio; T2 é repetição). T6 por último, porque o que
ele mede já tem histórico bom e é o menos provável de mudar o veredito.

---

## 5. Quando promover, e para qual número

`CONFIG_VERSION` está em 25 e nada nesta linha o move. A promoção é de
**rótulo**, não de schema: `v2.6.1-beta` → `v2.7.0`, sem `-beta`, cortada da
mesma `main` e publicada com `--latest`.

⚠️ `prerelease=true` **nunca** vira Latest — é o erro que já custou uma tag
neste repositório.

A promoção acontece quando **B1, B2 e B3 estiverem fechados**, cada um com o
número ou o log neste documento. Não antes, e não por prazo.

---

## 6. Registro

| data | o que rodou | resultado |
|---|---|---|
| 21/09 | T1 parcial: `wifi_scan_hw_test.py --ap` | 18/18 |
