# Contribuindo com o SIMUT

[English](CONTRIBUTING.md) | **Português** | [Español](CONTRIBUTING.es-ES.md)

Obrigado pelo interesse em contribuir! O SIMUT é um firmware IoT open-source para Raspberry Pi Pico W. Veja como ajudar.

[English](CONTRIBUTING.md) | **Português** | [Español](CONTRIBUTING.es-ES.md)

## Antes de Começar

- **Abra uma issue primeiro** para discutir sua ideia antes de escrever código. Isso evita esforço desperdiçado se a mudança não se encaixar no roadmap do projeto.
- Consulte a [Política de Segurança](SECURITY.md) se sua contribuição envolver autenticação, rede ou manipulação de dados.

## Encontrando Algo para Trabalhar

Procure issues com a label [`good first issue`](https://github.com/angeloINTJ/simut/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22) — são curadas para novos contribuidores e cobrem desde documentação até testes embarcados. Cada uma tem escopo claro, critérios de aceitação e arquivos de referência para começar.

| Habilidade | Exemplos de issues |
|------------|-------------------|
| C/C++ embarcado | Testes do HistoryCodec, testes do CLI parser, fuzz testing |
| Python / DevOps | Ambiente Docker, cppcheck CI, pre-commit hooks |
| Documentação / i18n | Tradução para espanhol, badges de prova social |
| Design | Novo tema de cores integrado |

Não sabe por onde começar? Comente em qualquer `good first issue` que o mantenedor ajudará a definir o escopo.

## Configuração de Desenvolvimento

### Opção A — Docker (recomendado para novos contribuidores)

Sem necessidade de instalar PlatformIO, Python ou toolchain ARM localmente. O Docker gerencia tudo.

**Pré-requisitos:** [Docker Desktop](https://www.docker.com/products/docker-desktop/) (macOS / Windows) ou [Docker Engine](https://docs.docker.com/engine/install/) (Linux)

```bash
# Clonar o repositório
git clone https://github.com/angeloINTJ/simut.git
cd simut

# Compilar firmware para Raspberry Pi Pico W
docker compose run build

# Rodar todos os testes unitários nativos
docker compose run test
```

> **Nota da primeira execução:** O Docker fará o build da imagem e baixará o toolchain ARM (~500 MB). Isso acontece apenas uma vez — execuções subsequentes usam a imagem em cache e são rápidas.
>
> **Usuários Linux:** Exporte `UID` e `GID` antes de executar para que os artefatos de build não fiquem com ownership root:
> ```bash
> export UID GID
> docker compose run build
> ```

| Comando | Comando PlatformIO equivalente |
|---|---|
| `docker compose run build` | `pio run -e pico_w_release` |
| `docker compose run test` | `pio test -e native && pio test -e native_history_v5` |

---

### Opção B — PlatformIO Local

### Pré-requisitos

- [PlatformIO Core](https://platformio.org/install/cli) 6.x ou superior
- Raspberry Pi Pico W
- Para testes em hardware: display TFT ILI9341 + touch XPT2046 + sensor DS18B20

### Build

```bash
# Clonar o repositório
git clone https://github.com/angeloINTJ/simut.git
cd simut

# Compilar firmware
pio run -e pico_w_release

# Rodar testes unitários
pio test -e native
```

### Gravar no Dispositivo

```bash
# Gravar firmware
pio run -e pico_w_release -t upload

# Enviar dados do LittleFS (pacotes de idioma, favicon)
pio run -e pico_w_release -t uploadfs
# ATENÇÃO: uploadfs REFORMATA a partição LittleFS — destrói histórico, config e calibração de um dispositivo já em serviço. Só na primeira gravação.
```

## Convenções de Código

- **Idioma:** Todos os comentários, nomes de variáveis e documentação devem estar em inglês
- **Nomenclatura:** `camelCase` para métodos e variáveis, `_underlinePrefix` para membros privados, `UPPER_SNAKE_CASE` para constantes
- **Indentação:** Tabs para indentação, espaços para alinhamento
- **Estilo de chaves:** K&R — chave de abertura na mesma linha da declaração
- **Doxygen:** Todos os métodos públicos em headers devem ter documentação `@brief`
- **NULL:** Use `nullptr` em código C++ (não `NULL`)
- **Comentários:** Explique o *porquê*, não o *quê* — o código é o "quê"

## Orçamento de Flash

A flash está criticamente apertada — ~95 % do slot de app de 1020 KB no env de release (o env `pico_w_test` roda a ~99 %). Antes de adicionar funcionalidades, considere:

1. Pode ser otimizada para usar menos espaço?
2. Pode substituir algo de menor valor?
3. Pode residir no LittleFS em vez do binário do firmware?

## Processo de Pull Request

1. Abra uma issue descrevendo a mudança que deseja fazer
2. Faça um fork do repositório e crie um branch (`feature/minha-funcionalidade`)
3. Escreva seu código e teste em hardware se possível
4. Garanta que `pio run -e pico_w_release` compila com **zero warnings**
5. Garanta que `pio test -e native` passa em todos os testes
6. Atualize a documentação em `docs/` se sua mudança afetar o comportamento do usuário
7. Envie o PR com uma descrição clara, referenciando o número da issue
8. O checklist do template de PR guiará os passos restantes

## Testes

- Testes unitários usam o framework [Unity](http://www.throwtheswitch.org/unity)
- Execute com `pio test -e native`
- Adicione testes para nova lógica de validação, encoding/decoding e caminhos críticos de segurança
- Teste em hardware é obrigatório para mudanças em display, sensores, WiFi e OTA

## Comunidade

- Reporte bugs via [GitHub Issues](https://github.com/angeloINTJ/simut/issues)
- Tire dúvidas no [GitHub Discussions](https://github.com/angeloINTJ/simut/discussions)
- Siga o [Código de Conduta](CODE_OF_CONDUCT.md)
- Vulnerabilidades de segurança: siga a [Política de Segurança](SECURITY.md) — não abra uma issue pública

## Ferramentas de IA

Usamos assistentes de IA (Claude, Copilot, etc.) como **ferramentas de engenharia**, não como substitutos de julgamento. IA ajuda com boilerplate, rascunhos de documentação e scaffolding de testes — mas decisões de arquitetura, temporização PIO, orçamento de flash e hardening de segurança são trabalho humano. Se você usar IA na sua contribuição, tudo bem — apenas revise o resultado. Código gerado é sua responsabilidade.

## Licença

Ao contribuir, você concorda que suas contribuições serão licenciadas sob a Licença MIT.
