# Recursos do SIMUT

[English](FEATURES.md) | [Português](FEATURES.pt-BR.md) | [Español](FEATURES.es-ES.md)

Tudo o que o firmware faz, em detalhe. O [README](../README.pt-BR.md) tem o resumo.

## Sensoriamento e alarmes
- **16 slots universais de sensor** — GP0–GP15. Cada slot aceita DS18B20, DHT22 ou BMP280/BME280; o BMx280 é reclassificado sozinho pelo ID do chip. Tipo e pinos são definidos em tempo de execução, sem recompilar.
- **Temperatura, umidade e pressão** como canais de primeira classe.
- **Calibração** — offsets por sensor e curvas de até 5 pontos por canal, lineares ou suaves.
- **Validação dos sensores:**
  - verificação da ROM do DS18B20, com a sonda trocada em quarentena até a certa voltar;
  - histerese de erro: 3 falhas para entrar, 5 sucessos para sair;
  - leituras fora da faixa descartadas.
- **Alarmes em todo canal:**
  - limites mínimo e máximo por canal;
  - alarme de falha que dispara mesmo com os limites do sensor desligados;
  - silêncio de 120 s e mudo global;
  - melodias no buzzer e aviso visual no display.
- **Janelas de manutenção** — por sensor, até 30 dias, definidas pelo painel ou por um servidor. Com uma aberta, os alarmes ficam suprimidos, e o início e o fim da janela são avisados como `maint_on` / `maint_off`.

## Painel touch (`release`)
- **Painel touch ILI9341 320×240** — dashboard, gráficos de histórico com faixa de mín/máx, estatísticas, calendário, configurações.
- **Identidade no painel** — o operador escolhe a conta e depois digita o PIN dela:
  - 32 contas, cada uma com o seu PIN;
  - política de PIN configurável: tamanho mínimo, 1 a 3 glifos por tecla, dígitos ou 0-9A-Z;
  - teclado sorteado de novo a cada toque;
  - bloqueio por conta: a sexta falha bloqueia a conta, e 20 falhas no total bloqueiam o painel.
- **Administração na tela:**
  - o item Usuários cria contas e define os bits de permissão e os PINs;
  - as 13 linhas de Configurações são filtradas pelo que a conta pode fazer;
  - Configurações → 8 liga o ponto de acesso de configuração;
  - Configurações → 4 acerta a data e a hora, e uma unidade sem rede configurada as pede no fim do boot.
- **Gestos no painel superior** — um toque alterna mín/máx, segurar 3 s fixa a seleção.
- **Renderização rápida com DMA** — composição em canvas pelo SPI a 62,5 MHz.
- **Área segura de 4 px em toda tela** — o ajuste de alinhamento da tela (±4 px por eixo) nunca corta conteúdo.
- **Temas** — até 8 carregados da LittleFS (11 vêm em `data/themes/`); o editor em `tools/theme-editor/` mostra a prévia num aparelho de verdade.
- **Sistema de som** — classes Toque, Confirmação, Erro, Alarme e Atenção, 6 melodias cada, com volumes separados para sistema e alarme.

