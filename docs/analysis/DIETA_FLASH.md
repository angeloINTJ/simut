# Dieta de flash — estudo

**Estado:** Living · **Medido em:** 2026-09-18, mapa do linker + builds A/B da
`pico_w_release` (árvore `study/espelho-delta` d64ea8f = main 50d3d2a + PR #127),
`pico_w_air` para o capítulo do Air · **Pergunta:** o que consome o 1 MB, e o
que devolve bytes sem tirar função?

A resposta curta: a imagem de release tem **1.039.972 B** contra um teto de OTA
de 1.040.384 B — **412 B de folga**. Cinco alavancas que não mudam nenhuma
função, medidas uma a uma e depois **juntas numa imagem só, dão −53.848 B**;
uma sexta, só de ferramenta, dá −2.888 B por cima.

> **APLICADO em 18/09/2026, no PR #129** — seis commits, um por alavanca, cada
> um com a sua medição e os seus testes, a partir da main `50d3d2a` (que não
> tem o PR #127, por isso a linha de base lá é 1.039.900 e não 1.039.972).
> Resultado real: **release 1.039.900 → 982.868 B (−57.032)**, folga de OTA de
> 484 B para 57.516 B. A seção 4 abaixo traz, lado a lado, o que este estudo
> previu e o que a aplicação mediu — três alavancas renderam mais do que o
> estudo dizia e uma rendeu menos. A seção 10 é nova: o que a aplicação
> descobriu que este documento não sabia.
>
> **Publicado na v2.4.9-beta (18/09).** A imagem de release da tag mede 982.844 B
> na árvore; o binário anexado ao release saiu com **985.732 B**, porque o
> `release-ota.yml` não instalava o `zopfli` quando a tag foi cortada (corrigido
> no #132, e as notas do release dizem os dois números). Folga de OTA do binário
> publicado: 54.652 B.

---

## 1. Como foi medido

Uma build com `-Wl,-Map` e o mapa atribuído por arquivo de origem — o script
está em `tools/flash_compose.py` e imprime a tabela da seção 2. Cada alavanca é
uma build A/B contra a mesma árvore: um flag, um arquivo temporário em `src/`
ou uma edição temporária no arquivo do framework, tudo restaurado depois
(md5 conferido) e **a build de controle voltou aos 1.039.972 B exatos**.

O número comparado é sempre o `.bin`, não o `used` que o PlatformIO imprime:
aqui eles distam 12.032 B (1.027.940 × 1.039.972) e o teto de OTA é medido
contra o `.bin`. As três armadilhas de medição que este estudo encontrou estão
na seção 8; a segunda delas desmente um número que já circulou neste projeto.

---

## 2. Onde o 1 MB vai (release)

| origem | bytes | % |
|---|---:|---:|
| **firmware próprio — código** (`.text` do `src/`) | 263.068 | 25,3% |
| **blob do rádio CYW43** (`liblwip.a`, `w43439A0_7_95_49_00_combined`) | 225.240 | 21,7% |
| firmware próprio — **13 páginas web gzip** (`WebUI_GZ`) | 97.499 | 9,4% |
| **BearSSL** (TLS) | 94.712 | 9,1% |
| **libc** (newlib) | 72.955 | 7,0% |
| núcleo arduino-pico + lwIP compilado da fonte (`libFrameworkArduino.a`) | 51.960 | 5,0% |
| **pool de strings mescladas** (o binário inteiro; ≈ metade é do `src/`) | 40.339 | 3,9% |
| pico-sdk (`libpico.a`: USB CDC, timers, PIO, divisor) | 28.820 | 2,8% |
| firmware próprio — dados (`.rodata` sem páginas e fontes, `.data`) | 16.326 | 1,6% |
| LittleFS | 22.231 | 2,1% |
| WiFi (`WiFiClientSecureBearSSL`, `BearSSLHelpers`, `WiFiClient`) | 19.460 | 1,9% |
| LEAmDNS | 15.804 | 1,5% |
| firmware próprio — fontes do TFT | 11.696 | 1,1% |
| driver CYW43 (`liblwip.a` sem o blob) | 12.326 | 1,2% |
| WebServer (parser multipart 6,6 kB + servidor 5,1 kB) | 10.643 | 1,0% |
| applier de OTA do framework (seção `.ota`) | 10.228 | 1,0% |
| Adafruit GFX + ILI9341 + XPT2046 | 10.715 | 1,0% |
| drivers PIO (Wire, OneWire, DHT22, BMx280, Buzzer) + SPI | 16.392 | 1,6% |
| HTTPClient | 6.111 | 0,6% |
| resto (PubSubClient, DNSServer, lwIP_*, libstdc++ 2,3 kB, boot2, partição, stubs, alinhamento) | 13.447 | 1,3% |

Três conclusões saem só dessa tabela. **O código próprio é um quarto da
imagem**, e nenhum objeto dele domina: `TelemetryManager` 25,3 kB,
`StorageManager` 24,5 kB, `WebManager_History` 15,0 kB, `WebManager_Commit`
14,5 kB, `DisplayManager` 13,3 kB, `DisplayManager_Touch` 11,9 kB (dos quais
`handleTouch( )` sozinho tem 11.044 B), `DisplayManager_Graph` 11,3 kB. Não há
um "vilão" de código; há o custo de um firmware com 55 rotas, quatro
transportes de telemetria e uma UI de toque. **O blob do rádio é fixo** — 21,7%
que nenhuma dieta toca. E **as duas bibliotecas de sistema, BearSSL e libc,
somam 16%** — é ali que estão as alavancas que não tiram função.

### 2.1 As páginas, uma a uma

| página | gzip -9 (hoje) | zopfli 15 it. | ganho |
|---|---:|---:|---:|
| HIST | 27.988 | 26.955 | −1.033 |
| CFG | 12.197 | 11.772 | −425 |
| LANG_JS | 10.713 | 10.354 | −359 |
| TEL | 7.919 | 7.699 | −220 |
| LOGIN | 6.713 | 6.501 | −212 |
| DASH | 5.892 | 5.754 | −138 |
| FILE | 5.272 | 5.172 | −100 |
| ALARMS | 4.596 | 4.491 | −105 |
| FORCE_CHPASS | 4.084 | 4.027 | −57 |
| LICENSE | 3.913 | 3.842 | −71 |
| NET | 2.787 | 2.728 | −59 |
| STYLE_CSS | 2.779 | 2.706 | −73 |
| USR | 2.646 | 2.610 | −36 |
| **total** | **97.499** | **94.611** | **−2.888 (−3,0%)** |

Os 13 blocos custam 2,4 s para recomprimir a 15 iterações; 50 iterações dão
mais 18 B. O formato continua sendo gzip padrão — a objeção que derrubou o
brotli (não vale em HTTP sem TLS) não se aplica.

### 2.2 A libc: o que puxou cada membro caro

| membro | bytes | quem puxa |
|---|---:|---|
| `vfprintf` | 9.674 | `vprintf` ← `panic( )` do pico-sdk; `printf`/`puts` no driver CYW43 e no `pheap`. **Nada do `src/` chama printf de FILE** |
| `svfprintf` | 9.214 | `vsnprintf` ← nosso (CLI, JSON, TFT) |
| `svfiscanf` | 6.866 | `siscanf` ← `_tzset_r`: **newlib lê a string POSIX `TZ` com scanf** |
| `vfiprintf` | 5.063 | `fiprintf` ← `assert( )` — release compila **sem** `NDEBUG` |
| `dtoa` | 4.913 | `%f` no `svfprintf` (79 usos no `src/`) |
| `strtod` | 4.869 | `atof` ← `String::toFloat`; `parseFloatStrict` |
| `mprec` | 3.377 | aritmética de precisão para `dtoa` e `strtod` |
| `strptime` | 3.125 | `HTTPClient` do framework |
| `mallocr` | 2.508 | o alocador |
| `gdtoa-gethex` | 2.035 | floats hexadecimais no `strtod` |
| `mktime` | 1.636 | nosso, via `localtime`/fuso |
| `tzset_r` + `tzcalc_limits` + `timelocal` + `lcltime_r` | 2.892 | o mesmo fuso |
| outros 113 membros | ≈ 16,8 k | |

Quatro linhas dessa tabela são alavancas medidas na seção 4: o `printf` que só
o SDK usa, o fuso que o newlib parseia com scanf para um valor que o firmware
guarda como **um inteiro de horas** (`"UTC%d"`, sem DST), o `strtod` completo
com floats hexadecimais e NaN, e o `assert` que a release ainda carrega.

`-specs=nano.specs` está em `build_flags` e **não faz nada**: o toolchain
earlephilhower (gcc 14.3.0, um único multilib `thumb`) não traz `libc_nano.a`,
e o link resolveu `lib/thumb/libc.a` — o `-Wl,-u,_printf_float` ao lado dele
veio na mesma carona. Uma newlib-nano exigiria outro toolchain; não é alavanca.

### 2.3 BearSSL por função

(96.001 B com as 1.289 B de strings; os grupos somam isso.)

| grupo | bytes | fica? |
|---|---:|---|
| handshake, engine, records | 25.720 | sim |
| x509 (minimal, decoder, PEM, chave) | 14.312 | sim |
| EC P-256 + `prime_i31` (P-384/521 das cadeias Let's Encrypt ECDSA) | 13.203 | sim |
| AES-CT, GCM, SHA-2 small, HMAC, PRF | 12.044 | sim |
| RSA (verificação de assinatura das cadeias RSA) | 8.410 | sim |
| SHA-384/512 | 4.808 | sim (suítes `_SHA384` e cadeias assinadas com SHA-384) |
| X25519 | 4.500 | sim |
| **CBC** (`aes_ct_ctrcbc`, `ssl_rec_cbc`) | 4.232 | não |
| **MD5/SHA-1** (PRF do TLS 1.0/1.1, certificados SHA-1) | 2.828 | não |
| **3DES** | 2.500 | não |
| **ChaCha20/Poly1305** | 2.356 | opcional (servidores que só oferecem ChaCha não existem na prática) |
| **CCM** | 1.088 | não |

O framework instala tudo isso porque `br_ssl_client_base_init( )` e
`br_ssl_server_base_init( )` em `WiFiClientSecureBearSSL.cpp` chamam todos os
`set_default_*` e anunciam quatro dezenas de suítes, de TLS 1.0 a 1.2. O certificado do
dispositivo é EC P-256; todo navegador e todo servidor deste século fecha
`ECDHE_{ECDSA,RSA}_WITH_AES_{128,256}_GCM`.

---

## 3. O Air

`pico_w_air`: **1.027.220 B**, folga de OTA de **13.164 B** — a 2.4.4 desligou o
mDNS no Air (`7eefad8`, 13/09) e devolveu os ~12 kB que faltavam quando o
orçamento registrou 876 B. O Air já carrega `NDEBUG`, `SIMUT_LICENSE_STUB` e
`SIMUT_MDNS=0`; das alavancas da seção 4 restam-lhe a poda TLS (BearSSL lá tem
80,5 kB, sem o servidor HTTPS), o fuso, o `printf`, o `strtod` e o zopfli —
**não medidas nele**, os deltas abaixo são da release.

O que o Air tem que a release não tem é **Bluetooth: ≈135 kB, 13,2% da
imagem**, e é o canal de configuração dele (`SerialBT`, SPP clássico), então
não é uma alavanca — é um custo a conhecer:

| parcela | bytes |
|---|---:|
| `liblwip-bt.a` acima da `liblwip.a` da release (BTstack + blob combinado) | 123.582 |
| — dos quais `hci` 29.562, `l2cap` 22.041, **`sm` 14.610 (Security Manager do BLE)**, `rfcomm` 11.754, `rijndael` 7.716, `cybt_shared_bus` 7.690, `uECC` 2.994, blob +7.491 | |
| — dos quais **strings de log 23.572**: formatos `"%s.%u: …"` do `log_info` | |
| `btstack_flash_bank.cpp.o` (dois bancos de 4 KiB para chaves de pareamento, em `.rodata`) | 8.212 |
| `libSerialBT.a` + `BluetoothManager.cpp.o` | 3.557 |

Em agosto o `platformio.ini` mediu 64.732 B para o mesmo flag — quando o
`BluetoothManager` era só stubs e o `--gc-sections` descartava a pilha. Com o
CLI por Bluetooth em uso, o custo dobrou: a pilha entra porque é chamada.

Os 23,6 kB de strings existem porque `include/btstack_config.h` do framework
define `ENABLE_LOG_INFO`, `ENABLE_LOG_DEBUG` e `ENABLE_LOG_ERROR`, e a
biblioteca vem **pré-compilada** (`lib/rp2040/liblwip-bt.a`). Recuperá-los, e o
Security Manager do BLE que um CLI SPP não usa (≈19 kB com `uECC` e
`le_device_db`), exige recompilar a biblioteca pelo `tools/libpico` do
framework — e o `patch.sh` deste projeto registra que a última mexida no
`btstack_config.h` quebrou a leitura de RSSI do CYW43. É a maior alavanca do
Air e a mais cara de puxar.

Também sobra no Air a `LICENSE_PAGE_GZ` (3.913 B): `SIMUT_LICENSE_STUB` cobre só
o texto do TFT (`HelpLicenseEN.h:236`), não a página web.

---

## 4. Alavancas medidas

Cada linha é uma build contra 1.039.972 B, sozinha. A penúltima é a soma real.

Os números desta coluna são os do estudo (árvore `study/espelho-delta`). A
coluna seguinte, **aplicado**, é o que cada commit do PR #129 mediu sobre a
main — árvores diferentes, e é por isso que diferem.

| alavanca | Δ `.bin` (estudo) | aplicado | o que é | risco / como validar |
|---|---:|---:|---|---|
| **Poda TLS** | **−16.696** | **−16.688** | patch em `WiFiClientSecureBearSSL.cpp` pelo mecanismo de `tools/arduino_pico_overrides` (que já carrega dois patches nesse arquivo): 4 suítes ECDHE-GCM no cliente, 2 no servidor EC, só TLS 1.2, sem MD5/SHA-1 nas listas de hash, sem `rsapub`, CBC, CCM, 3DES e ChaPol | servidor que só ofereça CBC, TLS ≤ 1.1 ou troca de chave RSA pura para de fechar; cadeia assinada com SHA-1 deixa de verificar (só importa com certificado carregado). Validar: bancada dos 4 transportes, login HTTPS em Chrome/Firefox, OTA por HTTPS |
| **Fuso fixo** | **−13.188** | **−13.148** | o fuso é `int8` de horas, sem DST. Guardar o offset numa global; `localtime_r(t) = gmtime_r(t + off)`; `mktime = timegm − off`; fim de `setenv("TZ")`/`tzset( )`. Definir `localtime_r`/`mktime` no `src/` basta — os membros da libc deixam de ser puxados; as ≈44 menções em 13 arquivos não mudam | a saga do buraco da meia-noite era **ordem** de aplicação do fuso, e uma global tem a mesma ordem — manter o `applyTimezone` onde está. Teste nativo para `timegm` (bissextos, virada de ano) e a fronteira de dia do histórico na bancada |
| **`printf` do SDK** | **−9.836** | **−9.836** | 20 linhas: `printf`/`vprintf`/`puts`/`putchar` por `vsnprintf` + `Serial.write`. Só o `panic( )` e o driver CYW43 chegam ao `vfprintf` de FILE | baixo. Forçar um `panic( )` na bancada e ver a mensagem sair pela serial |
| **`strtod` próprio** | **−7.296** | **−7.640** | parser decimal (sinal, inteiro, fração, expoente); sem hex, `inf`, `nan`, sem arredondamento correto | os valores viram `float` (24 bits de mantissa) — 1 ulp de `double` some no cast. `parseFloatStrict` continua rejeitando `0x1p3`/`inf`/`nan` por construção. Validar: ida e volta JSON → float → JSON dos coeficientes de calibração, e a suíte nativa dos validadores |
| **`NDEBUG` na release** | **−6.792** | **−6.832** | o mesmo flag que `pico_w_test` e `pico_w_air` já usam — toda soak de bancada roda assim. Leva junto os `__FILE__` dos asserts, entre eles um caminho absoluto de 116 B do diretório pessoal do mantenedor (assert inline do `hardware_dma`) que hoje **vai na imagem publicada** | um invariante que falhe passa calado em vez de reiniciar. `-DLFS_NO_ASSERT` sozinho dá −4.528 e está contido neste |
| **zopfli nas páginas** | **−2.888** | **−2.888** | `build_webui_gz.py` comprime com zopfli em vez de `gzip -9`; 2,4 s por regeneração completa, cache por hash da fonte para não pagar em toda build | nenhum em runtime: é gzip padrão. CI precisa de `pip install zopfli` |
| **as cinco primeiras, juntas** | **−53.848** | **as SEIS: −57.032** | uma imagem só: `NDEBUG` + poda TLS + os três shims → **986.124 B, folga de OTA de 54.260 B** | a soma das cinco individuais é 53.808 — são disjuntas. O zopfli entra por fora (o gerador reescreve o header na build) |
| mDNS desligado | −16.608 | não aplicado (decisão de produto) | `SIMUT_MDNS=0`, como test/Air/alpha já fazem — a bancada nunca exercita o responder | **decisão de produto**: some o `SIMUT.local` |

Com as seis primeiras a folga de OTA da release iria de **412 B para ≈57 kB**
(54.260 + 2.888); com o mDNS, ≈73 kB. É o que separa "não cabe mais um byte"
de poder embarcar o espelho por delta (`ESPELHO_DELTA.md` §6 pedia 1,5–2 kB)
e ainda ter a margem de 3 kB que o orçamento sempre usou.

---

## 5. Alavancas estimadas (não medidas — o número é aritmética, não build)

| alavanca | ≈ bytes | de onde vem o número |
|---|---:|---|
| Tabela de códigos de log | −3.000 | `translateCodeEn( )` é um `switch` de 141 casos sobre códigos de 0 a 999; o GCC emitiu uma tabela densa de 1.000 × 4 B (`CSWTCH.138`, **4.000 B**). Uma tabela esparsa ordenada gerada pelo `gen_logcodes.py` (141 × 6 B = 846 B) e busca binária |
| Tabela de CRC32 | −1.024 | `ota::CRC32_TABLE` (1 KiB) e `SystemUtils::crc32_update` calculam o **mesmo** CRC refletido `0xEDB88320`, o segundo bit a bit. O CRC-32/MPEG-2 de `ota/validation.cpp` é outro e fica |
| `FS_README_TEXT` | −2.000 | 2.192 B de texto em pt-BR gravado no LittleFS ao formatar; um ponteiro para a documentação faz o mesmo |
| Tabela de rotas | −2.500 | `WebManager::begin( )` tem 4.260 B para ~60 `server.on(path, method, std::bind(…))`; uma tabela `{path, method, handler}` e um laço. O `std::function` em si custa 580 B no total — não é ele |
| Script comum das páginas | −3.440 a −10.550 | redundância entre páginas medida no estudo do espelho; LOGIN e FORCE_CHPASS repetem 3.440 B de script de tema. Um `/s.js` cacheado pelo navegador paga **uma requisição a mais por página em HTTPS** e reabre a armadilha de ordem do `lang.js` síncrono — decisão de UX |
| `SIMUT_LICENSE_STUB` na release | −1.800 | `LICENSE_TEXT_EN` tem 1.833 B; a tela "sobre" do TFT deixa de mostrar a licença (a página web fica). Decisão de produto |
| CLI só em inglês | −1.000 | `AppManager_Commands.cpp.o` guarda 2.564 B de literais únicos mesmo com `SIMUT_CLI_FULL=0`, em pares pt/en ("Comando desconhecido…" 94 B + "Unknown command…" 85 B). Decisão de produto |
| Fontes | −1.000 a −3.000 | 11.696 B; a 24 pt já é subconjunto (3,0 kB); as duas de 8 bits carregam 224 glifos cada (tabela de 1.792 B cada) — cortar o que a UI não imprime é trabalho no gerador de fontes |
| `strptime` | −3.125 | puxado pelo `HTTPClient` do framework para uma data que o SIMUT nunca lê; mesmo mecanismo de override |
| Air: BTstack sem log | −23.572 + código | seção 3; exige recompilar `liblwip-bt.a` |
| Air: BTstack só clássico | −19.000 | `sm` + `uECC` + `le_device_db`; mesmo caminho |

Refatorar `handleTouch( )` (11.044 B), `handleApiCommitAll( )` (9.884 B) ou os
dois managers de 25 kB **não está nesta lista**: em `-Os` o ganho de
reestruturar código é imprevisível, e uma alavanca sem número não é alavanca.

---

## 6. O que NÃO rende (medido)

| tentativa | Δ | por quê |
|---|---:|---|
| `-Oz -fno-threadsafe-statics` | −48 | `-Os` já é o piso deste GCC |
| `String::toFloat` → `parseFloatStrict` | **+288** | o `strtod` fica de qualquer jeito; só o `strtod` próprio o tira |
| `-DLFS_NO_DEBUG/WARN/ERROR` por cima do `NDEBUG` | −48 | os traces do LittleFS já estão fora |
| `-fno-rtti` | ≈ −148 | é o total de `typeinfo` na imagem |
| LTO | — | o toolchain foi compilado com `--disable-lto` (nota no `platformio.ini`) |
| `-specs=nano.specs` | 0 | não há `libc_nano.a` no toolchain (§2.2) |
| páginas no LittleFS (`custom_fs_pages`) | −28 k a −70 k | existe, e o gerador **recusa** nos ambientes que embarcam: o apply do OTA formata a partição e a página sumiria a cada atualização |
| UI de hardware ausente (`custom_web_omit`) | **−4.096** no Air e no alpha | 21/09: o espelho do painel, a captura de tela e o seletor de tema iam para TODA imagem, inclusive as que não têm painel — 2.363 B de página gzipada falando com quatro rotas que o `#if SIMUT_DISPLAY_TFT` nem registra ali. Blocos `@IF tft` no `WebUI.h`, recortados por ambiente; o Air voltou de **980 B** para 5.076 B de folga de OTA |
| blob do rádio, `.ota`, `.partition` | — | 237 kB de plataforma que nenhuma dieta toca |

---

## 7. Ordem sugerida — EXECUTADA

1. **zopfli** — só ferramenta, −2.888. Uma tarde.
2. **`NDEBUG` na release** — um flag, −6.792, e tira o caminho pessoal da imagem.
3. **`printf` do SDK** — um arquivo de 20 linhas, −9.836.
4. **Fuso fixo** — um módulo de tempo pequeno com teste nativo, −13.188.
5. **`strtod` próprio** — com os testes dos validadores, −7.296.
6. **Poda TLS** — o patch no framework e uma sessão de bancada nos 4
   transportes, navegadores e OTA/HTTPS, −16.696.
7. Depois, as tabelas geradas da seção 5 (códigos de log, CRC, README): ≈ −6 kB
   sem decisão de produto.

As decisões de produto (mDNS, licença, script comum, CLI em inglês) ficam
fora da fila: cada uma é uma escolha do mantenedor, não uma otimização.

Cada passo entra com o número medido no commit e o orçamento
(`tools/flash_budget.json`) **abaixado** na mesma mudança — um orçamento que
não desce depois de uma dieta deixa de ser marca d'água.

Foi essa a ordem executada no PR #129, um commit por alavanca. O que a bancada
validou, no rig 192.168.3.24 com a `pico_w_test` desta branch:

| o quê | como |
|---|---|
| páginas (zopfli) | `web_test_suite.py`: 95 passaram / 0 falharam; cada página descomprime e o JS servido passa no `node --check` |
| `parseFloat` | round-trip de calibração `[[21,75, 22,25]]` → leitura corrigida de 21,77 para 22,27 |
| fuso | a mesma janela de epoch atravessa a meia-noite local em −03 e não em +09: `filesTried` deu (2,1), (1,1), (1,2) para −3/0/+9, sem tocar no relógio; o relógio do painel marcou 09:03:39 contra 12:03 UTC |
| TLS | telemetria contra o sink HTTPS da bancada (cert RSA): 6 requisições, 6 registros, 924 B aceitos; `SYS_TEL_SENT ctx=200` no log do aparelho |
| toda a imagem | 7/7 suítes nativas e as 5 imagens no portão de orçamento a cada passo |

**Não validado no ferro:** o servidor HTTPS do próprio aparelho com as suítes
podadas — provisionar o certificado exige gravar em `/config`, e o upload web
recusa isso de propósito (ver seção 10).

---

## 8. Armadilhas de medição

**O mapa entrega o pool de strings ao primeiro objeto.** `AppManager_Boot.cpp.o`
aparece no mapa com 39.185 B de strings — tem 1.611 B de literais únicos. As
seções mescláveis (`.rodata.str1.1` e as `.rodata.<função>.str1.1` que o
`-fdata-sections` emite) são listadas no tamanho **antes** da mesclagem, e o
pool mesclado inteiro (40.339 B) é somado ao primeiro contribuinte. Todo
"o `setup( )` tem 39 kB" nasceu daqui. O `flash_compose.py` calcula o pool como
`.rodata` de saída menos as seções não mescláveis e o reporta uma vez.

**`PLATFORMIO_BUILD_FLAGS` apaga o `.pio/build` inteiro.** Muda o checksum do
projeto e o PlatformIO limpa todos os ambientes — o ELF do Air desapareceu no
meio desta análise. Cada experimento por flag é uma build completa (~4 min), e
a build seguinte sem o flag é outra.

**A leitura de strings por `objcopy` tem que incluir as seções por função.**
A `.rodata.str1.1` plana de um objeto guarda 8,9 kB dos 21,6 kB de literais do
`src/`; o resto está em `.rodata.<mangled>.str1.1`. Filtrar por `*str1*`.

**`-Werror` vale para o arquivo do experimento.** Um `if` numa linha só custou
uma build de 4 min (`-Werror=misleading-indentation`).

**O controle é A contra A.** Depois de restaurar os três shims e o arquivo do
framework, a build limpa voltou a 1.039.972 B — sem esse número, nenhum dos
deltas acima teria base.

---

## 9. Reprodução

```bash
PLATFORMIO_BUILD_FLAGS="-Wl,-Map=/tmp/release.map" pio run -e pico_w_release   # apaga .pio/build
python3 tools/flash_compose.py /tmp/release.map .pio/build/pico_w_release/firmware.bin
arm-none-eabi-nm -S --size-sort -C .pio/build/pico_w_release/firmware.elf | tail -40
```

Os experimentos foram arquivos temporários em `src/` (nunca commitados) e uma
edição temporária de `libraries/WiFi/src/WiFiClientSecureBearSSL.cpp` no
pacote do framework, restaurada por cópia e conferida por md5. Nenhuma imagem
deste estudo foi ao ferro: são números de link, e a validação de cada alavanca
está na coluna de risco da seção 4.

---

## 10. O que a aplicação descobriu, e este estudo não sabia

**O manual manda provisionar o certificado por um caminho que não existe mais.**
`docs/MANUAL.md` (e a versão pt-BR) dizem para subir `web_cert.pem` e
`web_key.pem` pela página Files. `handleUploadData( )` recusa qualquer upload
que caia em `/config` — e com razão, um cert forjado ali é um MITM no tráfego
do admin. As duas coisas estão certas separadamente; juntas, não há como
instalar um certificado num aparelho de campo. **Não foi consertado aqui** — é
anterior a esta dieta e merece a sua própria mudança, que precisa decidir qual
é a porta (um comando de console, um endpoint dedicado com gate próprio, ou o
manual passar a dizer a verdade).

**A troca `toFloat` → `parseFloat` rendeu MAIS do que o previsto** (−7.640
contra −7.296) porque tirou também o `atof` do `String`, que o estudo contava
como já descontado.

**O `NDEBUG` rendeu mais** (−6.832 contra −6.792) e a **poda TLS menos**
(−16.688 contra −16.696): árvores diferentes, `--gc-sections` diferente. A
lição é a de sempre — um delta medido numa árvore não é o delta na outra, e o
número que vale é o do commit que o aplica.

**Três instrumentos mentiram durante a validação**, todos do mesmo jeito: não
mediam o que eu achava que mediam.

- A primeira prova do fuso pediu uma janela **no futuro**, e o handler corta a
  janela em "agora" — deu 1 arquivo onde eu esperava 2 e parecia defeito do
  firmware. A janela no passado deu exatamente o previsto.
- O sink de telemetria **não registra requisição nenhuma** em `--mode ok`; o
  `(nada)` que ele imprimia não significava "não chegou". O `--stats` conta, e
  contou 6.
- `pkill -f "server_http.py"` **matou o próprio shell** que rodava o comando —
  a armadilha já está registrada neste projeto e eu caí nela de novo.

**Um teste nativo pegou um defeito real do meu código antes do ferro:** a
`native_network` compila o `NetworkManager` no host, e definir `localtime_r`
ali teria sombreado a glibc do binário de teste inteiro. Os overrides ficaram
atrás de `#if defined(ARDUINO_ARCH_RP2040)`, e a aritmética — que é o que
importa — é testada no host contra a libc do próprio host.
