# Atualização, backup e restauração {#cap-17}

Este capítulo explica como atualizar o firmware pela interface web, o que sobrevive a uma atualização, como fazer e restaurar um backup e como voltar o aparelho à configuração de fábrica. Descreve também a página **Arquivos**, onde tudo isso acontece. É para o administrador do aparelho.

## A página Arquivos {#cap-17-arquivos}

Abra a gaveta de navegação e toque em **Arquivos** (*Files*). O endereço é `/files`. A página exige a permissão **Leitura** [PERM_FILE_READ]{.perm} e tem o título **Sistema de Arquivos** (*Flash Filesystem*).

A página lista o conteúdo do sistema de arquivos do aparelho, pasta por pasta. Acima da lista, uma trilha mostra a pasta atual; toque num nome da trilha para voltar a ela. Na lista, as pastas vêm primeiro, e **Subir** (*Parent*) volta à pasta de cima.

- **Para abrir uma pasta,** toque no nome dela.
- **Para baixar um arquivo,** toque no nome dele.
- **Um cadeado** no lugar da caixa de marcar indica um arquivo protegido: o firmware o reescreve a cada boot, e ele não pode ser apagado.

### Os botões {#cap-17-botoes}

Cada botão só aparece para a conta que pode usá-lo:

| Botão | O que faz | Quem vê |
|---|---|---|
| **Baixar** (*Download*) | Baixa os arquivos marcados | Toda conta que abre a página |
| **Backup** | Baixa o backup completo, `.bkp` ([Backup](#cap-17-backup)) | Só o administrador completo |
| **Restaurar** (*Restore*) | Restaura um backup ([Restauração](#cap-17-restauracao)) | Só o administrador completo |
| **Firmware** | Atualiza o firmware ([Atualizar o firmware](#cap-17-ota)) | Só o administrador completo |
| **Excluir** (*Delete*) | Apaga os arquivos marcados | **Excluir** [PERM_FILE_DELETE]{.perm} |
| **Enviar** (*Upload Here*) | Envia arquivos do computador para a pasta atual | **Upload** [PERM_FILE_UPLOAD]{.perm} |

O administrador completo é a conta com todas as permissões ([capítulo 8](#cap-08-admin)).

::: {.figura #fig-17-arquivos tipo="web" arquivo="17-arquivos.png" captura="rota /files; largura 1280; sessão admin; pasta raiz; pastas history, lang, themes e web; arquivos calib.csv, README.txt (com cadeado), system.blog e system.old.blog"}
A página Arquivos na raiz, vista pelo administrador: os seis botões, as pastas e os arquivos. O cadeado marca o README.txt, que não pode ser apagado.
:::

::: {.figura #fig-17-arquivos-leitura tipo="web" arquivo="17-arquivos-leitura.png" captura="rota /files; largura 390; sessão de um operador com as permissões Painel, Histórico e Leitura; pasta raiz"}
A mesma página para uma conta só com Leitura: resta o botão Baixar.
:::

### O que há no sistema de arquivos {#cap-17-conteudo}

| Caminho | O que é | Onde está descrito |
|---|---|---|
| `/history/` | O histórico, um arquivo por dia | [Capítulo 15](#cap-15) |
| `/lang/` | Os pacotes de idioma, `.lng` | [Capítulo 13](#cap-13-idioma) |
| `/themes/` | Temas do painel, `.thm` | [Capítulo 11](#cap-11) |
| `/web/` | Páginas servidas do disco. Em geral, vazia | — |
| `/calib.csv` | A calibração dos sensores | [Capítulo 6](#cap-06-calib-csv) |
| `/cert.pem` | O certificado da telemetria, quando instalado | [Capítulo 21](#cap-21-tls) |
| `/system.blog` e `/system.old.blog` | O log de eventos | [Capítulo 16](#cap-16) |
| `/README.txt` | Um mapa do sistema de arquivos, escrito pelo firmware | — |
| `/config/` | A configuração, as contas, o cursor da telemetria e o par HTTPS | Não aparece na página |

A pasta `/config` guarda as senhas e as chaves. A página não a lista, e o aparelho recusa baixar, enviar ou apagar qualquer coisa dentro dela.

### Baixar {#cap-17-baixar}

1. Marque os arquivos.
2. Toque em **Baixar**. Com mais de um arquivo, confirme a pergunta **Baixar N?**.

O navegador baixa um arquivo de cada vez. Baixar da pasta `/history` exige também a permissão **Histórico**, e baixar o log de eventos exige também **Logs** ([capítulo 8](#cap-08-permissoes)).

### Enviar {#cap-17-enviar}

1. Abra a pasta de destino.
2. Toque em **Enviar** e escolha um ou mais arquivos.
3. A página mostra **Upload concluído.** e atualiza a lista.

Um arquivo com o mesmo nome de outro da pasta o substitui. O aparelho recusa um nome de arquivo inválido, um envio maior que o espaço livre e qualquer destino dentro de `/config`.

::: atencao
**Confira a lista depois de enviar.** A página mostra **Upload concluído.** mesmo quando o aparelho recusa o arquivo. Confirme que o arquivo aparece na pasta com o tamanho certo.
:::

Alguns arquivos têm tratamento próprio:

- **`calib.csv`:** o aparelho só aceita o arquivo se a linha `VERSION` for maior que a do arquivo atual ([capítulo 6](#cap-06-calib-csv)).
- **Temas, `.thm`:** entram em uso na hora.
- **Pacotes de idioma, `.lng`:** o aparelho só lê o pacote no boot. Reinicie depois de enviar.

### Excluir {#cap-17-excluir}

1. Marque os arquivos.
2. Toque em **Excluir** e confirme a pergunta **Excluir N?**.
3. A página mostra **Arquivos excluídos.**

Se algum arquivo não pôde ser apagado, a página mostra **Alguns arquivos não puderam ser excluídos.** Com o painel em uso naquele momento, mostra **Display em uso. Tente novamente em alguns segundos.**

Cada envio e cada exclusão ficam no log de eventos, com o número da conta no contexto. O caminho do arquivo só aparece no console e no syslog ([capítulo 16](#cap-16-o-que)).

## Backup {#cap-17-backup}

O backup é uma cópia de todos os arquivos do aparelho num único arquivo `.bkp`: a configuração, as contas, o histórico, o log de eventos, os pacotes de idioma, os temas, a calibração, o cursor da telemetria, o certificado da telemetria e o par do HTTPS.

**Proteções do arquivo:**

- um cabeçalho de 40 bytes com um CRC32 próprio;
- um CRC32 sobre todo o conteúdo;
- a identidade do chip do aparelho que o gerou. O backup só é restaurado no mesmo aparelho.

**Para fazer um backup:**

1. Entre com o administrador completo.
2. Abra a página **Arquivos** e toque em **Backup**.
3. O navegador salva o arquivo `backup_<chip>_<data>.bkp`, em que `<chip>` é a identidade do chip em hexadecimal e `<data>` é a hora do aparelho em segundos Unix.

A página mostra **Download do backup iniciado.** Se o painel estiver em uso ou outra operação pesada estiver em andamento, o aparelho recusa o pedido: tente de novo em alguns segundos.

O log de eventos registra cada backup como **Config alterada** (303), módulo `SEC`, com o número da conta no contexto.

::: perigo
**O backup contém segredos.** Ele leva a configuração inteira, com os hashes das senhas, a senha do Wi-Fi e a chave privada do HTTPS. Guarde o arquivo `.bkp` com o mesmo cuidado de uma senha. Por isso, só o administrador completo faz backup.
:::

::: nota
**O backup não copia um aparelho para outro.** Como ele é preso à identidade do chip, um `.bkp` restaurado em outro aparelho é recusado com `chip ID mismatch`. Para configurar vários aparelhos iguais, use a API ([capítulo 26](#cap-26)) ou o gestor de frota ([capítulo 27](#cap-27)).
:::

## Restauração {#cap-17-restauracao}

A restauração grava de volta, no aparelho, os arquivos de um backup feito nele. Ela exige o administrador completo.

1. Abra a página **Arquivos** e toque em **Restaurar**.
2. Escolha o arquivo `.bkp`. Um arquivo com outra extensão é recusado com **Selecione um arquivo .bkp de backup**.
3. A página mostra **Etapa 1/3: Validando backup (CRC + chip ID)...** e envia o arquivo para conferência, sem gravar nada.
4. Leia a pergunta de confirmação e toque em **OK** para seguir.
5. A página mostra **Etapa 2/3: Aplicando restore (pode levar ~15 s)...** e envia o arquivo de novo, agora para gravar.
6. A página mostra **Etapa 3/3: Reiniciando dispositivo (~25 s)...**, e o aparelho reinicia sozinho.
7. Quando o aparelho volta, a página abre a entrada. Entre de novo.

O texto da confirmação, com o número de arquivos e o tamanho:

```text
Backup válido — N arquivos (X bytes).

Isto SOBRESCREVE os arquivos atuais do dispositivo (history, configs, sensores, themes).
A configuração de Wi-Fi e a senha de admin do backup serão restauradas.

Prosseguir com a restauração?
```

Se o aparelho não volta em 90 s, a página mostra **Dispositivo offline após 90 s. Verifique conexão e ligue manualmente se necessário.**

::: {.figura #fig-17-restaurar-confirmar tipo="web" arquivo="17-restaurar-confirmar.png" captura="rota /files; largura 1280; sessão admin; backup do próprio aparelho escolhido, validação concluída, janela de confirmação aberta com o número de arquivos e o tamanho"}
A confirmação da restauração, depois da validação: o número de arquivos, o tamanho e o aviso do que será sobrescrito.
:::

::: {.figura #fig-17-restaurar-etapa3 tipo="web" arquivo="17-restaurar-etapa3.png" captura="rota /files; largura 1280; sessão admin; aviso Etapa 3/3: Reiniciando dispositivo (~25 s)... visível"}
A última etapa da restauração: a página espera o aparelho voltar e abre a entrada.
:::

### O que a restauração faz {#cap-17-restauracao-efeito}

- **Cada arquivo do backup substitui o arquivo de mesmo nome.** Os arquivos que existem no aparelho e não estão no backup ficam como estão.
- **A configuração volta à do backup,** com a rede Wi-Fi, as contas e a senha do administrador daquela data. Se a senha do administrador mudou depois do backup, a antiga volta a valer.
- **O arquivo do dia corrente volta à versão do backup,** e o bloco aberto na RAM é descartado. As medições feitas entre o backup e a restauração se perdem ([capítulo 15](#cap-15-ota)).
- **O aparelho reinicia** para ler tudo de novo.
- **O log de eventos** registra a restauração como **Arquivo enviado** (574), módulo `OTA`, nível `WRN`, com o resultado no contexto.

### Motivos de recusa {#cap-17-restauracao-recusa}

Uma recusa aparece como **Validação falhou:** ou **Falha ao aplicar:**, seguido do motivo, em inglês:

| Motivo | O que quer dizer | O que fazer |
|---|---|---|
| `magic invalid` | O arquivo não é um backup do SIMUT | Escolha o arquivo `.bkp` certo |
| `unsupported schema` | O backup tem um formato que este firmware não conhece | Use um backup feito por este firmware |
| `header CRC mismatch` | O cabeçalho está corrompido | Use outra cópia do backup |
| `payload truncated` | O arquivo está incompleto | Baixe o backup de novo |
| `payload CRC mismatch` | O conteúdo está corrompido | Use outra cópia do backup |
| `chip ID mismatch (backup is from another device)` | O backup é de outro aparelho | Use um backup deste aparelho |
| `invalid path` ou `path too long` | Um caminho dentro do backup é inválido | Use outra cópia do backup |
| `I/O error` | A gravação na flash falhou | Veja a atenção abaixo |
| `internal error` | Erro inesperado | Tente de novo; se persistir, veja o log de eventos |

::: atencao
**Uma aplicação que falha no meio apaga o que já gravou.** A restauração grava cada arquivo direto no lugar do antigo. Se ela falha no meio, o aparelho apaga os arquivos que já tinha gravado e não reinicia. A configuração continua na RAM. Repita a restauração com o mesmo arquivo antes de reiniciar o aparelho.
:::

::: atencao
**A restauração grava no máximo 200 arquivos, com até 4 KiB de nomes somados.** Um backup acima disso passa pela validação e falha na aplicação com `I/O error`. Isso só acontece com muitos dias de histórico, num aparelho com poucos canais. Antes de fazer o backup, apague pela página **Arquivos** os dias mais antigos de `/history` que não precisa restaurar, depois de baixá-los.
:::

## Atualizar o firmware {#cap-17-ota}

### Antes de começar {#cap-17-ota-antes}

::: perigo
**Não há volta automática.** O aparelho tem um único espaço para o firmware. A imagem nova substitui a atual, e não existe uma cópia da anterior para recuar. Se algo der errado na aplicação, a recuperação é pelo cabo USB e pelo botão BOOTSEL ([capítulo 18](#cap-18)).
:::

Você precisa de:

- **o arquivo `.bin` da imagem certa.** Cada versão publica três imagens: `simut_v<versão>_release.bin`, `simut_v<versão>_alpha.bin` e `simut_v<versão>_air.bin`. Use a mesma variante que está no aparelho. O `.uf2` com o mesmo nome é para a gravação pelo USB ([capítulo 3](#cap-03)), não para esta página;
- **uma conta de administrador completo;**
- **o aparelho ligado numa fonte estável** durante cerca de 1,5 min;
- **uma conexão estável** entre o computador e o aparelho. Se envios longos pela porta 80 caem na sua rede, use uma porta alternativa do servidor web ([capítulo 9](#cap-09-servidor-web)).

A variante do aparelho aparece na resposta de `/api/status`, no campo `sys.env`, e na de `/api/perms`, no campo `env`: `release`, `alpha` ou `air` ([capítulo 26](#cap-26-status)).

### O que sobrevive e o que se perde {#cap-17-sobrevive}

A área onde a imagem nova é recebida é a mesma partição da flash que guarda o sistema de arquivos. Por isso, **a atualização reformata o sistema de arquivos.** Antes de começar, o aparelho guarda uma cópia da configuração principal numa área separada e a devolve no primeiro boot da imagem nova.

| Sobrevive | Se perde |
|---|---|
| A rede Wi-Fi e o IP | O histórico inteiro |
| As contas, as senhas e os PINs | O log de eventos |
| A configuração dos sensores e os limites de alarme | Os pacotes de idioma: a interface e o painel voltam ao inglês |
| A telemetria e a linha de alarmes | A calibração dos sensores, `/calib.csv` |
| O fuso, o NTP e as demais opções das páginas de configuração | Os temas enviados e as páginas em `/web` |
| A calibração do toque do painel | O par do HTTPS: o aparelho volta a atender em HTTP ([capítulo 9](#cap-09-https-ota)) |
| | O cursor da telemetria e o certificado `/cert.pem` |
| | A origem do CORS ([capítulo 9](#cap-09-cors)) |
| | [air]{.img} As opções do Air: `air idle` e `air charger` voltam ao padrão ([capítulo 19](#cap-19)) |

Tudo o que se perde está no backup que a página baixa antes de começar. Restaurá-lo depois da atualização devolve tudo ([Depois da atualização](#cap-17-ota-depois)).

::: nota
**A calibração do toque não é a calibração dos sensores.** O aviso da página lista "Calibração de touch (/calib)" entre as perdas. O arquivo que se perde é o `/calib.csv`, com a calibração dos sensores. A calibração do toque do painel fica na configuração e atravessa a atualização.
:::

### Passo a passo {#cap-17-ota-passos}

1. Entre com o administrador completo e abra a página **Arquivos**.
2. Toque em **Firmware**. A página mostra o aviso abaixo. Leia e toque em **OK**.
3. Escolha o arquivo `.bin`. Um arquivo com outra extensão é recusado com **Selecione um arquivo .bin de firmware**.
4. A página mostra **Etapa 1/4: Baixando backup .bkp...**. Ela baixa o backup, confere o cabeçalho dele e o salva no seu computador como `simut_pre-ota_<número>.bkp`.
5. A página pergunta se deve iniciar a atualização. Confira que o backup está salvo e toque em **OK**. Se tocar em **Cancelar**, nada muda no aparelho.
6. A página mostra **Etapa 2/4: Enviando firmware (~30 s)...** e envia a imagem. O aparelho grava e confere a imagem.
7. A página mostra **Etapa 3/4: Aplicando firmware...**. O aparelho aceita a aplicação e desliga a rede.
8. A página mostra **Etapa 4/4: Aguardando boot (~25 s)...** e, 5 s depois, abre a página de entrada. Ela ainda não responde: o aparelho está gravando a imagem.
9. Espere cerca de 1 min e recarregue a página de entrada.
10. Entre e confira a versão ([Conferir a versão](#cap-17-ota-conferir)).
11. Restaure o backup ([Depois da atualização](#cap-17-ota-depois)).

Não desligue o aparelho entre os passos 6 e 9.

O aviso do passo 2, na íntegra:

```text
Atualização de firmware via OTA.

A partição de arquivos é REFORMATADA — a área de staging divide a partição com ela.

Só a configuração sobrevive (capturada antes, restaurada depois):
  • Wi-Fi, senha de administrador, telemetria
  • Mapeamento de sensores e limites de alarme

Todo o resto que está no dispositivo se perde:
  • Histórico de leituras (/history)
  • Pacotes de idioma (/lang)
  • Temas customizados (/themes)
  • Calibração de touch (/calib)
  • Arquivos em /web
  • O log de eventos

Um backup .bkp será baixado automaticamente — restaure depois para recuperar tudo.

Prosseguir?
```

A pergunta do passo 5:

```text
Backup salvo no seu computador.

Iniciar a atualização?
  • Upload do firmware: ~30 s
  • Aplicação + reboot: ~25 s
  • Boot completo: ~25 s

Total: ~80 s. Não desligue o dispositivo durante o processo.
```

::: {.figura #fig-17-aviso-firmware tipo="web" arquivo="17-aviso-firmware.png" captura="rota /files; largura 1280; sessão admin; botão Firmware tocado; janela de confirmação do navegador com o aviso de reformatação"}
O aviso que abre a atualização: o que sobrevive e o que se perde.
:::

::: {.figura #fig-17-iniciar tipo="web" arquivo="17-iniciar.png" captura="rota /files; largura 1280; sessão admin; backup simut_pre-ota já baixado pelo navegador (barra de downloads visível); segunda janela de confirmação aberta"}
A segunda confirmação, depois de o backup chegar ao computador. Cancelar aqui não muda nada no aparelho.
:::

::: {.figura #fig-17-enviando tipo="web" arquivo="17-enviando.png" captura="rota /files; largura 1280; sessão admin; aviso Etapa 2/4: Enviando firmware (~30 s)... visível durante o envio"}
O envio da imagem. O painel do aparelho fica parado até o reinício.
:::

::: {.figura #fig-17-ota-linha-tempo tipo="diagrama" arquivo="17-ota-linha-tempo.png" captura="linha do tempo horizontal de uma atualização: 'backup .bkp baixado' (segundos, verde); 'envio e conferência da imagem, 35 a 36 s' (amarelo, rótulo 'sistema de arquivos já sobrescrito'); 'aplicação, cerca de 25 s' (vermelho, rótulo 'única janela em que um corte de energia exige BOOTSEL'); 'boot da imagem nova' (verde); marca 'interface web de volta, 52 a 56 s depois da aplicação'; embaixo, o que cada corte causa: configuração perdida, aparelho sem firmware, nada"}
Legenda: as etapas de uma atualização e o que um corte de energia em cada uma custa.
:::

### Tempos medidos {#cap-17-ota-tempos}

| Medição | Resultado |
|---|---|
| Envio e conferência da imagem | 34,9 a 35,9 s (v2.7.0, 22/09/2026, 6 atualizações) |
| Da aplicação até o aparelho voltar | 52 a 56 s (v2.7.0, 22/09/2026, 6 de 6 atualizações bem-sucedidas) |
| Interface web fora do ar | 48,4 s em média, em 21 atualizações seguidas na bancada, todas bem-sucedidas (agosto de 2026, imagem de 957.500 B) |

Os tempos dependem do tamanho da imagem e da rede. Para confirmar uma atualização, não conte com os tempos: confira a versão.

### O que o aparelho confere {#cap-17-ota-conferencias}

| Quando | O que é conferido | Se falhar |
|---|---|---|
| Antes de tudo | A conta é o administrador completo | O aparelho recusa com HTTP 403. A página só mostra o botão **Firmware** ao administrador completo |
| Etapa 1 | O cabeçalho do backup confere com o que o aparelho anunciou | **Backup corrompido (CRC). Abortado.** Nada muda no aparelho |
| Fim do envio | O tamanho está entre 100 KiB e 1016 KiB (1.040.384 bytes) | Recusa, `v=4` (pequeno) ou `v=5` (grande) |
| Fim do envio | Os primeiros 256 bytes formam um início de imagem válido para o RP2040, com o CRC que a ROM do chip confere | Recusa, `v=6` |
| Fim do envio | A imagem traz a etiqueta da variante (`SIMUT-ENV`) igual à do aparelho | Recusa, `v=7` |
| Primeiro boot | O CRC da imagem gravada confere com o da imagem recebida | O log registra o erro ([Conferir a versão](#cap-17-ota-conferir)) |

Uma imagem sem etiqueta de variante, de uma versão anterior à etiqueta, é aceita.

Uma recusa aparece como **Falha no envio (validação v=N). Cancelled.**. Um arquivo maior que 1 MiB nem chega a ser conferido e aparece como `v=undefined`.

::: perigo
**Uma recusa no envio também apaga o sistema de arquivos.** A imagem é gravada sobre o sistema de arquivos enquanto chega, e a conferência só acontece no fim. Quando o aparelho recusa a imagem, ele reformata o sistema de arquivos e continua funcionando com a configuração que está na RAM, mas o arquivo da configuração já não existe. **Restaure o backup do passo 4 antes de reiniciar o aparelho.** Se ele reiniciar antes, volta com a configuração de fábrica ([capítulo 18](#cap-18-fabrica)).
:::

### Conferir a versão {#cap-17-ota-conferir}

A única prova de uma atualização é o aparelho informar a versão nova. Confira em qualquer um destes lugares:

- na gaveta de navegação da interface web, abaixo dos itens, como `SIMUT 2.7.1`;
- na resposta de `/api/status`, no campo `sys.ver`;
- no console, com `show system info`.

O log de eventos começa vazio depois da atualização, e os primeiros registros contam o que aconteceu. Marque **INF**, **WRN** e **ERR** para vê-los:

| Registro | O que quer dizer |
|---|---|
| **Config alterada** (303), módulo `OTA`, nível `WRN` | O boot que se seguiu a uma aplicação |
| **Config alterada** (303), módulo `OTA`, nível `INF` | A imagem gravada foi conferida e está íntegra |
| **Config alterada** (303), módulo `OTA`, nível `ERR` | O CRC da imagem gravada não confere. Grave a imagem de novo pelo USB ([capítulo 18](#cap-18)) |

::: {.figura #fig-17-log-pos-ota tipo="web" arquivo="17-log-pos-ota.png" captura="rota /history; largura 1280; sessão admin; logo depois de uma atualização; seção Eventos do Sistema carregada com INF, WRN e ERR marcados; os dois registros Config alterada do módulo OTA, WRN e INF, no fim da lista"}
O log de eventos logo depois de uma atualização: o boot pós-aplicação (WRN) e a conferência da imagem (INF).
:::

### Depois da atualização {#cap-17-ota-depois}

1. Entre de novo. A sessão anterior não sobrevive ao reinício.
2. Se o aparelho usava HTTPS, ele volta em HTTP até o par ser restaurado ([capítulo 9](#cap-09-https-ota)).
3. Restaure o backup `simut_pre-ota_<número>.bkp` ([Restauração](#cap-17-restauracao)). Faça isso logo: as medições entre a atualização e a restauração se perdem.
4. Depois do reinício da restauração, confira o idioma, o histórico e a calibração dos sensores.

Se preferir não restaurar o backup inteiro, envie só o que faltou pela página **Arquivos**: o pacote de idioma, publicado junto de cada versão como arquivo `.lng`, e o `calib.csv`. Reinicie depois de enviar um pacote de idioma.

### Se a aplicação for recusada {#cap-17-ota-aplicacao-recusada}

Depois de um envio aceito, a página pede a aplicação. Se o aparelho recusar, a página mostra **Aplicação recusada (HTTP N).**, com o motivo:

| HTTP | Motivo | O que fazer |
|---|---|---|
| 403 | A conta não é o administrador completo | Entre com o administrador completo e recomece |
| 409 | Não há imagem aceita esperando | Recomece a atualização |
| 503 | O painel estava em uso naquele instante | Não reinicie o aparelho: a imagem gravada está esperando. Repita a aplicação pela API, com `POST /api/ota/apply` e a sessão do administrador ([capítulo 26](#cap-26-ota)) |

Enquanto a imagem espera a aplicação, o sistema de arquivos está fora de uso. Um reinício nesse estado descarta a imagem e deixa o aparelho com a configuração de fábrica. O log registra **Config alterada** (303), módulo `OTA`, nível `WRN`; no console, a linha traz o texto `Staged update discarded: device rebooted before apply`.

## Falta de energia durante a atualização {#cap-17-energia}

Uma campanha de testes em 11/09/2026 interrompeu atualizações em cada etapa, com um reinício pelo pino RUN no papel de corte de energia:

| Etapa interrompida | Firmware | Configuração | Como recuperar |
|---|---|---|---|
| Download do backup | Intacto | Intacta | Nada a fazer |
| Envio da imagem | Intacto, versão antiga | Perdida | Pegue a senha do administrador pelo console USB, configure a rede e restaure o backup ([capítulo 18](#cap-18-fabrica)) |
| Entre o envio e a aplicação | Intacto, versão antiga | Perdida | Idem |
| Aplicação, os cerca de 25 s em que a imagem é copiada | Corrompido: o aparelho não liga | — | BOOTSEL e cabo USB ([capítulo 18](#cap-18-bootsel)) |
| Boot da imagem nova | Atualizado | Intacta | Nada a fazer |

A aplicação é a única janela em que um corte deixa o aparelho sem firmware. Nas outras etapas, o pior caso é perder a configuração e os arquivos, que o backup devolve.

::: atencao
**Um aparelho instalado longe.** Uma falta de energia nos cerca de 25 s da aplicação exige ir até o aparelho com um computador e um cabo USB. Atualize aparelhos remotos com a alimentação garantida, de preferência com uma pessoa no local.
:::

## Atualização no alpha e no Air {#cap-17-variantes}

[alpha]{.img} O procedimento é o mesmo, com a imagem `simut_v<versão>_alpha.bin`.

[air]{.img} O Air só atualiza em M0, o modo acordado, porque em M1 não há servidor web ([capítulo 19](#cap-19-modos)):

1. Ligue o carregador no pino configurado, GP17 de fábrica. Com o carregador, o Air fica em M0 enquanto a atualização durar.
2. Sem carregador, ponha o Air em M0 de outro jeito: ligue-o de novo, ou rode `air stop` pelo console USB durante um despertar. Cada pedido autenticado da página renova o tempo de ociosidade.
3. Atualize com a imagem `simut_v<versão>_air.bin`, como nos passos acima.
4. Restaure o backup. Sem ele, o `air idle` volta a 300 s e o `air charger` volta ao GP17.

Depois do boot da imagem nova, o Air fica em M0 e volta a hibernar sozinho quando o tempo de ociosidade acaba.

## Reset de fábrica, formatação e senha do administrador {#cap-17-fabrica}

Três comandos do console serial devolvem o aparelho a um estado conhecido. Eles só funcionam pelo cabo USB: pelo Bluetooth, o aparelho responde `ERROR: Comando so pela USB (cabo serial).` e registra a tentativa. Cada um pede `confirm` no fim; sem ele, o aparelho só explica o que o comando faz.

| Comando | O que apaga | O que mantém |
|---|---|---|
| `system admin reset confirm` | A senha do administrador, que vira uma senha aleatória de 8 caracteres | Todo o resto |
| `system factory confirm` | A configuração inteira: rede, contas, sensores, alarmes, telemetria | Os arquivos: histórico, log, pacotes de idioma, calibração, par do HTTPS |
| `system format confirm` | Todo o sistema de arquivos, com a configuração e todos os arquivos | Só o firmware |

[air]{.img} No Air, que tem o console completo, entre antes no modo privilegiado com `enable` ([capítulo 14](#cap-14-modos)). Os comandos também estão descritos no [capítulo 14](#cap-14-recuperacao).

::: nota
**Nenhum desses comandos existe na interface web ou no painel.** Eles servem justamente para quando a web não é alcançável ou ninguém sabe a senha do administrador.
:::

### A senha do administrador {#cap-17-admin-reset}

```text
SIMUT> system admin reset confirm
Senha admin resetada. Nova senha (unica vez):
 K7M2QX9A
Trocar no 1o login via web (forcado).
```

- A senha nova usa letras maiúsculas e algarismos, sem `O`, `0`, `I` e `1`. O exemplo acima é ilustrativo.
- Ela é gravada na hora e vale depois de reiniciar.
- Ela aparece uma única vez. Anote antes de fechar o console.
- A interface web obriga a trocá-la no primeiro login ([capítulo 13](#cap-13-troca-obrigatoria)).
- O comando não muda as permissões nem o PIN do painel do administrador.

Se a gravação falhar, o console avisa `ERROR: NAO SALVOU: vale so ate reiniciar.`: a senha nova vale até o próximo reinício. Rode o comando de novo.

### Reset de fábrica {#cap-17-factory}

`system factory confirm` grava a configuração de fábrica e reinicia. Depois dele:

- o aparelho não tem rede Wi-Fi configurada e abre o ponto de acesso de configuração ([capítulo 9](#cap-09-ap));
- só existe a conta `admin`, com uma senha aleatória;
- o histórico, o log, os pacotes de idioma, a calibração e o par do HTTPS continuam no sistema de arquivos.

A senha aleatória desse reset não é mostrada. Depois do reinício, rode `system admin reset confirm` para ter uma senha do administrador.

### Formatação {#cap-17-format}

`system format confirm` reformata o sistema de arquivos inteiro e reinicia. O aparelho volta como novo, mas com o mesmo firmware:

- a configuração, as contas, o histórico, o log, os pacotes de idioma, a calibração e o par do HTTPS se perdem;
- no boot seguinte, o aparelho cria a configuração de fábrica e mostra a senha inicial do administrador no console USB, uma única vez, na moldura `SEC-003: FACTORY DEFAULTS ATIVADO`, seguida de `Senha ADMIN inicial:` e da senha.

Se você perder essa moldura, rode `system admin reset confirm`.

A formatação é o caminho para um sistema de arquivos corrompido ([capítulo 18](#cap-18-sistema-arquivos)). Se você tem um backup, restaure-o depois de configurar a rede.
