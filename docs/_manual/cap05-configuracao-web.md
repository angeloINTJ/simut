# Configuração pela web: os três botões e a página Configurações {#cap-05}

Este capítulo explica como a interface web grava uma alteração de configuração, o que cada um dos três botões de gravação faz e quando o aparelho precisa reiniciar. Depois, percorre a página **Configurações** campo a campo. É para quem configura o aparelho pelo navegador.

## Como uma alteração é gravada {#cap-05-gravacao}

Nas páginas de configuração, nada é gravado enquanto você digita. Cada campo alterado vira uma alteração **preparada**, guardada na aba do navegador. As páginas **Configurações**, **Alarmes e Sons**, **Telemetria**, **Rede** e **Usuários** preparam alterações no mesmo conjunto: você pode alterar um limite de alarme, passar para a página **Rede**, alterar o DNS e gravar tudo de uma vez.

O conjunto preparado:

- sobrevive à troca de página e ao recarregar a página, dentro da mesma aba;
- some se você fechar a aba, tocar em **Sair** ou entrar de novo;
- não é visto por outra aba, outro navegador ou outra pessoa.

Na primeira alteração de um conjunto novo, um aviso lembra: **Clique em "Salvar e Reiniciar" para aplicar as alterações.** (*Click "Save & Restart" to apply your changes.*). A partir daí, a barra de topo mostra um selo e os botões de gravação ([capítulo 13](#cap-13-barra)). No celular, eles ficam numa barra fixa no rodapé.

Os campos de cada página mostram o valor gravado no aparelho, com as alterações preparadas por cima. O que aparece na página é o que será gravado.

### O ensaio: o aparelho decide {#cap-05-ensaio}

Cerca de 0,6 s depois da última alteração, a página envia o conjunto preparado ao aparelho como um **ensaio** (o parâmetro `_dry=1`). O aparelho aplica todas as verificações de faixa e de formato numa cópia da configuração, compara a cópia com a configuração em uso e responde quais grupos de configuração mudariam e se algum deles exige reinício. Nada é gravado e nada reinicia.

Quem decide é o aparelho, não a página. Um campo digitado de volta ao valor atual não conta como mudança.

O selo da barra de topo mostra a resposta:

| Selo | Quando | Botões oferecidos |
|---|---|---|
| **Alterações não salvas** (*Unsaved changes*) | O ensaio ainda não respondeu, ou o aparelho recusou o ensaio | Só **Salvar e reiniciar** |
| **Reinicia por:** (*Restarts for*) seguido dos grupos | O ensaio respondeu que algum grupo exige reinício | Só **Salvar e reiniciar** |
| **Não exige reinício** (*No restart needed*) | O ensaio respondeu que tudo se aplica sem reiniciar | **Testar**, **Aplicar agora** e **Salvar e reiniciar** |

O aparelho recusa o ensaio quando o conjunto inclui contas ([capítulo 8](#cap-08)), slots de sensor ou calibração ([capítulo 6](#cap-06)). Essas alterações só se gravam com **Salvar e reiniciar**. No SIMUT Air, o ensaio é sempre recusado ([No SIMUT Air](#cap-05-air)).

Os grupos aparecem com o nome curto que o aparelho usa, em inglês, como `time` ou `alarms`. Com **Não exige reinício**, pare o ponteiro sobre o selo para ver a lista dos grupos. A tabela de [Os grupos de configuração](#cap-05-grupos) diz o que cada nome inclui.

::: {.figura #fig-05-selo-reinicio tipo="web" arquivo="05-selo-reinicio.png" captura="rota /config; largura 1280; sessão admin; imagem release; campo Fuso Horário alterado de -3 para -4 e ainda não gravado; ensaio já respondido; recorte da barra de topo"}
Uma alteração que exige reinício: o selo diz Reinicia por: time e só o botão Salvar e reiniciar aparece.
:::

### Os três botões {#cap-05-botoes}

| Botão | O que faz | Depois |
|---|---|---|
| **Testar** (*Test*) | Aplica as alterações à configuração em uso sem gravá-las (`_nosave=1`) | O conjunto continua preparado. Um reinício volta aos valores gravados |
| **Aplicar agora** (*Apply now*) | Grava e aplica, sem reiniciar | O conjunto preparado é esvaziado |
| **Salvar e reiniciar** (*Save & restart*) | Grava e reinicia o aparelho, sempre (`_reboot=1`) | O conjunto preparado é esvaziado e a página se recarrega |

**Testar** e **Aplicar agora** só aparecem quando o ensaio respondeu **Não exige reinício**. **Salvar e reiniciar** aparece sempre que há alteração preparada, e reinicia mesmo quando a alteração se aplicaria sem reinício.

Para experimentar uma alteração sem gravá-la:

1. Faça a alteração, por exemplo um limite de alarme.
2. Espere o selo mostrar **Não exige reinício**.
3. Toque em **Testar**.

O aviso **Aplicado SEM salvar — um reinício desfaz** (*Applied WITHOUT saving — a restart undoes it*) aparece, seguido dos grupos aplicados. A barra de gravação continua visível, porque a alteração ainda não está gravada. Para mantê-la, toque em **Aplicar agora**. Para desistir dela, toque em **Sair** ou feche a aba e reinicie o aparelho.

::: atencao
**Testar não sobrevive a um reinício nem a uma falta de energia.** Qualquer reinício, pedido ou não, volta aos valores gravados. Use **Testar** para experimentar, não para configurar.
:::

Para gravar sem reiniciar:

1. Faça as alterações.
2. Espere o selo mostrar **Não exige reinício**.
3. Toque em **Aplicar agora**.

O aviso **Aplicado e salvo** (*Applied and saved*) aparece, seguido dos grupos aplicados. A sessão continua aberta e o aparelho não sai do ar.

Para gravar e reiniciar:

1. Faça as alterações.
2. Toque em **Salvar e reiniciar**.
3. Leia a confirmação e toque em **OK**.

A confirmação diz: **Isto salvará todas as alterações e reiniciará o sistema. O dispositivo ficará offline por ~10 segundos. Qualquer gravação de histórico/log em andamento será interrompida. Continuar?** Depois de **OK**, aparece **Salvo! Reiniciando sistema...** (*Saved! Restarting system...*) e a página se recarrega sozinha cerca de 12 s depois ([O que um reinício custa](#cap-05-custo)).

Se há uma calibração preparada ([capítulo 6](#cap-06)), **Salvar e reiniciar** primeiro envia a calibração ao aparelho e só depois grava o resto. Se a calibração falha, nada é gravado e aparece **Erro na calibração:** seguido do motivo, ou **Tempo esgotado na calibração. Tente novamente.**

::: {.figura #fig-05-tres-botoes tipo="web" arquivo="05-tres-botoes.png" captura="rota /alarms; largura 1280; sessão admin; imagem release; limite máximo de temperatura de um slot alterado; ensaio respondido Não exige reinício; recorte da barra de topo com o selo e os três botões"}
Uma alteração que se aplica sem reinício: o selo diz Não exige reinício e os três botões aparecem.
:::

::: {.figura #fig-05-confirmacao tipo="web" arquivo="05-confirmacao.png" captura="rota /config; largura 1280; sessão admin; Nome do aparelho alterado; janela de confirmação aberta depois de tocar em Salvar e reiniciar"}
A confirmação de Salvar e reiniciar, que avisa quanto tempo o aparelho fica fora do ar.
:::

### Os grupos de configuração {#cap-05-grupos}

O aparelho classifica cada alteração comparando a configuração de antes com a de depois, campo por campo. Um grupo só se aplica sem reinício quando o firmware relê aqueles campos a cada uso, ou tem uma função que empurra o valor novo para quem o usa. Todo o resto reinicia. Um campo que não pertence a nenhum grupo conhecido cai em `unclassified` e também reinicia.

| Grupo | O que inclui | Onde se altera | Sem reinício? |
|---|---|---|---|
| `alarms` | Limites de alarme por canal e alarmes ligados por slot | **Alarmes e Sons** ([capítulo 7](#cap-07)) | Sim |
| `maint` | Janelas de manutenção | **Alarmes e Sons** ([capítulo 7](#cap-07)) | Sim |
| `alarm_tel` | Linha de alarmes: ligada, fila, formato, modelos e caminho | **Telemetria** ([capítulo 22](#cap-22)) | Sim |
| `telemetry` | Telemetria pelo lado HTTP: servidor, porta, caminho, chave, lote mínimo e máximo, formato e modelos | **Telemetria** ([capítulo 21](#cap-21)) | Sim |
| `users` | Política de PIN do painel | **Configurações** ([Hardware](#cap-05-hardware)) | Não: reinicia (pelo painel, vale sem reinício) |
| `users` | Contas | **Usuários** ([capítulo 8](#cap-08)) | Não: o ensaio é recusado |
| `display` | Tema e idioma do painel | Nenhuma página grava estes campos por aqui | Sim |
| `identity` | Nome do aparelho | **Configurações** | Não |
| `time` | Fuso horário e servidor NTP | **Configurações**, **Rede** | Não |
| `logging` | **Registro Local** | **Configurações** | Não |
| `sensing` | Intervalo de amostra e resolução do DS18B20 | **Configurações** | Não |
| `slots` | Tipo, GPIOs, identificação e nome dos slots de sensor | **Configurações** ([capítulo 6](#cap-06)) | Não: o ensaio é recusado |
| `net` | Wi-Fi, DHCP, IP estático, DNS primário | **Rede** ([capítulo 9](#cap-09)) | Não |
| `mqtt` | Transporte, conta, senha e opções MQTT, e o TLS da telemetria | **Telemetria** ([capítulo 21](#cap-21)) | Não |
| `web` | A área de extensão da configuração: porta web, conexões persistentes, sons, intervalo do histórico, NTP ligado, DNS automático e secundário, syslog e Home Assistant | Várias | Não |
| `display_pin` | Campo antigo, sem uso | — | Não |
| `unclassified` | Qualquer diferença fora dos grupos acima | — | Não |

O tema do painel tem gravação própria, pelo **Painel de Controle**, e não passa por estes botões ([capítulo 13](#cap-13-tema-painel)).

::: nota
**Por que esses grupos reiniciam.** Vários subsistemas leem a configuração uma única vez, ao ligar: o servidor web fixa porta e protocolo ao subir, o cliente MQTT fixa o servidor, o certificado da telemetria é lido uma vez e os slots de sensor montam o circuito de leitura inteiro. Reiniciar é o único jeito de todos relerem o valor novo.
:::

::: atencao
**O ensaio não enxerga alguns campos.** O intervalo do histórico, o interruptor do NTP, os campos do syslog, o Home Assistant Discovery, o DNS automático, o DNS secundário e as conexões persistentes são gravados na área de extensão, e o ensaio não os escreve na cópia que compara. Se o conjunto preparado só tem esses campos, o selo pode mostrar **Não exige reinício**. Nesse caso, **Aplicar agora** grava e reinicia o aparelho mesmo assim, e **Testar** não aplica esses campos. Para alterá-los, use **Salvar e reiniciar**.
:::

### Valores recusados {#cap-05-recusados}

O aparelho confere cada campo antes de gravar. Um valor fora da faixa ou com formato errado é descartado: o campo mantém o valor anterior, e o resto da gravação continua.

- Depois de **Testar** ou **Aplicar agora**, o aviso **Campos não aplicados:** (*Fields not applied*) lista as chaves recusadas, como `s_int` ou `net.dns2`.
- Depois de **Salvar e reiniciar**, a página se recarrega sem esse aviso. Confira os campos alterados depois do reinício.

Alguns erros recusam a gravação inteira, e nada muda. O aviso é **Falha ao salvar.** (*Save failed.*) seguido do motivo e, quando existe, da seção entre parênteses. Os casos:

| Resposta | Motivo |
|---|---|
| `Alarm limit outside channel range` | Um limite de alarme fora da faixa possível do canal ([capítulo 7](#cap-07)) |
| O slot e o GPIO recusados, como `slot 4: GP2 already used by slot 2` | Conflito na atribuição de GPIO ([capítulo 6](#cap-06)) |
| `Forbidden (<seção>)` | A conta não tem a permissão daquela seção (tabela abaixo) |
| `Password change required` | A conta ainda precisa trocar a senha ([capítulo 13](#cap-13-troca-obrigatoria)) |
| `Display in use. Retry shortly.` | Alguém tocou no painel há menos de 5 s. Espere e tente de novo |
| `Bad payload` | O conjunto preparado passou de 6.144 bytes. Grave em partes |

Cada seção do conjunto exige a sua permissão, e a gravação é tudo ou nada: se uma seção é proibida, nenhuma é gravada.

| Seção | Páginas que a preparam | Permissão |
|---|---|---|
| `sys` | **Configurações** e **Telemetria** | **Sistema** [PERM_SYS_CONFIG]{.perm} |
| `slots`, `calib` | **Configurações** (sensores) | **Sistema** [PERM_SYS_CONFIG]{.perm} |
| `alarms` | **Alarmes e Sons** | **Sistema** [PERM_SYS_CONFIG]{.perm} |
| `net` | **Rede** | **Rede** [PERM_NET_CONFIG]{.perm} |
| `users` | **Usuários** | **Usuários** [PERM_USER_MGR]{.perm} |

### O que um reinício custa {#cap-05-custo}

Um **Salvar e reiniciar** leva o aparelho para fora do ar por cerca de 10 s. Na ordem:

1. O painel mostra **Aplicando configurações...** e depois **Reiniciando o sistema...**, cerca de 1,5 s cada.
2. O aparelho grava a configuração, registra o evento e responde à página.
3. O aparelho reinicia. Uma gravação de histórico ou de log em andamento é interrompida.
4. A página se recarrega cerca de 12 s depois e mostra a página de entrada, porque um reinício encerra todas as sessões ([capítulo 13](#cap-13-sessoes)).

Dois casos mudam o passo 4:

- **Porta web alterada:** a página avisa **Salvo. Redirecionando para nova porta...** e, cerca de 15 s depois, abre o endereço com a porta nova ([capítulo 9](#cap-09-servidor-web)).
- **Conta criada ou senha redefinida:** antes de recarregar, a página mostra a senha provisória de cada conta. Anote-a: ela não aparece de novo ([capítulo 8](#cap-08)).

Depois do reinício, o aparelho se reconecta ao Wi-Fi, acerta o relógio de novo ([capítulo 10](#cap-10)) e retoma a telemetria.

O log de eventos registra cada gravação com o código 303 (**Config alterada**), com um destes textos:

| Texto | Botão |
|---|---|
| `Admin committed changes — rebooting` | **Salvar e reiniciar**, ou **Aplicar agora** quando algum grupo exigiu reinício |
| `Admin committed changes — applied live` | **Aplicar agora** |
| `Admin committed changes — applied live [nosave]` | **Testar** |

### No SIMUT Air {#cap-05-air}

[air]{.img}

O Air recusa o ensaio e o **Testar**: a imagem do Air está no limite de memória de programa, e os dois caminhos que trabalham numa cópia da configuração ficaram de fora. Na barra de topo do Air, o selo fica em **Alterações não salvas** e só **Salvar e reiniciar** aparece. Toda gravação pela web reinicia o Air.

Lembre que a interface web do Air só existe enquanto ele está acordado em M0 ([capítulo 13](#cap-13-air), [capítulo 19](#cap-19)).

### Pela API {#cap-05-api}

Os três botões usam a mesma rota, `POST /api/commit_all`, com o conjunto preparado no campo `_payload`. O que muda é o parâmetro extra:

| Botão | Parâmetro |
|---|---|
| Ensaio da página | `_dry=1` |
| **Testar** | `_nosave=1` |
| **Aplicar agora** | nenhum |
| **Salvar e reiniciar** | `_reboot=1` |

A resposta diz o que aconteceu. Um ensaio de fuso horário, por exemplo:

```json
{"status":"dry","reboot":true,"applied":["time"],"reboot_for":["time"]}
```

Um integrador pode usar o ensaio para validar uma configuração antes de mandá-la a vários aparelhos. O formato completo está no [capítulo 26](#cap-26).

## A página Configurações {#cap-05-pagina}

A página **Configurações** (*System Config*, rota `/config`) exige a permissão **Sistema**. Ela tem seis seções, nesta ordem:

1. **Identidade** (*General Identity*)
2. **Data e Hora** (*Date & Time*)
3. **Hardware** (*Hardware & Sampling*)
4. **Sensores e GPIO** (*Sensors & GPIO*)
5. **Syslog Remoto (Auditoria)** (*Remote Syslog (Audit Trail)*)
6. **Calibração do Touch** (*Touch Calibration*)

As cinco primeiras preparam alterações para os botões da barra de topo. A página não tem botão de gravar próprio. **Calibração do Touch** age na hora, com botão próprio.

Se a página não consegue ler a configuração do aparelho, ela mostra uma faixa vermelha: **Não foi possível carregar as configurações atuais. Os campos estão desabilitados para evitar salvar valores em branco sobre a sua configuração.** Os campos ficam desabilitados, de propósito. Toque em **Tentar novamente** (*Retry*). A página já tenta três vezes antes de mostrar a faixa.

::: {.figura #fig-05-configuracoes tipo="web" arquivo="05-configuracoes.png" captura="rota /config; largura 1280; sessão admin; imagem release; valores de fábrica; nenhuma alteração preparada; página no topo mostrando Identidade, Data e Hora e o início de Hardware"}
A página Configurações aberta, com as seções Identidade e Data e Hora.
:::

::: {.figura #fig-05-erro-carga tipo="web" arquivo="05-erro-carga.png" captura="rota /config; largura 1280; sessão admin; aparelho sem responder a /api/config (por exemplo, rede interrompida depois de a página abrir); faixa de erro visível e campos desabilitados"}
A faixa de erro de carga: os campos ficam desabilitados para que nada em branco seja gravado.
:::

### Identidade {#cap-05-identidade}

| Campo | O que faz | Faixa e formato | Fábrica | Aplicação | Chave |
|---|---|---|---|---|---|
| **Nome** (*Device Name*) | Nome do aparelho | 1 a 31 caracteres, sem aspas `"` nem barra invertida `\`; espaços nas pontas são removidos | `simut` | Reinicia (`identity`) | `name` |
| **Fuso Horário** (*Timezone Offset (Hours)*) | Diferença do horário local para o UTC, em horas inteiras | −12 a +14 | −3 | Reinicia (`time`) | `tz` |
| **Registro Local** (*Enable Local Logging*) | Preferência de registro local | Ligado ou desligado | Ligado | Reinicia (`logging`) | `log` |

O **Nome** aparece em vários lugares:

- no nome da rede do ponto de acesso de configuração, seguido de `_SETUP` ([capítulo 9](#cap-09-ap));
- no endereço mDNS, seguido de `.local` ([release]{.img}, [capítulo 9](#cap-09-mdns));
- no nome Bluetooth ([alpha]{.img} [air]{.img}, [capítulo 14](#cap-14));
- no campo de origem das mensagens de syslog ([capítulo 25](#cap-25)) e no rótulo das métricas Prometheus ([capítulo 24](#cap-24));
- na etiqueta `{DEV}` da telemetria ([capítulo 21](#cap-21));
- em `show system info`, no console.

Como o nome vira endereço de rede e nome de rede Wi-Fi, prefira letras sem acento, números e hífen.

O **Fuso Horário** não tem horário de verão nem frações de hora. Detalhes, e um efeito colateral de digitar neste campo, estão no [capítulo 10](#cap-10-fuso).

::: atencao
**Registro Local não desliga nada nesta versão.** O aparelho grava a preferência e a mostra em `show system info` (`Logging: ATIVO` ou `INATIVO`), mas nenhuma parte do firmware a consulta: o log de eventos e o histórico continuam gravados com ela desligada. Alterá-la reinicia o aparelho sem outro efeito.
:::

### Data e Hora {#cap-05-data-hora}

| Campo | O que faz | Faixa e formato | Fábrica | Aplicação | Chave |
|---|---|---|---|---|---|
| **Sincronizar automaticamente via NTP** (*Synchronize automatically via NTP*) | Liga o acerto do relógio pela rede | Ligado ou desligado | Ligado | Reinicia (`web`) | `ntp_enabled` |
| **Data** (*Date*) | Data para o acerto manual | A partir de 01/01/2026 | A data do aparelho ao abrir a página | Na hora, pelo botão **Aplicar Agora** | — |
| **Hora** (*Time*) | Hora para o acerto manual, com segundos | `hh:mm:ss` | A hora do aparelho ao abrir a página | Na hora, pelo botão **Aplicar Agora** | — |

O botão **Aplicar Agora** (*Apply Now*) desta seção acerta o relógio imediatamente, sem passar pela barra de topo e sem reiniciar. Ele interpreta a data e a hora digitadas no fuso do campo **Fuso Horário**, como ele está na página. A resposta aparece abaixo do botão: **Hora aplicada.** (*Time applied.*), **Falhou ao aplicar.** (*Failed to apply.*) ou, com um campo vazio, **Preencha data e hora.** (*Fill date and time.*).

Com o NTP ligado, os campos de data e hora ficam esmaecidos, mas continuam editáveis: o acerto manual vale até a próxima sincronização. A dica abaixo do botão lembra: **Usa fuso horário do dispositivo. Salve e Reinicie para persistir o toggle do NTP.**

::: nota
**Mexer em Data ou Hora também faz a barra de gravação aparecer.** Os dois campos ficam no mesmo formulário e entram no conjunto preparado, mas o aparelho os ignora na gravação. Para acertar o relógio, use o **Aplicar Agora** da seção. Se a barra apareceu só por causa deles, toque em **Sair** e entre de novo para descartar o conjunto.
:::

O funcionamento do relógio, do NTP e do acerto manual está no [capítulo 10](#cap-10).

::: {.figura #fig-05-data-hora tipo="web" arquivo="05-data-hora.png" captura="rota /config; largura 1280; sessão admin; NTP desligado e gravado; seção Data e Hora com os campos de data e hora preenchidos; mensagem Hora aplicada. abaixo do botão Aplicar Agora"}
A seção Data e Hora com o NTP desligado, depois de um acerto manual.
:::

### Hardware {#cap-05-hardware}

| Campo | O que faz | Faixa e formato | Fábrica | Aplicação | Chave |
|---|---|---|---|---|---|
| **Resolução DS18B20** (*DS18B20 Resolution*) | Resolução das sondas DS18B20 | **9-bit**, 10-bit, 11-bit ou **12-bit** | 12 bits | Reinicia (`sensing`) | `res` |
| **Amostra (ms)** (*Sample Interval (ms)*) | Intervalo entre leituras dos sensores | 1.000 a 60.000 ms | 2.000 ms | Reinicia (`sensing`) | `s_int` |
| **Intervalo Histórico (min)** (*History Recording Interval (min)*) | Intervalo entre registros gravados no histórico | 1 a 1.440 min | 1 min | Reinicia (`web`) | `h_int` |
| **Teclado do painel: glifos por tecla** (*Panel keypad: glyphs per key*) | Quantos caracteres cada tecla do PIN mostra | 1, 2 ou 3 | 3 | Reinicia (`users`) | `pin_kb` |
| **Caracteres do PIN** (*PIN characters*) | Alfabeto do PIN | `0-9` ou `0-9 A-Z` | `0-9` | Reinicia (`users`) | `pin_alpha` |
| **Tamanho mínimo do PIN** (*Minimum PIN length*) | Menor PIN aceito | 4 até o teto do teclado: 16 com 1 glifo, 12 com 2, 8 com 3 | 4 | Reinicia (`users`) | `pin_min` |

Sobre a resolução: nesta versão, o aparelho espera 750 ms por leitura do DS18B20 em qualquer resolução. Baixar a resolução reduz a precisão sem encurtar a leitura. Detalhes no [capítulo 6](#cap-06).

A dica abaixo de **Intervalo Histórico (min)** diz: **1 a 1440 min (24h). Padrão: 1.** [air]{.img} No Air, esse intervalo é também o intervalo entre despertares, porque a tarefa principal de cada despertar é gravar um registro ([capítulo 19](#cap-19)).

[release]{.img} Os três campos do PIN formam a política de PIN do painel e só aparecem na imagem release, que é a única com painel de toque. Eles se validam juntos: o teto do tamanho mínimo muda com o teclado, e a dica abaixo do campo mostra a faixa permitida, como `4 – 8`. Pela web, salvar uma política nova reinicia o aparelho, porque ela pertence à classe `users`. Pelo painel (Configurações, linha 11), a mesma mudança vale na próxima tela de PIN, sem reinício. Uma política mais exigente marca as contas com PIN para escolher um PIN novo. O significado de cada teclado está no [capítulo 8](#cap-08).

::: {.figura #fig-05-hardware tipo="web" arquivo="05-hardware.png" captura="rota /config; largura 1280; sessão admin; imagem release; valores de fábrica; seção Hardware inteira, com a linha da política de PIN e a dica 4 – 8"}
A seção Hardware na imagem release, com a política de PIN do painel.
:::

### Sensores e GPIO {#cap-05-sensores}

A seção **Sensores e GPIO** (*Sensors & GPIO*) configura os 16 slots de sensor. Ela é descrita inteira no [capítulo 6](#cap-06). Em resumo:

- a dica **GP0–GP15 estão disponíveis para sensores; GP16 em diante pertencem ao display, ao touch e ao buzzer.**;
- um mapa dos GPIOs em uso;
- a tabela dos slots, com as colunas **Slot**, **Tipo**, **GPIOs**, **ID de Hardware** e **Nome**;
- os botões **+ Adicionar slot de sensor** e **Procurar sondas**;
- o editor de slot, com **Tipo**, **Nome**, **ID de Hardware**, **Estado** (**Ativo** e **Alarmes habilitados**), **Atribuição de GPIO**, **Calibração**, **Gravação no histórico** e **Hardware**, e os botões **Liberar slot**, **Descartar alterações** e **Concluído**.

Alterações de slot e de calibração só se gravam com **Salvar e reiniciar**. Com os 16 slots em uso, a página diz **Os 16 slots estão em uso. Libere um para adicionar outro.**

::: {.figura #fig-05-sensores tipo="web" arquivo="05-sensores.png" captura="rota /config; largura 1280; sessão admin; dois slots configurados (um DS18B20 e um BME280); seção Sensores e GPIO com o mapa de GPIOs e a tabela de slots; editor fechado"}
A seção Sensores e GPIO, com o mapa de GPIOs e a tabela de slots. O editor de slot está no capítulo 6.
:::

### Syslog Remoto (Auditoria) {#cap-05-syslog}

Esta seção envia os eventos do log a um coletor syslog na rede, para que a trilha de auditoria exista também fora do aparelho. A dica diz que o envio segue a RFC 5424 sobre UDP, que o servidor é um IPv4 da rede local e não um nome, e que não há garantia de entrega.

| Campo | O que faz | Faixa e formato | Fábrica | Aplicação | Chave |
|---|---|---|---|---|---|
| **Habilitar encaminhamento syslog** (*Enable syslog forwarding*) | Liga o envio | Ligado ou desligado | Desligado | Reinicia (`web`) | `slog_en` |
| **IP do Coletor** (*Collector IP*) | Endereço do coletor | IPv4, como `192.0.2.10`; vazio desliga | Vazio | Reinicia (`web`) | `slog_srv` |
| **Porta UDP** (*UDP Port*) | Porta do coletor | 1 a 65535 | 514 | Reinicia (`web`) | `slog_port` |
| **Nível Mínimo** (*Minimum Level*) | Menor nível enviado | Debug, Info, Warning, Error ou Fatal | Info | Reinicia (`web`) | `slog_lvl` |

Com o interruptor ligado e o **IP do Coletor** vazio, nada é enviado. O formato das mensagens e a configuração do coletor estão no [capítulo 25](#cap-25).

::: {.figura #fig-05-syslog tipo="web" arquivo="05-syslog.png" captura="rota /config; largura 1280; sessão admin; syslog ligado com IP do Coletor 192.0.2.10, porta 514 e nível Info; recorte das seções Syslog Remoto e Calibração do Touch"}
As duas últimas seções da página: o syslog e a calibração do touch.
:::

### Calibração do Touch {#cap-05-touch}

O botão **Resetar Calibração** (*Reset Touch Calibration*) apaga a calibração do toque guardada e abre o assistente de calibração no painel. Ele age na hora, sem passar pela barra de topo:

1. Toque em **Resetar Calibração**.
2. Confirme em **Resetar calibração do touch para padrão de fábrica?**
3. Vá até o painel e siga os passos do assistente ([capítulo 11](#cap-11)).

O aviso **Calibração resetada. Recalibre pelo display.** confirma. Se alguém tocou no painel há menos de 5 s, aparece **Display em uso. Tente novamente em alguns segundos.** O log de eventos registra `Touch calibration reset via web` com o código 303.

Nas imagens alpha e Air, que não têm painel de toque, a seção também aparece. O botão só apaga o registro de calibração guardado e não tem outro efeito.

## Referência rápida {#cap-05-referencia}

Todos os campos da página **Configurações** que passam pela barra de topo, na seção `sys` de `POST /api/commit_all`. Os campos da página **Telemetria** estão no [capítulo 21](#cap-21) e no [capítulo 22](#cap-22); os da página **Rede**, no [capítulo 9](#cap-09).

| Chave | Campo | Faixa | Fábrica | Grupo |
|---|---|---|---|---|
| `name` | **Nome** | 1 a 31 caracteres | `simut` | `identity`, reinicia |
| `tz` | **Fuso Horário** | −12 a +14 | −3 | `time`, reinicia |
| `log` | **Registro Local** | `1` ou `0` | `1` | `logging`, reinicia |
| `ntp_enabled` | **Sincronizar automaticamente via NTP** | `1` ou `0` | `1` | `web`, reinicia |
| `res` | **Resolução DS18B20** | 9 a 12 | 12 | `sensing`, reinicia |
| `s_int` | **Amostra (ms)** | 1000 a 60000 | 2000 | `sensing`, reinicia |
| `h_int` | **Intervalo Histórico (min)** | 1 a 1440 | 1 | `web`, reinicia |
| `pin_kb` | **Teclado do painel: glifos por tecla** | 1 a 3 | 3 | `users`, reinicia |
| `pin_alpha` | **Caracteres do PIN** | 0 (`0-9`) ou 1 (`0-9 A-Z`) | 0 | `users`, reinicia |
| `pin_min` | **Tamanho mínimo do PIN** | 4 ao teto do teclado | 4 | `users`, reinicia |
| `slog_en` | **Habilitar encaminhamento syslog** | `1` ou `0` | `0` | `web`, reinicia |
| `slog_srv` | **IP do Coletor** | IPv4 ou vazio | vazio | `web`, reinicia |
| `slog_port` | **Porta UDP** | 1 a 65535 | 514 | `web`, reinicia |
| `slog_lvl` | **Nível Mínimo** | 0 (Debug) a 4 (Fatal) | 1 (Info) | `web`, reinicia |

As três chaves `pin_*` só existem na imagem release. Nas outras, a página não as envia.
