# HistoryV5 — Emendas da Revisão 2.0 (E1–E8)

| Campo | Valor |
|---|---|
| Documento | Registro de emendas ao documento normativo `HistoryV5_Instrucoes_Implementacao.md` |
| Aplicação | A Rev 2.0 (`HistoryV5_Instrucoes_Implementacao_Rev2.md`) já as incorpora — é substituição direta do arquivo em `docs/` |
| Base | Ratificações das divergências 2.1–2.5 de `docs/HistoryV5_Implementacao.md` e medições de `docs/RELATORIO_V4_vs_V5.md` |
| Regra | Onde este registro e a Rev 2.0 divergirem, vale a Rev 2.0 |

Princípio das emendas: **as cinco divergências registradas pela implementação foram ratificadas** — quatro corrigiam pressupostos desatualizados ou contradições do próprio documento; a quinta (leitor único) é ganho estrutural. Nenhuma emenda altera o formato em disco: bytes gravados pela 2.0.1-alpha permanecem válidos sob a Rev 2.0.

---

## E1 — §1 · Premissas do formato legado corrigidas

O documento comparava o V5 contra um formato plano de 28 B que já havia saído do firmware; o legado real (`.sim4`) já era delta comprimido. A missão passa a declarar o caso de negócio verdadeiro — escritas em flash 1.440 → ~168/dia, envelope min/máx, corrupção contida em 1 h, autodescrição — e a autonomia normativa vem de medição: ~7,6 KiB/dia e ≥ 100 dias na configuração de referência de 11 canais (medido: 116). R3 vira fórmula por `nCh`.

## E2 — §1-R9 e §10 · Orçamento de memória reescrito

"RAM estática ≤ 2,2 KiB" não descrevia a arquitetura real (os buffers vivem no objeto `StorageManager`, alocado uma vez no boot). Norma nova: **custo líquido ≤ +0,5 KiB** vs formato anterior (medido: ~+0,2 KiB), teto de 4,5 KiB no objeto, proibida alocação por amostra ou por requisição. A queda de 44 KB de RAM estática é do leitor único (E-nota abaixo), não do formato.

## E3 — §4, §5, §6, §8 · Schema derivado da configuração *(ratifica 2.1 e 2.2)*

A tabela fixa `kCompiledSchema` descrevia um layout pré-legado e, seguida ao pé da letra, **suprimiria umidade e pressão do histórico** — violação de R1 para cumprir uma escolha de escopo. Norma nova: `StorageManager::buildH5Schema()` deriva o schema dos slots provisionados; convenção `id = slot × 8 + canal` (0..127, nunca reciclado dentro de um schema); `kind`/`scaleExp` vêm da `SensorChannelTable` (as escalas do enum são orientação — o arquivo é autodescritivo); canal fora do alcance do `int16` (lux) entra como `H5_KIND_GENERIC`, `scaleExp = 0`. A leitura segue os SCHEMAs do próprio arquivo; `STO_SCHEMA_MISMATCH` deixa de existir como conceito.

## E4 — §5 · Contratos do encoder *(ratifica 2.3 e 2.4)*

Duas correções que o teste de propriedade e a análise de RAM impuseram: (a) `add()` recusa registro fora do alcance do RAW (`epoch < t0` ou `epoch − t0 > 65535`) — fecha a contradição §14-1 × §14-2 e restaura a garantia `chunk ≤ H5_BLOCK_MAX_BYTES`, testada no pior caso exato; (b) o encoder guarda **amostras** (~2,1 KiB), não bitstream incremental, e `sealStream()` (3 passagens, janela de 64 B) é o caminho canônico de gravação. O teste de equivalência byte a byte `seal()` × `sealStream()` torna-se obrigatório, como o portão de paridade.

## E5 — §10 · Orçamentos rebaseados na física medida

O piso de acesso salteado do LittleFS é ~0,28 ms/bloco — medido, estável, insensível a otimizações de chamada. Orçamentos novos: envelope ≤ 0,30 ms/bloco (24 h ≤ 10 ms · 7 d ≤ 55 ms · 30 d ≤ 220 ms); decode ≤ 1,5 ms/bloco **condicionado ao mutex por bloco** no handler (o ganho de só 12% do `BitReader` provou que o gargalo não é a decodificação); 24 h por decode ≤ 60 ms mantido. A premissa fica registrada no §10 para ninguém voltar a orçar contra o filesystem.

## E6 — §13 · Plano encerrado → estado e pendências

WP1–WP4 executados; permanecem obrigatórios os dois portões (paridade 200 000 casos; equivalência de selagem). Fila de pendências: **(1)** mutex por bloco no decode, **(2)** A4 — 20 cortes aleatórios, **(3)** A6 — soak 72 h, **(4)** A3 pleno — salto de relógio isolado cobrindo o caminho `add()`-recusa. OTA é frente própria e não entra nesta fila.

