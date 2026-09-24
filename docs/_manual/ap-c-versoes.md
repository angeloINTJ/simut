# Histórico de versões {#ap-c}

O que cada versão da linha 2.x trouxe para quem usa o aparelho, da mais nova para a mais antiga. O registro completo, com as medições de cada mudança, está no `CHANGELOG.pt-BR.md` do repositório.

## Linha 2.7 — estável

| Versão | Data | O que trouxe |
|---|---|---|
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
