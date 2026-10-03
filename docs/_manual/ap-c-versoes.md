# Histórico de versões {#ap-c}

O que cada versão da linha 2.x trouxe para quem usa o aparelho, da mais nova para a mais antiga. O registro completo, com as medições de cada mudança, está no `CHANGELOG.pt-BR.md` do repositório.

## Linha 2.10 — estável

| Versão | Data | O que trouxe |
|---|---|---|
| v2.10.0 | 03/10/2026 | A telemetria envia cada registro pelo lugar onde ele foi gravado, não pela hora: um relógio que volta não deixa mais registros sem envio, e um arquivo do dia cuja posição deixou de ser confiável vai inteiro de novo ([capítulo 21](#cap-21-fora-de-ordem)). Gravar contas não reinicia mais o aparelho, e uma sessão web termina quando a conta dela é excluída ou tem a senha trocada por outra pessoa ([capítulo 8](#cap-08-pagina)). Um nome sem conta custa na entrada o mesmo tempo que uma senha errada. O ensaio da página de configuração não mexe mais no aparelho em funcionamento: o silêncio geral, o fuso e os campos da área de extensão só valem depois de gravados ([capítulo 5](#cap-05-ensaio)). Uma restauração que falha no meio fica com os arquivos que terminou, sem o limite de 200 arquivos, e a aplicação exige antes a conferência do mesmo backup ([capítulo 17](#cap-17-restauracao)). Um limite de alarme que não é número é recusado em vez de virar 0 ([capítulo 26](#cap-26-commit)). O menu Configurações do painel segue a ordem de uso, um grupo por página ([capítulo 11](#cap-11-menu)). A página Arquivos abre arquivos de texto organizados ([capítulo 17](#cap-17-ver)), e a **Prévia ao Vivo** da telemetria é montada com os dados do aparelho, com um registro de cada tipo ([capítulo 21](#cap-21-construtor) e [capítulo 22](#cap-22-codigos)) |

## Linha 2.9 — estável

| Versão | Data | O que trouxe |
|---|---|---|
| v2.9.0 | 02/10/2026 | Pelo ar, o aparelho só instala imagem assinada pelo projeto: o `.bin` de uma release ou uma build do configurador; uma build própria vai pelo USB ([capítulo 17](#cap-17-ota-assinatura)). O painel mostra cada etapa de uma atualização, e o motivo de uma recusa ([capítulo 11](#cap-11-atualizacao)). O ponto de acesso de configuração só abre quando pedido, e o aparelho continua medindo, gravando e alarmando com ele no ar ([capítulo 9](#cap-09-ap-quando)). Uma unidade sem rede configurada pede a data e a hora no fim do boot, e Configurações → Data e hora abre a mesma tela ([capítulo 10](#cap-10-painel)). SDA e SCL trocados não travam mais o boot ([capítulo 2](#cap-02-i2c)). "NTP sincronizado" passa a querer dizer que a hora foi acertada de verdade ([capítulo 10](#cap-10-onde)). Uma borda de alarme recusada pela fila cheia sai quando há espaço ([capítulo 22](#cap-22-fila)). A tela Licença mostra a abertura no idioma do pacote, o texto MIT em inglês e todos os componentes de terceiros ([capítulo 11](#cap-11-licenca)) |

## Linha 2.8 — estável

| Versão | Data | O que trouxe |
|---|---|---|
| v2.8.0 | 30/09/2026 | Um envio de firmware cortado ou recusado não leva mais a configuração: a cópia que atravessa a atualização é gravada no início do envio, o aparelho grava a configuração de volta assim que um envio termina sem aplicação, e o boot seguinte usa a cópia uma vez e a apaga. Os demais arquivos continuam voltando só pelo backup, e a proteção vale a partir de um aparelho que já tem a v2.8.0 ([capítulo 17](#cap-17-sobrevive)). No formato **Dinâmico**, cada linha de telemetria ganha o campo **Cabeçalho Content-Type**, que antes era sempre `text/plain`; vazio, vale `application/json` ([capítulo 21](#cap-21-dinamico) e [capítulo 22](#cap-22-dinamico)). A configuração passa à versão 26 do formato: uma versão anterior gravada depois dela não a lê e volta com a configuração de fábrica ([capítulo 3](#cap-03-antigo)) |

## Linha 2.7 — estável

| Versão | Data | O que trouxe |
|---|---|---|
| v2.7.4 | 26/09/2026 | **Procurar sondas** não deixa mais em erro um BMP280 ou BME280 nos pinos de I²C de hardware ([capítulo 6](#cap-06-procurar)). Mudar só **Amostra (ms)** ou **Registro Local**, que não têm efeito, grava sem reiniciar ([capítulo 5](#cap-05-grupos)). Na alpha e no Air, a página Configurações perde a seção Calibração do Touch, e as rotas dos temas e da calibração do painel deixam de responder ([capítulo 26](#cap-26-imagens)). O site ganha o [configurador de build](https://angelointj.github.io/simut/configurador/), que monta uma imagem com outro conjunto de recursos ([capítulo 3](#cap-03-sob-medida)) |
| v2.7.3 | 25/09/2026 | A página de entrada mostra a versão do firmware ([capítulo 13](#cap-13-entrada)). A página Configurações ganha o botão **Reiniciar sem salvar**, que reinicia o aparelho e descarta o que a página não salvou ([capítulo 5](#cap-05-reiniciar)). O ícone da aba e o logotipo da página de entrada passam a seguir a marca nova |
| v2.7.2 | 24/09/2026 | O Air carrega o relógio através do sono: os carimbos ficam dentro de ±0,09 s em vez de atrasar 0,8 s a cada despertar ([capítulo 19](#cap-19)). A telemetria monta cada envio um registro inteiro por vez: uma fila longa não sai mais como JSON inválido nem pula registros ([capítulo 21](#cap-21)). Com um sensor, o LCD do alpha mostra quantos registros esperam envio, e o ícone de Wi-Fi cresce da esquerda para a direita ([capítulo 12](#cap-12)). No painel, as setas da tela Segurança do PIN movem a seleção em vez de fechá-la, apertar a política marca as contas para trocar o PIN, e o Modo de Configuração mostra a rede, a chave e o endereço do AP ([capítulo 11](#cap-11)). Os nomes dos eventos vêm do pacote de idioma. Toda imagem fica 16 kB menor |
| v2.7.1 | 22/09/2026 | O ponto de acesso de configuração volta a aceitar conexões: um celular entra em 4,1 s. O AP abre sozinho num aparelho sem rede configurada ou que perdeu a sua. O painel ganha a linha 12 em Configurações, e o LCD do alpha mostra o nome e a chave da rede |
| v2.7.0 | 22/09/2026 | A linha sai do beta com base em medições: 8,18 h sem reinício e 6 de 6 atualizações pelo ar sem perda. Corrige o V-09 (uma conta restrita podia criar outra com mais permissões do que tinha). A autópsia de travamento passa a guardar mais três registros |

## Linha 2.6

| Versão | Data | O que trouxe |
|---|---|---|
| v2.6.1-beta | 21/09/2026 | Busca de redes Wi-Fi na página Rede, inclusive de dentro do ponto de acesso. A interface web passa a ter só o que cada imagem usa |
| v2.6.0-beta | 20/09/2026 | No painel, a conta é escolhida antes do PIN, e a política de PIN passa a ser configurável. O PIN só é compilado onde há painel de toque |

## Linha 2.5

| Versão | Data | O que trouxe |
|---|---|---|
| v2.5.0-beta | 20/09/2026 | O painel passa a saber quem está diante dele: 32 contas, cada uma com o seu PIN, três permissões de alarme e o teclado embaralhado. Um servidor passa a editar alarmes ao vivo e a abrir janelas de manutenção. O espelho do painel cai de 613 para 213 ms por quadro |

## Linha 2.4

| Versão | Data | O que trouxe |
|---|---|---|
| v2.4.10-beta | 18/09/2026 | Espelho do painel mais rápido (leitura a 6 MHz). O console completo volta ao Air. `POST /api/tls` instala o par de certificados num aparelho em serviço |
| v2.4.9-beta | 18/09/2026 | A imagem release devolve 57 kB de flash sem abrir mão de recursos. O painel superior passa a ter um gesto por função: toque curto mostra mín/máx, segurar fixa a seleção |
| v2.4.7 e v2.4.8-beta | 18/09/2026 | O painel ao vivo no navegador, e clicável |
| v2.4.1-beta | 08/09/2026 | O despertar do Air fica 2,7× mais curto. O reset de senha do console sobrevive ao boot seguinte. A auditoria de segurança de 07/09/2026 é fechada |
| v2.4.0-beta | 07/09/2026 | Chega o SIMUT Air, imagem sem display com ciclo de hibernação (experimental). O bloco do histórico sobrevive a um despertar |

## Linha 2.3

| Versão | Data | O que trouxe |
|---|---|---|
| v2.3.5 a v2.3.9-beta | 24 a 29/08/2026 | O alpha ganha Bluetooth e mostra vários sensores. A interface web abre no idioma instalado. A página de licença é reescrita |
| v2.3.4 | 24/08/2026 | A linha 2.3 fica estável, depois de uma dieta de RAM que manteve todos os recursos |
| v2.3.3-beta | 23/08/2026 | Os alarmes ganham a própria linha de telemetria, e o erro de sensor passa a ser um alarme |
| v2.3.0 a v2.3.2-beta | 20 e 21/08/2026 | Páginas HTTPS carregam em um terço do tempo (keep-alive). A piscada branca entre páginas some |

## Linha 2.2

| Versão | Data | O que trouxe |
|---|---|---|
| v2.2.16 a v2.2.18-beta | 19 e 20/08/2026 | O servidor web por HTTPS fica utilizável no navegador, e uploads por HTTPS param de falhar |
| v2.2.13 e v2.2.14-beta | 19/08/2026 | Home Assistant (MQTT Discovery), `/metrics` para Prometheus e syslog remoto (RFC 5424) |
| v2.2.8 a v2.2.12-beta | 17 a 19/08/2026 | A interface web inteira volta para dentro do firmware. Os gráficos passam a ter renderizador próprio, sem CDN |
| v2.2.5-beta | 16/08/2026 | Cada permissão passa a ser conferida onde o dado é lido. O log passa a registrar transições, não batimentos |
| v2.2.0 a v2.2.4-beta | 15 e 16/08/2026 | Reforma visual da interface web, com tema escuro. Correções no relógio provisório e no histórico da meia-noite |

## Linha 2.1

| Versão | Data | O que trouxe |
|---|---|---|
| v2.1.10 | 14/08/2026 | A linha 2.1 fica estável. É a versão que o manual ilustrado anterior retratava |
| v2.1.5 a v2.1.9-beta | 12 a 14/08/2026 | Sistema visual único no painel, com acentos e desenho por DMA. Gráficos em baldes de tempo com a banda de mín/máx. Teclado de senha para a ponta do dedo |
| v2.1.0 a v2.1.4-beta | 10 e 11/08/2026 | A restauração passa a conferir quem pede antes de gravar. A hora ainda aberta na RAM chega aos gráficos e ao CSV |

## Linha 2.0

| Versão | Data | O que trouxe |
|---|---|---|
| v2.0.1 a v2.0.3-alpha | 01 a 10/08/2026 | O histórico passa a ser gravado em blocos de uma hora: uma corrupção custa uma hora, não um dia. Curvas de calibração de até 5 pontos. O envio da telemetria resiste a uma rede hostil |
