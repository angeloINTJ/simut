# Manual do CLI — SIMUT v1.5.4-beta

## Visão geral

O SIMUT adota o modelo **Cisco IOS** com 4 modos hierárquicos. Cada modo tem seu
próprio prompt e conjunto de comandos. O caractere `?` mostra os comandos
disponíveis no modo atual.

### Árvore de modos

```
SIMUT>                   User EXEC — diagnóstico, telemetria, help
  └── enable ─────────────────────────────────────────────────────┐
SIMUT#                   Privileged EXEC — manutenção, salvar,    │
  │                      reiniciar, sensores, entrada p/ config   │
  ├── configure terminal ────────────────────────────────────┐    │
  │  SIMUT(config)#     Global Config — sistema, rede,       │    │
  │                     telemetria, usuários                 │    │
  │    ├── sensor <N> ─────────────────────────────────┐    │    │
  │    │  SIMUT(config-sensor-N)#  Sensor — tipo,      │    │    │
  │    │                           pinos, alarmes      │    │    │
  │    ├── exit → volta p/ SIMUT#                      │    │    │
  │    └── end ────────────────────────────────────────┘    │    │
  ├── disable → volta p/ SIMUT>                                 │
  └── exit → volta p/ SIMUT>                                    │
```

| Transição | Comando |
|-----------|---------|
| `SIMUT>` → `SIMUT#` | `enable` |
| `SIMUT#` → `SIMUT>` | `disable` ou `exit` |
| `SIMUT#` → `SIMUT(config)#` | `configure terminal` |
| `SIMUT(config)#` → `SIMUT#` | `exit` ou `end` |
| `SIMUT(config)#` → `SIMUT(config-sensor-N)#` | `sensor <N>` |
| `SIMUT(config-sensor-N)#` → `SIMUT(config)#` | `exit` |
| `SIMUT(config-sensor-N)#` → `SIMUT#` | `end` |

---

## 1. Modo User EXEC — `SIMUT>`

Prompt padrão após o boot. Modo somente leitura — nenhum comando altera
configuração.

### Diagnóstico

| Comando | Descrição |
|---------|-----------|
| `show sensors` | Lista todos os slots de sensor ativos com tipo, GPIO, canais, nome, alarmes |
| `show system info` | Nome do dispositivo, versão do firmware, serial, resolução DS18B20, WiFi, fuso, NTP |
| `show system log` | Despeja o log de eventos do Flash (até 2000 linhas) |
| `show net status` | Endereço IP, RSSI (sinal WiFi) |
| `show metrics` | Métricas operacionais: uptime, heap, heap mínimo, WiFi/MQTT reconexões, telemetria (envios, falhas, bytes), leituras de sensores, saves de config |
| `show storage stats` | Estatísticas do sistema de arquivos Flash (LittleFS) |
| `show themes` | Lista temas de UI disponíveis com índice e nome |
| `show sensor types` | Lista drivers de sensor compilados no firmware com canais e requisitos de pinos |
| `show gpio` | Mapa dos 16 GPIOs — livre vs. ocupado por slot |
| `gpio` | Atalho para `show gpio` |

### Telemetria

| Comando | Descrição |
|---------|-----------|
| `tel sync` | Força envio imediato da telemetria pendente |
| `tel dump` | Arma dump do próximo payload de telemetria no console (diagnóstico) |

### Sessão

| Comando | Descrição |
|---------|-----------|
| `language pt` | Interface em Português (Brasil) |
| `language en` | Interface em English |
| `language` | Mostra o idioma atual |
| `help`, `ajuda`, `?` | Lista comandos disponíveis neste modo |

### Navegação

| Comando | Descrição |
|---------|-----------|
| `enable` | Entra no modo Privileged EXEC (`SIMUT#`) |

---

## 2. Modo Privileged EXEC — `SIMUT#`

Modo de manutenção e porta de entrada para configuração. **Todos os comandos
do modo User EXEC continuam disponíveis.**

### Persistência e sistema

| Comando | Descrição |
|---------|-----------|
| `write memory` | Salva a configuração RAM no Flash (equivalente ao `copy run start` do Cisco) |
| `reload` | Reinicia o sistema. Requer confirmação: `reload confirm` |
| `clear log` | Apaga os arquivos de log do Flash. Requer confirmação: `clear log confirm` |

