#!/usr/bin/env python3
"""Draws, checks and lists the diagrams of the manual: the figures of tipo="diagrama" that chapters declare with an .svg (docs/images/manual/NN-slug.svg).

The diagrams are not drawn by hand and are not images of a device. Each is an SVG that this file draws from the sources at the commit: the pins from
src/simut_config.h and the build profiles (tools/generated/profiles.ini), the sensor probe's pair and addresses from src/sensors, the radio's GPIOs from
src/air/AirConfig.h. What the code does not say is a constant here, with where it comes from: the Pico W's physical pinout (the Raspberry Pi Pico W datasheet,
checked by tools/test_manual_diagramas.py against chapter 2's own table), the 4,7 kOhm of the chapter's pull-ups, and the example pins of the wiring figures.

The SVG names classes (dg-*) and never a colour: the manual's CSS (the CSS block of tools/build_manual.py) styles them with its tokens, and build_manual.py puts the <svg>
in the page itself, so a diagram follows the theme button and the page's fonts, which an <img> could not. The drawing is 720 px wide on a 4 px grid, text 13 px (12 px
in notes) in the reader's own font: its width varies from machine to machine, so the lines are broken here by a width estimate that errs on the wide side (the width of
DejaVu Sans), and the builder refuses a text that overlaps another or leaves the canvas.

  --write      draws the diagrams and writes them into --images (default docs/images/manual); the same code gives the same bytes.
  --check      draws them again from the sources and compares the TEXT with the committed files (not pixels: the drawing is deterministic, and the smoothing of a font
               changes with the version of FreeType): one line for each diagram that differs, is missing, has a literal colour (a fill, stroke or style that is not a class)
               or an id that does not start with its slug (an id in line is global to the page); for each .svg in the folder that no row of FIGURAS names; for each row whose
               block is gone from the chapters or has another file or tipo; for each block of tipo="diagrama" with an .svg that no row names; and for each block whose
               captura= is not the text --list prints. A block of tipo="diagrama" whose file still ends in .png (12-wifi-niveis.png, which tools/lcd_alpha/manual_lcd.py draws,
               and the diagrams not drawn yet) is not this tool's and is left alone. Exit 0 clean, 1 with findings, 2 when it cannot draw.
  --list       the table: the file, the chapter, the function and the captura= text of the figure block in the chapter, tab-separated.
  --preview P  writes P/<slug>.claro.html and P/<slug>.escuro.html for each diagram: the manual's own CSS (imported from build_manual.py, not copied), the figure in
               the same frame as in the page, and data-theme fixed.
  --images     the folder of the diagrams (the controls point it at a scratch copy).
  --capitulos  the folder of the chapters, docs/_manual (the controls point it at a scratch copy).
  --fonte      the root of the tree the numbers are read from, the repository by default (the controls point it at a scratch copy with one number changed).

A number that is not where the code says it is, a premise the figure rests on that the code no longer keeps (the alpha drives its LCD in parallel mode; the Air's charger
reads high with the charger on), two functions on one pin, or a role on a pin that is a sensor slot: the tool says which file and which pattern, or which pin, and exits 2.
It never draws from a guess. The captura= text says what the figure is: "desenhado pelo código (tools/manual_diagramas.py)", what it shows and where its numbers come
from. tools/test_manual_diagramas.py holds the controls of --check, one scratch copy and one fault each.
"""
import argparse
import html
import math
import pathlib
import re
import sys
import unicodedata
from typing import NamedTuple

sys.dont_write_bytecode = True
ROOT = pathlib.Path(__file__).resolve().parents[1]
HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))        # explicit: python3 -I leaves the script's own folder off the path (build_manual.py, which --preview imports)
IMAGENS = ROOT / 'docs' / 'images' / 'manual'
CAPITULOS = ROOT / 'docs' / '_manual'
LARGURA = 720                        # the drawing's width: the manual's CSS shows it whole up to 720 px (.moldura-diagrama svg)
GRADE = 4                            # every coordinate and size is a multiple of it (Ângulo, rule 5)
FLAGS_PADRAO = re.M                  # the flags of every pattern the sources are read with; the controls mangle the matches with the same ones

CONFIG = 'src/simut_config.h'
PERFIS = 'tools/generated/profiles.ini'
AIR_CONFIG = 'src/air/AirConfig.h'
BME_SENSOR = 'src/sensors/BME280SensorDriver.h'
BME_DRIVER = 'src/sensors/BME280Driver.h'
BOOT = 'src/AppManager_Boot.cpp'
RELEASE_MANIFEST = 'tools/release_manifest.py'
COMANDOS = 'src/CommandParser.cpp'
MENU = 'src/display/SettingsMenu.h'
WORKFLOW_OTA = '.github/workflows/release-ota.yml'
TELEMETRIA = 'src/TelemetryManager.cpp'
HA_DISCOVERY = 'src/HaDiscovery.h'
WEB_CORE = 'src/WebManager_Core.cpp'
WEB_METRICS = 'src/WebManager_Metrics.cpp'
WEB_AUTH = 'src/WebManager_Auth.cpp'
WEB_H = 'src/WebManager.h'
OTA_STAGE_H = 'src/ota/firmware_stage.h'
OTA_VALID_H = 'src/ota/validation.h'
NET_DEFS = 'src/SystemDefs_Network.h'
SYSLOG_H = 'src/SyslogManager.h'
SYSLOG_CPP = 'src/SyslogManager.cpp'
LOOP = 'src/AppManager_Loop.cpp'
NETWORK = 'src/NetworkManager.cpp'
RECORDS = 'src/SystemDefs_Records.h'
WEB_OTA = 'src/WebManager_Ota.cpp'
WEB_API = 'src/WebManager_Api.cpp'
STORAGE_H = 'src/StorageManager.h'
OTA_LAYOUT = 'src/ota/ota_layout.h'
LOG_H = 'src/LogManager.h'
DISPLAY_H = 'src/DisplayManager.h'
NET_CYW43 = 'src/net/NetworkManager_Cyw43.cpp'
NET_MGR = 'src/NetworkManager.cpp'
NET_MGR_H = 'src/NetworkManager.h'
APP_CORE = 'src/AppManager_Core.cpp'
LOGCODES = 'tools/logcodes.tsv'
LOGPOLICY_H = 'src/LogPolicy.h'
LOGPOLICY_CPP = 'src/LogPolicy.cpp'
WEBUI = 'WebUI.h'
OTA_STAGING = 'src/ota/staging.cpp'
OTA_APPLIER = 'src/ota/applier.cpp'
AIR_CPP = 'src/AppManager_Air.cpp'
APP_H = 'src/AppManager.h'
LOG_MANAGER = 'src/LogManager.cpp'
NET_LINK = 'src/net/NetLink.h'


class Falta(Exception):
    """The tool cannot draw from these sources: a pattern it reads a number with is not where the code says it is, or the code no longer says what a figure rests on.
    The message names the file and the pattern (or the pin); main( ) turns it into exit 2."""


class Figura(NamedTuple):
    bloco: str      # the id of the figure block in the chapter: ::: {.figura #fig-02-ds18b20 ...}
    arquivo: str    # docs/images/manual/<arquivo>
    capitulo: str   # the chapter of docs/_manual that holds the block
    desenho: str    # the name, in this file, of the function that draws it: def desenho_x(num, slug) -> the SVG text
    nota: str       # what the figure shows and where its numbers come from: the middle of the captura= text
    tipo: str = 'diagrama'   # the tipo= of the block


# One row per diagram this tool draws, in the order of the chapters. The diagrams of the manual that are not here yet keep their .png in the chapter and are left alone by
# --check; a row is added with its drawing function, its block's arquivo= moved to .svg and its captura= made the text --list prints.
FIGURAS = (
    Figura('fig-01-arquitetura', '01-arquitetura.svg', 'cap01-o-simut.md', 'desenho_blocos',
           'o aparelho em blocos: os sensores nos GPIO de sensor, o RP2040 com o Core 0 e o Core 1, a flash de 2 MB em programa e sistema de arquivos com as pastas e o log de eventos, e as saídas, com o console '
           'pelo cabo USB e pelo Bluetooth; os tipos de sensor vêm de src/SystemDefs_Records.h, os tamanhos da flash de src/ota/ota_layout.h (conferidos com o platformio.ini), as pastas de src/StorageManager.h, '
           'o log de src/LogManager.h, o uso do Core 1 de src/DisplayManager.h e dos perfis, as imagens com Bluetooth de SIMUT_BLUETOOTH, e as saídas apoiam-se nas leituras das figuras de cada mecanismo'),
    Figura('fig-02-pinos-release', '02-pinos-release.svg', 'cap02-hardware.md', 'desenho_pinos_release',
           'vista de cima do Pico W com os 40 pinos numerados e os da release (SPI0, TFT, toque e cigarra) lidos de src/simut_config.h e do perfil pico_w_release; '
           'a pinagem física é a do datasheet do Pico W e os quatro GPIOs do rádio vêm de src/air/AirConfig.h'),
    Figura('fig-02-pinos-alpha', '02-pinos-alpha.svg', 'cap02-hardware.md', 'desenho_pinos_alpha',
           'vista de cima do Pico W com os 40 pinos numerados e os da alpha (LCD em paralelo, de GP16 a GP21, e cigarra) lidos de src/simut_config.h e do perfil pico_w_alpha; '
           'a pinagem física é a do datasheet do Pico W e os quatro GPIOs do rádio vêm de src/air/AirConfig.h'),
    Figura('fig-02-pinos-air', '02-pinos-air.svg', 'cap02-hardware.md', 'desenho_pinos_air',
           'vista de cima do Pico W com os 40 pinos numerados e os dois do Air (energia dos sensores e carregador) lidos de src/simut_config.h e do perfil pico_w_air; '
           'a pinagem física é a do datasheet do Pico W e os quatro GPIOs do rádio vêm de src/air/AirConfig.h'),
    Figura('fig-02-air-energia', '02-air-energia.svg', 'cap02-hardware.md', 'desenho_air_energia',
           'esquema do GP16 acionando a chave do lado positivo que alimenta os sensores e do divisor que leva os 5 V do carregador ao GP17; os pinos e a polaridade vêm de '
           'src/simut_config.h, o pull-down e o GP16 alto no boot de src/AppManager_Boot.cpp, e o resto é o texto do capítulo'),
    Figura('fig-02-ds18b20', '02-ds18b20.svg', 'cap02-hardware.md', 'desenho_ds18b20',
           'ligação de uma sonda DS18B20 de três fios ao Pico W, com o resistor de 4,7 kΩ entre o dado e o 3V3; os pinos físicos são os do datasheet do Pico W, '
           'o GP2 é um exemplo e vale qualquer GPIO de sensor (os limites vêm de MAX_SENSORS, em src/simut_config.h)'),
    Figura('fig-02-dht22', '02-dht22.svg', 'cap02-hardware.md', 'desenho_dht22',
           'ligação de um módulo DHT22 de três terminais ao Pico W, com a nota do pull-up; os pinos físicos são os do datasheet do Pico W, '
           'o GP3 é um exemplo e vale qualquer GPIO de sensor (os limites vêm de MAX_SENSORS, em src/simut_config.h)'),
    Figura('fig-02-bmx280', '02-bmx280.svg', 'cap02-hardware.md', 'desenho_bmx280',
           'ligação de um módulo BME280 de seis pinos ao Pico W, no par de pinos que a busca de sondas tenta (src/sensors/BME280SensorDriver.h), '
           'com o endereço do driver (src/sensors/BME280Driver.h) e os pinos físicos do datasheet do Pico W'),
    Figura('fig-03-arquivos', '03-arquivos.svg', 'cap03-instalar.md', 'desenho_arquivos',
           'os arquivos de uma versão (quatro imagens, em colunas neutras, dois pacotes de idioma, o manifesto e as somas) e o caminho de cada um até o aparelho, com o acento só nos '
           'caminhos; as imagens vêm de ENVS em tools/release_manifest.py, os pacotes de data/lang e os nomes do manifesto e das somas do passo Checksums de .github/workflows/release-ota.yml'),
    Figura('fig-04-fluxo', '04-fluxo.svg', 'cap04-primeiro-boot.md', 'desenho_primeiro_boot',
           'fluxograma do primeiro boot, da senha do admin ao aparelho pronto, com os dois caminhos para pôr o aparelho na rede; o nome e o endereço do ponto de acesso, o número do '
           'item do menu (a posição de MENU_SETUP_AP em MENU_ORDER) e as palavras dos comandos vêm de src/StorageManager.cpp, src/net/NetworkManager_Cyw43.cpp, src/WebAddress.h, '
           'src/display/SettingsMenu.h, src/CommandParser.cpp e src/AppManager_Commands.cpp'),
    Figura('fig-09-escada', '09-escada.svg', 'cap09-rede.md', 'desenho_escada',
           'a escada de reconexão numa linha: a queda, cinco ciclos de espera e tentativa com a espera dobrando, as três esperas de dormência e o recomeço, até a rede voltar, sem abrir o AP de '
           'configuração; a primeira espera, o teto, os ciclos, a dormência, o prazo da busca e da tentativa vêm de src/SystemDefs_Network.h, src/NetworkManager.h e '
           'src/net/NetworkManager_Cyw43.cpp, e o que o desenho afirma da escada (dobra, dormência depois dos ciclos, recomeço, zera numa conexão, não pede o AP) é um trecho desses arquivos e de '
           'src/NetworkManager.cpp que o código tem de manter'),
    Figura('fig-10-correcao', '10-correcao.svg', 'cap10-data-hora.md', 'desenho_correcao',
           'o que o primeiro acerto do relógio faz com as medições gravadas sob o relógio provisório: o aparelho desligado por 2 h, o boot com o relógio provisório, os blocos carimbados 2 h atrasados, o acerto '
           'e o deslocamento de +7200 s, com os cinco eventos marcados na linha; o número e o nome de cada evento vêm de tools/logcodes.tsv, a diferença mínima para corrigir de src/NetworkManager.cpp, e o que o '
           'desenho afirma da correção é um trecho desse arquivo e de src/AppManager_Core.cpp que o código tem de manter; as 2 h e os 3 blocos são o exemplo da figura'),
    Figura('fig-16-transicao', '16-transicao.svg', 'cap16-log-eventos.md', 'desenho_transicao',
           'três horas da família Telemetria no log de eventos: o console vê todos os eventos, e a flash só as transições (o primeiro envio depois do boot, a primeira falha, a primeira volta) e um registro por hora, '
           'com a contagem dos suprimidos a cada hora; o que a política grava e quando vem de um espelho de src/LogPolicy.cpp, que o teste confere com o código compilado, com a cadência de src/LogPolicy.h e '
           'os nomes dos eventos de tools/logcodes.tsv; o cenário (um envio a cada 10 min e o coletor fora do ar entre os minutos 65 e 95) é o exemplo da figura'),
    Figura('fig-17-ota-linha-tempo', '17-ota-linha-tempo.svg', 'cap17-atualizacao-backup.md', 'desenho_ota_linha_tempo',
           'as etapas de uma atualização no Pico W, numa linha em escala (o backup, o envio e a conferência da imagem, a aplicação, o boot da imagem nova), e o que um corte de energia custa em cada uma; '
           'os tempos do envio e da volta da interface web são os medidos que o capítulo traz (v2.7.0, 22/09/2026), os da aplicação e do boot são as estimativas da janela de confirmação de WebUI.h, e que o envio '
           'só escreve na área de preparo e que só a aplicação reescreve o programa são trechos de src/ota/staging.cpp e src/ota/applier.cpp que o código tem de manter'),
    Figura('fig-19-despertar', '19-despertar.svg', 'cap19-air.md', 'desenho_despertar',
           'dois despertares do Air, cada um em escala, num intervalo de 60 s: o de leitura (boot, aquecimento de 0,4 s, amostragem e a decisão que grava o histórico, a 9,31 s do boot) e o de telemetria, que '
           'repete as larguras até a decisão e acrescenta a conexão e o envio, até cerca de 12,7 s; GP16 alto durante todo o despertar, a faixa do rádio ligado só no de telemetria e o sono entre eles fora de '
           'escala; o aquecimento vem de src/AppManager_Air.cpp; a ordem das fases, o rádio que a leitura nunca liga, o Wi-Fi em paralelo na amostragem, a decisão que grava o histórico antes de escolher e o fim do '
           'GP16 e do rádio no sono são trechos de src/AppManager.h, src/AppManager_Boot.cpp e src/AppManager_Air.cpp que o código tem de manter; o boot de 2,47 s e os totais de 9,31 s e 12,7 s são os medidos '
           'na bancada em 08/09/2026, conferidos com o capítulo 19 e com a análise de energia'),
    Figura('fig-15-caminho', '15-caminho.svg', 'cap15-historico.md', 'desenho_caminho_historico',
           'o caminho de um registro: o bloco aberto na RAM, a cópia em .wip, a selagem no arquivo do dia e a fila de arquivos diários, com o limite de ocupação que apaga o mais antigo; '
           'os registros por bloco vêm de src/HistoryV5.h, o limite de src/StorageManager.cpp e os nomes de src/StorageManager.h e src/HistoryV5.h'),
    Figura('fig-20-integracao', '20-integracao.svg', 'cap20-integracao-visao-geral.md', 'desenho_integracao',
           'o aparelho e o que ele troca com os outros sistemas, nos dois sentidos: o que ele abre (telemetria, linha de alarmes, descoberta do Home Assistant, syslog), o que o servidor abre na porta da '
           'interface web (a API REST, o /metrics) e o anúncio mDNS, que não abre conexão; o protocolo e a porta do syslog vêm de src/SyslogManager.h e src/SystemDefs_Records.h, o nome do serviço de '
           'src/NetworkManager.cpp, as imagens com o anúncio de SIMUT_MDNS em src/simut_config.h e nos perfis, a rota do /metrics de src/WebManager_Core.cpp, e as afirmações repetidas das figuras de '
           'cada mecanismo (alarmes, Home Assistant, Prometheus) apoiam-se nas mesmas leituras e premissas delas'),
    Figura('fig-21-cursor', '21-cursor.svg', 'cap21-telemetria.md', 'desenho_cursor',
           'a fila de registros do histórico com o cursor entre os confirmados e os pendentes, o lote, o coletor, a confirmação 2xx que move o cursor e a falha que o deixa parado; '
           'a faixa de códigos que confirma vem de src/TelemetryManager.cpp e a espera depois de uma falha de BACKOFF_MIN_MS e BACKOFF_MAX_MS em src/TelemetryManager.h'),
    Figura('fig-22-fluxo', '22-fluxo.svg', 'cap22-linha-alarmes.md', 'desenho_linha_alarmes',
           'a fila de alarmes na RAM e as duas raias, HTTP e MQTT, com a confirmação que tira os registros da fila; o reenvio, a espera da resposta e as pontas dos dois tópicos vêm de '
           'src/TelemetryManager.h, src/SystemDefs_Network.h e src/TelemetryManager.cpp'),
    Figura('fig-23-fluxo', '23-fluxo.svg', 'cap23-home-assistant.md', 'desenho_home_assistant',
           'o caminho das mensagens entre o aparelho, o broker e o Home Assistant: a descoberta e o estado retidos, os valores a cada envio, a última vontade que o broker publica se o aparelho some '
           'e o que o Home Assistant lê de cada uma; o prefixo e a forma do tópico de descoberta vêm de src/HaDiscovery.h, o tópico de dados de src/StorageManager.cpp, o de estado, a última vontade '
           'e as chaves do registro de src/TelemetryManager.cpp; o nó, o broker e o identificador do sensor são os exemplos do capítulo'),
    Figura('fig-24-coleta', '24-coleta.svg', 'cap24-prometheus.md', 'desenho_coleta',
           'uma leitura do /metrics pelo Prometheus: o pedido com a conta, as três perguntas do aparelho na ordem em que o código as faz (o endereço está bloqueado, a senha confere, a conta tem Painel), '
           'as quatro respostas e a escada do bloqueio que uma senha errada sobe; a rota, o esquema da credencial, os códigos e a ordem das perguntas vêm de src/WebManager_Core.cpp e '
           'src/WebManager_Metrics.cpp, as rodadas da senha, o tempo medido de uma leitura (registrado no comentário de PASSWORD_HMAC_ROUNDS) e a escada de src/SystemDefs_Network.h; '
           'o endereço, a conta e o intervalo de 60 s são os exemplos do capítulo'),
    Figura('fig-25-caminho', '25-caminho.svg', 'cap25-syslog.md', 'desenho_syslog',
           'o caminho de uma linha de syslog, do evento ao coletor, e os pontos onde ela se perde, com o log de eventos na flash como o outro caminho do mesmo evento; a profundidade da fila, que é também '
           'o que uma passada manda, vem de SLOTS em src/SyslogManager.h e a porta de SYSLOG_DEFAULT_PORT em src/SystemDefs_Records.h, e cada coisa que o desenho afirma da fila, do laço, do boot e do '
           'reinício é um trecho de src/SyslogManager.cpp, src/AppManager_Loop.cpp, src/AppManager_Boot.cpp, src/LogManager.cpp ou src/net/NetLink.h que o código tem de manter; '
           'o endereço do coletor, o nome do nível e os coletores são os exemplos do capítulo'),
    Figura('fig-26-entrada', '26-entrada.svg', 'cap26-api-rest.md', 'desenho_entrada',
           'a entrada na API em dois pedidos, numa sequência entre o cliente e o aparelho: o código de uso único que GET /api/login_init devolve (vale 60 s e serve a uma tentativa), o SHA-256 da senha que o '
           'cliente calcula, o POST /api/login com user, pass e nonce, a resposta com o Set-Cookie SIMUTSESS e o corpo com ok e redirect, os pedidos seguintes com o cookie ou o Authorization: Bearer e o '
           'GET /logout, que encerra só a sessão do token (204 com Bearer, 302 para /login com cookie); as rotas, os campos, as chaves das respostas, o nome do cookie e do esquema, o tempo de vida do código e '
           'os códigos do logout vêm de src/WebManager_Core.cpp, src/WebManager_Auth.cpp e src/WebManager.h, e o que o desenho afirma do código gasto por uma tentativa, da senha em SHA-256 calculada pela '
           'página e do logout que só encerra a sessão do token são trechos de src/WebManager_Auth.cpp e de WebUI.h que o código tem de manter; o endereço e o começo do código são os exemplos do capítulo'),
    Figura('fig-26-ota', '26-ota.svg', 'cap26-api-rest.md', 'desenho_ota_api',
           'a atualização do firmware pela API, numa sequência entre o cliente e o aparelho, nos quatro passos do capítulo: baixar o backup e conferir o CRC, enviar a imagem com op=stage e commit=1 (a resposta '
           'com st 5, v 0 e committed 1), aplicar com POST /api/ota/apply (a resposta 202, e o aparelho aplica e reinicia) e confirmar (repetir GET /api/login_init a cada 3 s até responder, entrar de novo e ler '
           'a versão em /api/status, que é o único comprovante), com a restauração do backup depois (op=validate e op=apply); as rotas, as operações e a chave da versão vêm de src/WebManager_Core.cpp, '
           'src/WebManager_Ota.cpp e src/WebManager_Api.cpp, os valores da resposta de src/ota/firmware_stage.h e src/ota/validation.h, o intervalo de WebUI.h, e que só uma imagem preparada e válida com commit '
           'fica pronta para aplicar, que a resposta do apply sai antes de o aparelho reiniciar e que a restauração só aceita o backup validado por último são trechos desses arquivos que o código tem de manter; '
           'o endereço é o exemplo do capítulo'),
    Figura('fig-27-arquitetura', '27-arquitetura.svg', 'cap27-frota.md', 'desenho_arquitetura',
           'os caminhos de uma frota: vários aparelhos numa rede local, a telemetria e a linha de alarmes que eles empurram ao coletor com os cabeçalhos de identidade, o que o Prometheus e o gestor de frota '
           'perguntam a cada um, o anúncio mDNS à rede local e o manifest.json da release no GitHub que o gestor lê; os nomes dos cabeçalhos e a premissa de que os dois POSTs os levam vêm de '
           'src/TelemetryManager.cpp, o esquema e o nome do cookie da sessão de src/WebManager_Auth.cpp, a rota e o esquema do /metrics de src/WebManager_Core.cpp e src/WebManager_Metrics.cpp, o serviço '
           'e as imagens do anúncio de src/NetworkManager.cpp e SIMUT_MDNS, o nome do manifesto do passo Checksums de .github/workflows/release-ota.yml'),
    Figura('fig-27-ondas', '27-ondas.svg', 'cap27-frota.md', 'desenho_ondas',
           'a atualização da frota em três ondas, com a barreira de conferência entre uma onda e a seguinte, e a sequência que cada aparelho percorre em cada onda (backup, envio, aplicação, confirmação, '
           'restauração, idioma) com a rota de cada passo; as rotas, o método de cada uma, as operações do restore, o sinal de confirmar o envio, a chave da versão e a pasta dos idiomas vêm de '
           'src/WebManager_Core.cpp, src/WebManager_Ota.cpp, src/WebManager_Api.cpp e src/StorageManager.h, e as ondas e o que cada uma leva são as frases do capítulo'),
)


def slug_de(arquivo):
    return arquivo[:-len('.svg')]


def captura(f):
    """The captura= text of the figure's block in the chapter."""
    return f'desenhado pelo código (tools/manual_diagramas.py): {f.nota}; refazer com python3 tools/manual_diagramas.py --write'


def tabela_ok():
    """A problem with the table itself, or None. A table that lets two figures share a block or a file, a captura= that breaks the attribute or a row with no drawing
    function would be found by no figure."""
    for campo in ('bloco', 'arquivo'):
        valores = [getattr(f, campo) for f in FIGURAS]
        for v in valores:
            if valores.count(v) > 1:
                return f'FIGURAS names the {campo} {v!r} twice'
    for f in FIGURAS:
        if not re.fullmatch(r'\d\d-[a-z0-9]+(-[a-z0-9]+)*\.svg', f.arquivo):
            return f'FIGURAS row {f.bloco!r}: the file {f.arquivo!r} is not NN-slug.svg'
        if f.tipo != 'diagrama':
            return f'FIGURAS row {f.bloco!r}: the kind {f.tipo!r} is not diagrama'
        if not callable(globals().get(f.desenho)):
            return f'FIGURAS row {f.bloco!r}: there is no function {f.desenho!r} in this file'
        if re.search(r'["}\n]', captura(f)):
            return f'FIGURAS row {f.bloco!r}: the captura= text has a double quote, a closing brace or a line break (it breaks the attribute)'
    return None


