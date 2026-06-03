---
title: "SIMUT — Manual Completo do Usuário [RASCUNHO NÃO REVISADO]"
subtitle: "Sistema de Monitoramento de Cadeia Fria"
author: "Ângelo Moisés Alves"
version: "v1.0.0"
date: ""
documentclass: report
geometry: margin=2.5cm
header-includes: |
    \usepackage{xcolor}
---

# SIMUT — Manual Completo do Usuário

> ⚠️ **STATUS: RASCUNHO — NÃO REVISADO**
>
> Este documento foi gerado automaticamente como experimento de pipeline
> (auto-captura de telas TFT via `screen <code>` + páginas web via Selenium
> + render markdown→HTML→PDF via pandoc + chromium). **Conteúdo NÃO foi
> revisado por humano**. Pode conter:
>
> - Descrições incorretas de comportamentos de UI
> - Endpoints e permissões desatualizados
> - Recomendações imprecisas de configuração
> - Detalhes de hardware que não batem com o seu kit
>
> Use como ponto de partida apenas. Para informações autoritativas consulte
> o código fonte, `SECURITY.md` e `STABILITY_PLAN.md` no repositório.

---

**Versão do firmware:** v1.0.0
**Hardware alvo:** Raspberry Pi Pico W (RP2040 + CYW43439)
**Repositório:** https://github.com/angeloINTJ/SIMUT
**Licença:** MIT

---

## Sumário