## LCD de caracteres (`alpha`)
- **Leituras** — alterna entre todos os slots e canais ativos a cada 3 s, com dígitos grandes para temperatura e umidade e uma etiqueta `S<n>` indicando o slot.
- **Ponto de acesso de configuração** — mostra o endereço, o SSID e a chave, rolando os valores longos.
- **Console Bluetooth** — veja a nota de segurança em [Ambientes](../README.pt-BR.md#ambientes).
- **Pendentes de telemetria** — com um único sensor, o canto inferior esquerdo mostra a contagem de pendentes de telemetria (`N`, ou `Nk` a partir de mil), e o ícone de Wi-Fi cresce da esquerda para a direita.

## Interface web
- **11 páginas** — comprimidas com gzip (zopfli) na flash, com temas claro e escuro que seguem a preferência do sistema, gerenciador de arquivos e sessões multiusuário que expiram após 15 minutos ociosas.
- **Espelho do painel ao vivo** (`release`) — o quadro atual do painel no navegador, 213 ms por quadro. Um clique nele é um toque na tela.
- **Cada mudança diz quanto custa** — três botões:
  - *Testar*: aplica sem salvar;
  - *Aplicar agora*: salva sem reiniciar;
  - *Salvar e reiniciar*.

  O próprio aparelho classifica cada mudança com um ensaio (dry run) antes de a página oferecer os botões.
- **Reiniciar sem salvar** — um botão no fim da página Configurações reinicia o aparelho e descarta o que a página não salvou; volta a configuração gravada.
- **Versão na tela de login** — a versão do firmware aparece abaixo do nome, antes de qualquer login.
- **Busca de redes Wi-Fi** — escolha a rede numa lista, inclusive de dentro do ponto de acesso de configuração.
- **Gráficos de histórico e exportação CSV no navegador** — a página baixa os arquivos binários brutos de cada dia e ela mesma decodifica, agrupa em baldes (mín/máx/média) e exporta. A hora recente, ainda não selada, vem de `/api/history/open`. O renderizador de gráficos é embutido — sem CDN.
- **API HTTP** — 62 rotas. Cada uma é protegida por uma permissão ou pública por projeto, e o CI confere isso.

## Telemetria e integrações
- **Quatro transportes** — HTTP, HTTPS, MQTT e MQTTS:
  - payload em JSON, CSV ou template customizado;
  - TLS 1.2 (ECDHE com AES-GCM), com o certificado do servidor conferido contra um `/cert.pem` enviado ao aparelho.
- **Lote por quantidade:**
  - `t_int` é o lote mínimo: o rádio fica desligado até essa quantidade de registros esperar (0 = desligado);
  - `t_bat` é o máximo por requisição, um teto que a memória livre pode baixar;
  - o tamanho do lote se adapta a sucessos e falhas, e o tempo de resposta do servidor dita o intervalo até o próximo.
- **Uma segunda linha para alarmes:**
  - eventos de alarme, falha e manutenção seguem numa fila própria (32 por padrão, até 64);
  - cada evento só sai da fila quando o servidor confirma (HTTP 2xx ou ack MQTT);
  - cada um leva o nome da conta que agiu.
- **Integrações** — MQTT Discovery do Home Assistant (opcional), `/metrics` do Prometheus (sessão ou HTTP Basic) e syslog remoto (RFC 5424 sobre UDP).
- **Ganchos de frota:**
  - cabeçalhos de identidade `X-SIMUT-*` nos envios;
  - na imagem `release`, um serviço mDNS `_simut._tcp` com id, versão, imagem e TLS no registro TXT;
  - tokens Bearer e origem CORS configurável.

## Rede e horário
- **Wi-Fi que se reconecta sozinho:**
  - escada de tentativas: 5 s, dobrando até 120 s, depois dormência e uma nova rodada;
  - SSIDs ocultos e checagem de qualidade do sinal;
  - IP estático, dois servidores DNS, servidor NTP próprio ou relógio manual, e porta web configurável.
- **Ponto de acesso de configuração:**
  - chama-se `<nome do aparelho>_SETUP` — `simut_SETUP` de fábrica;
  - WPA2, com chave por aparelho mostrada no console USB e na tela de boot do TFT;
  - portal cativo em `http://192.168.4.1`.

  Só abre quando alguém pede. Uma unidade com a rede fora do ar continua medindo e tentando a rede. Três formas de entrar:
  - Configurações → 8 no painel;
  - o comando `ap` no console (USB, ou Bluetooth no alpha e no Air);
  - segurar o painel por 3 s durante o boot.
- **NTP** — o intervalo entre tentativas cresce de 20 s a 15 min, com fallback para `pool.ntp.org`. Até o NTP sincronizar ou alguém acertar o relógio, um relógio provisório parte do registro mais novo gravado. O painel o marca com `?` entre a data e a hora, e o primeiro acerto depois do boot, pelo NTP ou à mão, corrige os blocos do histórico que o boot começou.

## Armazenamento e histórico
- **Histórico binário compacto (V5)** — codificação delta + âncora a 5,38 bytes/registro, cerca de 116 dias no sistema de arquivos de 1 MB (11 canais a cada minuto, medido em arquivos de bancada em 31/07/2026):
  - blocos de 60 registros, cada um com o seu CRC;
  - o bloco aberto é salvo a cada registro;
  - passando de 86 % de ocupação, o dia mais antigo é apagado.
- **Configuração** — com CRC32, gravada num arquivo temporário e renomeada, com um `.bak` de reserva. Os segredos ficam ofuscados em repouso, não cifrados: o acesso físico à flash está fora do modelo de ameaça ([SECURITY.md §3](../SECURITY.md#3-secret-storage)).
- **Log de eventos** — 2 × 800 registros e 155 códigos de evento:
  - eventos de rotina são gravados nas mudanças de estado, com um pulso por hora e uma contagem do que foi suprimido;
  - registros de segurança, configuração e falha fatal nunca são filtrados.

## Segurança
- **Contas e permissões:**
  - 32 contas, 13 bits de permissão;
  - ninguém concede um bit que não tem;
  - backup, restauração, OTA e instalação de certificado exigem a máscara de admin completa.
- **Senhas:**
  - HMAC-SHA256, 5000 rodadas, salt aleatório de hardware de 8 bytes por usuário e um pepper preso à placa;
  - uma unidade recém-saída de fábrica gera uma senha de admin aleatória de 8 caracteres, imprime uma vez no console USB e exige a troca no primeiro login.
- **Limites contra força bruta:**
  - bloqueio de login de 2 s a 300 s por cliente, com `429` quando todos os slots de bloqueio estão ocupados;
  - limite de taxa por IP nas rotas pesadas;
  - o console Bluetooth tem bloqueio exponencial próprio e para de se anunciar 5 minutos depois do boot.
- **Sessões** — cookie `HttpOnly; SameSite=Strict` (`Secure` no HTTPS), ou token Bearer.
- **Uploads** — path traversal, percent-encoding, bytes de controle e nomes reservados são recusados, e `/config` fica fora do alcance do gerenciador de arquivos.
- **HTTPS opcional** (`release`) — o par de certificado é instalado com `POST /api/tls`. TLS 1.2, ECDHE com AES-GCM.
- **Auditorias** — as auditorias de 16/08/2026, da v2.3.6-beta e de 07/09/2026 estão fechadas. O último achado, V-09 (uma conta restrita podia criar outra com mais bits do que tinha), foi corrigido na v2.7.0, e tanto ele quanto as correções de 07/09 foram verificados no hardware. Veja **[SECURITY.md](../SECURITY.md)**.

## Resiliência e forense
- **Autópsia de travamento a cada boot** — os registradores de scratch do watchdog dizem qual módulo travou em cada núcleo. Desde a v2.7.0, mais três registros também guardam o módulo do Core 1, o heap livre e o uptime no momento do travamento.
- **Disciplina de flash entre os núcleos** — o Core 1 é pausado em toda escrita na flash (medido, não presumido).
- **Disciplina de watchdog** — o watchdog é alimentado em toda operação de arquivo, então clientes HTTP lentos não travam o laço.

## Atualização, backup e recuperação
- **OTA pela página web:**
  - só admin;
  - a imagem é conferida antes de ser gravada (tamanho, CRC do boot2, variante da imagem) e de novo no boot seguinte;
  - Wi-Fi, contas e slots de sensor atravessam a atualização, e o resto do sistema de arquivos é reformatado, por isso a página baixa um backup antes;
  - o aparelho volta em menos de um minuto: 52–56 s na campanha da v2.7.0.
- **Backup e restauração** — o sistema de arquivos inteiro num arquivo, com CRC32 e preso ao chip.
- **[Guia de recuperação](RECOVERY.md)** — caminhos por BOOTSEL, picotool e 1200 bps para todo modo de falha.

## SIMUT Air (experimental)
- **Dois modos, sem display e sem buzzer:**
  - **M0** é o acordado: web, console, Bluetooth e sensores;
  - **M1** é o ciclo: dorme pelo alarme do RTC, acorda, lê, grava o histórico e dorme de novo.
- **O rádio só quando compensa** — ele só liga quando há `t_int` registros esperando. Um wake de leitura leva 9,31 s com um DS18B20, e com intervalo de 60 s o aparelho fica acordado cerca de 13 % do tempo.
- **Pino do carregador** — um pino de detecção do carregador (GP17 por padrão) o mantém acordado enquanto está na tomada.
- **Console completo** — é a única imagem publicada com o console completo.

## Internacionalização
- **3 idiomas de interface** — inglês embutido; português (pt-BR) e espanhol (es-ES) vêm como packs `.lng` no sistema de arquivos. Um aparelho roda o inglês mais o pack instalado.