### Depuração

| Comando | Descrição |
|---------|-----------|
| `debug on` | Ativa stream de logs no console. Prompt e modo não mudam, mas linhas de log aparecem inline |
| `debug off` | Desativa stream de logs. Console fica limpo |
| `debug` | Mostra estado atual (ON/OFF) |

### Sensores — operações

| Comando | Descrição |
|---------|-----------|
| `sensor scan` | Varredura de hardware nos GPIOs 0–16 (OneWire → DHT22) + I2C (BME280 nos pinos 4,5) |
| `sensor accept <gpio>` | Autoriza sensor OneWire (DS18B20) detectado no pino. Lê ROM, aplica calibração do `calib.csv`, salva no Flash |
| `sensor define <gpio> <rom> <hwid> <nome>` | Define manualmente um sensor (legado). ROM em hex (16 chars, `0000000000000000` p/ não-OneWire). Ex: `sensor define 4 28AA123456789ABC DS4 "Freezer 1"` |
| `sensor wipe <gpio>` | Reseta o epoch do histórico do sensor (gráfico começa do zero). Requer confirmação: `sensor wipe <gpio> confirm` |
| `sensor reschema confirm` | Religa o histórico aos slots como estão configurados **agora**. Use após trocar o `hwid` de um sensor: o schema fica gravado no cabeçalho do `.sim4` e o casamento é por `hwid`, então uma troca faz o histórico gravar registros vazios em silêncio até o arquivo do dia seguinte. **Destrutivo** — recria o arquivo de hoje e perde os registros anteriores do dia; por isso exige `confirm`. O aviso `code=515` no log indica que isso é necessário |
| `sensor <slot> <campo> <valor>` | Configura sensor diretamente do modo privilegiado (atalho). Campos: `type`, `create`, `name`, `hwid`, `pin`, `active`, `alarm`, `tmin`, `tmax`, `hmin`, `hmax`. Ver Seção 4 |

### Telemetria

| Comando | Descrição |
|---------|-----------|
| `tel reset` | Reseta o cursor de telemetria (cache RAM + arquivo Flash). Próximo envio cobre até 30 dias para trás |

### Factory e admin

| Comando | Descrição |
|---------|-----------|
| `conf system factory` | Factory reset — apaga TODA a config e reinicia. Requer confirmação: `conf system factory confirm` |
| `conf system admin reset` | Reseta a senha do admin para o padrão. Requer confirmação: `conf system admin reset confirm` |
| `conf system touch reset` | Reseta a calibração do touch para valores de fábrica. Requer confirmação: `conf system touch reset confirm` |

### Tela TFT

| Comando | Descrição |
|---------|-----------|
| `screen <nome>` | Navega diretamente para uma tela TFT. Nomes: `dash` (dashboard), `set` (configurações), `thm` (temas), `lng` (idioma), `pwd` (senha), `lic` (licença), `sts` (status), `alm` (alarmes), `gra` (gráfico), `touchcal` (calibração touch), `touchsens` (sensibilidade touch), `offset` (offset do display) |
| `touch sim <X> <Y>` | Injeta toque simulado na tela (X: 0–319, Y: 0–239). Uso: automação de screenshots |

### Navegação

| Comando | Descrição |
|---------|-----------|
| `configure terminal` | Entra no modo Global Config (`SIMUT(config)#`) |
| `disable` | Volta ao modo User EXEC |
| `exit` | Volta ao modo User EXEC (mesmo que `disable`) |

---

## 3. Modo Global Config — `SIMUT(config)#`

Modo de configuração do sistema. Comandos alteram apenas a RAM — execute
`write memory` (no `SIMUT#`) para persistir no Flash. Use `do <cmd>` para
executar qualquer comando do modo privilegiado sem sair do config.

### Sistema