1. [Visão Geral](#1-visão-geral)
2. [Hardware](#2-hardware)
3. [Primeiro Acesso](#3-primeiro-acesso)
4. [Display TFT — Telas e Toques](#4-display-tft--telas-e-toques)
5. [Interface Web](#5-interface-web)
6. [CLI por Serial USB](#6-cli-por-serial-usb)
7. [CLI por Bluetooth](#7-cli-por-bluetooth)
8. [OTA — Atualização de Firmware](#8-ota--atualização-de-firmware)
9. [Backup e Restauração](#9-backup-e-restauração)
10. [Segurança](#10-segurança)
11. [Solução de Problemas](#11-solução-de-problemas)
12. [Apêndices](#12-apêndices)

---

## 1. Visão Geral

SIMUT é um datalogger profissional para **monitoramento de temperatura e umidade** em ambientes regulados (laboratórios, farmácias, blood banks, freezers de vacinas, banhos-maria, estufas microbiológicas).

### 1.1 Capacidades principais

- **Até 11 sensores** simultâneos: 1 SHT31 (ambiente — T+UR) + 10 DS18B20 (1-Wire, T)
- **Display TFT 320×240** com touch resistivo — operação sem PC
- **Interface web** completa (dashboard, histórico, alarmes, configuração)
- **Histórico binário** (1 ponto/min, formato compacto, ~1 ano de dados em 1 MB)
- **Telemetria HTTP/MQTT** para integração com SCADA / nuvem
- **Bluetooth Classic SPP** — CLI remota sem cabo
- **OTA** auto-flash via interface web (admin-only)
- **Backup/restore** completo via API (BKP1 com CRC32 + chip_id matching)
- **Multi-usuário** (até 5 contas com permissões granulares)
- **i18n**: PT-BR + EN dinâmico

### 1.2 Cenários típicos

| Setpoint | Ambiente | Faixa típica |
|---|---|---|
| -80 °C | Ultra-freezer biobanco | ≤ -65 °C |
| -30 °C | Freezer de plasma | -35 a -25 |
| -20 °C | Freezer de vacinas | -25 a -15 |
| +5 °C | Geladeira de vacinas/medicamentos | +2 a +8 |
| +25 °C | Estufa BOD | +24 a +26 |
| +37 °C | Estufa de cultura microbiológica | +36 a +38 |
| +37 °C | Banho-maria de inativação | +36 a +38 |
| +45 °C | Banho-maria de coagulação | +44 a +46 |

### 1.3 Gestão de alarmes

Cada sensor tem **min/max independentes** (T e UR). Quando valor sai do range:

- Beep audível no buzzer SIMUT
- Banner colorido no dashboard
- Tela cheia de alarme com timestamp + valor
- Registro permanente no histórico (`alarm_open`/`alarm_close`)
- Trigger opcional via telemetria HTTP/MQTT

---

## 2. Hardware

### 2.1 Componentes oficiais

| Componente | Modelo | Função |
|---|---|---|
| MCU | Raspberry Pi Pico W (RP2040 + CYW43439) | Processamento + WiFi/BT |
| Display | TFT 320×240 ILI9341 SPI | UI principal |
| Touch | XPT2046 SPI | Interação no display |
| Sensores T+UR | SHT31 I2C | Ambiente |
| Sensor T | DS18B20 1-Wire | Pontos individuais (até 10) |
| Buzzer | PIO active buzzer | Alarmes audíveis |
| Storage | LittleFS no flash interno (1.5 MB usável) | Config + histórico |

### 2.2 Pinout (Pico W)

| GPIO | Função |
|---|---|
| 0 | Reservado (UART0 TX) |
| 1 | Reservado (UART0 RX) |
| 4 | Sensor DS18B20 #1 (1-Wire) |
| 5 | Sensor DS18B20 #2 (1-Wire) |
| 6 | I2C SDA (SHT31) |
| 7 | I2C SCL (SHT31) |
| 8 | UART1 TX (debug — ver §11) |
| 9 | UART1 RX |
| 10..15 | DS18B20 #3..#8 (1-Wire) |
| 16 | TFT MISO |
| 17 | TFT CS |
| 18 | TFT SCK |
| 19 | TFT MOSI |
| 20 | TFT DC |
| 21 | TFT RST |
| 22 | Touch CS |
| 26 | Buzzer (PIO) |
| 27 | Touch IRQ |
| 28 | Reserva |

### 2.3 Alimentação

- **USB-C** (5 V, suficiente para Pico W + display + sensores)
- **Consumo médio**: 80–120 mA @ 5 V (display ativo, WiFi conectado)
- **Pico**: ~250 mA (rajada de telemetria + render TFT)

Recomendações:

- Use cabo USB de qualidade (≥22 AWG) — quedas de tensão causam reboots
- Em produção, conecte a UPS ou no-break para sobreviver quedas curtas
- Não use hubs USB sem alimentação externa

---

## 3. Primeiro Acesso

### 3.1 Boot inicial

1. **Conecte o cabo USB** ao Pico W. O LED verde do board pisca; após ~30 s o display mostra o dashboard.
2. **Sem WiFi configurada**: TFT mostra `IP: (sem rede)` no header. Vá direto para o modo AP (próximo passo).
3. **Modo AP**: segure o dedo na tela durante o boot por **3 segundos** quando a barra de progresso aparecer. SIMUT vira Access Point `SIMUT-config-XXXX`. Conecte com seu celular/laptop. Senha do AP: impressa no display.
4. **Configure WiFi**: navegue para `http://192.168.4.1`, vá em "Network", preencha SSID e senha da sua rede, salve.
5. **Reboot**: o device reinicia em modo cliente. Display mostra o IP DHCP atribuído (ex: `192.168.3.195`).

### 3.2 Primeira senha

A senha de fábrica é **OTP de uso único** mostrada no display após `factory reset` (ou no primeiro boot se LFS virgem). Anote.

1. Acesse `http://<IP-do-SIMUT>` no navegador
2. Login com `admin` + OTP
3. Sistema **força troca de senha** na primeira sessão
4. Defina uma senha forte (mín. 8 caracteres recomendado)

> **Senha esquecida?** Reseta via CLI serial: `conf system admin reset confirm` → captura novo OTP no terminal.

### 3.3 mDNS

Após WiFi associado, acesse via `http://simut.local` em vez do IP — funciona em macOS, Linux com `avahi-daemon`, Windows com Bonjour Print Services. Útil quando o IP é DHCP e pode mudar.

---

## 4. Display TFT — Telas e Toques

O SIMUT possui **9 telas principais** acessíveis via toque ou via comando CLI `screen <code>`. Cada tela tem botões/áreas tocáveis específicos.

### 4.1 Dashboard (tela inicial)

![Dashboard TFT](screenshots_v4/tft_01_dashboard.png){ width=70% }

Tela principal exibida no boot. Mostra:

- **Header**: hora atual, IP, ícone WiFi (RSSI), ícone BT, versão firmware
- **Painel ambiente**: T (°C) + UR (%) do SHT31 — atualiza a cada 2 s
- **Painel slot ativo**: T do sensor DS18B20 selecionado, nome customizável, status (OK / out-of-range / sem leitura)
- **Botões inferiores**: navegação entre slots (◄ / ►), ícone de gráfico, ícone de menu/settings

**Toques disponíveis:**

| Área | Ação |
|---|---|
| Painel ambiente (esquerda) | Toggle min/max ambiente |
| Painel slot (direita) | Toggle min/max slot ativo |
| Botão ◄ (canto inferior esq) | Slot anterior |
| Botão ► (canto inferior dir) | Próximo slot |
| Ícone gráfico (centro inf) | Abre gráfico do slot ativo |
| Ícone menu (canto sup dir) | Vai para autenticação → Settings |
| Hold 3s na tela durante boot | Modo AP (para reconfigurar WiFi) |

### 4.2 Settings (menu principal)

![Settings TFT](screenshots_v4/tft_02_settings_main.png){ width=70% }

Acessível via toque no ícone de menu no dashboard (após autenticação por PIN/senha). Lista as opções:

- **Sons** — volume, melodias, mute
- **Idiomas** — PT-BR / EN
- **Senha** — troca senha do admin local (ver §4.5)
- **Touch Cal** — recalibra touch resistivo (8 pontos)
- **Touch Sens** — sensibilidade do touch
- **Temas** — esquema de cores do display
- **Alarmes** — gerencia setpoints e ativação
- **Status do Sistema** — info técnica (heap, uptime, etc)
- **Licença** — chave de produto
- **Reboot** — reinicia o device

**Toques:**

| Área | Ação |
|---|---|
| Item de menu | Abre sub-tela |
| Botão ▼ (rolagem) | Próxima página de opções |
| Botão ◄ (canto inf esq) | Volta para o dashboard |

### 4.3 Settings → Temas

![Themes TFT](screenshots_v4/tft_03_settings_themes.png){ width=70% }

Lista temas pré-instalados + temas customizados (uploadáveis via web). Cada tema é um `.thm` em `/themes/` no LFS.

- Tema atual marcado com ✓
- Toque em qualquer tema para aplicar imediatamente
- "Aplicando tema..." aparece por ~200 ms enquanto o display redesenha

### 4.4 Settings → Idioma

![Language TFT](screenshots_v4/tft_04_settings_language.png){ width=70% }

Seleção entre **Português (Brasil)** e **English**. Mudança imediata + persistente após reboot. Lang packs em `/lang/language_pt-BR.lng` (~22 KB) e `/lang/language_en.lng` (não vem por padrão — upload via web).

### 4.5 Settings → Senha

![Password TFT](screenshots_v4/tft_05_settings_password.png){ width=70% }

Trocar PIN de acesso ao Settings via touch. Diferente da senha admin web (essa é numérica curta para conveniência no display). Padrão: `1234`.

Esta senha **só protege o menu Settings via touch**. Web e CLI usam credenciais separadas.

### 4.6 Settings → Licença

![License TFT](screenshots_v4/tft_06_settings_license.png){ width=70% }

Mostra a **chave de licença** instalada e o status (válida, expirada, modo demo). A chave é um identificador único atrelado ao chip_id do RP2040 — para controle de instalações em ambientes regulados.

### 4.7 Status do Sistema

![System Status TFT](screenshots_v4/tft_07_system_status.png){ width=70% }

Snapshot técnico em tempo real:

- Versão firmware
- Uptime
- Heap livre / total / maior bloco
- IP, RSSI, MAC
- Sensores ativos
- Telemetria (enviadas, falhas, retries)
- Storage (FS usado/total)

Tela útil para debug rápido — se o RSSI estiver < -80 dBm, sinal WiFi está fraco; se heap < 20 KB, há vazamento ou fragmentação.

### 4.8 Settings → Alarmes

![Alarms TFT](screenshots_v4/tft_08_settings_alarms.png){ width=70% }

Lista todos os sensores com:

- Range Tmin / Tmax
- Range UMmin / UMmax (apenas SHT31)
- Status do alarme (ON/OFF)

**Toque no sensor** para editar limites:

- Botões + / − incrementam ±0.5 °C (T) ou ±1% (UR)
- Hold em + ou − = repeat (acelera após 1 s)
- Botão "Salvar" persiste na flash (write memory implícito)
- Botão "Cancelar" desfaz alterações

### 4.9 Gráfico

![Graph TFT](screenshots_v4/tft_09_graph.png){ width=70% }

Plot temporal das últimas N amostras (default 200 pontos = ~3.3 h se intervalo 1 min).

**Toques:**

| Área | Ação |
|---|---|
| Touch e arrasta horizontal | Pan no tempo |
| Touch duplo | Zoom in (próximo nível: 1d, 7d, 30d) |
| Botão ⊞ (sup dir) | Stats (min/max/média/desvio) |
| Botão ◄ (canto inf esq) | Volta para o dashboard |

---

## 5. Interface Web

Acesso via browser em `http://<IP-do-SIMUT>` ou `http://simut.local`. Compatível com Chrome, Firefox, Safari, Edge.

### 5.1 Login

![Web Login](screenshots_v4/web_01_login.png){ width=85% }

- Campo **User**: nome do usuário (admin padrão)
- Campo **Password**: senha web (≠ PIN do display)
- Botão **Sign In**: autentica
- Link **Reset password**: gera OTP via CLI (não via web — segurança)

Após 5 tentativas falhas seguidas de um mesmo IP, lockout de 30 s (exponencial). Tentar de outro IP/computador reseta o contador desse IP.

> **Pegadinha técnica:** o navegador hashea a senha com SHA-256 hexadecimal **antes** de enviar via POST. Se acessar via curl direto, precisa fazer o hash no client antes — senão recebe HTTP 401 mesmo com senha certa.

### 5.2 Dashboard Web

![Web Dashboard](screenshots_v4/web_02_dashboard.png){ width=85% }

Visão geral em tempo real:

- Header com nome do device, hora, status WiFi/BT
- Cards com cada sensor: T atual, UR (se SHT31), barra de status colorida (verde=OK, amarelo=atenção, vermelho=alarme)
- Mini-gráfico das últimas amostras
- Botão **Force sync telemetry** (admin): força upload imediato

### 5.3 Histórico

![Web History](screenshots_v4/web_03_history.png){ width=85% }

Tabela de leituras armazenadas:

- Filtros: data inicial, final, sensor, alarmes apenas
- Paginação (50 linhas por página)
- Botão **Export CSV**: baixa o range filtrado
- Botão **Export JSON**: idem em formato estruturado
- Coluna "alarme" marcada quando o ponto disparou alarme

### 5.4 Alarmes

![Web Alarms](screenshots_v4/web_04_alarms.png){ width=85% }

- Tabela com todos os eventos `alarm_open` / `alarm_close` históricos
- Filtros por sensor, range temporal
- Coluna **duração** calculada automaticamente
- Botão **Acknowledge** (admin): marca como reconhecido (não silencia, só registra)

### 5.5 Configuração

![Web Config](screenshots_v4/web_05_config.png){ width=85% }

Tela com TODAS as configs persistentes do device:

- **Sistema**: nome, fuso, NTP, idioma, tema, intervalo de histórico
- **Sensores**: nome amigável de cada slot, mapeamento ROM↔slot, ranges T/UR
- **Telemetria**: server, port, path, batch size, intervalo, formato (JSON/CSV/custom)
- **Calibração**: offset por sensor (correção de leitura)

Salvar persiste em RAM. Botão "**Apply & Reboot**" faz `write memory` + `reload`. Algumas mudanças (ex: porta web) só valem após reboot.

### 5.6 Rede

![Web Network](screenshots_v4/web_06_network.png){ width=85% }

- WiFi: SSID, senha, modo (DHCP/static)
- Static IP (se selecionado): IP, máscara, gateway, DNS
- mDNS: hostname (default `simut`)
- Reset → AP mode (botão de emergência)

### 5.7 Usuários

![Web Users](screenshots_v4/web_07_users.png){ width=85% }

Gerencia até 5 usuários com permissões granulares (admin only):

| Permissão | Bit | Permite |
|---|---|---|
| `PERM_DASHBOARD` | 0x01 | Ver dashboard |
| `PERM_HISTORY` | 0x02 | Ver/exportar histórico |
| `PERM_LOGS` | 0x04 | Ver logs do sistema |
| `PERM_SYS_CONFIG` | 0x08 | Mudar configs do sistema |
| `PERM_NET_CONFIG` | 0x10 | Mudar configs de rede |
| `PERM_FILE_READ` | 0x20 | Listar/baixar arquivos LFS |
| `PERM_FILE_UPLOAD` | 0x40 | Upload de arquivos LFS |
| `PERM_FILE_DELETE` | 0x80 | Deletar arquivos LFS |
| `PERM_USER_MGR` | 0x100 | Criar/remover usuários |
| `PERM_CALIB` | 0x200 | Calibração de sensores |
| `PERM_FULL_ADMIN` | 0xFFFF | TUDO + OTA destrutivo |

**OTA é restrito a `PERM_FULL_ADMIN`** (apenas slot 0 admin) desde v1.0.0. Outros usuários com `PERM_FILE_UPLOAD` podem fazer backup/restore mas não atualizar firmware.

### 5.8 Arquivos

![Web Files](screenshots_v4/web_08_files.png){ width=85% }

Gerencia o filesystem LittleFS interno:

- Lista de pastas + arquivos com tamanho
- **Upload** (PERM_FILE_UPLOAD): envia .lng, .thm, .csv, etc.
- **Download** (PERM_FILE_READ): baixa qualquer arquivo
- **Delete** (PERM_FILE_DELETE)
- **Backup** (qualquer auth): baixa `.bkp` íntegro de TODA a LFS
- **Restore** (PERM_FILE_UPLOAD): aplica `.bkp` salvo
- **Firmware** (admin only): OTA — substitui o `.bin` do app

### 5.9 Licença

![Web License](screenshots_v4/web_09_license.png){ width=85% }

Visualiza chave de licença + permite registrar/upgradear (admin only). Para uso em ambientes regulados que exigem auditoria de instalação.

---

## 6. CLI por Serial USB

A maneira mais robusta de configurar o SIMUT — **independe de WiFi e UI web**. Funciona até em modo de recuperação.

### 6.1 Conexão

Conecte o cabo USB do Pico W ao computador. Surge `/dev/ttyACM0` (Linux/macOS) ou `COMx` (Windows). Use qualquer terminal serial:

| Sistema | Terminal recomendado |
|---|---|
| Linux | `picocom -b 115200 /dev/ttyACM0`, `screen`, `minicom` |
| macOS | `screen /dev/cu.usbmodemXXXX 115200` |
| Windows | PuTTY (Connection type=Serial, Speed=115200, COMx) |

Parâmetros: **115200 8N1, sem flow control**.

### 6.2 Prompt e modos

- `SIMUT>` — modo silencioso (debug off)
- `SIMUT#` — modo verbose (debug on, mostra logs em tempo real)

Alternar: `debug on` / `debug off`.

### 6.3 Comandos por categoria

#### Idioma

```
language pt          Portugues (Brasil)
language en          English
language             Mostra atual
```

#### Monitoramento

```
show system info     Nome, versao, config
show system log      Despeja log binario do flash
show storage stats   FS usado/total
show net status      IP, RSSI, NTP
show themes          Lista temas
show metrics         Heap, uptime, telemetria, sensores, storage
show sensors         Lista sensores mapeados
```

#### Diagnóstico de sensores

```
sensor scan          Varre 1-Wire procurando novos sensores
sensor accept <gpio> Autoriza sensor recém-detectado
sensor wipe <gpio> [confirm]  Apaga histórico do slot
```

#### Configuração de sistema

```
conf system name <valor>           Nome amigavel
conf system ssid <nome>            WiFi SSID
conf system pass <senha>           WiFi senha
conf system timezone <offset>      UTC offset (ex: -3)
conf system ntp <servidor>         Servidor NTP (vazio = default)
conf system theme <id|indice>      Tema da UI
conf system admin reset [confirm]  Gera nova OTP do admin
conf system touch reset [confirm]  Reseta cal touch
conf system factory [confirm]      WIPE TOTAL + reboot
conf system history_interval <min> 1..1440 (default 1)
```

#### Telemetria

```
conf tel server <url>              Endereco do server
conf tel port <n>                  Porta
conf tel path <caminho>            Endpoint
conf tel batch <n>                 Registros por upload (max 50)
conf tel interval <ms>             Intervalo (0=off)
conf tel crypto <on|off>           HTTPS/TLS
conf tel mode <json|csv|custom>    Formato payload
tel sync                           Forca upload agora
tel dump                           Captura proximo payload no console
```

#### Mapeamento de sensores

```
sensor define <gpio> <rom> <hwid> "<nome>"
  Ex: sensor define 4 28AABB.. STM0001 "Geladeira_vacinas"
```

#### Configuração de IP estático

```
conf ip dhcp                       Volta para DHCP
conf ip static                     Ativa modo static
conf ip addr <ipv4>                IP do device
conf ip mask <ipv4>                Mascara
conf ip gateway <ipv4>             Gateway
conf ip dns <ipv4>                 DNS primario
conf net dns auto                  DNS via DHCP
conf net dns manual <ip1> [ip2]    DNS manual
```

#### Limites de sensor

```
conf sensor tmin <gpio> <n>        Limite mínimo de T
conf sensor tmax <gpio> <n>        Limite máximo
conf sensor hmin <gpio> <n>        Mínimo de UR (SHT31)
conf sensor hmax <gpio> <n>        Máximo de UR
conf sensor alarm <gpio> <on|off>  Habilita alarme do sensor
conf sensor ds18b20 resolution <9-12>  Resolução global (9..12 bits)
```

#### Usuários

```
conf user add <name> <pass>        Cria novo usuário
conf user del <name>               Remove
conf user pass <name> <newpass>    Troca senha
```

#### NTP / Hora

```
conf ntp on|off                    Liga/desliga sync
conf time AAAA-MM-DD HH:MM:SS      Seta RTC manual (hora local)
```

#### Manutenção

```
write memory                       Persiste config RAM → flash
reload [confirm]                   Reboot
clear log [confirm]                Apaga log binario
```

#### Web

```
conf web port <1..65535>           Porta do servidor web
```

#### Automação de manual / debug

```
screen <code>                      Força tela TFT direto (bypass touch).
                                   Codes: dash, set, thm, lng, pwd, lic,
                                          sts, alm, gra
touch sim X Y                      Injeta toque simulado em (X, Y).
                                   X 0..319, Y 0..239. Auto-clear ~100ms.
```

### 6.4 Workflow típico

1. `show system info` — confirmar versão
2. `show net status` — confirmar WiFi
3. `conf system ssid ...` + `conf system pass ...` — configurar rede
4. `write memory` — persistir
5. `reload confirm` — reiniciar para aplicar

---

## 7. CLI por Bluetooth

O SIMUT expõe a mesma CLI via **Bluetooth Classic (RFCOMM-SPP)** quando o BT está habilitado. Útil para configurar o device sem cabo USB nem rede WiFi.

### 7.1 Pareamento

O SIMUT aparece como `SIMUT-BT-XXXX` (XXXX = últimos 4 chars do chip_id) na varredura BT do seu dispositivo.

#### 7.1.1 Linux (bluetoothctl)

```bash
bluetoothctl
> power on
> agent on
> scan on
# Aguarde até ver SIMUT-BT-XXXX e copiar o MAC (ex: 28:CD:C1:15:4E:99)
> pair 28:CD:C1:15:4E:99
> trust 28:CD:C1:15:4E:99
> exit

# Bind RFCOMM channel
sudo rfcomm bind 0 28:CD:C1:15:4E:99 1

# Conecta ao terminal
picocom -b 115200 /dev/rfcomm0
```

#### 7.1.2 Windows

1. Configurações → Dispositivos → Bluetooth e outros
2. "Adicionar Bluetooth ou outro dispositivo" → Bluetooth
3. Selecione `SIMUT-BT-XXXX` e parear
4. Após pareado, abra "Mais opções de Bluetooth" → "Portas COM"
5. Anote a porta COM atribuída (ex: COM5)
6. Abra PuTTY → Serial → COM5, 115200 baud

#### 7.1.3 macOS

Não tem suporte nativo a Bluetooth Classic SPP. Use:

- **CoolTerm** (gratuito) — escolha o device pareado
- Ou aplicativos terceiros tipo **Serial** ou **Bluetooth SPP Pro**

#### 7.1.4 Android

App recomendado: **Serial Bluetooth Terminal** (Kai Morich, gratuito):

1. Pareie via Bluetooth do sistema (Configurações → Bluetooth)
2. Abra o app, toque ⋮ → Devices → Bluetooth Classic
3. Selecione SIMUT-BT-XXXX
4. Toque ▶ para conectar
5. Use a CLI normalmente

#### 7.1.5 iOS

iOS **não permite** apps acessarem Bluetooth Classic SPP por restrição da Apple. Use macOS ou Android.

### 7.2 Autenticação

Após conectar, o SIMUT exige autenticação no primeiro comando (exceto `help`):

```
SIMUT-BT> auth admin <senha>
```

Sessão BT autenticada permanece válida enquanto a conexão estiver aberta. Após `disconnect`, próximo connect exige novo `auth`.

### 7.3 Limitações

- Apenas **1 conexão BT simultânea** (limit do BTstack default)
- Throughput: ~5 KB/s — OK para CLI mas lento para log dumps grandes
- BT Classic tem alcance ~10 m (sem LE — não dá para usar BLE scan/advertising)

---

## 8. OTA — Atualização de Firmware

Atualização via interface web. **Restrita a admin** (perms = 0xFFFF) desde v1.0.0.

### 8.1 Workflow

1. Web → **Files** → botão **Firmware**
2. Modal alerta: "OTA reformata LittleFS. Configs preservadas; history/themes/calib APAGADOS." Continue se OK.
3. Backup automático: SIMUT baixa um `.bkp` da LFS atual via browser. **Salve isso!** É seu seguro pós-OTA.
4. Selecione o `.bin` do firmware novo (gerado pelo seu build PIO/CLI/CI)
5. Validação client-side: tamanho válido + boot2 CRC-32/MPEG-2 OK + versão SIMUT_VERSION encontrada
6. Confirmação final com sumário (versão atual → nova, tamanho)
7. Upload (~30 s para 1 MB de firmware)
8. Apply: device pisca display "Aplicando firmware..." e reboota
9. ~60 s depois, web volta — login + restore manual do backup baixado em (3) caso queira recuperar themes/lang/history

### 8.2 Segurança da rota

- **Endpoint POST `/api/restore?op=stage`**: pre-check de admin ANTES de apagar 1 MB de flash
- **Endpoint POST `/api/ota/apply`**: admin-only (response 403 "Forbidden — admin only" caso contrário)
- **Endpoint GET `/api/ota/staging_test`**: selftest destrutivo, admin-only

### 8.3 Recuperação em caso de brick

Se o boot pós-OTA falhar (rara, mas possível):

1. Pressione e segure **BOOTSEL** no Pico W
2. Plugue o USB (mantendo BOOTSEL pressionado)
3. Aparece volume `RPI-RP2`
4. Arraste o `.uf2` do firmware ANTERIOR (release v1.0.0 baixável do GitHub) **OU** use `picotool load -x firmware.uf2`
5. Boot recupera automaticamente. LFS preservada (pads de erase OTA não tocam config).

### 8.4 Confiabilidade validada

- **F-RESTORE FECHADO** (v1.0.0): 98/100 PASS no loop de 100 iterações de OTA + restore com config real preservada (canonical 455 KB, 32 arquivos, 29 críticos)
- **0 ConnResets**, 0 recoveries forçados, 4.2 s avg/restore
- Auto-reboot via `LogManager::safeReboot()` pós-apply

---

## 9. Backup e Restauração

### 9.1 Formato BKP1

Todos os backups SIMUT são `.bkp` no formato custom **BKP1**:

```
[Header 40 bytes]
  magic         u32     "BKP1" (0x31504B42)
  schema_version u16    1
  reserved      u16
  chip_id       u8[8]   ID único do RP2040
  firmware_ver  u32     vM.m.p encoded
  timestamp     u32     Unix epoch UTC
  payload_size  u32
  payload_crc32 u32
  header_crc32  u32
[Payload TLV]
  Para cada arquivo:
    path_length u16
    content_length u32
    path        char[path_length]
    content     u8[content_length]
```

CRC32 = polinômio EDB88320 (gzip-compatível). Validador standalone Python: `tools/verify_backup.py`.

### 9.2 Operações via web

| Botão (Files) | Endpoint | Permissão |
|---|---|---|
| Backup | `GET /api/backup` | qualquer auth |
| Restore | `POST /api/restore?op=apply` | `PERM_FILE_UPLOAD` |
| Firmware (OTA) | `POST /api/restore?op=stage` + `/api/ota/apply` | **admin only** |

### 9.3 Operações via curl

```bash
# Login (pré-hash)
PWHASH=$(echo -n "$ADMIN_PASS" | sha256sum | awk '{print $1}')
NONCE=$(curl -s http://192.168.3.195/api/login_init | jq -r '.nonce')
curl -s -c cookies.txt -d "user=admin&pass=$PWHASH&nonce=$NONCE" \
     http://192.168.3.195/api/login

# Backup
curl -s -b cookies.txt -o backup.bkp http://192.168.3.195/api/backup

# Restore
curl -s -b cookies.txt -F "file=@backup.bkp" \
     "http://192.168.3.195/api/restore?op=apply"
```

### 9.4 Recomendação de boas práticas

- **Backup mensal** (mínimo) por device em produção
- Backup **antes de qualquer mudança crítica** (OTA, factory reset, troca de hardware)
- Armazene em **mídia separada** (não no mesmo computador que admin SIMUT)
- Teste o restore periodicamente em device de teste
- chip_id no backup garante que restore não funciona em device diferente — para clone, gere um config equivalente do zero

---

## 10. Segurança

Documento completo: `SECURITY.md` no repositório.

### 10.1 Modelo de auth

- **Web**: SHA-256(senha) hex no client → POST → server compara com hash armazenado (HMAC + salt + 2500 rounds, pepper = chip_id único)
- **Bluetooth**: comando `auth admin <senha>` na CLI BT (texto plano local sob criptografia BT)
- **Serial USB**: sem auth (acesso físico = trust)
- **Touch display**: PIN curto (default 1234) gateway para o Settings menu

### 10.2 Lockout

5 tentativas falhas de login web → lockout 30 s (exponencial até 1 h). Por IP. CLI/BT não têm lockout.

### 10.3 OTA admin-only

Desde v1.0.0: stage + apply requerem `PERM_FULL_ADMIN`. Outros usuários (com PERM_FILE_UPLOAD) podem fazer backup/restore mas **não atualizar firmware**.

### 10.4 Audit log

Todos eventos de segurança registrados em `system.blog` (binário, rotação 800 entradas):

- `LOGIN_OK`, `LOGIN_FAIL`, `LOGOUT`
- `CONFIG_CHANGED` (com user ID + descrição)
- `ALARM_OPEN`, `ALARM_CLOSE`
- `OTA_STAGE`, `OTA_APPLY`
- `FACTORY_RESET`, `LOG_CLEAR`

Visualizar: web → Dashboard → "Logs" tab. Ou CLI: `show system log`.

### 10.5 Boas práticas operacionais

1. **Troque a senha admin** imediatamente após primeiro boot
2. **Não compartilhe** senha via WhatsApp/email — use gerenciador (Bitwarden, 1Password)
3. **Crie usuários adicionais** com permissões mínimas necessárias (princípio do menor privilégio)
4. **Coloque o SIMUT em VLAN/subnet isolada** se possível — limita exposição
5. **Backup antes de qualquer ação destrutiva**
6. **Audit log review mensal** — busque eventos `LOGIN_FAIL` repetidos (indício de brute force)

---

## 11. Solução de Problemas

### 11.1 Display não acende

- Cabo USB defeituoso? Teste outro
- Pico W queimado? LED verde do board pisca?
- Firmware corrompido? Faça recovery via BOOTSEL (§8.3)

### 11.2 WiFi não conecta

1. `show net status` via serial → mostra `IP: (IP unset)` + `RSSI: -100`?
2. Verifique SSID + senha (case sensitive): `conf system ssid` / `conf system pass`
3. `write memory` + `reload confirm`
4. Se persistir: tente AP mode (segure touch 3 s no boot) e reconfigure via web

### 11.3 mDNS (`simut.local`) não responde

- `avahi-daemon` rodando (Linux)? `systemctl status avahi-daemon`
- Bonjour Print Services instalado (Windows)?
- Roteador bloqueia multicast (alguns isolam SSIDs)? Teste em outro WiFi
- Reinicie o SIMUT — pode levar até 30 s para mDNS responder após boot

### 11.4 Senha esquecida

- **Admin web**: serial → `conf system admin reset confirm` → captura nova OTP no terminal → web `/login_chpass` com OTP
- **PIN do display**: serial → `conf system touch reset confirm` (volta para default 1234)
- **Total**: `conf system factory confirm` (apaga TUDO + reboot, OTP novo no display)

### 11.5 Sensor não aparece

1. `show sensors` — está mapeado?
2. `sensor scan` — hardware é detectado?
3. Cabo 1-Wire OK? DS18B20 precisa pull-up 4.7 kΩ no data line
4. Endereço ROM correto? `sensor define <gpio> <rom_hex> <slot> "<nome>"`

### 11.6 Telemetria não envia

- `show metrics` mostra `Falhas` > 0? Veja CLI debug logs
- Servidor acessível? `ping <server>` no host
- Endpoint correto? `conf tel server <url>` + `conf tel path <path>`
- Cert SSL inválido? Tente `conf tel crypto off` (HTTP plano) para testar
- `tel sync` — força tentativa imediata e mostra erro detalhado

### 11.7 OTA falhou

1. Sintoma: device offline pós-flash
2. Recovery via BOOTSEL (§8.3)
3. Restaure backup (Files → Restore com .bkp salvo pré-OTA)

### 11.8 Heap baixo / OOM

- `show metrics` → Heap < 20 KB?
- Reboot resolve a curto prazo
- Causa raiz: vazamento (relate via GitHub Issues com `show system log`)

### 11.9 Touch erratico

- Recalibrar: Settings → Touch Cal → siga instruções (8 pontos)
- Se persistir: `conf system touch reset confirm` (default sane) + cal de novo
- Hardware: filme protetor superficial pode causar desvio

### 11.10 Debug avançado via UART1 (GP8/GP9)

Se USB-CDC do SIMUT travar (`F-USB-CDC-DEAD`), use UART1 via outro Pico (PicoHand) ou USB-UART adapter:

- Wiring: SIMUT GP8 (TX) → adapter RX, GND comum
- Baud: 115200
- Útil para capturar boot logs pós-watchdog quando USB CDC fica mute

---

## 12. Apêndices

### A. Quick Reference — CLI

```
# Status
help                      Lista comandos
show system info          Versão + config
show net status           IP + RSSI
show metrics              Heap + telemetria + sensores
show sensors              Lista sensores
show storage stats        FS usado/total

# Config básica
conf system name "X"      Nome amigável
conf system ssid "rede"   WiFi SSID
conf system pass "1234"   WiFi senha
conf system timezone -3   Fuso UTC
conf system theme 2       Theme idx
write memory              Persiste
reload confirm            Reboot

# Sensores
sensor scan               Detecta novos
sensor accept <gpio>      Autoriza
conf sensor tmin 4 -25    Tmin do sensor GPIO 4
conf sensor tmax 4 -15    Tmax
conf sensor alarm 4 on    Liga alarme

# Reset
conf system admin reset confirm   OTP nova
conf system touch reset confirm   PIN default
conf system factory confirm       APAGA TUDO

# Automação (debug)
screen <code>             Força tela TFT
touch sim X Y             Simula toque

# Códigos de tela
dash  = Dashboard
set   = Settings menu
thm   = Themes
lng   = Language
pwd   = Password (PIN)
lic   = License
sts   = System status
alm   = Alarms
gra   = Graph
```

### B. Permission Bitmask

```
PERM_DASHBOARD    0x0001  Ver dashboard
PERM_HISTORY      0x0002  Ver/exportar histórico
PERM_LOGS         0x0004  Ver logs
PERM_SYS_CONFIG   0x0008  Mudar configs
PERM_NET_CONFIG   0x0010  Mudar rede
PERM_FILE_READ    0x0020  Listar/baixar arquivos
PERM_FILE_UPLOAD  0x0040  Upload arquivos
PERM_FILE_DELETE  0x0080  Deletar arquivos
PERM_USER_MGR     0x0100  Gerenciar users
PERM_CALIB        0x0200  Calibração
PERM_FULL_ADMIN   0xFFFF  Admin total + OTA
```

### C. Status codes do BKP1

```
0  OK
1  Bad magic
2  Unsupported schema
3  Header CRC mismatch
4  Payload truncated
5  Payload CRC mismatch
6  chip_id mismatch (backup de outro device)
7  Path inválido
8  Path muito longo
9  Erro de I/O
10 Internal error
```

### D. Endpoints HTTP principais

```
GET  /api/login_init          Solicita nonce (auth)
POST /api/login               Login (user, pass=sha256, nonce)
POST /api/login_chpass        Troca senha (oldpass, newpass)
GET  /api/perms               Permissões da sessão atual
GET  /api/info                Snapshot do device (json)
GET  /api/screenshot          BMP 320x240 do display
GET  /api/screenshot_chunk?n  Chunk de 16 rows com CRC32
GET  /api/backup              .bkp íntegro da LFS
POST /api/restore?op=validate Validar .bkp sem aplicar
POST /api/restore?op=apply    Restaurar .bkp (PERM_FILE_UPLOAD)
POST /api/restore?op=stage    Stage de firmware (admin only)
POST /api/ota/apply           Apply firmware (admin only)
GET  /api/files               Lista arquivos LFS
POST /api/upload?uploadDir=   Upload de arquivo
GET  /download?file=          Download
POST /api/delete?file=        Delete
POST /api/mkdir               mkdir
POST /api/commit_all          Salva config (sys, alarms)
POST /api/set_time            Seta RTC manual
```

### E. Recursos online

- **Repositório**: https://github.com/angeloINTJ/SIMUT
- **Issues + suporte**: GitHub Issues
- **Releases (binários)**: https://github.com/angeloINTJ/SIMUT/releases
- **Documentação técnica**: `STABILITY_PLAN.md`, `SECURITY.md` no repo

---

**Fim do manual — v1.0.0 — **