## E7 — §14 · Decisões corrigidas e uma nova

Itens 1–2 reescritos conforme E4. Item 8 novo: o caminho de envelope **não valida CRC** (o CRC único cobre o payload, que o envelope existe para não ler) — decisão aceita; corrupção de caudas pode ser plotada até a próxima leitura densa, e o decode valida sempre. Não "corrigir" triplicando leituras.

## E8 — §9, §11, §12 · Arrasto do legado real

Conversor documentado contra o `.sim4` verdadeiro (`--convert-v4`; delta LSB-first vs V5 MSB-first; `HistV4MeasureDef` de 12 B com padding — armadilhas no §7 das notas de implementação); purga e preservação referem `.sim4`; matriz de rejeição perde o caso "SCHEMA divergente" e ganha `nCh` inválido e arquivo truncado.

## E9 — §10, §13 · O que a medição desmentiu, e o instrumento que faltava

Emenda de 01/08/2026, posterior à Rev 2.0 e escrita contra número medido, não contra leitura de código.

**O que a pendência §13-1 dizia** — que o gargalo do decode era "a leitura do chunk e o par de mutex por registro" — **estava errado no termo dominante**. Os três achados, na bancada, janela de 24 h, 6 canais, 1 062 registros em 32 blocos:

1. **O mutex por registro custava ~20%** do caminho de leitura (2,88 → 2,31 ms/bloco ao passar a trancar por bloco). Real, e o menor dos três.
2. **O chunk era mesmo lido duas vezes** — `verifyDataCrc( )` percorria o payload em pedaços de 64 B e `readChunk( )` relia tudo. Unificar numa leitura só, com o CRC calculado sobre a cópia em RAM, **não moveu o número**: essas releituras caíam no cache do LittleFS. A mudança fica (menos I/O, mesma garantia §3.7-4), mas não é ganho de latência.
3. **O termo dominante é o entrelaçamento.** O mesmo `h5DecodeNext( )` custa 13,7 µs isolado e 27,9 µs entre duas formatações JSON — o handler roda `snprintf` de seis floats por registro e despeja o cache de instruções do XIP. Metade do custo do decodificador não é decodificação.

**E o enquadramento que nenhuma das revisões anteriores tinha:** o laço de registros de uma janela de 24 h custa **1 503 ms**, dos quais 44 ms são leitura e decodificação. Os orçamentos do §10 governam **3% da resposta**; os 97% restantes são formatar e enviar. Isso não invalida os orçamentos — invalida a expectativa de que cumpri-los mude o que o usuário sente. A alavanca continua sendo o envelope: mesma janela, 0,20 s contra 1,64 s.

**Instrumento, agora permanente** (sem ele as duas sessões anteriores atribuíram custo por raciocínio, e erraram): a resposta do `/api/history_multi` traz `blocks` (blocos lidos de verdade — reboot e troca de schema deixam blocos PARTIAL, então `registros/60` erra exatamente quando importa), `loadMs` (a metade de flash de `readMs`) e `loopMs` (uma medição do laço inteiro contra as milhares que somam `readMs` — é o que prova que `readMs` mede o trabalho e não a si mesmo). E `?emit=0` decodifica e mede sem formatar nem enviar um único ponto.

---

## Fora do documento normativo — decisões operacionais desta revisão

0. **Histórico congelado no tempo** (01/08): diagnosticado e corrigido no mesmo dia — ver §13-2 da Rev 2.0. Nenhum dado havia se perdido; o arquivo estava certo e o leitor desistia dele. Duas lições de método, porque as duas custaram tempo nesta sessão: **(a)** a API mentiu por omissão e o `tsMaxT` que eu li como "último registro" é o instante do **máximo de temperatura** — só o dump dos bytes do arquivo resolveu; **(b)** `?probe=1` e `mode=` respondem sobre caminhos diferentes, e um `sliceRequired` calculado como se fosse decode aparece mesmo quando o envelope foi escolhido. Continua aberto o defeito C (um SCHEMA por append, ~20% do orçamento diário de bytes).
1. **OTA é o bloqueio nº 1 e frente própria.** Enquanto um stage falho formatar o LittleFS, qualquer tentativa de OTA custa o histórico inteiro. Mitigação imediata até a causa do reset ser achada: guarda de confirmação explícita + oferta de export do histórico antes do `stage`.
2. **Higiene da bancada, antes de qualquer outra coisa:** a senha de admin voltou ao padrão de fábrica no episódio de OTA — trocar agora.
3. A "Ressalva honesta" do relatório está **aceita e absorvida** (E1): o ganho de tamanho sobre o `.sim4` é 1,41×; o argumento do V5 são as escritas, a leitura por envelope, a robustez e a autodescrição — todos medidos.
4. O leitor único (divergência 2.5) fica registrado como ganho estrutural: −44 KB de RAM estática e um só lugar para corrigir bugs de leitura, no lugar de cinco.