| Comando | Descrição | Range |
|---------|-----------|-------|
| `system name <nome>` | Nome do dispositivo (aparece no dashboard, WiFi, etc.) | 1–31 chars, sem controle |
| `system theme <id>` | Tema da UI. `<id>` pode ser nome (`Default`) ou índice numérico | `show themes` lista opções |
| `system timezone <offset>` | Fuso horário UTC | −12 a +14 |
| `system ntp <servidor>` | Servidor NTP. Vazio = `pool.ntp.org` | max 31 chars |
| `system history_interval <min>` | Intervalo entre registros de histórico | 1–1440 minutos |
| `language <pt\|en>` | Idioma da interface CLI e display | `pt` ou `en` |
| `time <AAAA-MM-DD> <HH:MM:SS>` | Ajuste manual do relógio (hora local) | — |
| `ds18b20 resolution <bits>` | Resolução global para sensores DS18B20 | 9–12 bits |

### Rede

| Comando | Descrição | Range |
|---------|-----------|-------|
| `wifi ssid <nome>` | Nome da rede WiFi (case-sensitive) | max 31 chars |
| `wifi pass <senha>` | Senha da rede WiFi | max 31 chars |
| `ip dhcp` | Usar DHCP para obter IP | — |
| `ip static` | Usar IP estático | — |
| `ip addr <ipv4>` | Endereço IP estático | formato `x.x.x.x` |
| `ip mask <ipv4>` | Máscara de rede | formato `x.x.x.x` |
| `ip gateway <ipv4>` | Gateway padrão | formato `x.x.x.x` |
| `ip dns <ipv4>` | Servidor DNS | formato `x.x.x.x` |
| `dns auto` | DNS via DHCP | — |
| `dns manual <ip1> [ip2]` | DNS manual (primário, secundário opcional) | formato `x.x.x.x` |
| `ntp on` | Sincronizar relógio via NTP | — |
| `ntp off` | Não sincronizar relógio | — |
| `web port <porta>` | Porta do servidor web (aplica após reload) | 1–65535 |

### Telemetria

| Comando | Descrição | Range |
|---------|-----------|-------|
| `tel server <url>` | URL do servidor de telemetria | max 63 chars |
| `tel port <porta>` | Porta do servidor | 1–65535 |
| `tel path <caminho>` | Caminho do endpoint (ex: `/api/v1/data`) | max 31 chars |
| `tel batch <n>` | Registros por upload | 1–50 |
| `tel interval <ms>` | Intervalo entre uploads automáticos (0 = desligado) | ≥ 0 |
| `tel crypto on` | Ativar SSL/HTTPS para telemetria | — |
| `tel crypto off` | Desativar SSL/HTTPS | — |
| `tel mode json` | Payload em formato JSON | — |
| `tel mode csv` | Payload em formato CSV | — |
| `tel mode custom` | Payload em formato customizado | — |

### Usuários (web interface)

| Comando | Descrição |
|---------|-----------|
| `user add <nome> <senha>` | Criar novo usuário para a interface web |
| `user del <nome>` | Remover usuário (admin não pode ser removido) |
| `user pass <nome> <nova_senha>` | Alterar senha de um usuário |

### Sensor

| Comando | Descrição |
|---------|-----------|
| `sensor <N>` | Entra no modo Sensor Config para o slot `<N>` (0–15) |

### Sub-comandos de conveniência

| Comando | Descrição |
|---------|-----------|
| `do <comando>` | Executa um comando do modo privilegiado sem sair do config. Ex: `do show sensors` |
| `show <...>` | Todos os comandos `show` funcionam diretamente no modo config |
| `exit` | Volta ao modo Privileged EXEC |
| `end` | Volta ao modo Privileged EXEC (atalho direto) |

---

## 4. Modo Sensor Config — `SIMUT(config-sensor-N)#`

Configuração de um sensor individual. **Comandos curtos** — o slot `<N>` é
implícito. Todos os comandos `show` globais continuam disponíveis.

### Tipo e criação

| Comando | Descrição |
|---------|-----------|
| `type <ds18b20\|dht22\|bme280>` | Define o tipo de driver do sensor |
| `create <ds18b20\|dht22\|bme280>` | Reseta o slot e configura para o tipo. Mostra requisitos de pinos e GPIOs livres |

### Identidade

| Comando | Descrição | Range |
|---------|-----------|-------|
| `name <nome>` | Nome amigável (aparece no dashboard e tabelas) | 1–31 chars |
| `hwid <id>` | Hardware ID interno (chave para `calib.csv`) | max 15 chars |

### GPIO

| Comando | Descrição |
|---------|-----------|
| `pin <idx> <gpio>` | Atribui um GPIO ao pino `<idx>` do sensor. `<idx>`: 0–3. `<gpio>`: 0–15. Ex: `pin 0,5` atribui GPIO5 ao pino 0 |

