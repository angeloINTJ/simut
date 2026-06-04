# Changelog

Todas as mudanças notáveis do firmware SIMUT.

## Não lançado

### Documentação

- **Landing page v2** — reformulada com imagem hero, tabela comparativa Por que SIMUT?, seção de screenshots, diagrama de arquitetura, e CTAs claros
- **Correção do GitHub Pages** — remoção de blocos HTML incompatíveis com o kramdown; adição de `_config.yml` para configuração do tema
- **Social preview** — adição de meta tags `og:image` e `twitter:image` para que compartilhamentos de URL mostrem o dashboard TFT no Reddit, Twitter, Discord e WhatsApp
- **Tópicos do GitHub** — expansão de 9 para 20 tópicos destacando inovações (offline-first, touchscreen, dual-core, ota-updates, rbac, secure, i18n)
- **Guia de montagem** — revisão dos pinos e detalhes de conexão; correção da tabela Pin Reference no GitHub Pages (kramdown rejeitava linha `...`)
- **Changelog em português** — `CHANGELOG.pt-BR.md` espelhando a versão em inglês
- **Reconhecimento de contribuidores** — geração do `CONTRIBUTORS.md` com grid de emojis All-Contributors; adição do badge em ambos os READMEs

### Comunidade

- **Primeira contribuição externa** 🎉 — suíte de testes de 672 linhas para o HistoryCodec v2 cobrindo roundtrip de encoding, fronteiras de anchor frame, compressão de NaN e proteção contra buffer overflow ([@LorenzoLongaretto](https://github.com/LorenzoLongaretto))
- **12 issues `good first issue`** criadas abrangendo documentação, design, DevOps, embarcado, i18n e segurança
- **5 novos labels** adicionados: `tests`, `display`, `i18n`, `ci`, `tools`, `security`

### Infraestrutura

- **.editorconfig** — indentação consistente entre editores
- **Imagem de social preview** — PNG 1280×640 para compartilhamento Open Graph
- **Imagens da landing page** — dashboard TFT, screenshots da Web UI, GIF animado de demonstração
- **Suíte de testes verificada** — 49/49 testes passando em 0.9s (27 validators + 22 HistoryCodec)

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
