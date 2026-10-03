# Verificação no hardware

[English](VERIFICATION.md) | [Português](VERIFICATION.pt-BR.md) | [Español](VERIFICATION.es-ES.md)

O que foi medido no hardware real, e em que bancada. O [README](../README.pt-BR.md) tem o resumo.

**A bancada:**
- um Pico W com o painel TFT e o touch;
- um segundo Pico, a *PicoHand*, que aciona as linhas de RESET e BOOTSEL do alvo, cronometra a linha de acordado/dormindo e finge um carregador (ver o [AGENTS.md](../AGENTS.md));
- suítes de bancada em `tools/` para a API web, o painel, a telemetria, a OTA, quedas de Wi-Fi e o ciclo do Air.

O que foi medido no hardware real, do mais recente ao mais antigo:

| Data | O quê | Resultado |
|---|---|---|
| 03/10/2026 | Candidato à release (v2.10.0), assinado pelo CI | Suíte web com 87 aprovados e 0 falhas na imagem de teste; atualização pelo ar a partir da v2.9.0 publicada na release, no Air e no alpha: o arquivo de configuração idêntico byte a byte, todos os valores de configuração que a API web informa iguais, cinco sensores lendo, o `.bkp` restaurado com 69 a 70 de 74 arquivos idênticos (os outros são o log, o histórico e o cursor da telemetria); a candidata sobre ela mesma, conferindo a própria assinatura; de volta à v2.9.0 pelo ar e de novo para cima, a configuração igual; uma imagem sem assinatura (8), a do Air (7) e a candidata com um byte trocado (9) recusadas; um envio cortado em 400 kB e um reset, com e sem o sistema de arquivos cheio no meio: configuração intacta; 30 min sem reinício |
| 03/10/2026 | O LCD 16×2 da alpha (v2.10.0) | Cada passo de uma atualização pelo ar (`Atualizando...`, `Conferindo a assinatura...`, `Imagem conferida`, `Instalando...`, depois o boot e a barra), as três recusas pelo nome (`sem assinatura`, `outro modelo`, `assin. invalida`), o envio cortado (`Atualiz.cortada`), e as três páginas do ponto de acesso depois do `ap` (o endereço, a rede, a chave) |
| 02/10/2026 | Candidato à release (v2.9.0), assinado pelo CI | Suíte web com 87 aprovados e 0 falhas na imagem de teste; atualização pelo ar a partir da v2.8.0 publicada na release, no Air e no alpha: o arquivo de configuração idêntico byte a byte, todos os valores de configuração que a API web informa iguais, cinco sensores lendo, o `.bkp` restaurado com 70 ou 71 de 74 arquivos idênticos (os outros são o log e o histórico); a candidata sobre ela mesma, conferindo a própria assinatura; uma imagem sem assinatura (8), a do Air (7) e a v2.8.0 (8) recusadas; um envio cortado em 400 kB e um reset, com e sem o sistema de arquivos cheio no meio: configuração intacta; 30 min sem reinício |
| 30/09/2026 | Candidato à release (v2.8.0) | Suíte web com 87 aprovados e 0 falhas; atualização pelo ar a partir da v2.7.4 publicada: o arquivo de configuração idêntico byte a byte, fora a versão, cinco sensores lendo, o `.bkp` restaurado com 67 de 72 arquivos idênticos (os outros cinco deviam diferir); um envio cortado em 400 kB, depois um reset: configuração intacta; 10 min sem reinício; Air e alpha atualizados pelo ar a partir da v2.7.4 com a configuração (as opções próprias do Air de volta pelo `.bkp`) |
| 30/09/2026 | Um envio cortado, depois o sistema de arquivos cheio (v2.8.0) | Mesma flash de partida, envio cortado em 400 kB, nada alterado, o sistema de arquivos enchido até 100 % e esvaziado, reset: antes do #195 o aparelho voltou em padrões de fábrica, depois dele com a configuração. O mesmo corte na v2.7.3, sem encher: padrões de fábrica (#192) |
| 30/09/2026 | Content-Type da telemetria personalizada (v2.8.0) | Coletor num PC: o cabeçalho recebido bate com o campo para `application/x-ndjson`, `text/csv` e `application/json; charset=utf-8`; vazio manda `application/json`; o formato JSON ignora o campo; `bad value`, `aplicação/json` e `json` são recusados ao salvar |
| 26/09/2026 | Candidato à release (v2.7.4) | Suíte web com 87 aprovados e 0 falhas; os cinco sensores das três famílias; depois de uma busca de sensores o BMP280 seguiu lendo por 90 s (antes do conserto ele falhava uns 10 s depois); um commit ensaiado do intervalo de amostra responde `"reboot":false`; 10 min sem reiniciar |
| 26/09/2026 | O LCD 16×2 da alpha (v2.7.4) | Num HD44780 ligado em paralelo: a tela de boot com a versão e a barra de progresso, a tela de conectado com o IP, e depois cada sensor, um por vez, com o slot e o nível do Wi-Fi |
| 25/09/2026 | Página Configurações e tela de login (v2.7.3) | *Reiniciar sem salvar*, na imagem release e no build de teste: fora do ar 3,3 s depois do clique, de volta aos 26,4 s, e um nome editado e nunca salvo não sobreviveu ao reinício. A tela de login mostra a versão nos dois temas; 9 páginas, 0 erros de script |
| 24/09/2026 | Painel: Segurança do PIN e Modo de Configuração (v2.7.2) | As setas do rodapé ficam na tela (a v2.7.1 a fechava); um toque não salvo não muda mais a política gravada; o Confirmar mostra a rede, a chave e 192.168.4.1 (a v2.7.1 ficava na confirmação, com o AP já no ar) |
| 23/09/2026 | Relógio do Air através do sono (v2.7.2) | Carimbos entre −0,085 e +0,030 s em 10 wakes (a v2.7.1 perdia 0,8 s por wake); a correção do NTP caiu de 9–10 s para 0,08 s |
| 23/09/2026 | Filas longas de telemetria no Air (v2.7.2) | 0 corpos inválidos; 13.681 de 13.682 registros entregues acordado, 13.670 de 13.671 hibernando (v2.7.1: 68 de 69 corpos eram JSON inválido) |
| 22/09/2026 | Soak da v2.7.0 | 8,18 h, 0 reboots; o maior bloco livre do heap variou −42 B |
| 22/09/2026 | Atualizações pelo ar da v2.7.0 | 6 de 6 aplicadas; 57 arquivos restaurados, 0 registros faltando |
| 22/09/2026 | Ponto de acesso de configuração (v2.7.1) | Um cliente entra em 4,1 s, no `release` e no `alpha` com Bluetooth ligado, também com MAC aleatório. O fallback automático abre depois de 6–7 min sem rede (removido em 01/10/2026) |
| 22/09/2026 | Correção do V-09 | 10 de 10 vereditos, com controles positivos |
| 21/09/2026 | Coletor fora do ar por 3 h 58 min | 237 registros na fila, 0 reboots; drenados numa rodada com 0 faltando, mais 25 registros da linha de alarmes |
| 21/09/2026 | Suítes web | 67/67 como admin, 87/87 como conta restrita; 500 commits que gravam na flash, 0 reboots |
| 21/09/2026 | Busca de redes Wi-Fi | 18 de 18, 0,94 s por varredura, também de dentro do ponto de acesso |
| 20/09/2026 | Contas, PINs e política no painel | 32/32 |
| 19/09/2026 | Espelho do painel | 613 → 213 ms por quadro; idêntico ao framebuffer, pixel a pixel (0 de 76.800 diferentes) |
| 11/09/2026 | Queda de energia durante a atualização | Só a janela de aplicação, de ~25 s, deixa o aparelho precisando de BOOTSEL |
| 10/08/2026 | Histórico através de resets | 10 de 10 resets de hardware e 10 de 10 reboots perderam 0 registros |

Uma coisa do LCD 16×2 não passou pela tela de verdade: o leiaute de um sensor só, com a contagem de telemetria pendente (a bancada tem cinco sensores).