Requisitos por tipo:
- **DS18B20**: 1 pino (idx=0, 1-Wire)
- **DHT22**: 1 pino (idx=0, Data)
- **BME280/BMP280**: 2 pinos (idx=0=SDA, idx=1=SCL)

### Ativação

| Comando | Descrição |
|---------|-----------|
| `active on` | Ativar slot. Requer todos os pinos atribuídos |
| `active off` | Desativar slot (libera GPIOs) |

### Alarmes

| Comando | Descrição | Range |
|---------|-----------|-------|
| `alarm on` | Ativar alarmes para este sensor | — |
| `alarm off` | Desativar alarmes | — |
| `tmin <valor>` | Limite mínimo de temperatura | float (ex: `-10.0`) |
| `tmax <valor>` | Limite máximo de temperatura | float (ex: `50.0`) |
| `hmin <valor>` | Limite mínimo de umidade (DHT22/BME280) | float (ex: `30.0`) |
| `hmax <valor>` | Limite máximo de umidade (DHT22/BME280) | float (ex: `70.0`) |

### Navegação

| Comando | Descrição |
|---------|-----------|
| `show sensors` | Mostra a tabela completa de sensores |
| `show gpio` | Mostra o mapa de GPIOs |
| `do <comando>` | Executa comando do modo privilegiado |
| `exit` | Volta ao modo Global Config |
| `end` | Volta direto ao modo Privileged EXEC |

---

## 5. Comandos de conveniência (qualquer modo)

| Comando | Descrição |
|---------|-----------|
| `?` | Lista comandos disponíveis no modo atual |
| `help` | Mesmo que `?` |
| `ajuda` | Mesmo que `?` (atalho PT) |

---

## 6. Confirmação de comandos destrutivos

Comandos que causam perda de dados ou reinicialização exigem o sufixo
` confirm` (com espaço). Exemplos:

```
reload confirm
clear log confirm
conf system factory confirm
conf system admin reset confirm
conf system touch reset confirm
sensor wipe 3 confirm
```

Sem o sufixo, o comando exibe um aviso e não executa.

---

## 7. Fluxo de trabalho típico

### Primeira configuração de sensores

```
SIMUT> enable
SIMUT# configure terminal
SIMUT(config)# sensor 0
SIMUT(config-sensor-0)# create bme280
SIMUT(config-sensor-0)# pin 0,0
SIMUT(config-sensor-0)# pin 1,1
SIMUT(config-sensor-0)# name BMP280-Ambiente
SIMUT(config-sensor-0)# active on
SIMUT(config-sensor-0)# exit
SIMUT(config)# sensor 1
SIMUT(config-sensor-1)# create dht22
SIMUT(config-sensor-1)# pin 0,2
SIMUT(config-sensor-1)# name DHT22-Sala
SIMUT(config-sensor-1)# alarm on
SIMUT(config-sensor-1)# tmin 15.0
SIMUT(config-sensor-1)# tmax 35.0
SIMUT(config-sensor-1)# hmin 30.0
SIMUT(config-sensor-1)# hmax 70.0
SIMUT(config-sensor-1)# end
SIMUT# write memory
```

### Configuração de rede

```
SIMUT> enable
SIMUT# configure terminal
SIMUT(config)# wifi ssid MinhaRede
SIMUT(config)# wifi pass MinhaSenha
SIMUT(config)# ntp on
SIMUT(config)# system timezone -3
SIMUT(config)# end
SIMUT# write memory
SIMUT# reload confirm
```

### Diagnóstico rápido

```
SIMUT> show sensors
SIMUT> show net status
SIMUT> show metrics
SIMUT> tel dump
```

---

## 8. Referência rápida de modos

| Prompt | Significado | Como sair |
|--------|-------------|-----------|
| `SIMUT>` | User EXEC — só leitura | — |
| `SIMUT#` | Privileged EXEC — manutenção | `disable` ou `exit` |
| `SIMUT(config)#` | Global Config — config do sistema | `exit` ou `end` |
| `SIMUT(config-sensor-N)#` | Sensor Config — config de 1 sensor | `exit` (p/ config) ou `end` (p/ #) |
