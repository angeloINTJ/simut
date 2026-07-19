# Changelog

[English](CHANGELOG.md) | **Português**

Todas as mudanças notáveis do firmware SIMUT.

## v1.5.0-beta (2026-07-19)

### Configuração de Hardware Centralizada — `simut_config.h`

- **Arquivo único de configuração** — Todas as opções configuráveis agora ficam em `src/simut_config.h`: tipo de display, pinos, sensores, Bluetooth, mDNS, pacotes de temas, pino do buzzer e limites avançados. Antes espalhadas por mais de 8 arquivos.
- **9 seções documentadas** — Tipo de display, pinos TFT, pinos Alpha/HD44780 (I2C e paralelo), buzzer, sensores, comunicação, pacotes de temas, pino padrão 1-Wire, limites avançados. Cada opção com comentários explicativos.
- **Guardas `#ifndef` em todas as definições** — Cada define permite override em tempo de compilação via flags `-D` no `platformio.ini`. Os defaults correspondem à configuração de release existente.
- **Retrocompatível** — Headers de configuração antigos (`DisplayConfig.h`, `SensorConfig.h`) delegam ao `simut_config.h`. Todas as cadeias de `#include` preservadas. Sem breaking changes.
- **Suporte a Arduino IDE** — Guarda `__has_include("simut_arduino_config.h")` no topo do `simut_config.h` para pacotes de release. Configs de release simplificadas para definir overrides antes do include.

### Limpeza do Sistema de Build

- **`platformio.ini` desduplicado** — Flags de sensores e features removidos do `[pico_base]` (agora no `simut_config.h`). Apenas overrides específicos por ambiente permanecem no `[env:pico_w_alpha]`.
- **Pacotes de release simplificados** — `release/*/simut_arduino_config.h` agora inclui `simut_config.h` em vez de duplicar todas as definições.

### Correções de Bugs

- **BluetoothManager.cpp** — Adicionado guarda `#if SIMUT_BLUETOOTH` ausente em todas as implementações dos métodos. Previne erros de redefinição quando `SIMUT_BLUETOOTH=0` e o arquivo é compilado (builds debug).
- **HD44780_16x2.h** — Envolvido `_initLcd()` e seu ponto de chamada em `#if HD44780_MODE_PARALLEL`. A sequência de inicialização paralela 4-bit era incorretamente compilada no modo I2C.

### Seleção de Pacotes de Temas

- **Movida para `simut_config.h`** — Pacotes de temas (`SIMUT_THEMES_HEALTH`, `_PRO`, `_MEDICAL`, `_SAFETY`, `_RETRO`, `_NATURE`, `_UTILITY`) agora são habilitados descomentando linhas no arquivo de configuração, não editando `Themes.cpp`.
- **`Themes.h` inclui `simut_config.h`** — Flags de temas são visíveis onde quer que `Themes.h` seja incluído.

### Coexistência de Recursos PIO — Resolução de Conflitos Multi-Sensor

- **Conflito no pio0 identificado** — OneWirePIO (DS18B20, 27 slots de instrução) + WirePIO (BME280 I2C, 32 slots) = 59 > 32 disponíveis. WirePIO carregava primeiro, bloqueando OneWirePIO (DS18B20 morria — sem fallback GPIO).
- **Saturação de SMs no pio1** — 2× DHT22 (2 SMs) + CYW43 WiFi (1 SM) + BuzzerPIO (2 SMs) = 5 > 4 SMs. Resolvido pelo fallback automático do BuzzerPIO para pio0.
- **Correção no `BME280Driver.h`** — Adicionado `forceGPIO(true)` antes de cada `begin()`. BMx280PIO agora usa apenas GPIO bit-bang I2C (sem PIO+DMA), mantendo os slots de instrução do pio0 livres para OneWirePIO. Modo GPIO é um pouco mais lento mas totalmente confiável.
- **`docs/PIO_ANALYSIS.md`** — Análise completa de alocação de recursos PIO cobrindo todas as bibliotecas (OneWirePIO, DHTBus, WirePIO, BuzzerPIO, CYW43), orçamento de slots de instrução por bloco, contagem de state machines, canais DMA, cenários de conflito e mecanismos de resolução.

### Validação em Hardware — Teste de Coexistência com 4 Sensores

Testado no Pico W com display TFT + buzzer + WiFi:

| Sensor | GPIOs | Tipo | Status |
|--------|-------|------|--------|
| BMP280 | GP0 (SDA), GP1 (SCL) | driver BME280 | ✅ Lendo (GPIO bit-bang) |
| DHT22 #1 | GP2 | DHT22 | ✅ Detectado, lendo |
| DHT22 #2 | GP3 | DHT22 | ✅ Detectado, lendo |
| DS18B20 | GP4 | DS18B20 | ✅ Detectado (ROM: 283C21…), lendo |