# ---- the numbers, read from the sources ----------------------------------------------------------------------------------------------------------------------
class Fonte:
    """The files the numbers are read from, under `raiz`. Every pattern a number is read with is recorded in `padroes` and every file in `lidos`: the controls copy
    those files into a scratch tree and mangle each pattern's matches there, so a reader added later is held to the same control without being listed anywhere."""

    def __init__(self, raiz=ROOT):
        self.raiz = pathlib.Path(raiz)
        self._texto = {}
        self.lidos = []
        self.padroes = []
        self.listados = []

    def ler(self, rel):
        if rel not in self._texto:
            try:
                self._texto[rel] = (self.raiz / rel).read_bytes().decode('utf-8')
            except (OSError, UnicodeDecodeError) as e:
                raise Falta(f'{rel}: cannot be read ({e}); the diagrams read their numbers from it') from None
            self.lidos.append(rel)
        return self._texto[rel]

    def achar(self, rel, padrao, texto=None):
        """The match of `padrao` in the file (or in `texto`, a part of it), or Falta naming both."""
        t = self.ler(rel) if texto is None else texto
        if (rel, padrao) not in self.padroes:
            self.padroes.append((rel, padrao))
        m = re.search(padrao, t, FLAGS_PADRAO)
        if m is None:
            raise Falta(f'{rel}: the pattern {padrao} was not found; the diagrams read their numbers there, so the code and the diagrams have to be looked at together')
        return m

    def listar(self, rel, glob):
        """The names of the files of the folder `rel` that match `glob`, sorted; Falta when there is none: the diagrams name what the folder holds. The controls copy those
        folders (Fonte.listados) into a scratch tree, as they copy the files read (Fonte.lidos)."""
        if (rel, glob) not in self.listados:
            self.listados.append((rel, glob))
        try:
            nomes = sorted(p.name for p in (self.raiz / rel).glob(glob) if p.is_file())
        except OSError as e:
            raise Falta(f'{rel}: cannot be listed ({e}); the diagrams name the files it holds') from None
        if not nomes:
            raise Falta(f'{rel}: no file matches {glob}; the diagrams name the files it holds')
        return nomes

    def define(self, rel, nome):
        """The number of `#define NOME n` in the file."""
        return int(self.achar(rel, rf'^#define\s+{nome}\s+(\d+)\b').group(1))

    def secao(self, env):
        """The text of [env:`env`] in the build profiles."""
        t = self.ler(PERFIS)
        m = self.achar(PERFIS, rf'^\[env:{env}\][ \t]*$')
        fim = re.search(r'^\[', t[m.end():], FLAGS_PADRAO)
        return t[m.end():m.end() + fim.start()] if fim else t[m.end():]

    def flags(self, env):
        """{name: value} of the -D flags of [env:`env`]; a bare -DNAME is '1'. The profile's flag wins over simut_config.h's default, as the compiler's -D does."""
        return {k: v or '1' for k, v in re.findall(r'^[ \t]+-D(\w+)(?:=(\S+))?[ \t]*$', self.secao(env), FLAGS_PADRAO)}

    def exigir(self, rel, padrao, porque, texto=None):
        """Falta unless the file (or `texto`, a part of it) still matches `padrao`: a premise the figure rests on, with `porque` in the message to say which (achar( )'s message names only the
        pattern)."""
        if (rel, padrao) not in self.padroes:
            self.padroes.append((rel, padrao))
        if re.search(padrao, self.ler(rel) if texto is None else texto, FLAGS_PADRAO) is None:
            raise Falta(f'{rel}: no longer matches the pattern {padrao}, and {porque}')

    def exigir_flag(self, env, nome, valor, porque):
        """Falta unless [env:`env`] builds with -D`nome`=`valor`: a premise the figure rests on."""
        padrao = rf'^[ \t]+-D{nome}={valor}[ \t]*$'
        secao = self.secao(env)
        if (PERFIS, padrao) not in self.padroes:
            self.padroes.append((PERFIS, padrao))
        if re.search(padrao, secao, FLAGS_PADRAO) is None:
            raise Falta(f'{PERFIS}: [env:{env}] no longer matches the pattern {padrao}, and {porque}')


# The Raspberry Pi Pico W's pinout, from its datasheet (the Pico W pinout figure and its pin table): pin 1 is GP0 at the USB end of the left row and the pins run down the
# left (1 to 20) and up the right (21 to 40) as seen from above, so 40 is VBUS beside the USB connector. Pin 33 is AGND, the analogue ground. GP23, GP24, GP25 and GP29 have
# no pin: the Pico W uses them for the radio. tools/test_manual_diagramas.py compares this table with the one in docs/_manual/cap02-hardware.md, which was written apart.
PINAGEM = {1: 'GP0', 2: 'GP1', 3: 'GND', 4: 'GP2', 5: 'GP3', 6: 'GP4', 7: 'GP5', 8: 'GND', 9: 'GP6', 10: 'GP7', 11: 'GP8', 12: 'GP9', 13: 'GND', 14: 'GP10',
           15: 'GP11', 16: 'GP12', 17: 'GP13', 18: 'GND', 19: 'GP14', 20: 'GP15', 21: 'GP16', 22: 'GP17', 23: 'GND', 24: 'GP18', 25: 'GP19', 26: 'GP20',
           27: 'GP21', 28: 'GND', 29: 'GP22', 30: 'RUN', 31: 'GP26', 32: 'GP27', 33: 'AGND', 34: 'GP28', 35: 'ADC_VREF', 36: '3V3', 37: '3V3_EN',
           38: 'GND', 39: 'VSYS', 40: 'VBUS'}
PINO_DO_GPIO = {int(nome[2:]): pino for pino, nome in PINAGEM.items() if nome.startswith('GP')}
EXTERNOS = tuple(sorted(g for g in PINO_DO_GPIO if g >= 16))     # the GPIOs beyond GP15 that reach the edge: GP16 to GP22 and GP26 to GP28
PINO_3V3, PINO_GND = 36, 38          # the pins the wiring figures name: 3V3 (OUT), and one of the seven GND pins, on the same row of the board
RESISTOR = '4,7 kΩ'                  # chapter 2 (lines 164, 166, 181 and 267) gives it for the DS18B20's data line and the I2C's two; the firmware does not know the resistor
EXEMPLO_DS18B20, EXEMPLO_DHT22 = 2, 3   # the example GPIOs of the wiring figures, those chapter 6 shows (cap06-sensores.md:59, a DS18B20 on GP2 and a DHT22 on GP3): any sensor GPIO would do
# The examples of the Home Assistant figure, those chapter 23 writes (cap23-home-assistant.md: the node in the table of the device's data, the broker and its port in the capture of the
# Telemetria page, the sensor's id in the table of the entities): the node is the Client ID of a device with the MAC's last six digits 0a1b2c. The firmware knows none of them;
# tools/test_manual_diagramas.py checks that the chapter still says them.
NO_EXEMPLO, BROKER_EXEMPLO, PORTA_EXEMPLO, HWID_EXEMPLO = 'simut_0a1b2c', 'broker.exemplo.com.br', 1883, 'STM0001'
# The examples of the Prometheus figure, those chapter 24 writes (cap24-prometheus.md: the address in the table of addresses, the account in the steps that create it, the interval in the
# advice and in the configuration's example, the permission's name where the chapter pairs it with PERM_DASHBOARD). The firmware knows none of them; tools/test_manual_diagramas.py
# checks that the chapter still says them.
IP_EXEMPLO, CONTA_EXEMPLO, INTERVALO_PROMETHEUS, PERMISSAO_PAINEL = '192.0.2.10', 'metricas', 60, 'Painel'
# The examples of the syslog figure, those chapter 25 writes (cap25-syslog.md: the collector's address in the configuration's example, the name of the level's field, the two collectors it
# sets up). tools/test_manual_diagramas.py checks that the chapter still says them.
IP_COLETOR_EXEMPLO, NIVEL_MINIMO, COLETORES_SYSLOG = '192.0.2.10', 'Nível Mínimo', ('rsyslog', 'syslog-ng')
# The names the integration figure gives its peers, chapter 20's own words (the table of what the device offers: the management server and Prometheus; the table of ports: the local announcement; the
# collector of syslog). tools/test_manual_diagramas.py checks that the chapter still says them.
PAR_GESTAO, PAR_PROMETHEUS, PAR_REDE_LOCAL = 'servidor de gestão', 'Prometheus', 'rede local'
PAR_GESTOR = 'gestor de frota'                  # chapter 27's name for the program that asks every device (the 27 figures' peer)
# The waves of a fleet's update, chapter 27's own sentences ("Em ondas": the three numbered paragraphs, each name and what the paragraph says the wave holds), and the barrier between two of them
# as the key to the figure words it: what the paragraphs say is checked between waves (the version, the telemetry, the alarm line) and for how long (a period). The firmware knows none of it;
# tools/test_manual_diagramas.py checks that the chapter still says them.
ONDAS = (('Onda 0', 'um aparelho de bancada de cada imagem em uso'), ('Onda 1', 'poucos aparelhos em uso real'),
         ('Onda 2', 'o resto, em grupos pequenos, um aparelho de cada vez dentro de cada grupo'))
BARREIRA_ONDAS = 'conferir versão, telemetria e alarmes por um período'
# The example of the time-correction figure: the device off for 2 h (so the provisional clock, which starts at the last record, is 2 h behind: the correction is +7200 s) and the three
# blocks it wrote before the first set of the clock. The firmware knows neither; the diagram says it is an example.
HORAS_DESLIGADO, BLOCOS_EXEMPLO = 2, 3
# The scenario of the log-policy figure: three hours of a device that sends telemetry every 10 minutes, the collector down between minutes 65 and 95. The firmware knows none of it; the figure
# says it is an example. What the policy does with it is the code's (the mirror below, held to the code by the test).
DURACAO_H, ENVIOS_CADA_MIN, FORA_DO_AR_MIN = 3, 10, (65, 95)
# The measured times of an update, chapter 17's table of "Tempos medidos" (cap17-atualizacao-backup.md, the first two rows): the send and the check of the image, 34,9 to 35,9 s, and the return of the
# web interface after the apply, 52 to 56 s, both on the bench with v2.7.0 on 2026-09-22 (six updates). Measurements, not code: the test checks that the chapter still says them.
ENVIO_MEDIDO_S, VOLTA_MEDIDA_S, MEDIDO_EM = (34.9, 35.9), (52, 56), 'v2.7.0, 22/09/2026'
# The Air's wake, measured on the bench on 2026-09-08 (docs/analysis/SIMUT_AIR_PLANO_ENERGIA.md section 10.1; chapter 19's table "Tempos medidos" gives the second and the third): the setup until the
# first phase, 2,47 s (the end of setup( ), where the WARMUP phase begins: section 9.1 of the same analysis, `phase=WARMUP`); the wake until the decision, 9,31 s; and a wake with telemetry, about
# 12,7 s. The figure's interval of 60 s is the one the chapter measured with (1 min). Measurements, not code: the test checks that the chapter and the analysis still say them.
AIR_SETUP_S, AIR_ATE_DECIDIR_S, AIR_DESPERTAR_TELEMETRIA_S, AIR_INTERVALO_S = 2.47, 9.31, 12.7, 60
ESCALA_DESPERTAR = 24      # px a second in the wake figure: a sixth of a second a grid step
NONCE_EXEMPLO = '9f2c…'    # the start of chapter 26's example code (32 hexadecimal digits): the figure of the entry shows it cut

IMAGEM_DO_PERFIL = {'release': 'pico_w_release', 'alpha': 'pico_w_alpha', 'air': 'pico_w_air'}


