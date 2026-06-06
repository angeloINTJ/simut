# Changelog

Todas as mudanças notáveis do firmware SIMUT.

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

- **Suporte a múltiplos sensores** — Até 10 DS18B20 (1-Wire) + 1 sensor ambiente DHT22
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