- **WiFi**: Conectado (RSSI -45 dBm), servidor web respondendo
- **PIO após correção**: pio0 31/32 slots (OneWirePIO + BuzzerPIO fallback), pio1 23/32 slots (DHTBus×2 + CYW43)
- **Heap**: 94,3 KB estável, sem vazamentos após 11+ minutos de operação contínua
- **Leituras**: 857/916 OK (93,2%), 59 erros concentrados na inicialização
- Todos os 4 sensores configurados e ativados via CLI, configuração persistida no flash

### Orçamento de Flash

- **Release (TFT + todos sensores + mDNS)**: 94,1% (982604 / 1044480 bytes)
- **Alpha (HD44780 paralelo + todos sensores + mDNS)**: 85,4% (891920 / 1044480 bytes)
- **RAM (release)**: 35,8% (93760 / 262144 bytes)

## v1.2.0-beta (2026-06-06)

### Subsistema OTA — Atualização Completa para v4.6.2

- **F-OTA-BOOTLOOP corrigido** — Loop20 OTA 100% PASS. Causa raiz: deadlock reentrante do LittleFS durante escrita do README.md + inicialização do Core 1 adiada para pós-WiFi + safeReboot usa MMIO idêntico ao applier_reboot.
- **F-RESTORE** — Backup/restore confiável via API (98/100 PASS). Snapshot de configuração preservado através do apply OTA com integridade CRC32. Reescreve atômica do calib.csv com VERSION=epoch.
- **F-RAM-SLIM** — Uso de RAM 49,6% → 33,7% (-41 KB / -16pp). Eliminados graph caches, removidos glifos de fonte não usados, buffers compartilhados.
- **F-TEL-HTTPS-RESILIENT** — Corrige crash+reboot quando servidor HTTPS cai. Heap budget mais conservador para conexões TLS.
- **F-OTA-STAGE-NOBLOCK + F-FLASH-DIET** — Corrige TCP drop durante staging de firmware OTA. Upload não-bloqueante com chunk sizing adaptativo.
- **F-DISPLAY-MARGINS** — `fillMarginsBlack` + override do `fillScreen` no `TftWithOffset` para bordas limpas.
- **F-BOOT-CYW43-CYCLE** — Power-cycle do `WL_REG_ON` sempre no `setup()` para inicialização confiável do WiFi.
- **F-SCREENSHOT-INTEGRITY** — Elimina perda/corrupção de linhas no `/api/screenshot` via leitura multi-amostra com voto majoritário.
- **F-OTA-ADMIN-ONLY** — Endpoints OTA exigem `PERM_FULL_ADMIN`.
- **F-TEL-ADAPTIVE** — Telemetria com vazão adaptativa (dimensionamento de lote apenas no backend).
- **F-UI-OTA-FLOW** — Mensagens de UX para OTA + restore com feedback de progresso.

### Documentação & Ferramentas

- **Glossário** — `docs/GLOSSARY.md` decodificando todas as tags inline (F-\*, BUG-\*, SEC-\*, CON-\*, DOC-\*, REF-\*) usadas nos comentários do código.
- **Limpador de comentários** — `tools/cleanup_comments.py` remove referências de histórico de versão e marcadores de changelog dos comentários para preparação de releases.

### Orçamento de Flash

| Configuração | Flash |
|---|---|
| Ambos sensores ON | 1031464 (98,8%) |
| Apenas DS18B20 | ~1028400 (98,5%) |
| Apenas DHT22 | ~1029500 (98,6%) |
| Ambos OFF | ~1024900 (98,1%) |

### Testes

49/49 testes passando (27 validators + 22 HistoryCodec).

## v1.1.0-beta (2026-06-06)

### Arquitetura de Sensores — Sistema Modular de Drivers

- **Flags de compilação por sensor** — `SIMUT_SENSOR_DS18B20`, `SIMUT_SENSOR_DHT22`, `SIMUT_SENSOR_BME280` no `platformio.ini` permitem desabilitar drivers não usados para liberar flash (DS18B20: -2,7 KB, DHT22: -1,6 KB, ambos: -6,1 KB)
- **Configuração universal de slot** — `SensorRecord` v16 com campo `sensorType` explícito + suporte multi-pinos (`pins[4]`), pronto para sensores I2C, SPI, ADC e UART
- **Drivers organizados** — diretório `src/sensors/` com `DS18B20Driver.h`, `DHT22Driver.h`, `SensorConfig.h`, `SensorHelpers.h`
- **Migração de flash v15→v16** — Atualização automática de schema preservando todas as configs de sensores, detecção de tipo via ROM durante a migração
- **Catálogo SensorPresets** — 130+ formatos de exibição pré-definidos em `sensors/SensorPresets.h` cobrindo 30+ grandezas físicas (temperatura, umidade, pressão, peso, luz, química, elétrica, vazão, etc.)
- **Sistema SensorFormat** — `SensorValueFormat` (unidade, decimais, ícone) + `SensorFormat` (1-3 valores por sensor) + factory `forType()` em `sensors/SensorHelpers.h`