class Numeros:
    """What the diagrams draw, read from `fonte` when first asked and kept: the drawing functions ask for names, and each answer is a pattern in a file."""

    def __init__(self, fonte):
        self.f = fonte

    def macro(self, nome, imagem):
        """`nome` as the build of `imagem` has it: simut_config.h's #define, overridden by a -D of the profile when the profile has one."""
        valor = self.f.define(CONFIG, nome)
        sobre = self.f.flags(IMAGEM_DO_PERFIL[imagem]).get(nome)
        if sobre is not None:
            try:
                return int(sobre)
            except ValueError:
                raise Falta(f'{PERFIS}: [env:{IMAGEM_DO_PERFIL[imagem]}] sets -D{nome}={sobre}, which is not a number') from None
        return valor

    def slots(self, imagem='release'):
        """How many sensor slots the image has: GP0 to GP(n-1)."""
        return self.macro('MAX_SENSORS', imagem)

    def exemplo(self, gpio):
        """`gpio`, an example pin of a wiring figure, once it is known to be a sensor GPIO (GP0 to GP(MAX_SENSORS - 1))."""
        if gpio >= self.slots():
            raise Falta(f'{CONFIG}: MAX_SENSORS is {self.slots()}, so GP{gpio} is not a sensor GPIO any more, and a figure uses it as the example of one')
        return gpio

    def spi0(self):
        """The three pins of the SPI0 bus the TFT and the touch share, as section 2 of simut_config.h names them."""
        m = self.f.achar(CONFIG, r'SPI bus: MOSI=GP(\d+), MISO=GP(\d+), SCK=GP(\d+)')
        return {'MOSI': int(m.group(1)), 'MISO': int(m.group(2)), 'SCK': int(m.group(3))}

    def radio(self):
        """The four GPIOs the Pico W keeps for its radio, as AirConfig.h's airPinValid( ) refuses them."""
        m = self.f.achar(AIR_CONFIG, r'case (\d+): case (\d+): case (\d+): case (\d+): return false;\s*/\* CYW43 on the Pico W \*/')
        return tuple(int(x) for x in m.groups())

    def bme(self):
        """The pair of GPIOs the sensor search probes for a BME280, and the two addresses the driver tries, the primary with SDO to GND."""
        m = self.f.achar(BME_SENSOR, r'BMx280PIO_RP2040 probe\((\d+), (\d+), BME280_ADDR_PRIMARY\)')
        sda, scl = int(m.group(1)), int(m.group(2))
        for g in (sda, scl):
            self.exemplo(g)
        m = self.f.achar(BME_DRIVER, r'\(addr == 0x([0-9A-Fa-f]{2})\) \? \(uint8_t\)0x([0-9A-Fa-f]{2}) : \(uint8_t\)0x([0-9A-Fa-f]{2})')
        primario, secundario, volta = m.groups()
        c = self.f.achar(BME_DRIVER, r'Tries primary address \(0x([0-9A-Fa-f]{2}), SDO→GND\) first, then secondary \(0x([0-9A-Fa-f]{2})\)')
        if primario != volta or (c.group(1), c.group(2)) != (primario, secundario):
            raise Falta(f'{BME_DRIVER}: the code (0x{primario}, 0x{secundario}, 0x{volta}) and its comment (0x{c.group(1)}, 0x{c.group(2)}) disagree on the BME280 addresses')
        return {'sda': sda, 'scl': scl, 'sdo_gnd': f'0x{primario}', 'sdo_3v3': f'0x{secundario}'}

    def papeis(self, imagem):
        """{gpio: (label, device)} of the pins beyond the sensor slots that `imagem` uses, as the sources say, in the order of the physical pins. Two functions on one pin,
        or a function on a pin that is a sensor slot or is not on the edge, is Falta: the map would draw a wiring that cannot be built."""
        env, slots, p = IMAGEM_DO_PERFIL[imagem], self.slots(imagem), {}

        def poe(gpio, rotulo, dispositivo=''):
            if gpio not in EXTERNOS:
                raise Falta(f'{CONFIG}: in the {imagem} image {rotulo} is on GP{gpio}, which is {"a sensor slot pin" if gpio < slots else "not one of GP16 to GP22 and GP26 to GP28"}; '
                            f'the map of pins draws those as functions')
            if gpio in p:
                raise Falta(f'{CONFIG}: in the {imagem} image GP{gpio} has two functions, {p[gpio][0]} and {rotulo}')
            p[gpio] = (rotulo, dispositivo)

        macro = lambda nome: self.macro(nome, imagem)
        if imagem == 'release':
            spi = self.spi0()
            poe(spi['MISO'], 'MISO', 'TFT e toque')
            poe(macro('TOUCH_CS'), 'T_CS', 'toque')
            poe(spi['SCK'], 'SCK', 'TFT e toque')
            poe(spi['MOSI'], 'MOSI', 'TFT e toque')
            poe(macro('TOUCH_IRQ'), 'T_IRQ', 'toque')
            poe(macro('BUZZER_PIN'), 'cigarra')
            poe(macro('TFT_RST'), 'RST', 'TFT')
            poe(macro('TFT_DC'), 'DC', 'TFT')
            poe(macro('TFT_CS'), 'CS', 'TFT')
        elif imagem == 'alpha':
            self.f.exigir_flag(env, 'HD44780_MODE_PARALLEL', '1', 'the alpha of chapter 2 drives its LCD in parallel mode on GP16 to GP21 (the I2C backpack would take GP26 and GP27)')
            for nome, rotulo in (('HD44780_RS', 'RS'), ('HD44780_EN', 'EN'), ('HD44780_D4', 'D4'), ('HD44780_D5', 'D5'), ('HD44780_D6', 'D6'), ('HD44780_D7', 'D7')):
                poe(macro(nome), rotulo, 'LCD')
            poe(macro('BUZZER_PIN'), 'cigarra')
        else:
            self.f.exigir_flag(env, 'SIMUT_AIR', '1', 'the Air has no display and no buzzer, and only its two pins beyond the slots')
            self.carregador()
            poe(macro('AIR_SENSOR_POWER_PIN'), 'energia dos sensores', 'saída')
            poe(macro('AIR_CHARGER_PIN'), 'carregador', 'entrada')
        return p


    def carregador(self):
        """The premises of the Air's power figure, which draws a divider off 5 V into an input with a pull-down that reads high with the charger on, and the sensors' switch driven
        high while the device is awake: the polarity setting, and the two lines of AppManager_Boot.cpp that set the pins."""
        alto = self.f.define(CONFIG, 'AIR_CHARGER_ACTIVE_HIGH')
        if alto != 1:
            raise Falta(f'{CONFIG}: AIR_CHARGER_ACTIVE_HIGH is {alto}, and the figure of the Air\'s power draws a divider off 5 V that reads high with the charger on (chapter 2); '
                        f'the figure has to be drawn for what that setting means first')
        self.f.achar(BOOT, r'gpio_pull_down\(AIR_CHARGER_PIN\)')
        self.f.achar(BOOT, r'airSensorPower\(AIR_SENSOR_POWER_PIN, true\)')


    def imagens(self):
        """[(the image's published name, its build environment)] in the order tools/release_manifest.py lists them (ENVS): release, alpha, air and releasetwo."""
        m = self.f.achar(RELEASE_MANIFEST, r'^ENVS = \{([^}]*)\}')
        pares = re.findall(r'"(\w+)":\s*"(\w+)"', m.group(1))
        if len(pares) < 2:
            raise Falta(f'{RELEASE_MANIFEST}: ENVS names {len(pares)} image(s), and the figure of a version\'s files draws a column for each')
        return pares

    def publicados(self):
        """(the names of the language packs a release carries, the manifest's name, the checksum file's): the packs are what data/lang holds, the other two the Checksums
        step of the release workflow, which sums *.bin, *.uf2, *.lng and the manifest into the checksum file."""
        m = self.f.achar(WORKFLOW_OTA, r'sha256sum \*\.bin \*\.uf2 \*\.lng (\S+) > (\S+)')
        return self.f.listar('data/lang', '*.lng'), m.group(1), m.group(2)


    def ap(self):
        """The setup access point: its network name (the device's default name, which StorageManager gives a new config, and the suffix NetworkManager appends), its
        address (SIMUT_AP_IP_TEXT) and the number of its item in the panel's menu (the place of MENU_SETUP_AP in MENU_ORDER, counted from 1)."""
        nome = self.f.achar('src/StorageManager.cpp', r'safeCopy\(_currentConfig\.deviceName, "([^"]+)", sizeof').group(1)
        sufixo = self.f.achar('src/net/NetworkManager_Cyw43.cpp', r'String apName = String\(deviceName\) \+ "([^"]+)";').group(1)
        ip = self.f.achar('src/WebAddress.h', r'^#define SIMUT_AP_IP_TEXT "([^"]+)"').group(1)
        itens = re.findall(r'MENU_\w+', self.f.achar(MENU, r'MENU_ORDER\[\] = \{([^}]*)\}').group(1))
        if 'MENU_SETUP_AP' not in itens:
            raise Falta(f'{MENU}: MENU_ORDER no longer lists MENU_SETUP_AP, and the figure of the first boot sends the release to that item of the menu')
        return {'ssid': nome + sufixo, 'ip': ip, 'item': itens.index('MENU_SETUP_AP') + 1}

    def comandos(self):
        """The console commands the first boot uses, as the parser and the reload's own message spell them: ap, enable, configure terminal, system ssid, system pass
        and reload confirm. The words are captured, not matched: a command renamed in the parser is a command renamed in the figure."""
        f = self.f
        ap = f.achar(COMANDOS, r'if \(t0 == "(\w+)" \|\| [^)]*\) \{ cmd\.type = CMD_AP;').group(1)
        enable = f.achar(COMANDOS, r'if \(t0 == "(\w+)"\)\s*\{ cmd\.type = CMD_ENABLE;').group(1)
        c = f.achar(COMANDOS, r't0 == "config" \|\| t0 == "(\w+)"\) && t1 == "(\w+)"\) \{ cmd\.type = CMD_CONFIGURE;')
        sistema = f.achar(COMANDOS, r'if \(t0 == "(system)"\) \{').group(1)
        ssid = f.achar(COMANDOS, r'if \(t1 == "(\w+)"\)\s+\{ cmd\.type = CMD_SET_WIFI_SSID').group(1)
        senha = f.achar(COMANDOS, r'if \(t1 == "(\w+)"\)\s+\{ cmd\.type = CMD_SET_WIFI_PASS').group(1)
        reload = f.achar('src/AppManager_Commands.cpp', r"case CMD_RELOAD:\s+if \(!cmd\.confirmed\) \{[^}]*?Use '([^']+)' para prosseguir").group(1)
        return {'ap': ap, 'enable': enable, 'configure': f'{c.group(1)} {c.group(2)}', 'ssid': f'{sistema} {ssid}', 'pass': f'{sistema} {senha}', 'reload': reload}


    def pasta(self, nome):
        """The path of DIR_`nome` in StorageManager.h: one pattern, so the figures that draw a folder read it the same way."""
        return self.f.achar(STORAGE_H, rf'^#define DIR_{nome} "([^"]+)"').group(1)

    def historico(self):
        """The history's numbers: the records a block holds (H5_BLOCK_MAX_RECORDS), the percentage of the file system above which the oldest day is deleted (the cleanup
        in StorageManager.cpp), the folder, the open block's snapshot in it and the extension of a day's file."""
        f = self.f
        return {'registros': int(f.achar('src/HistoryV5.h', r'^#define H5_BLOCK_MAX_RECORDS\s+(\d+)').group(1)),
                'limite': int(f.achar('src/StorageManager.cpp', r'\(\(info\.usedBytes \* 100\) / info\.totalBytes\) <= (\d+)\) return;').group(1)),
                'pasta': self.pasta('HISTORY'),
                'wip': f.achar('src/StorageManager.h', r'^#define FILE_H5_WIP DIR_HISTORY "([^"]+)"').group(1),
                'ext': f.achar('src/HistoryV5.h', r'^#define HISTORY_FILE_EXT\s+"([^"]+)"').group(1)}


    def telemetria(self):
        """What the collector's answer means to the telemetry: the HTTP codes that confirm a batch (the test in TelemetryManager.cpp, as "2xx") and the wait after a failure,
        in seconds, from its first to its ceiling (BACKOFF_MIN_MS and BACKOFF_MAX_MS in TelemetryManager.h)."""
        f = self.f
        m = f.achar('src/TelemetryManager.cpp', r'if \(code >= (\d+) && code < (\d+)\) \{')
        baixo, alto = int(m.group(1)), int(m.group(2))
        if baixo % 100 or alto != baixo + 100:
            raise Falta(f'src/TelemetryManager.cpp: the confirming codes are {baixo} to {alto - 1}, no longer one whole class (2xx), which is what the figure of the cursor draws')
        espera = (int(f.achar('src/TelemetryManager.h', r'BACKOFF_MIN_MS = (\d+);').group(1)), int(f.achar('src/TelemetryManager.h', r'BACKOFF_MAX_MS = (\d+);').group(1)))
        return {'ok': f'{baixo // 100}xx', 'espera_min': espera[0] // 1000, 'espera_max': espera[1] // 1000}


    def alarmes(self):
        """The alarm line's numbers: the wait before the queue is sent again (ALARM_RETRY_INTERVAL_MS), the time an HTTP answer is waited for (NET_SOCKET_TIMEOUT_MS) and the
        two MQTT topics' ends, the data topic's with "/alarm" and with "/alarm/ack"."""
        f = self.f
        return {'reenvio': int(f.achar('src/TelemetryManager.h', r'ALARM_RETRY_INTERVAL_MS = (\d+);').group(1)) // 1000,
                'espera': int(f.achar('src/SystemDefs_Network.h', r'NET_SOCKET_TIMEOUT_MS = (\d+);').group(1)) // 1000,
                'topico': f.achar('src/TelemetryManager.cpp', r'mqttAlarmTopic\( \) \{\s+return mqttDataTopic\( \) \+ "([^"]+)";').group(1),
                'ack': f.achar('src/TelemetryManager.cpp', r'mqttAlarmAckTopic\( \) \{\s+return mqttDataTopic\( \) \+ "([^"]+)";').group(1)}


    def homeassistant(self):
        """The Home Assistant figure's words, as the code spells them: the discovery topic's prefix, component and end (HaDiscovery.h); the data topic a new configuration starts with
        (StorageManager.cpp, which the blank field's fallback in TelemetryManager.cpp has to agree with) and the status topic the resolver makes from it, by the rule of
        mqttStatusTopic( ); the availability message's key and its two words, which the device's own messages and what Home Assistant is told to read have to agree on; and the
        telemetry record's first key and the prefix of a temperature's. Premises, not numbers: the discovery configs, the online message and the last will are published retained."""
        f = self.f
        prefixo = f.achar(HA_DISCOVERY, r'^constexpr const char\* PREFIX = "([^"]+)";').group(1)
        c = f.achar(HA_DISCOVERY, r'snprintf\(out, cap, "%s/(\w+)/%s/%s/(\w+)", PREFIX, nodeId, objectId\)')
        dados = f.achar('src/StorageManager.cpp', r'safeCopy\(_currentConfig\.mqttTopic, "([^"]+)", sizeof').group(1)
        vazio = f.achar(TELEMETRIA, r'if \(t\.length\( \) == 0\) t = "([^"]+)";').group(1)
        if vazio != dados:
            raise Falta(f'{TELEMETRIA}: a blank topic falls back to {vazio!r} while a new configuration starts with {dados!r} (src/StorageManager.cpp); the figure names one topic for the data')
        com_pasta = f.achar(TELEMETRIA, r'if \(lastSlash > 0\) return base\.substring\(0, lastSlash\) \+ "(/\w+)";').group(1)
        sem_pasta = f.achar(TELEMETRIA, r'return base \+ "(/\w+)";').group(1)
        if com_pasta != sem_pasta:
            raise Falta(f'{TELEMETRIA}: the status topic ends in {com_pasta} when the data topic has a folder and in {sem_pasta} when it has none; the figure names one')
        barra = dados.rfind('/')
        estado = (dados[:barra] if barra > 0 else dados) + com_pasta
        on = f.achar(TELEMETRIA, r'onlinePayload = "\{\\"device\\":\\"" \+ devName \+ "\\",\\"(\w+)\\":\\"(\w+)\\"')
        off = f.achar(TELEMETRIA, r'willPayload = "\{\\"device\\":\\"" \+ devName \+ "\\",\\"(\w+)\\":\\"(\w+)\\"')
        chave = f.achar(HA_DISCOVERY, r'\\"avty_tpl\\":\\"\{\{ value_json\.(\w+) \}\}\\"').group(1)
        ha_on = f.achar(HA_DISCOVERY, r'\\"pl_avail\\":\\"(\w+)\\"').group(1)
        ha_off = f.achar(HA_DISCOVERY, r'\\"pl_not_avail\\":\\"(\w+)\\"').group(1)
        if (on.group(1), off.group(1), on.group(2), off.group(2)) != (chave, chave, ha_on, ha_off):
            raise Falta(f'{TELEMETRIA} and {HA_DISCOVERY} disagree on the availability message: the device publishes {on.group(1)!r}: {on.group(2)!r} and, as its last will, '
                        f'{off.group(1)!r}: {off.group(2)!r}; Home Assistant is told to read {chave!r} and to wait for {ha_on!r} and {ha_off!r}')
        for padrao, o_que in ((r'ok = _mqttClient\.publish\(topic, payload, true\);', 'the discovery configs'),
                              (r'_mqttClient\.publish\(willTopicFull\.c_str\( \), onlinePayload\.c_str\( \), true\);', 'the online message'),
                              (r'willTopicFull\.c_str\( \),\s+0,\s+true,\s+willPayload\.c_str\( \)', 'the last will')):
            f.exigir(TELEMETRIA, padrao, f'the figure of Home Assistant draws {o_que} as published retained')
        ts = f.achar(TELEMETRIA, r'snprintf\(dest, maxLen, "\{\\"(\w+)\\":%lu", \(unsigned long\)rec\.epoch\);').group(1)
        t = f.achar(TELEMETRIA, r'if \(hwid\[0\]\) snprintf\(tKey, sizeof\(tKey\), "(\w)%s", hwid\);').group(1)
        return {'prefixo': prefixo, 'componente': c.group(1), 'fim': c.group(2), 'dados': dados, 'estado': estado, 'chave': chave, 'online': ha_on, 'offline': ha_off, 'ts': ts, 't': t}


    def prometheus(self):
        """The Prometheus figure's words and numbers, as the code has them: the route and the credential scheme the scraper sends (WebManager_Core.cpp, WebManager_Metrics.cpp); the four
        answers, in the order the route decides them (the lock first, then the password, then the permission, then the text); the rounds of the password check and the time one read costs,
        which the comment of PASSWORD_HMAC_ROUNDS records with the rig and the date it was measured on; and the lockout ladder, as authLockoutMs( ) computes it (a base shifted by the failure
        count, capped at the ceiling, the count itself capped). Premises, refused when the code drifts: every image has the route, it asks for PERM_DASHBOARD, and a wrong password on the
        route and on the login page raises the same penalty."""
        f = self.f
        for imagem in IMAGEM_DO_PERFIL:
            if self.macro('SIMUT_WEB_METRICS', imagem) != 1:
                raise Falta(f'{CONFIG}: SIMUT_WEB_METRICS is not 1 in the {imagem} image, and the figure of the collection draws the route every image answers (chapter 24)')
        rota = f.achar(WEB_CORE, r'_server->on\("(/\w+)", HTTP_(\w+), std::bind\(&WebManager::handleMetrics, this\)\);')
        cabecalho = f.achar(WEB_METRICS, r'String h = _server->header\("(\w+)"\);\s+if \(!h\.startsWith\("(\w+) "\)\) return 0;')
        bloqueado = f.achar(WEB_METRICS, r'if \(respondIfLockedOut\(ls, (\d+)\)\) return -1;[\s\S]*?int foundId = verifyPasswordFor\(').group(1)
        r = f.achar(WEB_METRICS, r'if \(auth == 0\) \{\s+_server->sendHeader\("WWW-Authenticate", "(?:[^"\\]|\\.)*"\);\s+_server->send\((\d+), "text/plain", "unauthorized\\n"\);\s+return;\s+\}\s+'
                                  r'if \(!\(\(uint16_t\)auth & (PERM_\w+)\)\) \{\s+_server->send\((\d+), "text/plain", "forbidden\\n"\);')
        if r.group(2) != 'PERM_DASHBOARD':
            raise Falta(f'{WEB_METRICS}: the route asks for {r.group(2)}, and the figure draws the answer for an account without PERM_DASHBOARD, the {PERMISSAO_PAINEL} of chapter 24')
        ok = f.achar(WEB_METRICS, r'_server->send\((\d+), "text/plain; version=0\.0\.4; charset=utf-8", ""\);').group(1)
        for codigo, classe, cor in ((bloqueado, 4, 'red'), (r.group(1), 4, 'red'), (r.group(3), 4, 'red'), (ok, 2, 'green')):
            if int(codigo) // 100 != classe:
                raise Falta(f'{WEB_METRICS}: an answer of the route is now {codigo}, and the figure of the collection draws it in {cor}, the colour of a {classe}xx')
        mesmo_bloqueio = 'the figure of the collection draws a wrong password raising the same penalty on the route and on the login page (chapter 24)'
        f.exigir(WEB_METRICS, r'if \(foundId < 0\) \{\s+if \(ls < 0\) ls = ensureLoginStateSlot\(clientIP\);\s+if \(ls >= 0\) applyExponentialPenalty\(ls\);', mesmo_bloqueio + ': the route does not')
        f.exigir(WEB_AUTH, r'int foundId = verifyPasswordFor\(u, p\);\s+if \(foundId < 0\) \{\s+if \(ls >= 0\) \{\s+uint32_t penaltyMs = applyExponentialPenalty\(ls\);', mesmo_bloqueio + ': the login page does not')
        rodadas = int(f.achar(NET_DEFS, r'constexpr uint16_t PASSWORD_HMAC_ROUNDS = (\d+);').group(1))
        medido_ms = int(f.achar(NET_DEFS, r'a /metrics read with Basic (\d+) ms').group(1))
        base = int(f.achar(NET_DEFS, r'uint32_t ms = (\d+)UL << n;').group(1))
        teto = int(f.achar(NET_DEFS, r'constexpr uint32_t AUTH_LOCKOUT_MAX_MS = (\d+);').group(1))
        limite = int(f.achar(NET_DEFS, r'constexpr uint8_t AUTH_FAIL_CAP = (\d+);').group(1))
        f.exigir(NET_DEFS, r'return \(ms > AUTH_LOCKOUT_MAX_MS\) \? AUTH_LOCKOUT_MAX_MS : ms;', 'the figure draws the lockout as a base shifted by the failure count and capped at AUTH_LOCKOUT_MAX_MS')
        escada = []
        for falha in range(1, limite + 1):
            ms = min(base << min(falha, limite), teto)
            escada.append(ms // 1000)
            if ms == teto:
                break
        return {'metodo': rota.group(2), 'rota': rota.group(1), 'cabecalho': cabecalho.group(1), 'esquema': cabecalho.group(2), 'bloqueado': bloqueado, 'negado': r.group(1), 'proibido': r.group(3),
                'ok': ok, 'rodadas': f'{rodadas:,}'.replace(',', '.'), 'tempo': f'{medido_ms / 1000:.1f}'.replace('.', ','), 'escada': escada}


    def syslog(self):
        """The syslog figure's numbers: the ring's depth, which is also the lines a pass sends (SLOTS in SyslogManager.h) and the port a new configuration has (SYSLOG_DEFAULT_PORT). The rest
        of what the figure draws is premises, each a pattern the code has to keep, refused with its reason when it does not: the gate is the minimum level; a full ring drops its oldest line; a pass
        is run out of the setup access point, with the link up, takes up to the ring's depth, and drops a line whose send fails; the ring is an array in the RAM; a requested reboot flushes it
        first; the sink is installed after the network and the telemetry are up; the flash log keeps only the code and the context, past the routine filter, while the syslog copy is made for every
        event; the link, on the Wi-Fi image, is the station joined to an access point."""
        f = self.f
        for rel, padrao, porque in (
                (SYSLOG_CPP, r'if \(!_enabled \|\| ev\.level < _minLevel\) return;', 'the first question of the path is whether the event reaches the minimum level'),
                (SYSLOG_CPP, r'if \(_count == SLOTS\) \{[^}]*?_head = \(_head \+ 1\) % SLOTS;\s+_count--;', 'a full ring drops its oldest line'),
                (SYSLOG_CPP, r'void SyslogManager::pump\( \) \{\s+if \(!_enabled\) return;\s+if \(!net::linkUp\( \)\) return;', 'a pass sends only with the link up'),
                (SYSLOG_CPP, r'for \(int i = 0; i < SLOTS; i\+\+\) \{\s+if \(!popLine\(line\)\) break;\s+if \(!sendLine\(line, strlen\(line\)\)\) \{[^}]*?break;',
                 'a pass takes up to the ring\'s depth in lines, and a line whose send fails is dropped, not sent again'),
                (SYSLOG_H, r'char _ring\[SLOTS\]\[LINE_MAX\];', 'the queue is an array in the RAM'),
                (LOOP, r'if \(!_isApMode\) \{[^}]*?_syslogMgr->pump\( \);', 'a pass is run out of the setup access point only'),
                (BOOT, r'setPreRebootHook\(\[\]\( \) \{[^}]*?app\._syslogMgr->flushBlocking\( \);', 'a requested reboot flushes the ring first'),
                (BOOT, r'_telemetryMgr->begin\([^;]*\);[\s\S]*?_syslogMgr->configure\(\*_storageMgr\);\s+LogManager::instance\( \)\.setSyslogSink\(',
                 'the sink is installed after the telemetry is up, which starts after the network'),
                (LOG_MANAGER, r'_policy\.shouldPersist\([^{}]*\{\s+CompactLogRecord rec;[^}]*?rec\.code = \(uint16_t\)code;\s+rec\.context = [^}]*?writeCompactToFlash\(rec\);\s+\}[^{}]*?if \(_syslogSink\) \{',
                 'the flash log keeps only the code and the context, past the routine filter, and the syslog copy is made for every event, outside the routine filter'),
                (NET_LINK, r'bool linkUp\( \) \{ return WiFi\.status\( \) == WL_CONNECTED; \}', 'on the Wi-Fi image the link is the station joined to an access point')):
            f.exigir(rel, padrao, f'the figure of the syslog path draws that {porque}')
        return {'fila': int(f.achar(SYSLOG_H, r'static constexpr int SLOTS = (\d+);').group(1)),
                'porta': int(f.achar('src/SystemDefs_Records.h', r'constexpr uint16_t SYSLOG_DEFAULT_PORT = (\d+);').group(1))}


    def integracao(self):
        """What the integration figure draws that the figures of each mechanism do not: that the telemetry has two transports, HTTP and MQTT (a premise: a third would need a third word in the
        figure); the syslog's RFC and protocol (SyslogManager.h) and its default port; the name of the service the mDNS announcement registers (NetworkManager.cpp) and the images that
        have the announcement (SIMUT_MDNS, in simut_config.h and the profiles); the method and the route of the metrics. It also asks for the readings of the alarm line, of Home Assistant and of
        Prometheus, so the claims it repeats from those figures (the alarm line's confirmation, the retained discovery, the route) stand on the same premises."""
        f = self.f
        f.exigir(RECORDS, r'enum TelemetryTransport \{\s+TEL_TRANSPORT_HTTP = 0,\s+TEL_TRANSPORT_MQTT = 1\s+\};', 'the telemetry has two transports, HTTP and MQTT, which is what the integration figure draws')
        rfc, protocolo = f.achar(SYSLOG_H, r'@brief (RFC \d+) / (UDP) syslog forwarder').groups()
        porta = int(f.achar(RECORDS, r'constexpr uint16_t SYSLOG_DEFAULT_PORT = (\d+);').group(1))
        anuncio = self.mdns()
        self.alarmes()
        self.homeassistant()
        metricas = self.prometheus()
        return {'rfc': rfc, 'protocolo': protocolo, 'porta_syslog': porta, 'mdns': anuncio['servico'], 'imagens_mdns': anuncio['imagens'], 'metodo': metricas['metodo'], 'rota': metricas['rota']}


    def corpo(self, rel, definicao):
        """The text of the function whose definition line matches `definicao` (a pattern that starts at column 0), up to the next line that starts at column 0 with a word: here a function
        body is everything between two definitions."""
        t = self.f.ler(rel)
        m = self.f.achar(rel, definicao)
        fim = re.search(r'^\w', t[m.end():], FLAGS_PADRAO)
        return t[m.start():m.end() + fim.start()] if fim else t[m.start():]

    def mdns(self):
        """The announcement of the service the device makes on the local network: its name as `_simut._tcp`, as MDNS.addService( ) is called in NetworkManager.cpp, and the images that
        have it (SIMUT_MDNS on: simut_config.h's default, a profile's -D wins), named as the manual names them."""
        servico = self.f.achar(NETWORK, r'MDNS\.addService\(nullptr, "(\w+)", "(\w+)", _advPort\)')
        imagens = [imagem for imagem in IMAGEM_DO_PERFIL if self.macro('SIMUT_MDNS', imagem) == 1]
        if not imagens:
            raise Falta(f'{CONFIG}: no image has SIMUT_MDNS on, and the figures draw the announcement of the service as one a device makes')
        return {'servico': f'_{servico.group(1)}._{servico.group(2)}', 'imagens': imagens}

    def sessao(self):
        """(the scheme the session can travel in besides the cookie, the cookie's name), as sessionTokenHeader( ) in WebManager_Auth.cpp has them. Premise, refused when the code drifts: the
        cookie is read first, and the Bearer header only when there is no cookie (the figures of the fleet and of the entry draw the session as one or the other)."""
        f = self.f
        f.exigir(WEB_AUTH, r'String WebManager::sessionTokenHeader\( \) \{\s+if \(_server->hasHeader\("Cookie"\)\) return _server->header\("Cookie"\);',
                 'the session is read from the cookie first, and from the Bearer header only when there is no cookie, which the figure of the fleet draws')
        return f.achar(WEB_AUTH, r'if \(a\.startsWith\("(\w+) "\)\) return "(\w+)=" \+ a\.substring\(7\);').groups()


    def rota_do(self, handler):
        """(the method, the path) of the route whose handler is WebManager::`handler`, as WebManager_Core.cpp registers it: a path renamed in the code is renamed in the figures."""
        m = self.f.achar(WEB_CORE, rf'_server->on\("(/[\w/.]+)", HTTP_(\w+),\s*std::bind\(&WebManager::{handler}, this\)')
        return m.group(2), m.group(1)


    def frota(self):
        """What the fleet figure draws: the four identity headers every POST of the telemetry and of the alarm line carries (the names, from addIdentityHeaders( ) in TelemetryManager.cpp; premises,
        that both POSTs call it), the scheme the session can travel in besides the cookie and the cookie's name (sessionTokenHeader( ) in WebManager_Auth.cpp; premise, that the cookie is
        read first), the metrics' route and scheme, the mDNS announcement and the name of the release's manifest."""
        f = self.f
        cabecalhos = self.corpo(TELEMETRIA, r'^static void addIdentityHeaders\(HTTPClient& http, StorageManager\* storage\) \{')
        f.achar(TELEMETRIA, r'http\.addHeader\("(X-SIMUT-\w+)", ', texto=cabecalhos)
        nomes = re.findall(r'http\.addHeader\("(X-SIMUT-\w+)", ', cabecalhos)
        for definicao, o_que in ((r'^bool TelemetryManager::attemptHttpUpload\([^)]*\) \{', 'the telemetry\'s POST'), (r'^bool TelemetryManager::attemptAlarmHttpUpload\([^)]*\) \{', 'the alarm line\'s POST')):
            f.exigir(TELEMETRIA, r'addIdentityHeaders\(http, _storageRef\);', f'{o_que} no longer carries the identity headers, which the figure of the fleet draws on both', self.corpo(TELEMETRIA, definicao))
        esquema, cookie = self.sessao()
        comum = nomes[0]
        for n in nomes[1:]:
            while not n.startswith(comum):
                comum = comum[:-1]
        rotulo_cabecalhos = comum + '/'.join(n[len(comum):] for n in nomes) if comum.endswith('-') else ', '.join(nomes)
        metricas = self.prometheus()
        anuncio = self.mdns()
        return {'cabecalhos': rotulo_cabecalhos, 'sessao': esquema, 'cookie': cookie, 'esquema_metricas': metricas['esquema'], 'metodo': metricas['metodo'], 'rota': metricas['rota'],
                'mdns': anuncio['servico'], 'imagens_mdns': anuncio['imagens'], 'manifesto': self.publicados()[1]}


    def ondas(self):
        """The update sequence a wave runs on each device, as the routes and the code have it: the backup (GET /api/backup), the stage of the image (POST /api/restore, op stage, with commit),
        the apply (POST /api/ota/apply), the version read back (sys.ver of GET /api/status), the restore of the backup (op validate, then op apply) and the language packs (the /lang folder). The
        routes are found by their handlers, so a path renamed in the code is renamed in the figure. Premises: the restore's three operations are the three the figure names, and an apply of
        a backup that was not the one validated last is refused with a 409 (which is why the restore comes after the validation)."""
        f = self.f

        rota = self.rota_do

        stage, apply = f.achar(WEB_OTA, r'bool is_stage = \(op == "(\w+)"\);\s+bool is_apply = \(op == "(\w+)"\);').groups()
        comentario = f.achar(WEB_OTA, r'@brief OTA endpoints — backup, restore \((\w+)/(\w+)/(\w+)\) and apply\.').groups()
        validate = [w for w in comentario if w not in (stage, apply)]
        if len(comentario) != 3 or len(validate) != 1 or {stage, apply} - set(comentario):
            raise Falta(f'{WEB_OTA}: the restore\'s operations are {comentario} in the header and {stage}, {apply} in the code, and the figure of the waves names three: stage, apply and the validation')
        f.exigir(WEB_OTA, r'NOT_VALIDATED \? 409', 'an apply of a backup that was not the one validated last is refused with a 409, which is why the figure of the waves restores after it has validated')
        chave, versao = f.achar(WEB_API, r'\{\\"(\w+)\\":\{\\"name\\":\\"%s\\",\\"(\w+)\\":\\"%s\\"').groups()
        argumento, valor = f.achar(WEB_OTA, r'bool commit = \(_server->arg\("(\w+)"\) == "(\w+)"\);').groups()
        return {'backup': rota('handleApiBackup'), 'restore': rota('handleApiRestoreFinish'), 'apply': rota('handleApiOtaApply'), 'estado': rota('handleApiStatus'),
                'stage': stage, 'validate': validate[0], 'apply_op': apply, 'commit': f'{argumento}={valor}', 'versao': f'{chave}.{versao}',
                'idioma': self.pasta('LANG')}


    def entrada(self):
        """What the entry figure draws that the code says: the three routes (GET login_init, POST login, GET logout, by their handlers), the keys of the code's answer, the fields of the form, the
        lifetime of the code (NONCE_LIFETIME_MS), the session's scheme and cookie, the keys and the redirect of the answer to a good login, and what a logout answers with a Bearer token (204) and
        with the cookie (302, to the login page). Premises, refused when the code drifts: an attempt spends the code (the one that matches and the one that does not); the login page hashes the
        password in the browser before it sends it; a logout ends only the session whose token is presented."""
        f = self.f
        init, login, logout = self.rota_do('handleApiLoginInit'), self.rota_do('handleApiLogin'), self.rota_do('handleLogout')
        vida_ms = int(f.achar(WEB_H, r'static const uint32_t NONCE_LIFETIME_MS = (\d+);').group(1))
        if vida_ms % 1000:
            raise Falta(f'{WEB_H}: NONCE_LIFETIME_MS is {vida_ms} ms, which is not whole seconds, and the figure of the entry says how many seconds the code lasts')
        chaves = f.achar(WEB_AUTH, r'snprintf\(json, sizeof\(json\), "\{\\"(\w+)\\":\\"%s\\",\\"(\w+)\\":%s,\\"(\w+)\\":%lu\}"').groups()
        usuario, senha = f.achar(WEB_AUTH, r'if \(!_server->hasArg\("(\w+)"\) \|\| !_server->hasArg\("(\w+)"\)\) \{').groups()
        codigo = f.achar(WEB_AUTH, r'bool hasArg\s+= _server->hasArg\("(\w+)"\);').group(1)
        esquema, cookie = self.sessao()
        dado = f.achar(WEB_AUTH, r'String cookieFlags = "(\w+)=" \+ newToken \+ "; Path=/;').group(1)
        if dado != cookie:
            raise Falta(f'{WEB_AUTH}: the login sets the cookie {dado} and the session is read from {cookie}, and the figure of the entry draws one cookie')
        ok, destino = f.achar(WEB_AUTH, r'snprintf\(resp, sizeof\(resp\), "\{\\"(\w+)\\":true,\\"(\w+)\\":\\"%s\\"\}", redirect\);').groups()
        raiz = f.achar(WEB_AUTH, r'mustChangePassword \? "/\w+" : "(/)";').group(1)
        sem_corpo = f.achar(WEB_AUTH, r'if \(byBearer\) \{[\s\S]*?_server->send\((\d+), "text/plain", ""\);').group(1)
        pagina, redireciona = f.achar(WEB_AUTH, r'_server->sendHeader\("Location", "(/\w+)", true\);\s+_server->send\((\d+), "text/plain", ""\);').groups()
        f.exigir(WEB_AUTH, r'if \(curMatch \|\| prevMatch\) \{\s+if \(ls >= 0\) \{ _loginStates\[ls\]\.nonce\[0\] = \'\\0\'; _loginStates\[ls\]\.prevNonce\[0\] = \'\\0\'; \}',
                 'the code is spent by the attempt that matches it, which is what the figure of the entry says: one attempt')
        f.exigir(WEB_AUTH, r'_loginStates\[ls\]\.nonce\[0\] = \'\\0\';\s+_loginStates\[ls\]\.prevNonce\[0\] = \'\\0\';\s+if \(nonceExpired\)',
                 'the code is spent by the attempt that does not match it too, which is what the figure of the entry says: one attempt, right or wrong')
        f.exigir(WEBUI, r'if \(pass\.length !== 64\) pass = sha256\(pass\);', 'the login page hashes the password in the browser before it sends it, which the figure of the entry draws as a step of the client')
        f.exigir(WEB_AUTH, r'const bool byBearer = !_server->hasHeader\("Cookie"\) && _server->hasHeader\("Authorization"\);',
                 'a logout with a Bearer token and no cookie is the one that answers with no body, which the figure of the entry draws')
        f.exigir(WEB_AUTH, r'session\.indexOf\("\w+=" \+ _activeSessions\[i\]\.token\) != -1\) \{[\s\S]*?_activeSessions\[i\]\.token = "";\s+break;',
                 'a logout ends only the session whose token is presented, which the figure of the entry draws')
        return {'init': init, 'login': login, 'logout': logout, 'status': self.rota_do('handleApiStatus'), 'vida_s': vida_ms // 1000, 'chaves': chaves, 'campos': (usuario, senha, codigo),
                'esquema': esquema, 'cookie': cookie, 'ok': ok, 'destino': destino, 'raiz': raiz, 'sem_corpo': sem_corpo, 'pagina': pagina, 'redireciona': redireciona}


    def api_ota(self):
        """What the figure of the update through the API draws that the code says: the routes and the operations the waves' reader finds (the backup, the stage with its commit, the apply, the
        status and the version in it, the restore's validation and apply), the login route, the header that carries the backup's CRC, the extension of the backup, the answer of a good stage (the
        keys st, v and committed, the values STAGED, the validation's OK and the 1 of a commit, and the 200 it comes with), the answer of the apply (202, the key accepted) and the interval at which
        the page waits for the device to come back (WebUI.h). Premises, refused when the code drifts: only a staged and valid image with commit is committed, and the apply's answer goes out
        before the device restarts."""
        f = self.f
        o = self.ondas()
        f.exigir(WEB_OTA, r'if \(ok_staged && valid && commit\) \{', 'only a staged and valid image with commit=1 is committed, ready for the apply, which is what the figure of the update through the API draws')
        cabecalho = f.achar(WEB_OTA, r'_server->sendHeader\("(X-Backup-\w+)", String\(scan\.payload_crc32\)\);').group(1)
        extensao = f.achar(WEB_OTA, r'"backup_%s_%lu\.(\w+)"').group(1)
        baixou = f.achar(WEB_OTA, r'_server->send\((\d+), "application/octet-stream", ""\);').group(1)
        st, v, committed = f.achar(WEB_OTA, r'"\{\\"(\w+)\\":%u,\\"bytes\\":%lu,\\"crc32\\":\\"%08lX\\","\s+"\\"(\w+)\\":%u,\\"dsize\\":%lu,\\"dcrc\\":\\"%08lX\\",\\"(\w+)\\":%u,\\"env\\":\\"%s\\"\}"').groups()
        um = f.achar(WEB_OTA, r'committed \? (\d+)u : 0u').group(1)
        bom_http = f.achar(WEB_OTA, r'_server->send\(overall_ok \? (\d+) : \d+, "application/json", buf\);').group(1)
        preparada = f.achar(OTA_STAGE_H, r'STAGED = (\d+),').group(1)
        v_bom = f.achar(OTA_VALID_H, r'OK\s+= (\d+),').group(1)
        f.exigir(WEB_OTA, r'\}"\);\s+_server->client\( \)\.flush\( \);\s+delay\(500\); /\* TCP flush before the reboot',
                 'the answer of the apply is flushed to the client before the device restarts, which is what the figure of the update through the API draws: the answer, and then the restart')
        codigo, aceito = f.achar(WEB_OTA, r'_server->send\((\d+), "application/json", "\{\\"(\w+)\\":true,\\"mode\\":\\"apply\\"\}"\);').groups()
        espera_ms = int(f.achar(WEBUI, r"await new Promise\(r => setTimeout\(r, (\d+)\)\);\s+try \{ let p = await fetch\('/api/login_init\?_=' \+ Date\.now\(\)\)").group(1))
        if espera_ms % 1000:
            raise Falta(f'{WEBUI}: the page waits {espera_ms} ms between two tries of login_init, which is not whole seconds, and the figure of the update through the API says how many')
        return {**o, 'init': self.rota_do('handleApiLoginInit'), 'login': self.rota_do('handleApiLogin'), 'crc': cabecalho, 'extensao': extensao, 'baixou': baixou, 'chaves': (st, v, committed),
                'valores': (preparada, v_bom, um), 'bom_http': bom_http, 'apply_http': codigo, 'aceito': aceito, 'espera_s': espera_ms // 1000}


    def flash(self):
        """The Pico W's flash as ota_layout.h lays it out, in KiB: the whole (OTA_FLASH_TOTAL), the area of the program (OTA_APP_MAX_SIZE), the largest image an update may be, the one that does not
        touch the config snapshot (OTA_APP_SAFE_MAX_SIZE), the file system (OTA_FILESYSTEM_SIZE) and the update's data at the end (OTA_EEPROM_RESERVED). The macros are evaluated from their
        definitions (a number, a sum, a product, another macro: any other shape is Falta). Premise: platformio.ini's board_build.filesystem_size is the file system ota_layout.h says."""
        f = self.f

        def avalia(nome, em_curso=()):
            if nome in em_curso:
                raise Falta(f'{OTA_LAYOUT}: {nome} is defined by itself')
            m = f.achar(OTA_LAYOUT, rf'^#define {nome}\s+(.+?)\s*(?:/\*.*)?$')
            expressao = m.group(1)
            for outro in sorted(set(re.findall(r'\bOTA_\w+\b', expressao))):
                expressao = re.sub(rf'\b{outro}\b', str(avalia(outro, em_curso + (nome,))), expressao)
            expressao = re.sub(r'(\d)u\b', r'\1', expressao)
            if not re.fullmatch(r'[\d\s*+\-()]+', expressao):
                raise Falta(f'{OTA_LAYOUT}: {nome} is defined as {m.group(1)!r}, which the figures cannot evaluate (a number, a sum, a product, another macro)')
            return eval(expressao, {'__builtins__': {}}, {})

        bytes_ = {chave: avalia(nome) for chave, nome in (('total', 'OTA_FLASH_TOTAL'), ('programa', 'OTA_APP_MAX_SIZE'), ('teto', 'OTA_APP_SAFE_MAX_SIZE'),
                                                          ('sistema', 'OTA_FILESYSTEM_SIZE'), ('dados', 'OTA_EEPROM_RESERVED'))}
        pio = f.achar('platformio.ini', r'^board_build\.filesystem_size = (\d+)([mk])$')
        if int(pio.group(1)) * (1024 * 1024 if pio.group(2) == 'm' else 1024) != bytes_['sistema']:
            raise Falta(f'platformio.ini: board_build.filesystem_size is {pio.group(1)}{pio.group(2)}, and {OTA_LAYOUT} (OTA_FILESYSTEM_SIZE) says {bytes_["sistema"] // 1024} KiB: the figures draw one flash')
        return {chave: valor // 1024 for chave, valor in bytes_.items()}

    def arquitetura(self):
        """What the block diagram of the device draws: the sensor types (the enum of SystemDefs_Records.h, without its two stand-ins), the slots (GP0 to GP(MAX_SENSORS - 1) of the release), the folders
        of the file system and the event log's file (StorageManager.h, LogManager.h), the images that have Bluetooth (SIMUT_BLUETOOTH), and what Core 1 does in each image. Premises, refused when the
        code drifts: Core 1 is launched in every image but the Air; the release draws its panel on the TFT, the alpha on the LCD and not on the TFT. It also asks for the integration figure's reading, so the
        outputs it draws stand on the premises of the figures of each mechanism."""
        f = self.f
        tipos = [n for n in re.findall(r'TYPE_(\w+) = \d+', f.achar(RECORDS, r'enum SensorType \{([^}]*)\};').group(1)) if n not in ('NONE', 'UNKNOWN_ACTIVITY')]
        pastas = [self.pasta(nome) for nome in ('CONFIG', 'HISTORY', 'LANG', 'THEMES')]
        log = f.achar(LOG_H, r'^#define LOG_FILE_CURRENT "(/[\w.]+)"').group(1)
        f.exigir(DISPLAY_H, r'#if SIMUT_AIR\s+static constexpr bool kUsesCore1 = false;\s+#else\s+static constexpr bool kUsesCore1 = true;',
                 'Core 1 is launched in every image but the Air, which is what the block diagram says about Core 1')
        f.exigir_flag('pico_w_air', 'SIMUT_AIR', '1', 'the block diagram gives the Air image no use of Core 1')
        if self.macro('SIMUT_DISPLAY_TFT', 'release') != 1:
            raise Falta(f'{CONFIG}: the release does not draw its panel on the TFT (SIMUT_DISPLAY_TFT), and the block diagram gives its Core 1 the TFT panel and the touch')
        if self.macro('SIMUT_DISPLAY_ALPHA', 'alpha') != 1 or self.macro('SIMUT_DISPLAY_TFT', 'alpha') != 0:
            raise Falta(f'{PERFIS}: the alpha does not write on the LCD alone (SIMUT_DISPLAY_ALPHA, SIMUT_DISPLAY_TFT), and the block diagram gives its Core 1 the LCD')
        self.integracao()
        return {'sensores': tipos, 'slots': self.slots('release'), 'pastas': pastas, 'log': log, 'bluetooth': [i for i in IMAGEM_DO_PERFIL if self.macro('SIMUT_BLUETOOTH', i) == 1]}


    def escada(self):
        """The reconnection ladder, in seconds, as the network code walks it: the first wait (WIFI_RECONNECT_BASE_MS), doubled after each failed attempt up to the ceiling (MAX_RECONNECT_DELAY),
        for WIFI_MAX_CONNECT_CYCLES attempts; then WIFI_DORMANT_MAX_WAITS waits of WIFI_DORMANT_DELAY_MS before the ladder starts again at the first wait. An attempt has the deadline of the
        connecting state (a literal in NetworkManager_Cyw43.cpp) and a search WIFI_SCAN_TIMEOUT_MS. Premises, refused when the code drifts: the wait doubles; the dormant waits follow the fast
        cycles; the restart goes back to the first wait; a connection resets the ladder; the ladder never asks for the setup AP; and no wait of the fast cycles reaches the ceiling."""
        f = self.f
        base = int(f.achar(NET_DEFS, r'WIFI_RECONNECT_BASE_MS = (\d+);').group(1))
        ciclos = int(f.achar(NET_DEFS, r'WIFI_MAX_CONNECT_CYCLES = (\d+);').group(1))
        dormencia = int(f.achar(NET_DEFS, r'WIFI_DORMANT_DELAY_MS = (\d+);').group(1))
        dormentes = int(f.achar(NET_DEFS, r'WIFI_DORMANT_MAX_WAITS = (\d+);').group(1))
        busca = int(f.achar(NET_DEFS, r'WIFI_SCAN_TIMEOUT_MS = (\d+);').group(1))
        teto = int(f.achar(NET_MGR_H, r'MAX_RECONNECT_DELAY = (\d+);').group(1))
        inicio = f.achar(NET_CYW43, r'else if \(timeSince\(_stateTimer, (\d+)\)\) \{\s+WiFi\.disconnect\(false\);')
        tentativa = int(inicio.group(1))
        f.exigir(NET_CYW43, r'_reconnectDelay = min\(_reconnectDelay \* 2, MAX_RECONNECT_DELAY\);', 'the wait doubles after each failed attempt, up to the ceiling, which is what the figure of the ladder draws')
        f.exigir(NET_CYW43, r'if \(_backoffCycles >= WIFI_MAX_CONNECT_CYCLES\) \{', 'the dormancy starts after the fast cycles, which is what the figure of the ladder draws')
        f.exigir(NET_CYW43, r'_dormantWaits\+\+;\s+_reconnectDelay = WIFI_DORMANT_DELAY_MS;', 'each dormant wait is the dormancy\'s, as many as WIFI_DORMANT_MAX_WAITS says, which is what the figure of the ladder draws')
        f.exigir(NET_CYW43, r'if \(_dormantWaits >= WIFI_DORMANT_MAX_WAITS\) \{[^}]*?resetReconnectLadder\( \);', 'after the dormant waits the ladder starts again, which is what the figure of the ladder draws')
        f.exigir(NET_MGR, r'void NetworkManager::resetReconnectLadder\( \) \{\s+_reconnectDelay = WIFI_RECONNECT_BASE_MS;', 'the restart goes back to the first wait, which is what the figure of the ladder draws')
        f.exigir(NET_CYW43, r'if \(WiFi\.status\( \) == WL_CONNECTED\) \{\s+_state = NET_CONNECTED_WAIT_IP;\s+_connectCycles = 0;[^}]*?resetReconnectLadder\( \);', 'a connection resets the ladder, which the figure of the ladder says')
        texto = f.ler(NET_CYW43)
        ramo = re.sub(r'/\*.*?\*/', '', texto[inicio.start():texto.find('_state = NET_DISCONNECT_PENDING;', inicio.end())], flags=re.S)
        if re.search(r'\bbeginAP\s*\(', ramo):
            raise Falta(f'{NET_CYW43}: the failed attempt\'s branch of the ladder calls beginAP( ), and the figure of the ladder says it never asks for the setup AP: the AP opens only when a person asks')
        esperas, espera = [], base
        for _ in range(ciclos):
            esperas.append(espera)
            espera = min(espera * 2, teto)
        if max(esperas) >= teto:
            raise Falta(f'{NET_MGR_H}: the ceiling of the wait ({teto // 1000} s) is reached by the ladder\'s waits {[e // 1000 for e in esperas]}, and the figure of the ladder says it is never reached')
        return {'esperas': [e // 1000 for e in esperas], 'tentativa': tentativa // 1000, 'busca': busca // 1000, 'teto': teto // 1000, 'dormencia': dormencia // 1000, 'dormentes': dormentes}


    def codigo(self, enum):
        """(the code, the Portuguese name) the log code table, tools/logcodes.tsv, gives the event `enum`: the figures name an event by its number and by the name the manual uses."""
        m = self.f.achar(LOGCODES, rf'^(\d+)\t{enum}\t[^\t]*\t([^\t]+)\t')
        return int(m.group(1)), m.group(2)

    def correcao(self):
        """What the time-correction figure draws: the five events (the provisional time set at the boot, the NTP sync, the correction's three: NTP correcting, corrected, caches invalidated) with the
        code and the name the log code table gives each, and the difference a set of the clock has to pass to correct (the literal of endProvisional( ), in seconds). Premises, refused when the
        code drifts: the provisional clock starts from the last record; the sync is logged before the correction; the correction covers the blocks this boot wrote and shifts them by the
        difference; and the events come in the order 408 (before the shift), 409 (after it), 410."""
        f = self.f
        limiar = int(f.achar(NET_MGR, r'if \(_timeSyncCb && abs\(delta\) > (\d+)\) \{').group(1))
        f.exigir(NET_MGR, r'void NetworkManager::setProvisionalTime\(uint32_t lastTs, uint32_t elapsedSec\) \{\s+if \(lastTs > \d+\) setProvisionalNow\(lastTs \+ elapsedSec, 0\);',
                 'the provisional clock starts from the last record of the history, which is what the figure of the correction draws')
        f.exigir(NET_MGR, r'SYS_NTP_SYNC, 0, "NTP OK: "[^;]*;\s+endProvisional\(', 'the sync is logged before the correction, which is the order of the figure of the correction')
        f.exigir(NET_MGR, r'_timeSyncCb\(_provisionalBase, delta\);', 'the correction covers only the blocks this boot wrote (from the provisional clock\'s start), which is what the figure of the correction draws')
        f.exigir(APP_CORE, r'blocks = _storageMgr->shiftHistoryTimeV5\(delta, "", bootTs\);', 'the correction shifts the blocks by the difference, which is what the figure of the correction draws')
        f.exigir(APP_CORE, r'APP_NTP_CORRECTING[\s\S]*?shiftHistoryTimeV5[\s\S]*?APP_NTP_CORRECTED[\s\S]*?APP_CACHE_INVALIDATED',
                 'event 408 comes before the shift, 409 after it and 410 last, which is the order of the figure of the correction')
        eventos = {chave: self.codigo(enum) for chave, enum in (('provisorio', 'NET_PROVISIONAL_TIME'), ('ntp', 'SYS_NTP_SYNC'), ('corrigindo', 'APP_NTP_CORRECTING'),
                                                                 ('corrigidos', 'APP_NTP_CORRECTED'), ('caches', 'APP_CACHE_INVALIDATED'))}
        return {'limiar': limiar, 'eventos': eventos}


    def transicao(self):
        """What the log-policy figure draws that the code says: the two cadences of LogPolicy (the heartbeat and the accounting, LOGPOL_HEARTBEAT_MS and LOGPOL_REPORT_MS, in ms) and the three events by their
        code and Portuguese name (the telemetry sent, the telemetry failed, the suppressed records). Premises, one pattern each and none on the text of another, refused when the code drifts: the telemetry's
        success and failure are the routed codes of one family; the first failure of a healthy family is written; a family that keeps failing is held to one record an hour; the first success after the boot
        or after a failure is written; a healthy family is held to one record an hour; the accounting window starts at the first suppressed record and is reported once an hour. The mirror
        (EspelhoLogPolicy) is held to the code by the test."""
        f = self.f
        batimento = int(f.achar(LOGPOLICY_H, r'^#define LOGPOL_HEARTBEAT_MS (\d+)UL').group(1))
        relatorio = int(f.achar(LOGPOLICY_H, r'^#define LOGPOL_REPORT_MS (\d+)UL').group(1))
        f.exigir(LOGPOLICY_CPP, r'LOGPOL_RULE\(SYS_TEL_SENT,\s+LOGGRP_TEL,\s+0\)', 'the telemetry\'s success is a routed code of one family, which is what the figure of the filter draws')
        f.exigir(LOGPOLICY_CPP, r'LOGPOL_RULE\(SYS_TEL_FAIL,\s+LOGGRP_TEL,\s+1\)', 'the telemetry\'s failure is a fault of the same one family, which is what the figure of the filter draws')
        for padrao, porque in ((r'if \(!gf\.faulty\) \{\s+gf\.faulty = true;\s+gf\.lastPersistMs = nowMs;\s+return true;', 'the first failure of a healthy family is written'),
                               (r'if \(elapsed\(nowMs, gf\.lastPersistMs, LOGPOL_HEARTBEAT_MS\)\) \{\s+gf\.lastPersistMs = nowMs;\s+return true;', 'a family that keeps failing is held to one record an hour'),
                               (r'if \(!g\.seen \|\| g\.faulty\) \{\s+g\.seen = true;\s+g\.faulty = false;\s+g\.lastPersistMs = nowMs;\s+return true;',
                                'the first success after the boot, or after a failure, is written'),
                               (r'if \(elapsed\(nowMs, g\.lastPersistMs, LOGPOL_HEARTBEAT_MS\)\) \{\s+g\.lastPersistMs = nowMs;\s+return true;', 'a healthy family is held to one record an hour'),
                               (r'void LogPolicy::countSuppressed\(uint32_t nowMs\) \{\s+if \(_suppressed == 0\) \{[^}]*?_lastReportMs = nowMs;', 'the accounting window starts at the first suppressed record'),
                               (r'if \(!elapsed\(nowMs, _lastReportMs, LOGPOL_REPORT_MS\)\) return 0;', 'the accounting is reported once an hour')):
            f.exigir(LOGPOLICY_CPP, padrao, f'the figure of the filter draws that {porque}')
        return {'batimento_ms': batimento, 'relatorio_ms': relatorio, 'envio': self.codigo('SYS_TEL_SENT'), 'falha': self.codigo('SYS_TEL_FAIL'), 'resumo': self.codigo('SYS_LOG_SUPPRESSED')}


    def ota_linha_do_tempo(self):
        """What the update timeline draws that the code says: the web page's estimates of the apply and of the boot (WebUI.h, the text of the confirmation: "Apply + reboot: ~25 s", "Full boot: ~25 s"),
        in seconds. Premises, refused when the code drifts: the stage writes only the staging area, the file system's partition; and the applier rewrites the program from the start of the flash, the
        one window in which a cut leaves the device without firmware. The measured times are constants of the tool (ENVIO_MEDIDO_S, VOLTA_MEDIDA_S)."""
        f = self.f
        aplicacao = int(f.achar(WEBUI, r'Apply \+ reboot: ~(\d+) s').group(1))
        boot = int(f.achar(WEBUI, r'Full boot: ~(\d+) s').group(1))
        f.exigir(OTA_STAGING, r'uint32_t flash_offs = OTA_STAGING_OFFSET \+ offset_in_staging;', 'the stage writes only the staging area, the file system\'s partition, which is what the figure of the update draws')
        f.exigir(OTA_APPLIER, r'flash_range_program\(i \* OTA_FLASH_SECTOR_SIZE,\s+s_applier_buf, OTA_FLASH_SECTOR_SIZE\);',
                 'the applier rewrites the program from the start of the flash, the one window in which a cut leaves the device without firmware, which is what the figure of the update draws')
        return {'aplicacao': aplicacao, 'boot': boot}


    def despertar(self):
        """What the Air wake figure draws that the code says: the warm-up, in seconds (the literal of the WARMUP phase). Premises, refused when the code drifts: the phases come in the order
        warm-up, sampling, decision, connection, send, sleep; a reading-only wake never starts the network; the sampling connects the Wi-Fi in parallel on a telemetry wake; the decision always writes
        the history and only then chooses the radio (when there is a link) or the sleep; GP16, which powers the sensors, rises at the start of the boot and falls when the wake goes to sleep, which is
        also where the radio is powered down. The measured times are constants of the tool."""
        f = self.f
        aquecimento = int(f.achar(AIR_CPP, r'case AIR_PHASE_WARMUP: \{[\s\S]*?if \(timeSince\(_airPhaseTimer, (\d+)\)\) \{\s+_airPhase = AIR_PHASE_SAMPLE;').group(1))
        f.exigir(APP_H, r'AIR_PHASE_OFF = 0,[^\n]*\n\s+AIR_PHASE_WARMUP,[^\n]*\n\s+AIR_PHASE_SAMPLE,[^\n]*\n\s+AIR_PHASE_DECIDE,[^\n]*\n\s+AIR_PHASE_PERSIST,[^\n]*\n\s+AIR_PHASE_CONNECT,[^\n]*\n\s+AIR_PHASE_FLUSH,[^\n]*\n\s+AIR_PHASE_SLEEP',
                 'the phases of a wake come in this order (warm-up, sampling, decision, connection, send, sleep), which is the order of the figure of the wake')
        f.exigir(BOOT, r'else if \(!_airRadioWake\) \{\s+/\* Reading-only wake: the network is not started at all\.',
                 'a reading-only wake never starts the network, which is what the figure of the wake draws: the radio band only in the telemetry wake')
        f.exigir(AIR_CPP, r'if \(_airRadioWake && !_airNetGaveUp\) \{\s+_netMgr->update\( \);', 'the sampling connects the Wi-Fi in parallel on a telemetry wake, which is what the figure of the wake draws')
        f.exigir(AIR_CPP, r'processHistoryLogging\( \);[\s\S]*?_airPhase = online \? AIR_PHASE_CONNECT : AIR_PHASE_SLEEP;',
                 'the decision always writes the history, and only then chooses the radio (when it has a link) or the sleep, which is what the figure of the wake draws')
        f.achar(BOOT, r'airSensorPower\(AIR_SENSOR_POWER_PIN, true\)')
        f.exigir(AIR_CPP, r'airSensorPower\(_airCfg\.sensorPowerPin, false\);[\s\S]*?net::powerDownForSleep\( \);',
                 'the wake going to sleep is where GP16 falls and the radio is powered down, which is where the bands of the figure of the wake end')
        return {'aquecimento_s': aquecimento / 1000}


def pino_de(gpio):
    return PINO_DO_GPIO[gpio]


def milhar(n):
    """A whole number as pt-BR writes it: 1.020, with the dot."""
    return f'{n:,}'.replace(',', '.')


class EspelhoLogPolicy:
    """LogPolicy::shouldPersist( ) and takeSuppressedReport( ) for one routed family (the log-policy figure draws the telemetry's), written as the code writes them. The two numbers it takes are the
    code's (LOGPOL_HEARTBEAT_MS, LOGPOL_REPORT_MS); the rules are the code's, which Numeros.transicao( ) holds the file to, one pattern each, and tools/test_manual_diagramas.py holds this mirror to
    the code itself: LogPolicy.cpp compiled on the host answers like it, on the figure's events and on random ones."""

    def __init__(self, batimento_ms, relatorio_ms):
        self.batimento, self.relatorio = batimento_ms, relatorio_ms
        self.visto = self.falhando = self.armado = False
        self.ultimo = self.suprimidos = self.ultimo_relatorio = 0

    @staticmethod
    def passou(agora, desde, intervalo):
        """elapsed( ) of LogPolicy.cpp: the signed difference of two 32-bit times, which is how millis( ) is compared."""
        d = (agora - desde) & 0xFFFFFFFF
        return (d - 0x100000000 if d >= 0x80000000 else d) >= intervalo

    def _conta(self, agora):
        if self.suprimidos == 0:
            self.ultimo_relatorio, self.armado = agora, True
        self.suprimidos += 1

    def persiste(self, falha, agora):
        """True when the record goes to the flash: the first failure of a healthy family; the first success since the boot or after a failure; and one record an hour of either kind."""
        if falha:
            if not self.falhando:
                self.falhando, self.ultimo = True, agora
                return True
            if self.passou(agora, self.ultimo, self.batimento):
                self.ultimo = agora
                return True
            self._conta(agora)
            return False
        if not self.visto or self.falhando:
            self.visto, self.falhando, self.ultimo = True, False, agora
            return True
        if self.passou(agora, self.ultimo, self.batimento):
            self.ultimo = agora
            return True
        self._conta(agora)
        return False

    def relata(self, agora):
        """The number of records suppressed since the last report, once an hour after the first of them, or 0."""
        if not self.armado or self.suprimidos == 0 or not self.passou(agora, self.ultimo_relatorio, self.relatorio):
            return 0
        n, self.suprimidos, self.armado = self.suprimidos, 0, False
        return min(n, 32767)


def simular_transicao(espelho, transicao):
    """The figure's scenario through the mirror: an event every ENVIOS_CADA_MIN minutes for DURACAO_H hours, a failure while the collector is down, and the accounting asked for after each one.
    Returns what the compiled policy is fed ('entrada'), and what the figure draws: each event with its minute, whether it failed and whether it reached the flash, and the accounting records."""
    entrada, eventos, relatorios = [], [], []
    for minuto in range(0, DURACAO_H * 60, ENVIOS_CADA_MIN):
        falha, ms = FORA_DO_AR_MIN[0] < minuto < FORA_DO_AR_MIN[1], minuto * 60_000
        eventos.append({'minuto': minuto, 'falha': falha, 'flash': espelho.persiste(falha, ms)})
        entrada.append(('E', ms, falha))
        n = espelho.relata(ms)
        entrada.append(('R', ms, False))
        if n:
            relatorios.append({'minuto': minuto, 'n': n})
    return {'entrada': entrada, 'eventos': eventos, 'relatorios': relatorios}


# ---- the SVG builder -----------------------------------------------------------------------------------------------------------------------------------------
def n(v):
    """A number as the SVG writes it: whole numbers without ".0", the rest with at most two decimals and no trailing zero."""
    if isinstance(v, float):
        if v == int(v):
            return str(int(v))
        return f'{v:.2f}'.rstrip('0').rstrip('.')
    return str(v)


# Advance widths, in em, of DejaVu Sans, which is about as wide as a system-ui face gets: a line that fits with these fits in Segoe UI, Roboto, Helvetica and Arial.
_EM = {}
for _letras, _largura in (
        (' \u00a0', .32), ('ijl', .28), ("'|", .30), ('.,:;!', .34), ('()[]{}/\\-', .39), ('ft', .39), ('r', .41), ('s', .52), ('c', .55), ('k', .59), ('vxyz', .59),
        ('aeopbdqghnu', .63), ('m', .97), ('w', .82), ('0123456789', .64),
        ('IJ', .30), ('L', .56), ('F', .58), ('E', .63), ('P', .60), ('T', .61), ('Y', .61), ('S', .63), ('B', .69), ('K', .66), ('R', .70), ('AVXZ', .69), ('C', .70),
        ('DNUH', .75), ('GOQ', .79), ('M', .86), ('W', .99),
        ('+=<>~', .84), ('%', .95), ('&', .78), ('*', .50), ('_', .50), ('·', .32), ('–', .50), ('−', .84), ('—', 1.0), ('Ω', .76), ('²', .40), ('×', .84), ('°', .50),
        ('→', .84), ('≥', .84), ('µ', .64), ('#', .84), ('@', 1.0), ('?', .53), ('"', .46), ('$', .64)):
    for _c in _letras:
        _EM[_c] = _largura
LARGURA_MONO = .62       # DejaVu Sans Mono's .602 and a margin
LARGURA_OUTROS = .65     # a character the table does not know (an accented letter is its base letter)


def largura_texto(s, tam=13, mono=False):
    """A conservative width, in px, of `s` set at `tam` px: the text is drawn in the reader's font, so the width varies from machine to machine."""
    if mono:
        return math.ceil(len(s) * LARGURA_MONO * tam)
    return math.ceil(sum(_EM.get(unicodedata.normalize('NFD', c)[0], LARGURA_OUTROS) for c in s) * tam)


def quebrar(texto, largura, tam=13, mono=False):
    """`texto` broken at spaces into lines that fit in `largura` px by largura_texto( ). A word wider than a line is an error: breaking inside it, or letting it out of its
    box, would hide a layout that does not fit. A no-break space (U+00A0, as in "3,3 V") keeps its two words on one line."""
    linhas, atual = [], ''
    for palavra in texto.split(' '):
        if not palavra:
            continue
        if largura_texto(palavra, tam, mono) > largura:
            raise ValueError(f'the word {palavra!r} is wider than the {largura} px of its line')
        tentativa = f'{atual} {palavra}' if atual else palavra
        if atual and largura_texto(tentativa, tam, mono) > largura:
            linhas.append(atual)
            atual = palavra
        else:
            atual = tentativa
    if atual:
        linhas.append(atual)
    return linhas


class Svg:
    """One diagram's SVG, built from shapes on a 4 px grid. The attributes of every element are written in a fixed order and the numbers by n( ), so the same calls give the
    same bytes. Boxes are drawn inside their coordinates (a 1 px line at x + .5, so that it is a whole pixel at 720 px). Text is placed by its baseline; the builder keeps the
    estimated box of every text and refuses one that overlaps another, leaves the canvas or sits off the grid."""

    def __init__(self, slug, altura=None):
        """`altura` is the canvas height, or None to let the drawing say: the height is then its lowest point and 16 px (a flow is laid out from the top, and its height is
        what its boxes add up to)."""
        if altura is not None and altura % GRADE:
            raise ValueError(f'{slug}: the height {altura} is off the {GRADE} px grid')
        self.slug, self._altura, self.fundo = slug, altura, 0
        self.corpo, self.textos, self.pontas = [], [], set()

    @property
    def altura(self):
        return self._altura if self._altura is not None else -(-(self.fundo + 16) // GRADE) * GRADE

    def _abaixo(self, y):
        self.fundo = max(self.fundo, y)

    def _grade(self, *valores):
        for v in valores:
            if v % GRADE:
                raise ValueError(f'{self.slug}: {v} is off the {GRADE} px grid')

    def caixa(self, x, y, w, h, cls='dg-caixa', raio=6, traco=1):
        """A box of w x h with its corner at (x, y); `raio` is 6 (a small box), 12 (a container) or 0; `traco` is the width of its line, drawn inside the box."""
        self._grade(x, y, w, h)
        if raio not in (0, 6, 12):
            raise ValueError(f'{self.slug}: a corner radius of {raio}: the Ângulo has 6 and 12')
        d = traco / 2
        self._abaixo(y + h)
        self.corpo.append(f'<rect class="{cls}" x="{n(x + d)}" y="{n(y + d)}" width="{n(w - traco)}" height="{n(h - traco)}" rx="{raio}"/>')

    def texto(self, x, y, conteudo, cls='dg-t', ancora='start'):
        """A text with its baseline at y. `conteudo` is a string or a list of (class, string) runs, the class '' for plain text: the runs are how a GP name goes in the
        code's font in the middle of a sentence. The size comes from `cls`: dg-t is 13 px, dg-n is 12 px."""
        self._grade(x, y)
        runs = [('', conteudo)] if isinstance(conteudo, str) else conteudo
        classes = cls.split()
        tam = 12 if 'dg-n' in classes else 13
        w = sum(largura_texto(s, tam, 'dg-m' in classes or 'dg-m' in c.split()) for c, s in runs)
        x0 = x if ancora == 'start' else x - w if ancora == 'end' else x - w / 2
        caixa = (x0, y - tam + 2, x0 + w, y + 4)
        rotulo = ''.join(s for _, s in runs)
        self._abaixo(caixa[3])
        if caixa[0] < 4 or caixa[2] > LARGURA - 4 or caixa[1] < 0 or (self._altura is not None and caixa[3] > self._altura):
            raise ValueError(f'{self.slug}: the text {rotulo!r} (about {w} px wide, from x={n(x0)}) leaves the canvas')
        for outra, rot in self.textos:
            if min(caixa[2], outra[2]) - max(caixa[0], outra[0]) > 1 and min(caixa[3], outra[3]) - max(caixa[1], outra[1]) > 1:
                raise ValueError(f'{self.slug}: the text {rotulo!r} overlaps {rot!r}')
        self.textos.append((caixa, rotulo))
        corpo = ''.join(html.escape(s, quote=False) if not c else f'<tspan class="{c}">{html.escape(s, quote=False)}</tspan>' for c, s in runs)
        a = '' if ancora == 'start' else f' text-anchor="{ancora}"'
        self.corpo.append(f'<text class="{cls}" x="{x}" y="{y}"{a}>{corpo}</text>')

    def linha(self, pontos, cls='dg-fio'):
        self._grade(*[v for p in pontos for v in p])
        self._abaixo(max(y for _, y in pontos))
        self.corpo.append(f'<polyline class="{cls}" points="{" ".join(f"{x},{y}" for x, y in pontos)}"/>')

    def seta(self, pontos, cls='dg-fio', ponta='dg-ponta'):
        """A line whose last point is an arrowhead's tip."""
        self._grade(*[v for p in pontos for v in p])
        self._abaixo(max(y for _, y in pontos))
        self.pontas.add(ponta)
        self.corpo.append(f'<polyline class="{cls}" points="{" ".join(f"{x},{y}" for x, y in pontos)}" marker-end="url(#{self.slug}-{ponta[3:]})"/>')

    def no(self, x, y, cls='dg-no', raio=4):
        """A junction or a terminal: a dot of 8 px, or of 16 (a marked event)."""
        self._grade(x, y)
        if raio not in (4, 8):
            raise ValueError(f'{self.slug}: a dot of radius {raio}: the figures have 4 and 8')
        self._abaixo(y + raio)
        self.corpo.append(f'<circle class="{cls}" cx="{x}" cy="{y}" r="{raio}"/>')

    def render(self):
        partes = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {LARGURA} {self.altura}" role="img" class="dg">']
        if self.pontas:
            partes.append('<defs>')
            for ponta in sorted(self.pontas):
                partes.append(f'<marker id="{self.slug}-{ponta[3:]}" viewBox="0 0 8 8" refX="8" refY="4" markerWidth="8" markerHeight="8" markerUnits="userSpaceOnUse" '
                              f'orient="auto"><path class="{ponta}" d="M0 0L8 4L0 8z"/></marker>')
            partes.append('</defs>')
        partes += self.corpo
        partes.append('</svg>')
        return '\n'.join(partes) + '\n'


# ---- the drawings --------------------------------------------------------------------------------------------------------------------------------------------
def gp_e_pino(gpio):
    """The runs of a Pico W terminal's label: the GPIO in the code's font, then its physical pin."""
    return [('dg-m', f'GP{gpio}'), ('', f' · pino {pino_de(gpio)}')]


def caixas_da_ligacao(s, h, titulo, subtitulo):
    """The two boxes of a wiring figure, drawn before the wires so that a terminal's dot sits on top of the box's line: the Pico W at the left and the sensor's module at the
    right, both `h` tall and 232 px apart, 192 and 232 px wide."""
    s.caixa(24, 16, 192, h, 'dg-caixa-2', 12)
    s.texto(40, 36, 'Pico W', 'dg-t dg-b')
    s.caixa(464, 16, 232, h, 'dg-caixa-2', 12)
    s.texto(480, 36, titulo, 'dg-t dg-b')
    s.texto(480, 52, subtitulo, 'dg-n dg-c2')


def terminal(s, y, pico, modulo, papel=None, fio='dg-fio', ate=464, mono=True):
    """One wire at height y from the Pico W's edge to the module's: `pico` is the label's runs at the Pico's side, `modulo` the terminal's name at the module's (in the code's
    font when `mono`) and `papel` what it is for, in the note's size. The wire ends in a dot on each box's line."""
    s.texto(200, y + 4, pico, 'dg-t', 'end')
    s.texto(480, y + 4, modulo, 'dg-t dg-m' if mono else 'dg-t')
    if papel:
        s.texto(528, y + 4, papel, 'dg-n dg-c2')
    s.linha([(216, y), (ate, y)], fio)
    ponto = 'dg-no-acento' if fio == 'dg-fio-acento' else 'dg-no'
    s.no(216, y, ponto)
    s.no(ate, y, ponto)


def nota_do_dado(num, gpio):
    """The note under a one-wire figure: the data wire goes on any sensor GPIO, GP0 to GP(MAX_SENSORS - 1) (the chapter's word is sensor, a slot being a place in the configuration,
    not a pin: cap06-sensores.md:86), `gpio` is only the example, and the ground on any ground pin."""
    return [('', 'O dado liga num GPIO de sensor ('), ('dg-m', 'GP0'), ('', ' a '), ('dg-m', f'GP{num.slots() - 1}'), ('', '; o '), ('dg-m', f'GP{gpio}'),
            ('', ' é um exemplo) e o GND, em qualquer pino de terra.')]


def desenho_ds18b20(num, slug):
    gpio = num.exemplo(EXEMPLO_DS18B20)
    a, b, c = 72, 120, 168
    s = Svg(slug, 248)
    caixas_da_ligacao(s, 184, 'DS18B20', 'sonda de três fios')
    terminal(s, a, [('dg-m', '3V3'), ('', f' · pino {PINO_3V3}')], 'VDD', 'alimentação')
    terminal(s, b, gp_e_pino(gpio), 'DQ', 'dado')
    terminal(s, c, [('dg-m', 'GND'), ('', f' · pino {PINO_GND}')], 'GND', 'terra')
    # the pull-up: the resistor between the data wire and the 3V3 wire, the one thing the figure draws in the accent
    x = 400
    s.linha([(x, a), (x, a + 12)], 'dg-fio-acento')
    s.caixa(x - 8, a + 12, 16, 24, 'dg-comp-acento', 6, 2)
    s.linha([(x, a + 36), (x, b)], 'dg-fio-acento')
    s.no(x, a, 'dg-no-acento')
    s.no(x, b, 'dg-no-acento')
    s.texto(x + 20, a + 28, RESISTOR, 'dg-t')
    s.texto(24, 232, nota_do_dado(num, gpio), 'dg-n dg-c2')
    return s.render()


def desenho_dht22(num, slug):
    gpio = num.exemplo(EXEMPLO_DHT22)
    a, b, c = 72, 120, 168
    s = Svg(slug, 248)
    caixas_da_ligacao(s, 184, 'DHT22', 'módulo de três terminais')
    terminal(s, a, [('dg-m', '3V3'), ('', f' · pino {PINO_3V3}')], '+', 'alimentação', mono=False)
    terminal(s, b, gp_e_pino(gpio), 'dados', mono=False)
    terminal(s, c, [('dg-m', 'GND'), ('', f' · pino {PINO_GND}')], '−', 'terra', mono=False)
    s.texto(232, b + 28, 'pull-up conforme o módulo', 'dg-n dg-c2')
    s.texto(24, 232, nota_do_dado(num, gpio), 'dg-n dg-c2')
    return s.render()


def desenho_bmx280(num, slug):
    bme = num.bme()
    v, csb, sda, scl, sdo, gnd = 72, 104, 152, 200, 248, 280
    s = Svg(slug, 360)
    caixas_da_ligacao(s, 296, 'BME280', 'módulo de seis pinos')
    terminal(s, v, [('dg-m', '3V3'), ('', f' · pino {PINO_3V3}')], 'VCC', 'alimentação')
    terminal(s, sda, gp_e_pino(bme['sda']), 'SDA', 'dados', 'dg-fio-acento')
    terminal(s, scl, gp_e_pino(bme['scl']), 'SCL', 'relógio', 'dg-fio-acento')
    terminal(s, gnd, [('dg-m', 'GND'), ('', f' · pino {PINO_GND}')], 'GND', 'terra')
    # CSB and SDO are tied to the 3V3 and GND wires: a branch at x=424
    x = 424
    s.linha([(x, v), (x, csb), (464, csb)])
    s.no(x, v)
    s.no(464, csb)
    s.texto(480, csb + 4, 'CSB', 'dg-t dg-m')
    s.texto(528, csb + 4, '3V3: modo I²C', 'dg-n dg-c2')
    s.linha([(x, gnd), (x, sdo), (464, sdo)])
    s.no(x, gnd)
    s.no(464, sdo)
    s.texto(480, sdo + 4, 'SDO', 'dg-t dg-m')
    s.texto(528, sdo + 4, f'GND: endereço {bme["sdo_gnd"]}', 'dg-n dg-c2')
    s.texto(24, 344, f'pull-ups de {RESISTOR} em SDA e SCL, se o módulo não tiver', 'dg-n dg-c2')
    return s.render()


BX, BY, BW, BH = 176, 36, 160, 428     # the board: its left edge, top edge, width and height
Y0, PASSO = 52, 20                      # the top of the first pad and the pitch of the rows
PAD_W, PAD_H = 24, 16
COL_NOME, COL_PAPEL = BX + BW + 8, BX + BW + 76       # the right side: where a pin's name starts, and what it is for

MAPAS = {
    'release': {'acento': 'TFT, toque e cigarra', '3v3': 'TFT e sensores', 'vbus': '5 V do USB', 'nota': None},
    'alpha': {'acento': 'LCD e cigarra', '3v3': 'sensores', 'vbus': '5 V do USB, para o LCD', 'nota': None},
    'air': {'acento': 'energia dos sensores e carregador', '3v3': 'sensores', 'vbus': '5 V do USB',
            'nota': ('GP{energia}', ' também é o sinal de acordado: fica em nível alto enquanto o aparelho está acordado.')},
}
CLASSES_DO_PINO = {'sensor': ('dg-caixa', ''), 'papel': ('dg-caixa-acento', ' dg-sobre-acento'), 'forte': ('dg-caixa-forte', ' dg-sobre-forte'),
                   'apagado': ('dg-caixa-apagada', ' dg-c2')}


def pad_de(pino):
    """(x, y) of the top left corner of a pin's pad: the left row runs down from pin 1, the right row up from pin 21, as seen from above."""
    linha = pino - 1 if pino <= 20 else 40 - pino
    return (BX + 4 if pino <= 20 else BX + BW - 4 - PAD_W), Y0 + PASSO * linha


def papel_do_pino(pino, papeis, slots, texto):
    """(kind, the role's label as runs or None) of a pin of the board in the image whose roles and slot count these are."""
    nome = PINAGEM[pino]
    if nome.startswith('GP'):
        g = int(nome[2:])
        if g < slots:
            return 'sensor', None
        if g in papeis:
            rotulo, dispositivo = papeis[g]
            return 'papel', (rotulo, dispositivo)
        return 'apagado', ('sem uso', '')
    if nome in ('GND', 'AGND'):
        return 'forte', None
    if nome == '3V3':
        return 'forte', (texto['3v3'], '')
    if nome == 'VBUS':
        return 'forte', (texto['vbus'], '')
    return 'apagado', None


def mapa_de_pinos(num, slug, imagem):
    papeis, slots, radio, texto = num.papeis(imagem), num.slots(imagem), num.radio(), MAPAS[imagem]
    s = Svg(slug, 544 if texto['nota'] is None else 568)
    s.caixa(BX, BY, BW, BH, 'dg-caixa-2', 12)
    s.caixa(BX + 56, 24, 48, 20, 'dg-caixa', 6)
    s.texto(BX + 80, 40, 'USB', 'dg-n dg-m dg-c2', 'middle')
    s.caixa(BX + 48, 68, 64, 48, 'dg-caixa', 6)
    s.texto(BX + 80, 96, 'RP2040', 'dg-n dg-m dg-c2', 'middle')
    s.texto(BX + 80, 216, 'Pico W', 'dg-t dg-b', 'middle')
    s.texto(BX + 80, 232, 'vista de cima', 'dg-n dg-c2', 'middle')
    s.caixa(BX + 32, 352, 96, 80, 'dg-caixa-apagada', 6)
    s.texto(BX + 80, 372, 'rádio', 'dg-n dg-c2', 'middle')
    for k, g in enumerate(radio):
        s.caixa(BX + 36 + 48 * (k % 2), 380 + 24 * (k // 2), 40, 16, 'dg-caixa-apagada', 6)
        s.texto(BX + 56 + 48 * (k % 2), 392 + 24 * (k // 2), f'GP{g}', 'dg-n dg-m dg-c2', 'middle')
    for pino in range(1, 41):
        x, y = pad_de(pino)
        tipo, papel = papel_do_pino(pino, papeis, slots, texto)
        cls, cor = CLASSES_DO_PINO[tipo]
        s.caixa(x, y, PAD_W, PAD_H, cls, 6)
        s.texto(x + 12, y + 12, str(pino), 'dg-n dg-m' + cor, 'middle')
        nome = PINAGEM[pino]
        if pino <= 20:
            s.texto(BX - 8, y + 12, nome, 'dg-t dg-m', 'end')
            continue
        s.texto(COL_NOME, y + 12, nome, 'dg-t dg-m' + (' dg-c2' if tipo == 'apagado' and papel is None else ''))
        if papel:
            rotulo, dispositivo = papel
            mono = rotulo.isupper()
            runs = [('dg-m' if mono else '', rotulo)] + ([('', f' · {dispositivo}')] if dispositivo else [])
            s.texto(COL_PAPEL, y + 12, runs, 'dg-t' + (' dg-c2' if tipo == 'apagado' else ''))
    # the group of the left row
    s.linha([(128, 60), (124, 60), (124, 440), (128, 440)], 'dg-fio-fino')
    s.texto(116, 236, 'sensores', 'dg-t', 'end')
    s.texto(116, 252, [('dg-m', 'GP0'), ('', ' a '), ('dg-m', f'GP{slots - 1}')], 'dg-t', 'end')
    s.texto(116, 268, f'{slots} slots', 'dg-n dg-c2', 'end')
    # the legend
    for k, (tipo, rot) in enumerate((('sensor', 'sensor'), ('papel', texto['acento']), ('forte', 'alimentação e terra'), ('apagado', 'sem uso, ou interno do Pico W'))):
        x, y = 16 + 352 * (k % 2), 488 + 24 * (k // 2)
        s.caixa(x, y, PAD_W, PAD_H, CLASSES_DO_PINO[tipo][0], 6)
        s.texto(x + 32, y + 12, rot, 'dg-t')
    if texto['nota'] is not None:
        energia = next(g for g, (rotulo, _) in papeis.items() if rotulo == 'energia dos sensores')
        s.texto(16, 548, [('dg-m', texto['nota'][0].format(energia=energia)), ('', texto['nota'][1])], 'dg-n dg-c2')
    return s.render()


def desenho_pinos_release(num, slug):
    return mapa_de_pinos(num, slug, 'release')


def desenho_pinos_alpha(num, slug):
    return mapa_de_pinos(num, slug, 'alpha')


def desenho_pinos_air(num, slug):
    return mapa_de_pinos(num, slug, 'air')


def caixa_de_texto(s, x, y, w, itens, cls='dg-caixa', raio=6, centro=None, altura=None):
    """A box at x, `w` wide, whose text is broken to fit inside 12 px of margin on each side: `itens` is [(text, class)], each text broken by quebrar( ). The height is the
    lines' (8 px, 16 px a line, 8 px), or `altura` when that is more (two boxes side by side keep one height). `y` is the top, or None with `centro` the height the box is centred
    on. Returns (y, height)."""
    linhas = []
    for texto, c in itens:
        tam = 12 if 'dg-n' in c.split() else 13
        if isinstance(texto, str):
            linhas += [(l, c) for l in quebrar(texto, w - 24, tam, 'dg-m' in c.split())]
        else:                       # runs, in one line: the code's font for a command in the middle of a sentence
            if sum(largura_texto(tx, tam, 'dg-m' in cr.split() or 'dg-m' in c.split()) for cr, tx in texto) > w - 24:
                raise ValueError(f'{s.slug}: the line {"".join(tx for _, tx in texto)!r} does not fit in a box of {w} px')
            linhas.append((texto, c))
    h = max(16 + 16 * len(linhas), altura or 0)
    if y is None:
        y = centro - h // 2
    s.caixa(x, y, w, h, cls, raio)
    for k, (l, c) in enumerate(linhas):
        s.texto(x + 12, y + 20 + 16 * k, l, c)
    return y, h


def desenho_air_energia(num, slug):
    num.carregador()
    g16, g17 = num.macro('AIR_SENSOR_POWER_PIN', 'air'), num.macro('AIR_CHARGER_PIN', 'air')
    s = Svg(slug, 352)
    y16, y17 = 96, 256
    s.caixa(24, 16, 152, 304, 'dg-caixa-2', 12)
    s.texto(40, 36, 'Pico W', 'dg-t dg-b')
    s.texto(160, y16 + 4, gp_e_pino(g16), 'dg-t', 'end')
    s.texto(160, y16 + 20, 'saída', 'dg-n dg-c2', 'end')
    s.texto(160, y17 + 4, gp_e_pino(g17), 'dg-t', 'end')
    s.texto(160, y17 + 20, 'entrada, com pull-down', 'dg-n dg-c2', 'end')
    # the switch the GP16 drives, and the bus it feeds
    y_chave, _ = caixa_de_texto(s, 256, None, 192, [('chave do lado positivo', 'dg-t dg-b'), ('MOSFET de canal P com acionamento, ou pino EN de um regulador de 3,3 V', 'dg-n dg-c2')],
                                'dg-caixa', 6, y16)
    caixa_de_texto(s, 544, None, 152, [('barramento de 3,3 V dos sensores', 'dg-t'), ('DS18B20, DHT22, BME280', 'dg-n dg-c2')], 'dg-caixa-2', 6, y16)
    s.seta([(176, y16), (256, y16)], 'dg-fio-acento', 'dg-ponta-acento')
    s.no(176, y16, 'dg-no-acento')
    s.texto(192, y16 - 8, 'controle', 'dg-n dg-c2')
    s.seta([(448, y16), (544, y16)])
    s.texto(460, y16 - 8, '3,3 V', 'dg-n dg-c2')
    s.seta([(352, 24), (352, y_chave)])
    s.texto(364, 40, f'entrada: 3V3 (pino {PINO_3V3}), ou a alimentação do regulador', 'dg-n dg-c2')
    s.texto(256, 176, [('dg-m', f'GP{g16}'), ('', ' alto = acordado: a chave liga os sensores')], 'dg-t')
    s.texto(256, 192, [('dg-m', f'GP{g16}'), ('', ' baixo = dormindo: os sensores ficam sem energia')], 'dg-t')
    # the charger, and the divider that brings its 5 V to the GP17
    caixa_de_texto(s, 544, None, 152, [('carregador de 5 V', 'dg-t')], 'dg-caixa-2', 6, y17)
    s.seta([(464, y17), (176, y17)], 'dg-fio-acento', 'dg-ponta-acento')
    s.linha([(544, y17), (496, y17)])
    s.caixa(464, y17 - 8, 32, 16, 'dg-comp', 6, 2)
    s.texto(480, y17 - 16, 'R1', 'dg-n dg-m dg-c2', 'middle')
    s.no(376, y17, 'dg-no-acento')
    s.linha([(376, y17), (376, y17 + 16)])
    s.caixa(368, y17 + 16, 16, 32, 'dg-comp', 6, 2)
    s.texto(392, y17 + 36, 'R2', 'dg-n dg-m dg-c2')
    s.linha([(376, y17 + 48), (376, y17 + 64)])
    for k, meia in enumerate((16, 12, 8)):
        s.linha([(376 - meia, y17 + 64 + 4 * k), (376 + meia, y17 + 64 + 4 * k)])
    s.texto(396, y17 + 76, 'GND', 'dg-n dg-c2')
    for k, l in enumerate(quebrar('o divisor leva os 5 V a 3,3 V: GP17 alto = carregador ligado', 168, 12)):
        s.texto(192, y17 + 28 + 16 * k, l, 'dg-n dg-c2')
    return s.render()


class Cx(NamedTuple):
    """A box drawn: its corner and size, and the points on its sides where a wire meets it (the middle of the side, which is on the grid when the width is a multiple of 8 and
    the height is 16 + 16 lines)."""
    x: int
    y: int
    w: int
    h: int

    def esq(self, dy=0):
        return (self.x, self.y + self.h // 2 + dy)

    def dir(self, dy=0):
        return (self.x + self.w, self.y + self.h // 2 + dy)

    def topo(self, dx=0):
        return (self.x + self.w // 2 + dx, self.y)

    def base(self, dx=0):
        return (self.x + self.w // 2 + dx, self.y + self.h)


def bloco(s, x, y, w, itens, cls='dg-caixa', raio=6, centro=None, altura=None):
    """caixa_de_texto( ) that returns the box as a Cx."""
    y, h = caixa_de_texto(s, x, y, w, itens, cls, raio, centro, altura)
    return Cx(x, y, w, h)


def desenho_arquivos(num, slug):
    imagens = [nome for nome, _ in num.imagens()]
    idiomas, manifesto, soma = num.publicados()
    s = Svg(slug, 392)
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    # the images: one column for each, a row for the .uf2 and one for the .bin, none of them coloured: the column says which image, the arrow where the file goes
    largura, passo = 88, 96
    direita = 16 + passo * (len(imagens) - 1) + largura          # where the files end and the arrows start
    for i, nome in enumerate(imagens):
        s.caixa(16 + passo * i, 16, largura, 24, 'dg-caixa-2', 6)
        s.texto(16 + passo * i + largura // 2, 32, nome, 'dg-t', 'middle')
    for y, ext, titulo, nota in ((48, '.uf2', 'cabo USB, com o BOOTSEL', 'aparelho novo ou recuperação'),
                                 (104, '.bin', 'página Arquivos', 'atualização pela rede')):
        for i in range(len(imagens)):
            s.caixa(16 + passo * i, y, largura, 48, 'dg-caixa', 6)
            s.texto(16 + passo * i + largura // 2, y + 28, ext, 'dg-n dg-m', 'middle')
        d = bloco(s, 464, y, 240, [(titulo, 'dg-t'), (nota, 'dg-n dg-c2')])
        s.seta([(direita, d.esq()[1]), d.esq()], *ponta)
    # the packs, the manifest and the checksums: one chip each, ending where the images' files end, each beside its destination
    for y, nomes, titulo, nota in ((168, idiomas, 'página Arquivos', 'pasta /lang, o pacote de idioma'),
                                   (232, [manifesto], 'gestor de frota', 'soma SHA-256 de cada imagem'),
                                   (296, [soma], 'conferência do download', f'sha256sum -c {soma}')):
        d = bloco(s, 464, y, 240, [(titulo, 'dg-t'), (nota, 'dg-n dg-c2')])
        chip = min(184, (direita - 16 - 8 * (len(nomes) - 1)) // len(nomes) // 8 * 8)      # 184 wide, or as wide as the images' block allows; a multiple of 8 for the centre
        for k, nome in enumerate(nomes):
            if largura_texto(nome, 12, True) > chip - 8:
                raise ValueError(f'{slug}: {nome} does not fit in its chip of {chip} px')
            x = direita - chip - (chip + 8) * (len(nomes) - 1 - k)
            s.caixa(x, d.esq()[1] - 16, chip, 32, 'dg-caixa', 6)
            s.texto(x + chip // 2, d.esq()[1] + 4, nome, 'dg-n dg-m', 'middle')
        s.seta([(direita, d.esq()[1]), d.esq()], *ponta)
    s.texto(16, 376, [('', 'Cada imagem tem o nome '), ('dg-m', 'simut_v<versão>_<imagem>.uf2'), ('', ' e o mesmo com '), ('dg-m', '.bin'), ('', '.')], 'dg-n dg-c2')
    return s.render()


def desenho_primeiro_boot(num, slug):
    ap, cmd = num.ap(), num.comandos()
    s = Svg(slug)
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    # the first three steps, in a row: the console, the password, the panel's question (the release's only)
    passos = ([('Liga o aparelho com o console USB aberto', 'dg-t')],
              [('Anota a senha do admin', 'dg-t'), ('ela aparece uma única vez', 'dg-n dg-c2')],
              [('O painel pergunta a data e a hora', 'dg-t'), ('só na release: SALVAR ou PULAR', 'dg-n dg-c2')])
    caixas = [bloco(s, x, None, w, itens, 'dg-caixa', 6, 48) for (x, w), itens in zip(((16, 176), (224, 192), (448, 256)), passos)]
    for a, b in zip(caixas, caixas[1:]):
        s.seta([a.dir(), b.esq()], *ponta)
    # two ways to put the device on the network: the access point (release and alpha), the console (the Air)
    s.linha([caixas[2].base(), (caixas[2].base()[0], 96), (184, 96)])
    s.seta([(544, 96), (544, 112)], *ponta)
    s.no(544, 96)
    s.seta([(184, 96), (184, 112)], *ponta)
    esquerda = [
        [([('', 'Comando '), ('dg-m', cmd['ap']), ('', ' no console')], 'dg-t'), (f'ou, na release, o item {ap["item"]} do menu do painel', 'dg-n dg-c2')],
        [([('', 'Conecta na rede '), ('dg-m', ap['ssid'])], 'dg-t'), ('no celular ou no computador', 'dg-n dg-c2')],
        [([('', 'Abre '), ('dg-m', ap['ip'])], 'dg-t'), ('no navegador', 'dg-n dg-c2')],
        [([('', 'Entra como '), ('dg-m', 'admin'), ('', ' e troca a senha')], 'dg-t')],
        [('Página Rede', 'dg-t'), ('Buscar, escolher a rede e digitar a senha', 'dg-n dg-c2')],
        [('Salvar e reiniciar', 'dg-t')],
    ]
    direita = [[(cmd['enable'], 'dg-t dg-m')], [(cmd['configure'], 'dg-t dg-m')], [(f'{cmd["ssid"]} <rede>', 'dg-t dg-m')], [(f'{cmd["pass"]} <senha>', 'dg-t dg-m')],
               [(cmd['reload'], 'dg-t dg-m')]]
    altura = lambda itens: 16 + 16 * sum(1 if not isinstance(tx, str) else len(quebrar(tx, 280, 12 if 'dg-n' in c.split() else 13, 'dg-m' in c.split())) for tx, c in itens)
    alt_e = sum(altura(i) for i in esquerda) + 16 * (len(esquerda) - 1)
    folga = alt_e - sum(altura(i) for i in direita)
    if folga % (4 * (len(direita) - 1)):
        raise ValueError(f'{slug}: the Air\'s chain cannot be spread over the access point\'s height ({folga} px over {len(direita) - 1} gaps, on the 4 px grid)')
    topo, base = 152, 152 + alt_e
    s.caixa(16, 112, 336, base + 16 - 112, 'dg-caixa-2', 12)
    s.caixa(384, 112, 320, base + 16 - 112, 'dg-caixa-2', 12)
    s.texto(32, 136, 'release e alpha: pelo ponto de acesso', 'dg-t dg-b')
    s.texto(400, 136, 'Air: pelo console', 'dg-t dg-b')
    for x, w, cadeia, vao in ((32, 304, esquerda, 16), (400, 288, direita, folga // (len(direita) - 1))):
        y, anterior = topo, None
        for itens in cadeia:
            b = bloco(s, x, y, w, itens)
            if anterior:
                s.seta([anterior.base(), b.topo()], *ponta)
            anterior, y = b, y + b.h + vao
    # they meet at the device on the network, and the rest of the way is the same
    s.linha([(184, base + 16), (184, base + 32), (360, base + 32)])
    s.linha([(544, base + 16), (544, base + 32), (360, base + 32)])
    s.no(360, base + 32)
    rede = bloco(s, 256, base + 48, 208, [('Aparelho na rede', 'dg-t')])
    s.seta([(360, base + 32), rede.topo()], *ponta)
    fim = [[('Pacote de idioma e reinício', 'dg-t')], [('Fuso e hora', 'dg-t')], [('Primeiro sensor', 'dg-t')], [('PIN do painel', 'dg-t'), ('só na release', 'dg-n dg-c2')],
           [('Pronto para usar', 'dg-t dg-b')]]
    centro = rede.y + rede.h + 16 + 16 + 32
    ultima = None
    primeira = None
    for i, itens in enumerate(fim):
        b = bloco(s, 20 + 140 * i, None, 120, itens, 'dg-caixa', 6, centro)
        if primeira is None:
            primeira = b
        if ultima:
            s.seta([ultima.dir(), b.esq()], *ponta)
        ultima = b
    s.linha([rede.base(), (360, rede.y + rede.h + 16), (primeira.topo()[0], rede.y + rede.h + 16)])
    s.seta([(primeira.topo()[0], rede.y + rede.h + 16), primeira.topo()], *ponta)
    return s.render()


def desenho_caminho_historico(num, slug):
    h = num.historico()
    s = Svg(slug)
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    c = 80                                  # the height of the record's way in; the rest is placed from it
    # the record's way in: the sensors, the record at each Intervalo Histórico, the open block in the RAM
    sensores = bloco(s, 16, None, 96, [('Sensores', 'dg-t')], 'dg-caixa', 6, c)
    registro = bloco(s, 136, None, 144, [('registro a cada Intervalo Histórico', 'dg-t')], 'dg-caixa', 6, c)
    aberto = bloco(s, 296, None, 144, [('bloco aberto', 'dg-t dg-b'), (f'na RAM, até {h["registros"]} registros', 'dg-n dg-c2')], 'dg-caixa', 6, c)
    s.seta([sensores.dir(), registro.esq()], *ponta)
    s.seta([registro.dir(), aberto.esq()], *ponta)
    # the copy at each record, dashed: the snapshot that a power cut leaves
    wip = bloco(s, 296, c + 88, 144, [(f'{h["pasta"]}{h["wip"]}', 'dg-t dg-m')])
    s.seta([aberto.base(), wip.topo()], 'dg-fio-tracejado', 'dg-ponta')
    s.texto(aberto.base()[0] - 8, c + 68, 'cópia a cada registro', 'dg-n dg-c2', 'end')
    # the seal: the block goes to the day's file, a stack of blocks that each carry a CRC and the minimum and maximum of every channel
    s.texto(704, c - 56, f'{h["pasta"]}/AAAAMMDD{h["ext"]}', 'dg-n dg-m', 'end')
    s.caixa(568, c - 48, 136, 152, 'dg-caixa-2', 12)
    for k in range(3):
        y = c - 40 + 48 * k
        s.caixa(576, y, 120, 40, 'dg-caixa', 6)
        s.texto(588, y + 24, 'CRC', 'dg-n dg-m')
        s.texto(684, y + 24, 'mín/máx', 'dg-n dg-m', 'end')
    s.seta([aberto.dir(), (568, c)], *ponta)
    for k, linha in enumerate(('selar:', f'{h["registros"]} registros,', 'virada do dia,', 'correção do NTP')):
        s.texto(456, c + 24 + 16 * k, linha, 'dg-n dg-c2')
    # the day files, oldest to today: the oldest goes when the file system passes the limit, today's never does
    fila = c + 184
    s.texto(64, fila - 16, 'arquivos diários, do mais antigo ao de hoje', 'dg-n dg-c2')
    for k in range(5):
        x = 64 + 128 * k
        s.caixa(x, fila, 120, 32, 'dg-caixa-apagada' if k == 0 else 'dg-caixa', 6)
        s.texto(x + 60, fila + 20, f'AAAAMMDD{h["ext"]}', 'dg-n dg-m' + (' dg-c2' if k == 0 else ''), 'middle')
    s.texto(64, fila + 52, f'o mais antigo: apagado acima de {h["limite"]}\u00a0%', 'dg-n dg-c2')
    s.texto(696, fila + 52, 'hoje: nunca é apagado', 'dg-n dg-c2', 'end')
    s.linha([(636, c + 104), (636, fila)])
    s.texto(628, c + 152, 'o arquivo de hoje', 'dg-n dg-c2', 'end')
    return s.render()


def desenho_cursor(num, slug):
    tel = num.telemetria()
    s = Svg(slug)
    ok, perigo = ('dg-fio-ok', 'dg-ponta-ok'), ('dg-fio-perigo', 'dg-ponta-perigo')
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    confirmados, pendentes, na_ram = 8, 8, 4
    x_cursor = 16 + 20 * confirmados + 4                  # between the last confirmed record and the first pending one
    x_pend = x_cursor + 8
    x_ram = x_pend + 20 * pendentes + 4                   # the hour still open in the RAM: a box of its own, at the end of the queue
    x_dir = x_ram + 20 * na_ram + 8                       # the box's right edge
    antes, depois = 64, 176                               # the two rows of squares: before the answer and after a 2xx
    for y, quadrado in ((antes, 'dg-caixa-aviso'), (depois, 'dg-caixa-2')):
        for i in range(confirmados):
            s.caixa(16 + 20 * i, y, 16, 16, 'dg-caixa-2', 0)
        s.caixa(x_ram - 8, y - 8, 20 * na_ram + 16, 32, 'dg-caixa', 6)
        for i in range(pendentes):
            s.caixa(x_pend + 20 * i, y, 16, 16, quadrado, 0)
        for i in range(na_ram):
            s.caixa(x_ram + 20 * i, y, 16, 16, quadrado, 0)
    s.texto(16, 28, 'antes', 'dg-n dg-b')
    s.texto(16, 140, 'depois de um ' + tel['ok'], 'dg-n dg-b')
    s.texto(16, 48, 'confirmados', 'dg-n dg-c2')
    s.texto(x_pend + 28, 48, 'pendentes', 'dg-n dg-c2')
    s.texto(x_ram + 40, 48, 'hora aberta, na RAM', 'dg-n dg-c2', 'middle')
    s.linha([(x_cursor, 56), (x_cursor, 88)], 'dg-fio-acento')
    s.texto(x_cursor, 48, 'cursor', 'dg-t dg-b', 'middle')
    s.linha([(x_dir - 4, 168), (x_dir - 4, 200)], 'dg-fio-acento')
    s.texto(x_dir - 4, 160, 'cursor', 'dg-t dg-b', 'middle')
    # the batch: the first pending records, up to the Lote máximo, with the hour that is still open
    s.linha([(x_pend, 100), (x_pend, 104), (x_dir, 104), (x_dir, 100)], 'dg-fio-fino')
    s.texto((x_pend + x_dir) // 8 * 4, 124, 'lote (até o Lote máximo)', 'dg-t', 'middle')
    coletor = bloco(s, 544, None, 160, [('coletor', 'dg-t dg-b')], 'dg-caixa-2', 6, 120)
    s.seta([(x_dir, 72), (504, 72), (504, 120), coletor.esq()], *ponta)
    # the answer: a 2xx moves the cursor to the end of the batch; anything else leaves it, and the device waits and sends the same stretch again
    s.seta([(coletor.x, 128), (520, 128), (520, 184), (x_dir + 4, 184)], *ok)
    s.texto(512, 160, tel['ok'], 'dg-t dg-ok dg-b', 'end')
    s.seta([coletor.base(), (624, 216)], *perigo)
    s.texto(636, 184, 'falha', 'dg-t dg-perigo dg-b')
    bloco(s, 528, 216, 176, [('espera e reenvia', 'dg-t dg-b'), (f'{tel["espera_min"]}\u00a0s, dobrando até {tel["espera_max"]}\u00a0s', 'dg-n dg-c2'),
                             ('o cursor não se move', 'dg-n dg-c2')], 'dg-caixa', 6)
    s.texto(16, 264, 'Cada quadrado é um registro do histórico, na ordem em que foi gravado.', 'dg-n dg-c2')
    return s.render()


def desenho_linha_alarmes(num, slug):
    a, ok_http = num.alarmes(), num.telemetria()['ok']
    s = Svg(slug)
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    ok, perigo = ('dg-fio-ok', 'dg-ponta-ok'), ('dg-fio-perigo', 'dg-ponta-perigo')
    segundos = lambda n: f'{n}\u00a0s'
    # the queue in the RAM: the same for both transports, three records waiting
    s.caixa(16, 96, 128, 232, 'dg-caixa-2', 12)
    s.texto(32, 120, 'fila na RAM', 'dg-t dg-b')
    for k in range(3):
        s.caixa(32, 168 + 32 * k, 96, 24, 'dg-caixa', 6)
        s.texto(80, 184 + 32 * k, f'seq {k + 1}', 'dg-n dg-m', 'middle')
    # the HTTP lane: the whole queue in one POST, and the collector's answer
    s.caixa(168, 16, 536, 160, 'dg-caixa-2', 12)
    s.texto(184, 40, 'HTTP', 'dg-t dg-b')
    s.caixa(568, 80, 120, 88, 'dg-caixa', 6)
    s.texto(628, 100, 'coletor', 'dg-t dg-b', 'middle')
    s.seta([(144, 112), (568, 112)], *ponta)
    s.texto(184, 104, 'POST com a fila inteira', 'dg-t')
    s.seta([(568, 136), (144, 136)], *ok)
    s.texto(184, 128, f'{ok_http}: os três registros saem da fila', 'dg-t dg-ok')
    s.seta([(568, 160), (144, 160)], *perigo)
    s.texto(184, 152, f'outro código ou nada em {segundos(a["espera"])}: a fila fica, novo envio em {segundos(a["reenvio"])}', 'dg-t dg-perigo')
    # the MQTT lane: the publish confirms nothing; only the collector's message on the ack topic takes the records out
    s.caixa(168, 192, 536, 208, 'dg-caixa-2', 12)
    s.texto(184, 216, 'MQTT', 'dg-t dg-b')
    broker = bloco(s, 360, 240, 104, [('broker', 'dg-t dg-b')])
    coletor = bloco(s, 568, 240, 120, [('coletor', 'dg-t dg-b'), ('grava e publica a confirmação', 'dg-n dg-c2')])
    s.seta([(144, 256), broker.esq()], *ponta)
    s.texto(184, 248, [('', 'publish em '), ('dg-m', f'<tópico>{a["topico"]}')], 'dg-n')
    s.seta([broker.dir(), (coletor.x, 256)], *ponta)
    s.seta([(coletor.x, 288), (broker.x + 72, 288), (broker.x + 72, broker.y + broker.h)], *ok)
    s.seta([(broker.x + 32, broker.y + broker.h), (broker.x + 32, 312), (144, 312)], *ok)
    s.texto(184, 332, [('', 'seq 1, 2 e 3 em '), ('dg-m', f'<tópico>{a["ack"]}')], 'dg-n')
    s.texto(184, 348, 'o aparelho tira os três da fila', 'dg-n dg-c2')
    s.texto(184, 384, f'Sem a confirmação, a fila inteira é publicada de novo a cada {segundos(a["reenvio"])}.', 'dg-t')
    return s.render()


def mono(*partes):
    """The runs of a line that is all code: a topic, a header, a payload."""
    return [('dg-m', ''.join(partes))]


def rotulo(s, x, y, itens, limite, cls='dg-n'):
    """The lines of a label, 16 px apart from the baseline y, in 12 px (`cls`, dg-n and a colour class if it has one): each is a string or a list of (class, text) runs. A line that would
    reach `limite` (the next line of the drawing: a lifeline, a box) is an error, as the builder's own guard is for a text that overlaps another."""
    for k, item in enumerate(itens):
        runs = [('', item)] if isinstance(item, str) else item
        w = sum(largura_texto(t, 12, c == 'dg-m') for c, t in runs)
        if x + w > limite:
            raise ValueError(f'{s.slug}: the line {"".join(t for _, t in runs)!r} is about {w} px wide from x={x}, past {limite}')
        s.texto(x, y + 16 * k, runs, cls)


class Sequencia:
    """A sequence between the client, at the left, and the device, at the right, as the figures of chapter 26 draw it: a header and a thin line down from each, and the rows in the order they
    happen. A request is a solid arrow to the device and an answer a dashed one back, each with its label above (12 px: the lines are strings or runs, the first usually in the code's font);
    a step of the chapter is an accent square on the client's line; a note is a box beside a line, the client's to its right and the device's to its left. The rows are kept and drawn at the
    end, when the lines' length is known, so that the lines go under everything."""
    X_C, X_A = 92, 612

    def __init__(self, s, cliente, aparelho):
        self.s, self.cliente, self.aparelho = s, cliente, aparelho
        self.linhas, self.y = [], 64                      # the rows start 16 px under the headers (a header is 32 px, from y=16)

    def _mensagem(self, tipo, itens, numero):
        y_seta = self.y + 16 * len(itens) + 8             # the labels' lines, then the arrow 12 px under the last baseline, then 12 px to the next row
        self.linhas.append((tipo, itens, numero, self.y, y_seta))
        self.y = y_seta + 12

    def pedido(self, itens, numero=None):
        self._mensagem('pedido', itens, numero)

    def resposta(self, itens, numero=None):
        self._mensagem('resposta', itens, numero)

    def nota(self, lado, texto, numero=None):
        w = -(-(largura_texto(texto, 12) + 24) // 8) * 8
        self.linhas.append(('nota', (lado, texto, w), numero, self.y, self.y + 16))
        self.y += 44

    def _marca(self, y, numero):
        self.s.caixa(self.X_C - 8, y - 8, 16, 16, 'dg-caixa-acento', 6)
        self.s.texto(self.X_C, y + 4, str(numero), 'dg-n dg-b dg-sobre-acento', 'middle')

    def render(self):
        s = self.s
        cabecalho = bloco(s, self.X_C - 60, 16, 120, [(self.cliente, 'dg-t dg-b')])
        bloco(s, self.X_A - 80, 16, 160, [(self.aparelho, 'dg-t')])
        for x in (self.X_C, self.X_A):
            s.linha([(x, cabecalho.y + cabecalho.h), (x, self.y)], 'dg-fio-fino')
        for tipo, dados, numero, y, y_seta in self.linhas:
            if tipo == 'nota':
                lado, texto, w = dados
                bloco(s, self.X_C + 16 if lado == 'cliente' else self.X_A - 16 - w, y, w, [(texto, 'dg-n')], 'dg-caixa-2', 6)
                if numero:
                    self._marca(y_seta, numero)
                continue
            rotulo(s, self.X_C + 24, y + 12, dados, self.X_A - 8)
            ponta = self.X_C + (8 if numero else 0)
            if tipo == 'pedido':
                s.seta([(ponta, y_seta), (self.X_A, y_seta)], 'dg-fio', 'dg-ponta')
            else:
                s.seta([(self.X_A, y_seta), (ponta, y_seta)], 'dg-fio-tracejado', 'dg-ponta')
            if numero:
                self._marca(y_seta, numero)
        return s.render()


def desenho_home_assistant(num, slug):
    ha = num.homeassistant()
    s = Svg(slug)
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    x_s, x_b, x_h = 76, 460, 644          # the three lifelines, each under the middle of its header; the first gap is wide enough for the discovery topic, in 12 px mono
    configuracao = f'{ha["prefixo"]}/{ha["componente"]}/{NO_EXEMPLO}/<objeto>/{ha["fim"]}'
    registro = f'{ha["dados"]} {{{ha["ts"]}, {ha["t"]}{HWID_EXEMPLO}, ...}}'
    disponibilidade = lambda palavra: f'{ha["estado"]} {{{ha["chave"]}: {palavra}}}'
    # the three actors: a header each, 48 px, and a thin line down from it
    for x, w, titulo, sub in ((x_s - 60, 120, 'SIMUT', NO_EXEMPLO), (x_b - 112, 224, 'Broker MQTT', f'{BROKER_EXEMPLO}:{PORTA_EXEMPLO}'), (x_h - 60, 120, 'Home Assistant', None)):
        s.caixa(x, 16, w, 48, 'dg-caixa', 6)
        if sub is None:
            s.texto(x + 12, 44, titulo, 'dg-t dg-b')
        else:
            if largura_texto(sub, 12, True) > w - 24:
                raise ValueError(f'{slug}: {sub} does not fit in a header of {w} px')
            s.texto(x + 12, 36, titulo, 'dg-t dg-b')
            s.texto(x + 12, 52, sub, 'dg-n dg-m')
    filas = ((1, ['ao conectar (retida, uma por medição):', mono(configuracao)],
              [[('', 'assina '), ('dg-m', f'{ha["prefixo"]}/#')], 'e cria dispositivo', 'e entidades']),
             (2, [[('dg-m', disponibilidade(ha['online'])), ('', ' (retida)')]],
              ['lê a disponibilidade em', mono(ha['estado'])]),
             (3, [[('dg-m', registro), ('', ' a cada envio')]],
              ['lê os valores em', mono(ha['dados'])]))
    # the rows, from the top: the labels above an arrow, the arrow 8 px below the last line, 16 px to the next row; then the row of the device that vanishes
    y_setas, y = [], 80
    for _, esquerda, direita in filas:
        y_setas.append(y + 16 * max(len(esquerda), len(direita)) + 8)
        y = y_setas[-1] + 16
    y_some = y + 8
    s.linha([(x_s, 64), (x_s, y_some + 12)], 'dg-fio-fino')                 # the device's line ends where it vanishes
    for x in (x_b, x_h):
        s.linha([(x, 64), (x, y_some + 48)], 'dg-fio-fino')

    # the device publishes, and Home Assistant reads what the broker holds: the two arrows of a row are at the same height, the message passing through the broker
    for (numero, esquerda, direita), y_seta in zip(filas, y_setas):
        rotulo(s, x_s + 12, y_seta - 12 - 16 * (len(esquerda) - 1), esquerda, x_b - 4)
        rotulo(s, x_b + 12, y_seta - 12 - 16 * (len(direita) - 1), direita, x_h - 4)
        s.seta([(x_s + 8, y_seta), (x_b, y_seta)], *ponta)
        s.caixa(x_s - 8, y_seta - 8, 16, 16, 'dg-caixa-acento', 6)
        s.texto(x_s, y_seta + 4, str(numero), 'dg-n dg-b dg-sobre-acento', 'middle')
        s.seta([(x_b, y_seta), (x_h, y_seta)], *ponta)
    # the device vanishes: its line ends in a cross, and the broker publishes the will it was left with
    s.linha([(x_s - 4, y_some + 8), (x_s + 4, y_some + 16)], 'dg-fio')
    s.linha([(x_s + 4, y_some + 8), (x_s - 4, y_some + 16)], 'dg-fio')
    s.no(x_b, y_some + 8)
    s.seta([(x_b, y_some + 8), (x_b - 32, y_some + 8), (x_b - 32, y_some + 32), (x_b, y_some + 32)], 'dg-fio-tracejado', 'dg-ponta')
    rotulo(s, x_s + 12, y_some + 12, ['se o aparelho some (última vontade, retida):', mono(disponibilidade(ha['offline']))], x_b - 36)
    s.texto(16, y_some + 72, 'O Home Assistant não manda nada ao aparelho.', 'dg-n dg-c2')
    return s.render()


def desenho_coleta(num, slug):
    p = num.prometheus()
    s = Svg(slug)
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    ok, perigo = ('dg-fio-ok', 'dg-ponta-ok'), ('dg-fio-perigo', 'dg-ponta-perigo')
    x_c, x_e, l_e = 424, 440, 248                     # the device's container starts at x_c; the steps inside it start at x_e, l_e wide
    x_m = x_e + l_e // 2                              # the line the steps hang on
    x_p = 92                                          # Prometheus' line, under the middle of its header
    prometheus = bloco(s, 16, 32, 152, [('Prometheus', 'dg-t dg-b'), (f'a cada {INTERVALO_PROMETHEUS}\u00a0s', 'dg-n dg-c2')])
    passos = ((p['bloqueado'], 'endereço bloqueado?', None, 'endereço bloqueado', False),
              (p['negado'], 'confere a senha', f'{p["rodadas"]} rodadas, cerca de {p["tempo"]}\u00a0s', 'senha errada', False),
              (p['proibido'], f'a conta tem {PERMISSAO_PAINEL}?', None, f'conta sem {PERMISSAO_PAINEL}', False),
              (p['ok'], 'monta o texto das métricas', None, 'texto do Prometheus', True))
    # the container is drawn before the steps it holds: its height is the steps' (four boxes, 24 px between them, 16 px of margin), worked out here
    alturas = [32 + (16 if nota else 0) for _, _, nota, _, _ in passos]
    y_primeiro = 96
    fim = y_primeiro + sum(alturas) + 24 * (len(passos) - 1) + 16
    s.linha([(x_p, prometheus.y + prometheus.h), (x_p, fim)], 'dg-fio-fino')
    s.caixa(x_c, 16, 704 - x_c, fim - 16, 'dg-caixa-2', 12)
    s.texto(x_e, 36, [('dg-b', 'SIMUT'), ('', ' '), ('dg-m', IP_EXEMPLO)], 'dg-t')
    # the request goes in at the first question, and the device asks in the order the route does: the lock first (the password is not even checked), then the password, the permission, the text
    s.seta([(prometheus.x + prometheus.w, 56), (x_m, 56), (x_m, y_primeiro)], *ponta)
    rotulo(s, prometheus.x + prometheus.w + 16, 28, [mono(f'{p["metodo"]} {p["rota"]}'), mono(f'{p["cabecalho"]}: {p["esquema"]} {CONTA_EXEMPLO}:…')], x_c - 4)
    y, caixas = y_primeiro, []
    for _, titulo, nota, _, _ in passos:
        c = bloco(s, x_e, y, l_e, [(titulo, 'dg-t')] + ([(nota, 'dg-n dg-c2')] if nota else []))
        caixas.append(c)
        y = c.y + c.h + 24
    for k, passagem in enumerate(('não', 'senha certa', 'sim')):
        de, para = caixas[k], caixas[k + 1]
        s.seta([de.base(), para.topo()], *ponta)
        s.texto(x_m + 12, de.y + de.h + 16, passagem, 'dg-n dg-c2')
    # the four answers, back to Prometheus: each leaves the step that gives it, at the step's height
    for c, (codigo, _, _, significado, final) in zip(caixas, passos):
        y_resposta = c.y + c.h // 2
        s.seta([(x_e, y_resposta), (x_p, y_resposta)], *(ok if final else perigo))
        s.texto(x_p + 16, y_resposta - 8, [('dg-b', codigo), ('', ' ' + significado)], 'dg-t dg-ok' if final else 'dg-t dg-perigo')
    # the lockout a wrong password raises: one rung for each, the heights on a log scale (each rung doubles the wait), the last one the ceiling
    escada = p['escada']
    passo = min(48, 448 // len(escada) // GRADE * GRADE)
    alturas_barras = [max(GRADE, math.ceil(math.log2(max(seg, 2)) * 8 / GRADE) * GRADE) for seg in escada]
    y_titulo = fim + 36
    y_base = y_titulo + 32 + max(alturas_barras)
    s.texto(16, y_titulo, 'Bloqueio do endereço, a cada senha errada', 'dg-t dg-b')
    s.linha([(16, y_base), (16 + passo * len(escada) - 8, y_base)], 'dg-fio-fino')
    for k, (segundos, altura) in enumerate(zip(escada, alturas_barras)):
        x = 16 + passo * k
        s.caixa(x, y_base - altura, passo - 8, altura, 'dg-caixa-2', 0)
        s.texto(x + (passo - 8) // 2, y_base - altura - 8, f'{segundos}\u00a0s', 'dg-n', 'middle')
        s.texto(x + (passo - 8) // 2, y_base + 16, f'{k + 1}º', 'dg-n dg-c2', 'middle')
    s.texto(16 + passo * (len(escada) - 1) + (passo - 8) // 2, y_base + 32, 'em diante', 'dg-n dg-c2', 'middle')
    rotulo(s, 16 + passo * len(escada) + 16, y_titulo + 48, ['Mesmo bloqueio da página de entrada.', 'Cada senha errada sobe um degrau:', f'a espera dobra, até {escada[-1]}\u00a0s.'], 716)
    return s.render()


def desenho_syslog(num, slug):
    sl = num.syslog()
    s = Svg(slug)
    ponta = ('dg-fio-acento', 'dg-ponta-acento')
    x_c, l_c = 160, 256                               # the chain of boxes: the bypass and its label are left of it, the losses right of it
    x_m, x_d = x_c + l_c // 2, x_c + l_c
    # the path of a line, from the event to the collector, one box each; the flash log is the other way the same event goes
    evento = bloco(s, x_c, 16, l_c, [('evento', 'dg-t'), ('em qualquer núcleo', 'dg-n dg-c2')])
    flash = bloco(s, x_d + 16, 16, 704 - x_d - 16, [('log de eventos na flash', 'dg-t'), ('filtro de rotina, só código e contexto', 'dg-n dg-c2')])
    s.seta([evento.dir(), (flash.x, evento.dir()[1])], 'dg-fio', 'dg-ponta')
    passos = ([(f'nível ≥ {NIVEL_MINIMO}?', 'dg-t')],
              [(f'fila de {sl["fila"]} linhas na RAM', 'dg-t'), ('a mais antiga sai quando enche', 'dg-n dg-c2')],
              [('laço principal, núcleo 0', 'dg-t'), (f'até {sl["fila"]} linhas por passada, só com Wi-Fi conectado e fora do AP', 'dg-n dg-c2')],
              [([('', 'UDP para '), ('dg-m', f'{IP_COLETOR_EXEMPLO}:{sl["porta"]}')], 'dg-t')],
              [('coletor', 'dg-t'), (', '.join(COLETORES_SYSLOG), 'dg-n dg-c2')])
    caixas = [evento]
    for itens in passos:
        anterior = caixas[-1]
        c = bloco(s, x_c, anterior.y + anterior.h + 24, l_c, itens)
        s.seta([anterior.base(), c.topo()], *ponta)
        caixas.append(c)
    _, nivel, fila, laco, udp, coletor = caixas

    def perda(y, linhas, de_x=None):
        """A red way out of the path at height y: a line from the path to a cross, and what is lost beside it."""
        s.linha([(de_x or x_d, y), (x_d + 20, y)], 'dg-fio-perigo')
        s.linha([(x_d + 16, y - 4), (x_d + 24, y + 4)], 'dg-fio-perigo')
        s.linha([(x_d + 24, y - 4), (x_d + 16, y + 4)], 'dg-fio-perigo')
        rotulo(s, x_d + 36, y + 4 - 8 * (len(linhas) - 1), linhas, 716, 'dg-n dg-perigo')

    perda(nivel.y + nivel.h // 2, ['antes do encaminhamento subir, no boot'])
    perda(fila.y + 16, ['fila cheia'])
    perda(fila.y + 40, ['travamento ou falta de energia'])
    perda(laco.y + laco.h // 2, ['envio falhou'])
    perda(udp.y + udp.h + 12, ['datagrama perdido na rede'], x_m)
    # a requested reboot empties the queue first: the lines skip the loop and go straight out
    y_fila, y_udp, x_b = fila.y + fila.h // 2, udp.y + udp.h // 2, x_c - 16
    s.seta([(x_c, y_fila), (x_b, y_fila), (x_b, y_udp), (x_c, y_udp)], 'dg-fio-tracejado', 'dg-ponta')
    rotulo(s, 16, (y_fila + y_udp) // 8 * 4 - 12, ['reinício pedido', '(quase todos):', 'esvazia a fila', 'antes de reiniciar'], x_b - 8)
    return s.render()


def desenho_integracao(num, slug):
    ig = num.integracao()
    s = Svg(slug)
    x_s, l_s, x_r, l_r = 16, 112, 504, 200               # the device's box, and the peers' boxes; the labels sit between, above their arrows
    x_t = x_s + l_s + 16
    nome = {'air': 'Air'}
    imagens = [nome.get(i, i) for i in ig['imagens_mdns']]
    so_nas = f'só na imagem {imagens[0]}' if len(imagens) == 1 else f'só nas imagens {", ".join(imagens[:-1])} e {imagens[-1]}'
    # the groups, top to bottom: the device opens the connection; the server opens it, at the web interface's port; the announcement, which opens none
    ultimo = 408
    s.caixa(x_s, 16, l_s, ultimo - 16, 'dg-comp-acento', 12, 2)
    s.texto(x_s + 16, 40, 'SIMUT', 'dg-t dg-b')
    cheia = ('dg-fio', 'dg-ponta')

    def grupo(y, titulo):
        if y > 32:
            s.linha([(x_t, y - 24), (704, y - 24)], 'dg-fio-fino')
        s.texto(x_t, y, titulo, 'dg-t dg-b')

    def linha_de(y, runs, par=None, sentido=1, estilo=cheia):
        """One arrow at height y from the device to a peer (sentido 1) or from the peer to the device (-1), its label above it, and the peer's box when `par` names one (the collector's
        box is taller, for its two arrows, and is drawn apart)."""
        rotulo(s, x_t, y - 8, [runs], x_r - 4)
        pontos = [(x_s + l_s, y), (x_r, y)] if sentido == 1 else [(x_r, y), (x_s + l_s, y)]
        s.seta(pontos, *estilo)
        if par:
            bloco(s, x_r, y - 16, l_r, [(par, 'dg-t')])

    grupo(32, 'o aparelho abre a conexão')
    s.caixa(x_r, 40, l_r, 80, 'dg-caixa', 6)
    s.texto(x_r + 12, 84, 'coletor', 'dg-t')
    linha_de(64, [('dg-b', 'Telemetria'), ('', ' (HTTP/HTTPS POST ou MQTT/MQTTS)')])
    linha_de(104, [('dg-b', 'Linha de alarmes'), ('', ' (mesmo transporte, com confirmação)')])
    linha_de(144, [('dg-b', 'Descoberta Home Assistant'), ('', ' (MQTT, mensagens retidas)')], 'broker MQTT')
    linha_de(184, [('dg-b', f'Syslog {ig["rfc"]}'), ('', f' ({ig["protocolo"]} {ig["porta_syslog"]})')], 'coletor de syslog')
    grupo(232, 'o servidor abre a conexão, na porta da interface web')
    linha_de(264, [('dg-b', 'API REST'), ('', ' com sessão')], PAR_GESTAO, -1)
    linha_de(304, [('dg-m', f'{ig["metodo"]} {ig["rota"]}')], PAR_PROMETHEUS, -1)
    grupo(352, 'anúncio local, sem conexão')
    linha_de(392, [('dg-b', 'Anúncio mDNS '), ('dg-m', ig['mdns'])], PAR_REDE_LOCAL, 1, ('dg-fio-tracejado', 'dg-ponta'))
    rotulo(s, x_t, 408, [f'multicast, {so_nas}'], x_r - 4, 'dg-n dg-c2')
    return s.render()


def desenho_arquitetura(num, slug):
    fr = num.frota()
    s = Svg(slug)
    cheia, tracejada = ('dg-fio', 'dg-ponta'), ('dg-fio-tracejado', 'dg-ponta')
    x_p, l_p = 504, 200                                  # the peers' column
    imagens = [{'air': 'Air'}.get(i, i) for i in fr['imagens_mdns']]
    so_nas = f'só na imagem {imagens[0]}' if len(imagens) == 1 else f'só nas imagens {", ".join(imagens[:-1])} e {imagens[-1]}'
    # the local network, and the devices in it as a pile: the front box is the one the arrows meet, the two behind it say there are many
    s.caixa(16, 16, 272, 344, 'dg-caixa-2', 12)
    s.texto(32, 40, PAR_REDE_LOCAL, 'dg-t dg-b')
    for x, y in ((48, 80), (60, 92)):
        s.caixa(x, y, 160, 176, 'dg-caixa', 6)
    s.caixa(72, 104, 160, 176, 'dg-caixa', 6)
    s.texto(84, 124, 'SIMUT', 'dg-t dg-b')
    s.texto(84, 140, 'vários aparelhos,', 'dg-n dg-c2')
    s.texto(84, 156, 'cada um com o seu uid', 'dg-n dg-c2')
    # what each peer does: the collector is pushed to, Prometheus and the manager ask
    y_c, y_p, y_g = 136, 200, 264
    def seta_de(y, sentido, linhas):
        rotulo(s, 300, y - 12 - 16 * (len(linhas) - 1), linhas, x_p - 8)
        s.seta([(232, y), (x_p, y)] if sentido == 1 else [(x_p, y), (232, y)], *cheia)
    seta_de(y_c, 1, ['telemetria e linha de alarmes', mono(fr['cabecalhos'])])
    seta_de(y_p, -1, [mono(f'{fr["metodo"]} {fr["rota"]}'), f'HTTP {fr["esquema_metricas"]}'])
    seta_de(y_g, -1, ['API REST', [('', 'cookie '), ('dg-m', fr['cookie']), ('', f' ou {fr["sessao"]}')]])
    bloco(s, x_p, y_c - 16, l_p, [('coletor', 'dg-t')])
    bloco(s, x_p, y_p - 16, l_p, [(PAR_PROMETHEUS, 'dg-t')])
    s.caixa(x_p, y_g - 16, l_p, 32, 'dg-comp-acento', 6, 2)
    s.texto(x_p + 12, y_g + 4, PAR_GESTOR, 'dg-t')
    # the manager reads the release's manifest at GitHub; the devices announce themselves to the local network
    s.seta([(x_p + 40, y_g + 16), (x_p + 40, y_g + 48)], *cheia)
    rotulo(s, x_p + 52, y_g + 36, [[('', 'lê o '), ('dg-m', fr['manifesto'])]], 716)
    bloco(s, x_p, y_g + 48, l_p, [('GitHub', 'dg-t'), ('ativos da release', 'dg-n dg-c2')])
    s.seta([(120, 280), (120, 360)], *tracejada)
    rotulo(s, 132, 304, ['anúncio mDNS', mono(fr['mdns'])] + quebrar(so_nas, 284 - 132, 12), 284)
    return s.render()


def desenho_ondas(num, slug):
    o = num.ondas()
    s = Svg(slug)
    acento = ('dg-fio-acento', 'dg-ponta-acento')
    x_e, l_e = 16, 344                                  # the waves' column
    x_p, l_p = 392, 312                                 # the sequence's panel
    # the three waves, with a barrier between two: the next one waits for the check
    ondas, y = [], 16
    for k, (nome, texto) in enumerate(ONDAS):
        c = bloco(s, x_e, y, l_e, [(nome, 'dg-t dg-b'), (texto, 'dg-n dg-c2')], 'dg-caixa-2', 12)
        if ondas:
            s.seta([barreira.base(), c.topo()], *acento)
        ondas.append(c)
        y = c.y + c.h + 24
        if k < len(ONDAS) - 1:
            if largura_texto(BARREIRA_ONDAS, 12) > l_e - 16:
                raise ValueError(f'{slug}: the barrier\'s text does not fit in one line of its box, {l_e} px')
            barreira = Cx(x_e, y, l_e, 32)
            s.caixa(barreira.x, barreira.y, barreira.w, barreira.h, 'dg-caixa-apagada', 6)
            s.texto(barreira.x + l_e // 2, barreira.y + 20, BARREIRA_ONDAS, 'dg-n', 'middle')
            s.seta([c.base(), barreira.topo()], *acento)
            y = barreira.y + barreira.h + 24
    # the sequence each device goes through, in every wave: the route of each step, as the code has it
    metodo = lambda chave: f'{o[chave][0]} {o[chave][1]}'
    passos = (('backup', [mono(metodo('backup'))]),
              ('envio', [mono(f'{metodo("restore")}?op={o["stage"]}&{o["commit"]}')]),
              ('aplicação', [mono(metodo('apply'))]),
              ('confirmação', [[('', 'versão nova em '), ('dg-m', o['versao'])]]),
              ('restauração', [mono(f'{metodo("restore")}?op={o["validate"]}'), [('', 'depois '), ('dg-m', f'?op={o["apply_op"]}')]]),
              ('idioma', [[('', 'pacotes em '), ('dg-m', o['idioma']), ('', ', e reinicia')]]))
    y_p = 16
    fim_do_painel = 84 + sum(16 + 16 * len(det) + 8 for _, det in passos) + 4
    s.caixa(x_p, y_p, l_p, fim_do_painel - y_p, 'dg-caixa', 12)
    s.texto(x_p + 12, 40, 'em cada aparelho', 'dg-t dg-b')
    s.texto(x_p + 12, 56, 'a mesma sequência, em todas as ondas', 'dg-n dg-c2')
    y = 84
    for k, (nome, detalhes) in enumerate(passos):
        s.caixa(x_p + 12, y, 16, 16, 'dg-caixa-acento', 6)
        s.texto(x_p + 20, y + 12, str(k + 1), 'dg-n dg-b dg-sobre-acento', 'middle')
        s.texto(x_p + 36, y + 12, nome, 'dg-t dg-b')
        rotulo(s, x_p + 36, y + 28, detalhes, x_p + l_p - 8)
        y += 16 + 16 * len(detalhes) + 8
    # each wave runs it: thin lines from the three waves to one arrow that meets the panel
    meio = ondas[1].y + ondas[1].h // 2
    for c in ondas:
        s.linha([(x_e + l_e, c.y + c.h // 2), (x_e + l_e + 16, c.y + c.h // 2)], 'dg-fio-fino')
    s.linha([(x_e + l_e + 16, ondas[0].y + ondas[0].h // 2), (x_e + l_e + 16, ondas[-1].y + ondas[-1].h // 2)], 'dg-fio-fino')
    s.seta([(x_e + l_e + 16, meio), (x_p, meio)], 'dg-fio', 'dg-ponta')
    return s.render()


def desenho_blocos(num, slug):
    a, fl = num.arquitetura(), num.flash()
    s = Svg(slug)
    imagens_bt = [{'air': 'Air'}.get(i, i) for i in a['bluetooth']]
    # the sensors, on the left, on GP0 to GP15: one box each type (the BME280 and the BMP280 are one family, on the same pair of pins)
    grupos = [[n] for n in a['sensores'] if not n.startswith('BM')] + ([[n for n in a['sensores'] if n.startswith('BM')]] if any(n.startswith('BM') for n in a['sensores']) else [])
    s.texto(16, 36, 'sensores', 'dg-t dg-b')
    s.texto(16, 52, f'GP0 a GP{a["slots"] - 1}', 'dg-n dg-m')
    centros, y = [], 64
    for grupo in grupos:
        c = bloco(s, 16, y, 136, [(' / '.join(grupo), 'dg-t')])
        centros.append(c.y + c.h // 2)
        y = c.y + c.h + 8
    # the chip: Core 0 does nearly everything, Core 1 only draws the screen
    s.caixa(176, 16, 328, 264, 'dg-caixa-2', 12)
    s.texto(192, 36, 'RP2040', 'dg-t dg-b')
    s.caixa(192, 48, 296, 120, 'dg-caixa', 6)
    s.texto(204, 68, 'Core 0', 'dg-t dg-b')
    for k, nome in enumerate(('sensores', 'alarmes', 'histórico', 'rede', 'web', 'console', 'telemetria')):
        x, y_chip = 200 + 96 * (k % 3), 76 + 28 * (k // 3)
        s.caixa(x, y_chip, 88, 24, 'dg-caixa-2', 6)
        s.texto(x + 44, y_chip + 16, nome, 'dg-n', 'middle')
    s.caixa(192, 184, 296, 80, 'dg-caixa', 6)
    s.texto(204, 204, 'Core 1', 'dg-t dg-b')
    for k, (imagem, papel) in enumerate((('release', 'painel TFT e toque'), ('alpha', 'LCD'), ('Air', 'sem uso'))):
        s.texto(204, 220 + 16 * k, [('dg-b', imagem), ('', f': {papel}')], 'dg-n')
    # the sensors' line meets Core 0 at its left edge, across the chip's border
    y_c0 = 108
    for c in centros:
        s.linha([(152, c), (164, c)], 'dg-fio')
    s.linha([(164, centros[0]), (164, centros[-1])], 'dg-fio')
    s.linha([(164, y_c0), (192, y_c0)], 'dg-fio')
    # what comes out of Core 0, on the right: a bus with a branch to each box, the console's included
    saidas = (('navegador', 'interface web'), ('servidor de telemetria', 'HTTP ou MQTT'), ('linha de alarmes', None), ('Home Assistant', None), ('Prometheus', None), ('syslog', None))
    y, centros_d = 16, []
    for nome, nota in saidas:
        c = bloco(s, 536, y, 168, [(nome, 'dg-t')] + ([(nota, 'dg-n dg-c2')] if nota else []))
        centros_d.append(c.y + c.h // 2)
        y = c.y + c.h + 8
    console = bloco(s, 536, y + 8, 168, [('console', 'dg-t'), ('cabo USB', 'dg-n dg-c2'), (f'Bluetooth: {" e ".join(imagens_bt)}', 'dg-n dg-c2')])
    centros_d.append(console.y + console.h // 2)
    s.linha([(488, y_c0), (520, y_c0)], 'dg-fio')
    s.linha([(520, centros_d[0]), (520, centros_d[-1])], 'dg-fio')
    for c in centros_d:
        s.linha([(520, c), (536, c)], 'dg-fio')
    # the flash, under the chip: the program and the file system, side by side
    y_f = max(console.y, 296)
    s.texto(16, y_f + 16, f'flash de {fl["total"] // 1024} MB', 'dg-t dg-b')
    metade = len(a['pastas']) // 2
    bloco(s, 16, y_f + 28, 240, [('programa', 'dg-t dg-b'), (f'{milhar(fl["programa"])} KiB', 'dg-n'), ('o firmware, com a interface web inteira embutida', 'dg-n dg-c2')], 'dg-caixa', 6, altura=96)
    bloco(s, 264, y_f + 28, 240, [('LittleFS', 'dg-t dg-b'), (f'{milhar(fl["sistema"])} KiB', 'dg-n'), (' '.join(a['pastas'][:metade]), 'dg-n dg-m'), (' '.join(a['pastas'][metade:]), 'dg-n dg-m'),
                                  ([('', 'log de eventos '), ('dg-m', a['log'])], 'dg-n')], 'dg-caixa', 6, altura=96)
    return s.render()


def desenho_escada(num, slug):
    e = num.escada()
    s = Svg(slug)
    escala = 1.6                                     # px a second, for the fast part: the waits double, so do their boxes
    largura = lambda segundos: max(GRADE, round(segundos * escala / GRADE) * GRADE)
    minutos = lambda segundos: f'{segundos // 60}\u00a0min' if segundos % 60 == 0 else f'{segundos}\u00a0s'
    # the key: what the three kinds of box are
    x = 16
    for classe, texto in (('dg-caixa-acento', f'busca (até {e["busca"]}\u00a0s) e tentativa (até {e["tentativa"]}\u00a0s)'), ('dg-caixa-2', 'espera'), ('dg-caixa-apagada', f'espera de {minutos(e["dormencia"])}, fora de escala')):
        s.caixa(x, 24, 16, 16, classe, 0)
        s.texto(x + 24, 36, texto, 'dg-n')
        x += 24 + largura_texto(texto, 12) + 16 + 4
        x = (x + 3) // 4 * 4
    # the line: the fall, then the fast cycles (a wait and an attempt each), then the dormant waits, each before an attempt
    s.texto(16, 116, 'queda', 'dg-n')
    s.seta([(52, 112), (64, 112)], 'dg-fio', 'dg-ponta')
    x_inicio = x = 64
    caixas = []
    for k, espera in enumerate(e['esperas']):
        w = largura(espera)
        s.caixa(x, 96, w, 32, 'dg-caixa-2', 0)
        s.texto((x + w // 2) // GRADE * GRADE, 88, f'{espera}\u00a0s', 'dg-n', 'middle')
        caixas.append((x, w))
        x += w
        s.caixa(x, 96, largura(e['tentativa']), 32, 'dg-caixa-acento', 0)
        x += largura(e['tentativa'])
    x_rapido = x
    for k in range(e['dormentes']):
        s.caixa(x, 96, 40, 32, 'dg-caixa-apagada', 0)
        s.texto(x + 20, 88, minutos(e['dormencia']), 'dg-n', 'middle')
        x += 40
        s.caixa(x, 96, largura(e['tentativa']), 32, 'dg-caixa-acento', 0)
        x += largura(e['tentativa'])
    x_fim = x
    if x_fim > 688:
        raise ValueError(f'{slug}: the line is {x_fim - x_inicio} px long and does not fit before the restart arrow')
    # the two phases, bracketed under the line
    for x1, x2, linhas in ((x_inicio, x_rapido, ['a espera dobra a cada falha', f'teto de {e["teto"]}\u00a0s, que a escada não atinge']),
                           (x_rapido, x_fim, [f'dormência: {e["dormentes"]} esperas de {minutos(e["dormencia"])}', 'só depois da última tentativa rápida'])):
        s.linha([(x1 + 4, 136), (x1 + 4, 144), (x2 - 4, 144), (x2 - 4, 136)], 'dg-fio-fino')
        meio = (x1 + x2) // 8 * 4
        s.texto(meio, 164, linhas[0], 'dg-n', 'middle')
        s.texto(meio, 180, linhas[1], 'dg-n dg-c2', 'middle')
    # the restart, back to the first wait, and what the ladder does not do
    s.seta([(x_fim, 112), (x_fim + 16, 112), (x_fim + 16, 216), (68, 216), (68, 128)], 'dg-fio', 'dg-ponta')
    s.texto(84, 208, f'recomeça na espera de {e["esperas"][0]}\u00a0s, e se repete até a rede voltar', 'dg-n')
    s.texto(16, 248, 'A escada não abre o AP de configuração: ele só abre quando alguém pede.', 'dg-n dg-c2')
    s.texto(16, 264, 'Uma conexão bem-sucedida, a qualquer momento, zera a escada.', 'dg-n dg-c2')
    return s.render()


def desenho_correcao(num, slug):
    c = num.correcao()
    ev = c['eventos']
    s = Svg(slug)
    acento = ('dg-fio-acento', 'dg-ponta-acento')
    desloc = HORAS_DESLIGADO * 80                         # the time off, 80 px an hour: the same width is the shift of the blocks
    x_boot, x_ntp, x_fim, y_a = 16 + desloc, 448, 704, 100
    marca = lambda chave, sufixo='': [('dg-b', str(ev[chave][0])), ('', f' {ev[chave][1]}{sufixo}')]
    # the clock: off, provisional (the panel paints it amber), set
    s.caixa(16, y_a, desloc, 32, 'dg-caixa-apagada', 0)
    s.texto(16 + desloc // 2, y_a + 20, f'desligado por {HORAS_DESLIGADO}\u00a0h', 'dg-n', 'middle')
    s.caixa(x_boot, y_a, x_ntp - x_boot, 32, 'dg-caixa-aviso', 0)
    s.texto((x_boot + x_ntp) // 8 * 4, y_a + 20, 'relógio provisório', 'dg-n', 'middle')
    s.caixa(x_ntp, y_a, x_fim - x_ntp, 32, 'dg-caixa-ok', 0)
    s.texto((x_ntp + x_fim) // 8 * 4, y_a + 20, 'relógio acertado', 'dg-n', 'middle')
    for x in (x_boot, x_ntp):
        s.linha([(x, y_a - 8), (x, y_a + 40)], 'dg-fio')
    # the events, on the line: the boot's, and the sync's with the correction's three
    s.linha([(x_boot, y_a - 8), (x_boot, 80)], 'dg-fio-fino')
    rotulo(s, x_boot + 8, 84, [marca('provisorio')], 716)
    s.linha([(x_ntp, y_a - 8), (x_ntp, 28)], 'dg-fio-fino')
    rotulo(s, x_ntp + 8, 32, [marca('ntp'), marca('corrigindo', f', +{HORAS_DESLIGADO * 3600}\u00a0s'), marca('corrigidos', f', {BLOCOS_EXEMPLO}\u00a0blocos'), marca('caches')], 716)
    s.texto(x_boot, 156, 'boot', 'dg-n', 'middle')
    s.texto(x_ntp, 156, 'primeiro acerto', 'dg-n', 'middle')
    s.texto(x_ntp, 172, f'corrige se a diferença passa de {c["limiar"]}\u00a0s', 'dg-n dg-c2', 'middle')
    # the blocks written since the boot: stamped by the provisional clock, 2 h behind (they fall in the time the device was off), then shifted by the difference
    passo, largura = 64, 40
    s.texto(64, 200, f'gravados com a hora provisória, {HORAS_DESLIGADO}\u00a0h atrás', 'dg-n')
    for k in range(BLOCOS_EXEMPLO):
        s.caixa(64 + passo * k, 208, largura, 32, 'dg-caixa-aviso', 0)
        s.caixa(64 + desloc + passo * k, 288, largura, 32, 'dg-caixa-ok', 0)
    centro = (2 * 64 + passo * (BLOCOS_EXEMPLO - 1) + largura) // 8 * 4
    s.seta([(centro, 240), (centro, 264), (centro + desloc, 264), (centro + desloc, 288)], *acento)
    s.texto(centro + desloc // 2, 260, f'+{HORAS_DESLIGADO * 3600}\u00a0s ({HORAS_DESLIGADO}\u00a0h)', 'dg-n', 'middle')
    s.texto(64 + desloc, 340, 'depois do acerto, na hora em que aconteceram', 'dg-n')
    return s.render()


def desenho_transicao(num, slug):
    tr = num.transicao()
    sim = simular_transicao(EspelhoLogPolicy(tr['batimento_ms'], tr['relatorio_ms']), tr)
    s = Svg(slug)
    x_de = lambda minuto: (200 + 28 * minuto // ENVIOS_CADA_MIN) // GRADE * GRADE            # 28 px for each send, on the grid
    y_c, y_f, y_r, y_eixo = 80, 144, 216, 288
    # the collector's outage, behind the rows
    x1, x2 = x_de(FORA_DO_AR_MIN[0]), x_de(FORA_DO_AR_MIN[1])
    s.caixa(x1, 56, x2 - x1, 128, 'dg-caixa-perigo', 0)
    s.texto((x1 + x2) // 8 * 4, 48, 'coletor fora do ar', 'dg-n', 'middle')
    # the three rows, named at the left
    for y, titulo, notas in ((y_c, 'console', ['todos os eventos,', f'um a cada {ENVIOS_CADA_MIN}\u00a0min']), (y_f, 'flash', ['as transições e', 'um por hora']), (y_r, 'contagem', ['uma vez por hora'])):
        s.texto(16, y - 4, titulo, 'dg-t dg-b')
        for k, nota in enumerate(notas):
            s.texto(16, y + 12 + 16 * k, nota, 'dg-n dg-c2')
    # the events: a small dot on the console for each, a large one on the flash where the policy writes it
    anterior = None                                      # the event before: None at the boot, then whether it failed
    for e in sim['eventos']:
        x = x_de(e['minuto'])
        s.no(x, y_c, 'dg-no-perigo' if e['falha'] else 'dg-no')
        if e['flash']:
            s.linha([(x, y_c + 8), (x, y_f - 12)], 'dg-fio-fino')
            s.no(x, y_f, 'dg-no-perigo' if e['falha'] else 'dg-no-ok', 8)
            if e['falha']:
                tipo = 'uma por hora' if anterior else '1ª falha'
            else:
                tipo = 'boot: 1º envio' if anterior is None else ('1ª volta' if anterior else 'uma por hora')
            if tipo in ('boot: 1º envio', 'uma por hora'):
                s.texto(x - 8, y_f - 20, tipo, 'dg-n', 'end')          # above, beside the line that comes down to the dot
            else:
                s.texto(x, y_f + 28, tipo, 'dg-n', 'middle')
        anterior = e['falha']
    # the accounting records, one an hour after the first suppressed one, with what they count
    codigo_resumo, nome_resumo = tr['resumo']
    for r in sim['relatorios']:
        x = x_de(r['minuto'])
        s.no(x, y_r)
        for k, linha in enumerate(quebrar(f'{nome_resumo} ({codigo_resumo}), ctx\u00a0=\u00a0{r["n"]}', 132, 12)):
            s.texto(x, y_r + 28 + 16 * k, linha, 'dg-n', 'middle')
    # the time, under them
    s.linha([(x_de(0), y_eixo), (x_de(DURACAO_H * 60), y_eixo)], 'dg-fio')
    for h in range(DURACAO_H + 1):
        s.linha([(x_de(h * 60), y_eixo), (x_de(h * 60), y_eixo + 8)], 'dg-fio')
        s.texto(x_de(h * 60), y_eixo + 28, 'boot' if h == 0 else f'{h}\u00a0h', 'dg-n', 'middle')
    return s.render()


def desenho_ota_linha_tempo(num, slug):
    o = num.ota_linha_do_tempo()
    s = Svg(slug)
    por_s = 4                                                   # px a second: the send, the apply and the boot are to scale; the backup's "segundos" is a box of its own
    virgula = lambda x: str(x).replace('.', ',')
    envio = f'{round(ENVIO_MEDIDO_S[0])} a {round(ENVIO_MEDIDO_S[1])}\u00a0s'
    volta = f'{VOLTA_MEDIDA_S[0]} a {VOLTA_MEDIDA_S[1]}\u00a0s'
    estagios = ((1, 'ok', 16, None, [('dg-b', 'backup .bkp baixado'), ('', ', segundos')], ['um corte aqui não muda nada: o firmware e a configuração ficam']),
                (2, 'aviso', por_s * round(sum(ENVIO_MEDIDO_S) / 2), envio, [('dg-b', 'envio e conferência da imagem'), ('', f', {envio}')],
                 ['sistema de arquivos já sobrescrito', 'um corte perde os arquivos, e a configuração fica']),
                (3, 'perigo', por_s * o['aplicacao'], f'cerca de {o["aplicacao"]}\u00a0s', [('dg-b', 'aplicação'), ('', f', cerca de {o["aplicacao"]}\u00a0s')],
                 ['única janela em que um corte de energia deixa o aparelho sem firmware: exige BOOTSEL e o cabo USB']),
                (4, 'ok', por_s * o['boot'], f'cerca de {o["boot"]}\u00a0s', [('dg-b', 'boot da imagem nova'), ('', f', cerca de {o["boot"]}\u00a0s')],
                 ['um corte aqui não muda nada: a imagem nova já está gravada']))
    # the line, to scale: a box for each stage in the colour of what a cut costs there, its number above and its time inside
    x, y_l, inicio_aplicacao = 24, 60, None
    for numero, estado, largura, dentro, _, _ in estagios:
        if numero == 3:
            inicio_aplicacao = x
        s.caixa(x, y_l, largura, 32, f'dg-caixa-{estado}', 0)
        s.caixa((x + largura // 2) // GRADE * GRADE - 8, 32, 16, 16, f'dg-caixa-{estado}', 6)
        s.texto((x + largura // 2) // GRADE * GRADE, 44, str(numero), 'dg-n dg-b', 'middle')
        if dentro and largura >= 96:
            s.texto((x + largura // 2) // GRADE * GRADE, y_l + 20, dentro, 'dg-n', 'middle')
        x += largura
    # the web interface comes back 52 to 56 s after the apply began: a bracket on the same scale, past the end of the boot
    x1, x2 = inicio_aplicacao + por_s * VOLTA_MEDIDA_S[0], inicio_aplicacao + por_s * VOLTA_MEDIDA_S[1]
    s.linha([(x1, y_l + 36), (x1, y_l + 44), (x2, y_l + 44), (x2, y_l + 36)], 'dg-fio-fino')
    rotulo(s, x2 + 12, y_l + 48, quebrar(f'interface web de volta, {volta} depois da aplicação', 716 - (x2 + 12) - 8, 12), 716)
    # what a cut costs at each stage
    y = 136
    s.texto(16, y - 4, 'o que um corte de energia custa em cada etapa', 'dg-t dg-b')
    y += 12
    for numero, estado, _, _, titulo, detalhes in estagios:
        s.caixa(16, y, 16, 16, f'dg-caixa-{estado}', 6)
        s.texto(24, y + 12, str(numero), 'dg-n dg-b', 'middle')
        rotulo(s, 44, y + 12, [titulo], 716, 'dg-t')
        rotulo(s, 44, y + 28, detalhes, 716)
        y += 16 + 16 * len(detalhes) + 16
    s.texto(16, y + 4, 'A figura é do Pico W. No Pico 2 W a imagem vai para o outro slot e o sistema de arquivos não é tocado.', 'dg-n dg-c2')
    return s.render()


def desenho_despertar(num, slug):
    d = num.despertar()
    gp = num.macro('AIR_SENSOR_POWER_PIN', 'air')
    s = Svg(slug)
    virgula = lambda x, casas: f'{x:.{casas}f}'.replace('.', ',')
    q = lambda segundos: round(segundos * ESCALA_DESPERTAR / GRADE)            # seconds to grid steps (a step is 4 px)
    aquecimento = d['aquecimento_s']
    passos_boot, passos_aq, ate_decidir, total = q(AIR_SETUP_S), max(1, q(aquecimento)), q(AIR_ATE_DECIDIR_S), q(AIR_DESPERTAR_TELEMETRIA_S)
    passos_am = ate_decidir - passos_boot - passos_aq                          # the sampling is what is left of the 9,31 s once the boot and the warm-up are taken out
    if passos_am < 4:
        raise Falta(f'{AIR_CPP}: the warm-up is {aquecimento} s, and with the boot of {AIR_SETUP_S} s it leaves no room for the sampling in the {AIR_ATE_DECIDIR_S} s the wake takes to decide')
    w_boot, w_aq, w_am, w_env = passos_boot * GRADE, passos_aq * GRADE, passos_am * GRADE, (total - ate_decidir) * GRADE
    x0, w_sono = 16, 160
    y_titulo, y_valor, y_marca, y_gp, y_radio, y_fase = 28, 44, 52, 64, 84, 132
    y_seta = y_fase + 20

    def um(x, titulo, telemetria):
        """One wake from x: the title, the mark that measures it, GP16's band, the radio's slot and the phases, to scale. The decision is a point (nobody measured how long it takes), where the
        mark of the reading wake ends and the connection and the send begin."""
        xa, xs = x + w_boot, x + w_boot + w_aq
        xd = xs + w_am
        fim = xd + (w_env if telemetria else 0)
        s.texto(x, y_titulo, titulo, 'dg-t dg-b')
        s.linha([(x, y_marca + 8), (x, y_marca), (fim if telemetria else xd, y_marca), (fim if telemetria else xd, y_marca + 8)], 'dg-fio-fino')
        s.texto((x + (fim if telemetria else xd)) // 8 * 4, y_valor, f'cerca de {virgula(AIR_DESPERTAR_TELEMETRIA_S, 1)}\u00a0s' if telemetria else f'do boot até a decisão: {virgula(AIR_ATE_DECIDIR_S, 2)}\u00a0s', 'dg-n', 'middle')
        s.caixa(x, y_gp, fim - x, 16, 'dg-caixa-2', 0)
        s.texto(x + 8, y_gp + 12, [('dg-m', f'GP{gp}'), ('', ' alto')], 'dg-n')
        if telemetria:
            s.caixa(xa, y_radio, fim - xa, 40, 'dg-caixa-acento', 0)
            s.texto(xa + 8, y_radio + 16, 'rádio ligado', 'dg-n dg-b dg-sobre-acento')
            s.texto(xa + 8, y_radio + 32, 'Wi-Fi conecta na amostragem', 'dg-n dg-sobre-acento')
        else:
            s.texto(x + 8, y_radio + 24, 'rádio desligado', 'dg-n dg-c2')
        s.caixa(x, y_fase, w_boot, 40, 'dg-caixa', 0)
        s.caixa(xa, y_fase, w_aq, 40, 'dg-caixa', 0)
        s.caixa(xs, y_fase, w_am, 40, 'dg-caixa', 0)
        s.texto(x + w_boot // 2 // GRADE * GRADE, y_fase + 24, 'boot', 'dg-n', 'middle')
        centro = xs + w_am // 2 // GRADE * GRADE
        if telemetria:
            s.texto(centro, y_fase + 24, 'amostragem', 'dg-n', 'middle')
            s.caixa(xd, y_fase, w_env, 40, 'dg-caixa', 0)
            s.texto(xd + w_env // 2 // GRADE * GRADE, y_fase + 16, 'conexão', 'dg-n', 'middle')
            s.texto(xd + w_env // 2 // GRADE * GRADE, y_fase + 32, 'e envio', 'dg-n', 'middle')
        else:
            s.texto(centro, y_fase + 16, 'amostragem', 'dg-n', 'middle')
            s.texto(centro, y_fase + 32, 'conversão do sensor', 'dg-n dg-c2', 'middle')
        # the decision, a point on the line; the warm-up is too narrow for its own label, so it has a flag
        s.no(xd, y_seta, 'dg-no', 8)
        xf = xa + w_aq // 2 // GRADE * GRADE
        s.linha([(xf, y_fase + 40), (xf, y_fase + 52)], 'dg-fio-fino')
        s.texto(xf, y_fase + 64, f'aquecimento {virgula(aquecimento, 1)}\u00a0s', 'dg-n')
        s.linha([(xd, y_seta + 8), (xd, y_fase + 72)], 'dg-fio-fino')
        s.texto(xd, y_fase + 84, 'decisão: grava o histórico', 'dg-n', 'end')
        return fim

    fim1 = um(x0, 'despertar de leitura', False)
    x1 = fim1 + w_sono
    s.caixa(fim1 + 8, y_fase, w_sono - 8, 40, 'dg-caixa-apagada', 0)
    meio = fim1 + 8 + (w_sono - 8) // 2 // GRADE * GRADE
    s.texto(meio, y_fase + 16, 'sono, até completar', 'dg-n', 'middle')
    s.texto(meio, y_fase + 32, f'o intervalo de {AIR_INTERVALO_S}\u00a0s', 'dg-n', 'middle')
    s.texto(meio, y_fase + 64, 'fora de escala', 'dg-n dg-c2', 'middle')
    um(x1, 'despertar de telemetria', True)
    y_nota = y_fase + 112
    s.texto(x0, y_nota, 'Em escala. Têm tempo conhecido: os totais, o aquecimento e o boot; a amostragem e o envio são o que falta.', 'dg-n dg-c2')
    s.texto(x0, y_nota + 16, 'O despertar de telemetria repete as larguras do de leitura até a decisão.', 'dg-n dg-c2')
    s.texto(x0, y_nota + 32, 'O rádio liga no boot, só no despertar de telemetria; onde a faixa começa não foi medido.', 'dg-n dg-c2')
    return s.render()


def desenho_entrada(num, slug):
    e = num.entrada()
    s = Svg(slug)
    q = Sequencia(s, 'Cliente', [('dg-b', 'SIMUT'), ('', ' '), ('dg-m', IP_EXEMPLO)])
    rota = lambda r: mono(f'{r[0]} {r[1]}')
    k1, k2, k3 = e['chaves']
    usuario, senha, codigo = e['campos']
    q.pedido([rota(e['init'])], 1)
    q.resposta([mono(f'200 {{"{k1}":"{NONCE_EXEMPLO}","{k2}":false,"{k3}":0}}'), [('dg-c2', f'o código vale {e["vida_s"]}\u00a0s e serve a uma tentativa')]])
    q.nota('cliente', 'calcula o SHA-256 da senha', 2)
    q.pedido([rota(e['login']), [('dg-m', usuario), ('', ', '), ('dg-m', senha), ('', ' (o SHA-256) e '), ('dg-m', codigo)]], 3)
    q.nota('aparelho', 'confere o código e a senha')
    q.resposta([mono(f'200 Set-Cookie: {e["cookie"]}=…'), mono(f'{{"{e["ok"]}":true,"{e["destino"]}":"{e["raiz"]}"}}')], 4)
    q.pedido([rota(e['status']), [('dg-m', f'Cookie: {e["cookie"]}=…'), ('', ' ou '), ('dg-m', f'Authorization: {e["esquema"]} …')]], 5)
    q.resposta([mono('200')])
    q.pedido([rota(e['logout'])])
    q.resposta([[('dg-m', e['sem_corpo']), ('', f' com {e["esquema"]}; '), ('dg-m', e['redireciona']), ('', ' para '), ('dg-m', e['pagina']), ('', ' com cookie')],
                [('dg-c2', 'encerra só a sessão do token')]])
    return q.render()


def desenho_ota_api(num, slug):
    a = num.api_ota()
    s = Svg(slug)
    q = Sequencia(s, 'Cliente', [('dg-b', 'SIMUT'), ('', ' '), ('dg-m', IP_EXEMPLO)])
    rota = lambda r: mono(f'{r[0]} {r[1]}')
    com_op = lambda op, extra='': mono(f'{a["restore"][0]} {a["restore"][1]}?op={op}{extra}')
    st, v, committed = a['chaves']
    preparada, v_bom, um = a['valores']
    chave, versao = a['versao'].split('.')
    q.pedido([rota(a['backup'])], 1)
    q.resposta([[('dg-m', a['baixou']), ('', ' o arquivo '), ('dg-m', f'.{a["extensao"]}'), ('', ' e o cabeçalho '), ('dg-m', a['crc'])], [('dg-c2', 'o cliente confere o CRC do arquivo com o do cabeçalho')]])
    q.pedido([com_op(a['stage'], f'&{a["commit"]}'), [('dg-c2', 'o .bin da imagem, assinado')]], 2)
    q.resposta([mono(f'{a["bom_http"]} {{"{st}":{preparada},…,"{v}":{v_bom},…,"{committed}":{um},…}}')])
    q.pedido([rota(a['apply'])], 3)
    q.resposta([mono(f'{a["apply_http"]} {{"{a["aceito"]}":true,…}}'), [('dg-c2', 'depois de responder, o aparelho aplica a imagem e reinicia')]])
    q.pedido([rota(a['init']), [('dg-c2', f'a cada {a["espera_s"]}\u00a0s, até o aparelho responder')]], 4)
    q.resposta([mono(a['bom_http'])])
    q.pedido([rota(a['estado']), [('dg-c2', 'depois de entrar de novo')]])
    q.resposta([mono(f'{a["bom_http"]} {{"{chave}":{{…,"{versao}":"…"}}}}'), [('dg-c2', 'só a versão nova, que o próprio aparelho informa, prova a atualização')]])
    q.pedido([com_op(a['validate']), [('dg-c2', 'o .bkp do primeiro passo')]])
    q.pedido([com_op(a['apply_op']), [('dg-c2', 'depois de responder, o aparelho restaura e reinicia')]])
    return q.render()


# ---- the chapters --------------------------------------------------------------------------------------------------------------------------------------------
class Bloco(NamedTuple):
    id: str
    tipo: str
    arquivo: str
    captura: str
    capitulo: str
    linha: int
    legenda: str


BLOCO = re.compile(r'^::: \{\.figura #(\S+)([^}\n]*)\}[ \t]*$', re.M)
ATRIBUTO = re.compile(r'(\w+)="([^"]*)"')


def ler_blocos(capitulos):
    """{id: Bloco} of every figure block of the chapters in `capitulos`. Raises OSError when the folder holds no chapter."""
    capitulos = pathlib.Path(capitulos)
    arquivos = sorted(capitulos.glob('cap*.md'))
    if not arquivos:
        raise OSError(f'no cap*.md in {capitulos}')
    blocos = {}
    for md in arquivos:
        texto = md.read_text(encoding='utf-8')
        for m in BLOCO.finditer(texto):
            a = dict(ATRIBUTO.findall(m.group(2)))
            legenda = texto[m.end():].split('\n:::', 1)[0].strip()
            blocos[m.group(1)] = Bloco(m.group(1), a.get('tipo', ''), a.get('arquivo', ''), a.get('captura', ''), md.name, texto.count('\n', 0, m.start()) + 1, legenda)
    return blocos


def conferir_capitulos(capitulos):
    """One line for each row whose block is gone or says another file or kind, each captura= that is not --list's, and each block of tipo="diagrama" with an .svg that no
    row names. A block of that kind whose file ends in .png is not this tool's."""
    achados = []
    blocos = ler_blocos(capitulos)
    nomeados = {f.bloco for f in FIGURAS}
    for f in FIGURAS:
        b = blocos.get(f.bloco)
        if b is None:
            achados.append(f'{f.bloco}: the figure block is gone from the chapters (the row of FIGURAS names {f.capitulo} and {f.arquivo})')
        elif (b.tipo, b.arquivo, b.capitulo) != (f.tipo, f.arquivo, f.capitulo):
            achados.append(f'{f.bloco}: the block says tipo={b.tipo!r}, arquivo={b.arquivo!r} in {b.capitulo}, the row says tipo={f.tipo!r}, arquivo={f.arquivo!r} in {f.capitulo}')
        elif b.captura != captura(f):
            achados.append(f'{f.bloco}: the captura= of the block ({b.capitulo}:{b.linha}) is not the text of FIGURAS (python3 tools/manual_diagramas.py --list prints it)')
    for b in blocos.values():
        if b.tipo == 'diagrama' and b.arquivo.endswith('.svg') and b.id not in nomeados:
            achados.append(f'{b.id}: a block of tipo="diagrama" with the SVG {b.arquivo} ({b.capitulo}:{b.linha}) that no row of FIGURAS in tools/manual_diagramas.py names')
    return achados


# ---- the files -----------------------------------------------------------------------------------------------------------------------------------------------
ATRIBUTO_DE_COR = re.compile(r'\s(fill|stroke|stop-color|flood-color|lighting-color|color|style)="([^"]*)"')
SEM_COR = ('none', 'currentColor', 'inherit')


def cores_literais(svg):
    """[(line, what)] for each place the SVG paints with something that is not a class: a <style>, a style= or any fill, stroke or colour attribute with a value of its own
    (a #hex, an rgb( ), a name). The diagrams are styled by the manual's CSS and its tokens; a colour in the file would not follow the theme."""
    achados = []
    for m in ATRIBUTO_DE_COR.finditer(svg):
        nome, valor = m.groups()
        if nome != 'style' and (valor in SEM_COR or valor.startswith('url(#')):
            continue
        achados.append((svg.count('\n', 0, m.start()) + 1, f'{nome}="{valor}"'))
    for m in re.finditer(r'<style\b', svg):
        achados.append((svg.count('\n', 0, m.start()) + 1, '<style>'))
    return sorted(achados)


def ids_sem_prefixo(svg, slug):
    """[(line, id)] for each id that does not start with the slug and a hyphen: an id in line is global to the page, and the manual holds a dozen diagrams."""
    return [(svg.count('\n', 0, m.start()) + 1, m.group(1)) for m in re.finditer(r'\sid="([^"]*)"', svg) if not m.group(1).startswith(slug + '-')]


def primeira_diferenca(a, b):
    """(line, column), from 1, of the first character where two texts differ."""
    i = next((k for k, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
    return a.count('\n', 0, i) + 1, i - (a.rfind('\n', 0, i) + 1) + 1


def desenhar_todas(fonte):
    """{file: the SVG text} of every row, drawn from the sources in `fonte`. Raises Falta (or ValueError, for a layout that does not fit) when it cannot."""
    num = Numeros(fonte)
    return {f.arquivo: globals()[f.desenho](num, slug_de(f.arquivo)) for f in FIGURAS}


def conferir_imagens(imagens, desenhados):
    """One line for each diagram that differs from the code, is missing, paints with a literal colour or has an id without its slug, and for each orphan .svg."""
    imagens = pathlib.Path(imagens)
    achados = []
    for f in FIGURAS:
        caminho = imagens / f.arquivo
        if not caminho.is_file():
            achados.append(f'{f.arquivo}: missing (python3 tools/manual_diagramas.py --write draws it)')
            continue
        try:
            texto = caminho.read_bytes().decode('utf-8')
        except UnicodeDecodeError as e:
            achados.append(f'{f.arquivo}: not UTF-8 text ({e})')
            continue
        if texto != desenhados[f.arquivo]:
            linha, coluna = primeira_diferenca(texto, desenhados[f.arquivo])
            achados.append(f'{f.arquivo}: differs from what the code draws, first at line {linha} column {coluna} (python3 tools/manual_diagramas.py --write draws it again)')
        for linha, valor in cores_literais(texto):
            achados.append(f'{f.arquivo}:{linha}: literal colour {valor}; a diagram is styled by classes and the manual\'s tokens, never by a colour of its own')
        for linha, ident in ids_sem_prefixo(texto, slug_de(f.arquivo)):
            achados.append(f'{f.arquivo}:{linha}: the id {ident!r} does not start with {slug_de(f.arquivo)}-; an id in line is global to the page')
    nomes = {f.arquivo for f in FIGURAS}
    for p in sorted(imagens.glob('*.svg')):
        if p.name not in nomes:
            achados.append(f'{p.name}: no row of FIGURAS in tools/manual_diagramas.py names it')
    return achados


def gravar(imagens, desenhados):
    imagens = pathlib.Path(imagens)
    imagens.mkdir(parents=True, exist_ok=True)
    for f in FIGURAS:
        with open(imagens / f.arquivo, 'w', encoding='utf-8', newline='\n') as fh:
            fh.write(desenhados[f.arquivo])


# ---- the preview ---------------------------------------------------------------------------------------------------------------------------------------------
def pagina_previa(f, svg, legenda, tema):
    """A page with the manual's CSS and the figure in the frame the manual gives it, `tema` fixed: what a reader sees, one diagram at a time."""
    import build_manual as bm
    quadro, _ = bm.svg_fragment(svg, re.sub(r'`', '', legenda))
    texto = html.escape(legenda, quote=False)
    texto = re.sub(r'`([^`]*)`', r'<code>\1</code>', texto)
    css = bm.CSS.replace('__FONT__', '')
    return (f'<!doctype html>\n<html lang="pt-BR" data-theme="{tema}">\n<head>\n<meta charset="utf-8">\n'
            f'<meta name="viewport" content="width=device-width, initial-scale=1">\n<title>{slug_de(f.arquivo)} ({tema})</title>\n<style>{css}</style>\n</head>\n<body>\n'
            f'<div class="pagina">\n<figure id="{f.bloco}" class="fig fig-diagrama"><div class="moldura moldura-diagrama">{quadro}</div>'
            f'<figcaption><b>Figura 1.</b> {texto}</figcaption></figure>\n</div>\n</body>\n</html>\n')


def previa(pasta, desenhados, capitulos):
    pasta = pathlib.Path(pasta)
    pasta.mkdir(parents=True, exist_ok=True)
    blocos = ler_blocos(capitulos)
    for f in FIGURAS:
        legenda = blocos[f.bloco].legenda if f.bloco in blocos else slug_de(f.arquivo)
        for tema in ('claro', 'escuro'):
            (pasta / f'{slug_de(f.arquivo)}.{tema}.html').write_text(pagina_previa(f, desenhados[f.arquivo], legenda, tema), encoding='utf-8')


# ---- the command line ----------------------------------------------------------------------------------------------------------------------------------------
def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    modo = ap.add_mutually_exclusive_group(required=True)
    modo.add_argument('--write', action='store_true', help='draw the diagrams and write them')
    modo.add_argument('--check', action='store_true', help='draw them again and compare the text with the committed files, and read the chapters')
    modo.add_argument('--list', action='store_true', help='file, chapter, function and captura= text of every diagram, tab-separated')
    modo.add_argument('--preview', metavar='PASTA', help='write <slug>.claro.html and <slug>.escuro.html for each diagram into PASTA')
    ap.add_argument('--images', default=str(IMAGENS), help='the folder of the diagrams (default docs/images/manual)')
    ap.add_argument('--capitulos', default=str(CAPITULOS), help='the folder of the chapters (default docs/_manual)')
    ap.add_argument('--fonte', default=str(ROOT), help='the root of the tree the numbers are read from (default: the repository)')
    a = ap.parse_args(argv)
    problema = tabela_ok()
    if problema:
        print(f'manual_diagramas: {problema}', file=sys.stderr)
        return 2
    if a.list:
        for f in FIGURAS:
            print('\t'.join((f.arquivo, f.capitulo, f.desenho, captura(f))))
        return 0
    try:
        desenhados = desenhar_todas(Fonte(a.fonte))
    except (Falta, ValueError) as e:
        print(f'manual_diagramas: {e}', file=sys.stderr)
        return 2
    if a.write:
        gravar(a.images, desenhados)
        print(f'manual_diagramas: wrote {len(FIGURAS)} diagrams to {a.images}')
        return 0
    try:
        if a.preview:
            previa(a.preview, desenhados, a.capitulos)
            print(f'manual_diagramas: wrote {2 * len(FIGURAS)} pages to {a.preview}')
            return 0
        achados = conferir_imagens(a.images, desenhados) + conferir_capitulos(a.capitulos)
    except OSError as e:
        print(f'manual_diagramas: cannot read the chapters: {e}', file=sys.stderr)
        return 2
    for linha in achados:
        print(linha)
    print(f'manual_diagramas: {len(FIGURAS)} diagrams drawn again from the sources and compared as text, the chapters read, {len(achados)} finding(s)')
    return 1 if achados else 0


if __name__ == '__main__':
    sys.exit(main())