### Display — Renderização de Painel Controlada por Driver

- **Ícones nos drivers** — `sensors/SensorDrawing.h` com ícones procedurais (termômetro, gota, manômetro, lâmpada, régua, tubo, raio, pulso, tubulação, bússola, bandeira, átomo, bateria, etc.) protegidos por flags de compilação
- **Painel renderizado pelo driver** — `DHT22_renderPanel()` e `DS18B20_renderPanel()` cuidam do layout completo (ícones, formatação, unidades) via dispatch `sensorRenderPanel()`
- **Painel de slot agora mostra umidade** — DHT22 em qualquer slot exibe temperatura e umidade com ícone de gota e sufixo traduzido (%UR/%RH)
- **Cores do tema** — Drivers recebem `C_TEXT_SUB`, `C_TEMP_OK`, `C_TEMP_HOT`, `C_HUMIDITY` do tema ativo; ícones acompanham mudanças de tema
- **Posicionamento original exato** — `textAnchor=92`, `iconX=14`, `rightMargin=15` copiados do `drawAmbientPanel` original
- **Formatador genérico** — `formatSensorValue()` em `DisplayManager_FmtFloat.h` trata NaN e casas decimais variáveis

### Correções de Bugs

- **Modo AP via toque no boot** — XPT2046 recebe comando SPI de ativação durante o boot inicial; pino PENIRQ lido diretamente via `gpio_get()`. Janela AP sempre abre independente do estado de settle.
- **Calibração de touch obrigatória no primeiro boot** — Sensibilidade + calibração de 4 pontos executa antes do dashboard quando `magic != 0xCA`. Cancelar durante o boot aplica defaults seguros.
- **Comando `sensor define`** — Sintaxe estendida aceita tipo do sensor: `sensor define <gpio> <rom> <tipo> <hwId> <nome>`. Sintaxe legada de 4 tokens auto-detecta pelo ROM.
- **Comando `sensor accept`** — Define `sensorType` explicitamente nos sensores DS18B20 aceitos.

### Orçamento de Flash

| Configuração | Flash |
|---|---|
| Ambos sensores ON | 1031464 (98,8%) |
| Apenas DS18B20 | ~1028400 (98,5%) |
| Apenas DHT22 | ~1029500 (98,6%) |
| Ambos OFF | ~1024900 (98,1%) |

### Testes

49/49 testes passando (27 validadores + 22 HistoryCodec).

## v1.0.0 (2026-06-03)

### Lançamento Público Inicial

- **Suporte a múltiplos sensores** — Até 16 sensores em slots configuráveis: DS18B20 (1-Wire), DHT22 (Data), BME280 (I2C)
- **Pipeline de sensor zero-trust** — Verificação de ROM, detecção de mismatch de hardware, histerese de erro
- **Display TFT ILI9341 320×240** — Dashboard, gráficos em tempo real, configurações via toque (XPT2046)
- **50 temas integrados** + suporte a temas customizados via LittleFS
- **Servidor web embarcado** — Sessões multi-usuário, RBAC (10 bits de permissão), gerenciador de arquivos
- **WebUI comprimida com gzip** — Páginas inline minificadas com CSS/JS compartilhado
- **Telemetria** — HTTP POST e MQTT com templates JSON/CSV/customizados, TLS/SSL
- **CLI de canal duplo** — USB Serial + Bluetooth (BLE)
- **Sincronização NTP** — Backoff exponencial, fallback multi-servidor, RTC virtual
- **Codec de histórico v2** — Delta + sensor-mask + codificação anchor, ~45% de redução de tamanho
- **Autenticação reforçada** — HMAC-SHA256, salt aleatório por usuário, 5000 rounds
- **Atualização OTA** — Upload via interface web, preservação de snapshot de configuração, auto-reboot
- **Backup e restauração** — Backup/restauração completa do LittleFS com integridade CRC32 (formato BKP1)
- **Perícia de crash** — Autópsia via scratch registers do watchdog com monitoramento cross-core
- **Internacionalização** — Inglês + Português/Espanhol via pacotes de idioma externos
