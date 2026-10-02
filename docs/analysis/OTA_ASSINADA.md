# OTA assinada — desenho

Estado: **em andamento**. Decisão do mantenedor, 01/10/2026: OTA só com imagem
assinada, e, nas escolhas de chave, bancada e build local, "o mais
profissional". Feitas as etapas 1 (este desenho, #219), 2 (ferramenta, módulo e
testes no host, #220) e 3 (ligar ao stage): o firmware do `main` só aceita imagem
assinada. Até a etapa 4, o CI não assina nada, então **nenhuma release sai antes
dela**: um aparelho que a instalasse recusaria todas as seguintes. As etapas
estão no fim.

## O problema

Até a v2.8.x, uma OTA confere três coisas no `.bin` recebido (`src/ota/validation.cpp`):

1. o tamanho cabe no slot (até 1.016 KiB);
2. o CRC do boot2 bate: é uma imagem de RP2040;
3. a etiqueta `SIMUT-ENV:` é da mesma variante (release, alpha, Air).

Elas pegam acidentes: o arquivo errado, o upload corrompido, a variante
trocada. Não pegam intenção. Qualquer `.bin` que passe nelas vira o firmware.
A barreira é a senha do admin completo (`POST /api/restore?op=stage` e
`POST /api/ota/apply` exigem `PERM_FULL_ADMIN`, `docs/AUTHORIZATION.md`). Por
HTTP, o SHA-256 dessa senha serve de senha a quem o captura (achado 41 da
triagem em `PLANO_REVISAO_EXTERNA.md`).

Quem instala o firmware controla o aparelho inteiro: o que a tela mostra, se o
alarme dispara, o que vai para o histórico e para o log, a senha do Wi-Fi.
Para um monitor de cadeia fria, a medição só vale o firmware que a produziu.

O `SHA256SUMS` e o atestado de procedência de cada release (#207) deixam uma
pessoa conferir o arquivo baixado. A assinatura faz o aparelho conferir.

## O que a assinatura garante, e o que não

| Garante | Não garante |
|---|---|
| Pelo ar, só entra firmware que passou pelo CI do projeto com a aprovação do mantenedor | Nada contra quem tem o USB: o BOOTSEL grava qualquer coisa. O RP2040 não tem boot seguro; o RP2350 tem (etapa S3 do Pico 2 W) |
| Uma chave de assinatura vazada é trocada sem tocar nos aparelhos | A configuração: o admin ainda desliga alarme e muda limite. Isso é da trilha de auditoria |
| Uma versão antiga com falha de segurança conhecida não volta pelo ar | Sigilo: a imagem continua legível. A assinatura prova origem e integridade |
| Um build de teste não chega a um aparelho de campo por engano | Um CI comprometido com a aprovação do mantenedor: aí a chave assina o que vier |

## Medidas (rig, 01/10/2026)

`pico_w_test` de `1d84e70` com um comando de bancada descartável
(`ota sigbench`), quatro rodadas iguais:

| O quê | Tempo |
|---|---|
| SHA-256 de 1.000.000 B lidos da flash (XIP) | 1,01 s |
| Uma verificação ECDSA P-256 (`br_ecdsa_i31_vrfy_raw`), assinatura boa | 0,40 s |
| A mesma, com um bit da assinatura trocado | 0,40 s, recusada |

A verificação completa de uma imagem (certificado e imagem, abaixo) leva
cerca de 1,8 s, uma vez por OTA, no fim do upload. O laço do SHA-256 alimenta
o watchdog a cada 4 KiB; cada verificação fica bem abaixo dos 8,4 s dele.

Medido depois, com a etapa 3 no rig (imagem de bancada com cronômetro e pintura
de pilha, 01/10/2026): a checagem de uma imagem de 1 MB leva **1,87 s** no fim
do stage e **1,94 s** no apply. O stage foi de 31,4 s para 33,3 s, e o `202` do
apply, de 0,05 s para 2,01 s. A checagem desce 3,7 KB de pilha abaixo da
chamada, sobre os 1,6 a 1,9 KB que o handler já usava: o Core 0 chega a 5,6 KB
do topo da pilha, além do banco SCRATCH_Y de 4 KB e dentro do SCRATCH_X, que
nenhuma imagem usa (o Core 1 roda numa pilha própria, e nada o lança na do
SDK). Sobram 2,5 KB até o heap.

Flash: o BearSSL já está em todas as imagens por causa do TLS, com a
verificação ECDSA, a curva P-256 e o SHA-256. Nem o spike nem a etapa 3
acrescentaram símbolo `br_*`: 196 antes e depois na release, 161 e 161 na
`pico_w_test` e no Air. O que a etapa 3 custa, medido contra o `main` (`2690c0a`),
no número que o PlatformIO imprime:

| Imagem | Flash | O quê |
|---|---|---|
| release, asserts, pico2 | +1.952 / +2.024 / +1.880 B | o módulo (`sigCheck` 548 B, `sigTrustParse` 172 B), a ligação ao BearSSL e ao stage (~390 B), a recusa no apply (+156 B), o bloco de confiança (90 B), as mensagens da página (+387 B comprimidos) |
| test, test_https | +2.024 / +1.632 B | o mesmo, com o bloco de bancada (156 B) |
| alpha, Air | +1.280 B | o mesmo |

A RAM ganha 112 B (o contexto do SHA-256); a janela de 4 KiB que a busca da
etiqueta já usava passou a ser a da assinatura também.

## As chaves

Duas camadas, para que o segredo que fica online possa ser trocado sem USB.

| Chave | Onde mora | Para quê |
|---|---|---|
| **Raiz de produção** (ECDSA P-256) | Offline, com o mantenedor: PKCS#8 cifrado com senha, em duas cópias (pendrive guardado e gerenciador de senhas), a senha guardada à parte. Nunca no CI | Só certifica chaves de assinatura. A pública vai compilada em toda imagem |
| **Assinatura de produção** (ECDSA P-256) | Segredo do *Environment* `release` do GitHub, com o mantenedor como revisor obrigatório | Assina as imagens: releases, builds do configurador, candidatas do teste de retenção |
| **Raiz de bancada** | No PC do mantenedor, fora do repositório | Certifica a chave de bancada. A pública vai só nas imagens que não são publicadas |
| **Assinatura de bancada** | No PC do mantenedor, fora do repositório | Builds locais que vão para o rig pelo ar |

Uma imagem de produção (release, alpha, Air, as do configurador) confia só
na raiz de produção. Uma imagem de bancada (`pico_w_test`, `pico_w_test_https`,
`pico_w_asserts`) confia nas duas: o rig recebe tanto o build local quanto a
candidata assinada pelo CI. Uma imagem de produção nunca confia na de
bancada: a chave menos guardada viraria porta dos fundos.

**Geração.** A raiz é gerada pelo mantenedor, num terminal dele
(`tools/ota_sign.py root-new`), com a senha digitada ali. A chave privada não
passa por nenhuma ferramenta de terceiros nem por sessão de agente.

**Rotação.** A chave de assinatura tem um número de série no certificado. Para
trocá-la, por rotina ou por suspeita, a raiz certifica uma nova com série
maior, e a próxima release sai assinada por ela. Cada imagem traz compilada a
menor série que aceita (`keys/ota_policy.json`, abaixo): a release assinada pela
chave nova sobe esse número, e a chave antiga morre aparelho por aparelho, à
medida que eles instalam essa release. Um aparelho que ainda não se atualizou
aceita a chave antiga até lá, e isso não tem remédio sem revogação online.

**Perda ou vazamento da raiz.** Perdida a raiz (as duas cópias), a chave de
assinatura em uso continua valendo, mas não há como trocá-la. Vazada a raiz,
quem a tem certifica o que quiser: a frota volta para o USB, aparelho por
aparelho, com uma raiz nova. É por isso que ela não fica online.

## O formato da imagem assinada

O `.bin` continua o mesmo, byte a byte, e ganha um trailer no fim:

| Campo | Bytes | O quê |
|---|---|---|
| `security_version` | 4 | O nível de segurança desta imagem (anti-rollback, abaixo) |
| `env` | 16 | O nome do ambiente, sem o prefixo `SIMUT-ENV:`, completado com zeros |
| `image_len` | 4 | Quantos bytes do `.bin` a assinatura cobre |
| certificado: série, escopo, chave pública | 4 + 1 + 3 + 65 | Escopo 1 = produção, 2 = bancada |
| certificado: assinatura da raiz | 64 | ECDSA P-256 crua (r ‖ s) sobre o SHA-256 dos campos do certificado |
| assinatura da imagem | 64 | ECDSA P-256 crua sobre o SHA-256 de `.bin` ‖ todos os campos acima |
| rodapé: tamanho do trailer, versão do formato, `SIMUTSIG` | 4 + 2 + 2 + 8 | O aparelho acha o trailer pelo fim do arquivo |

241 B. O `env` vai sem o prefixo para que o escâner da etiqueta, nos
aparelhos antigos e no cliente, não ache duas. As duas assinaturas levam um
prefixo de domínio (`SIMUT-OTA-CERT-v1`, `SIMUT-OTA-IMG-v1`) antes do que
cobrem: uma assinatura de certificado nunca passa por assinatura de imagem, nem
o contrário. A tabela byte a byte está em `tools/ota_sign.py`, que escreve o
trailer, e `src/ota/signature.cpp` o lê.

## O bloco de confiança

O que uma imagem aceita vai compilado nela: um bloco de 24 B mais 66 B por raiz,
com as raízes por escopo e os pisos que ela impõe à imagem seguinte (o menor
`security_version` e a menor série de cada escopo). `tools/ota_sign.py gen-trust`
o escreve em `src/ota/ota_trust.h` a partir de `keys/`, que guarda só as
públicas (`keys/README.md`), e o CI confere com `gen-trust --check`. Há dois
blocos: o de produção, com a raiz de produção, e o de bancada, com as duas.

O aparelho tira a política desse bloco (`sigTrustParse`), e o `ota_sign.py sign`
lê o mesmo bloco de dentro do `.bin` antes de assinar. O que a ferramenta confere
é, por construção, o que o aparelho vai exigir. Ela recusa assinar:

- uma imagem sem etiqueta `SIMUT-ENV`, com duas, ou com `--env` diferente dela;
- uma imagem sem bloco de confiança, ou com dois;
- uma imagem que confia numa raiz que `keys/` não nomeia;
- com a chave de produção, uma imagem que confia na raiz de bancada;
- uma imagem que, instalada, recusaria a próxima desta mesma chave: que não
  confia na raiz que a certificou, ou cuja menor série aceita está acima da dela.

O `security_version` do trailer é, por padrão, o piso da própria imagem: ela
nunca recusa a si mesma.

## A verificação no aparelho

No fim do stage, antes de responder que a imagem está pronta, depois das três
checagens que já existiam (`ota_validate_staging`):

1. acha o rodapé `SIMUTSIG` nos últimos 16 bytes; sem ele, recusa;
2. confere a assinatura da raiz sobre o certificado, com as raízes do bloco de
   confiança e o escopo que esta imagem aceita;
3. confere a série do certificado contra a menor que a imagem em execução aceita;
4. calcula o SHA-256 do `.bin` lido do staging, mais os campos do trailer, e
   confere a assinatura da imagem com a chave do certificado;
5. confere o `security_version` contra o menor que a imagem em execução aceita;
6. confere o `env` do trailer contra a variante em execução (a etiqueta da
   imagem recebida já foi conferida antes, `v=7`).

Recusas novas, depois das sete que já existiam, com o mesmo `v=` na resposta:
8 sem assinatura, 9 assinatura inválida (certificado ou imagem) ou trailer
malformado, 10 chave revogada (série abaixo da mínima), 11 nível de segurança
abaixo do mínimo, 12 escopo não aceito (imagem de bancada num aparelho de
produção). Assinada para outra variante, é a recusa que já existe para a
etiqueta trocada, 7.

A série mínima é por escopo. O rig recebe candidatas do CI e builds de bancada,
e com uma série só a primeira candidata revogaria a chave de bancada. A página
confere o rodapé antes de enviar, porque o envio apaga o sistema de arquivos antes
de qualquer recusa, e mostra o motivo de cada `v`; o log registra
`stage_v_fail v=N`; o manual explica cada um (caps. 17, 18, 26, 27 e 30). O
simut-rx ganha o mesmo, no repositório dele.

**A checagem roda de novo no apply.** A etapa 3 achou um furo que a assinatura
no stage não fecharia sozinha. `POST /api/ota/apply` só exige metadados
`COMMITTED`, e nada os amarra aos bytes que foram validados: um stage novo não os
apaga, um stage recusado ou cortado remonta o sistema de arquivos por cima da
área, e o applier copia o que estiver lá (ele calcula um CRC e não tem a quem
contá-lo; quem confere é o boot seguinte, que só registra). Uma imagem assinada
com `commit=1`, seguida do stage de outra, sem assinatura, cortado, deixaria a
segunda sob os metadados da primeira. Então o apply confere a assinatura de novo,
sobre o que a área tiver naquela hora; se não conferir, apaga os metadados e
responde `409` com o `v`. Custa o tempo de uma checagem antes do `202`. O applier
não muda.

## Anti-rollback

`security_version` sobe só quando uma release corrige uma falha de segurança, em
`keys/ota_policy.json`. Entre versões com o mesmo nível, voltar pelo ar continua
possível, como antes.

Os mínimos vão compilados na imagem, no bloco de confiança, e não gravados no
aparelho. O desenho da etapa 1 os guardava num registro no setor de metadados da
OTA, fora do LittleFS, porque uma restauração de `.bkp` reescreve o LittleFS
inteiro e ali eles voltariam a zero. Compilados, nada que o aparelho grave os
alcança: nem o `.bkp`, nem um setor gasto, nem um corte de energia no meio de
uma escrita, e o applier e os metadados não mudam. O aparelho exige da próxima
imagem o que a imagem em execução diz; ao instalar uma release de nível 2, passa
a recusar o nível 1. O efeito pelo ar é o do registro: voltar abaixo do nível
instalado, só pelo USB.

## A transição

A primeira release assinada sai do CI com o trailer. Um aparelho na v2.8.x
confere tamanho, CRC do boot2 e etiqueta, e o trailer não muda nenhum dos três:
o CRC cobre os primeiros 256 B, a etiqueta continua uma só, e o `.bin` da
release com o trailer tem cerca de 1.016.920 B, abaixo do teto de 1.040.384 B
(`OTA_APP_SAFE_MAX_SIZE`), com 23 KiB de folga. O aparelho a instala como uma
OTA comum, e a partir dela só aceita imagem assinada. Nenhum passo manual no
campo. O applier grava o trailer junto, depois do fim da imagem, onde ele não
atrapalha.

O teste de retenção da release (obrigatório) prova exatamente essa passagem: a
v2.8.0 no rig, a candidata assinada pelo ar, a configuração e o histórico
iguais depois.

## O CI

- `release-ota.yml` em três jobs: compilar (sem segredo); **assinar**, no
  Environment `release`, que espera a aprovação do mantenedor; publicar
  (`SHA256SUMS`, o manifesto do simut-rx e o atestado, já sobre os `.bin`
  assinados). O job de assinatura usa o `ota_sign.py sign`, que recusa o que o
  bloco de confiança proíbe (acima), e confere a própria saída antes de
  entregá-la.
- `build-custom.yml`: o mesmo job de assinatura, na mesma aprovação.
- Candidata do teste de retenção: um `workflow_dispatch` que compila e assina,
  sem publicar.
- O `.uf2` não leva assinatura: o USB não confere.

## A bancada

- Quais imagens confiam na raiz de bancada: as de perfil com
  `ota_trust_bench = true` em `tools/features.toml` (`pico_w_test`,
  `pico_w_test_https`, `pico_w_asserts`). O `gen_features.py` recusa a marca
  num perfil publicado, e o `build_custom.py` a tira de toda build do
  configurador, seja qual for o produto de base.
- As chaves de bancada moram em `~/.simut-ota/` no PC do mantenedor, sem senha,
  modo 600: a raiz e a chave de assinatura de série 1, geradas em 01/10/2026.
- Build local para o rig: `tools/ota_test.py` assina com a chave de bancada antes
  de subir (`--as-is` sobe o arquivo como está, para os casos de recusa). O rig
  com imagem de bancada aceita; com imagem de produção, recusa com `v=12`.
- Build local para um aparelho de campo: só USB.

## Testes, antes do código

**No host** (etapa 2, feita). `tools/ota_sign.py vectors` gera os vetores com um
jogo fixo de chaves de TESTE, que nenhuma imagem aceita: a imagem boa, uma
assinada pela bancada, uma com raiz forjada, uma com certificado "de produção"
assinado pela raiz de bancada, uma com o `env` sem terminador. O módulo do
aparelho (`src/ota/signature.*`) não tem Arduino nem BearSSL: o SHA-256 e o
ECDSA entram injetados. Na suíte `native_otasig` o SHA-256 é uma referência
conferida antes contra os vetores do FIPS 180-4, e o ECDSA responde verdadeiro
só para os trios (chave, resumo, assinatura) genuínos que a ferramenta
conferiu com ECDSA de verdade. Um módulo que resuma os bytes errados, na ordem
errada ou com o domínio errado não acha assinatura genuína e falha.

Na etapa 2 eram 22 casos no C++ e 26 na referência Python (`ota_sign.py
selftest`), com a mesma recusa esperada para cada vetor. Nove defeitos injetados
no módulo, um por vez, são pegos: domínio trocado, trailer fora do resumo, último
byte da imagem fora do resumo, `image_len` sem conferência, escopo ignorado na
verificação, sem revogação, sem rollback, sem conferência de `env`, `env` sem
terminador. Quatro deles passavam com a primeira versão dos vetores e pediram
vetores novos: uma imagem que não é múltipla do pedaço de 512 B, bytes entre a
imagem e o trailer, o certificado de escopo forjado e o `env` cheio.

A etapa 3 levou a suíte a 27 casos e o selftest a 54. No C++: o tamanho do
pedaço de leitura não muda veredito nenhum (1, 64, 512 e 4.096 B); os dois blocos
que o `gen-trust` gerou parseiam com as raízes e os pisos certos; um bloco
montado byte a byte a partir do formato leva cada campo ao lugar em que a
checagem o lê; um bloco malformado, campo a campo, é recusado. Em Python: o bloco
de ida e volta, cada recusa do `sign`, e os arrays do header gerado iguais aos
blocos. Dez defeitos no parser (séries trocadas, piso lido do campo errado, sem
conferir reservado, escopo, prefixo do ponto, espaço do chamador, tamanho exato
ou magia, último pedaço lido inteiro, raízes em ordem inversa) e oito nas regras
do `sign` (cada recusa da lista acima, o `security_version` padrão e a conferência
de curva) são pegos, um por vez.

A matemática é a do BearSSL, que o rig exercita inteira.

**No rig** (01/10/2026), antes e depois. O antes é a `pico_w_test` do `main`
(`2690c0a`); o depois, a da etapa 3 e, para os casos de produção, a release da
etapa 3. Web na :8080, por uma conta descartável. O rig foi despejado antes
(`c7c00182…`, duas leituras iguais) e restaurado byte a byte no fim.

| Caso | `main` | Etapa 3 |
|---|---|---|
| `.bin` sem trailer | aceita, `v=0` (31,4 s) | recusa, `v=8` (32,8 s); a release também, `v=8` |
| Um byte do `.bin` trocado depois de assinado | aceita, `v=0` | recusa, `v=9` |
| Assinada pela bancada, aparelho de produção | aceitaria qualquer imagem | recusa, `v=12` (release da etapa 3) |
| Assinada com nível abaixo do da imagem em execução | aceitaria | recusa, `v=11` |
| Chave de série abaixo da menor aceita | aceitaria | recusa, `v=10`, em 31,7 s: antes do hash da imagem |
| Assinada certa | aceita | aceita, `v=0` (33,3 s) |
| Stage com commit, outro stage cortado em 400 KB, depois o apply | o `COMMITTED` sobrevive ao corte (o reset seguinte loga `Staged update discarded: device rebooted before apply`): um apply ali copiaria os bytes do stage cortado | `409` com `v=9` em 2,10 s; o aparelho segue na mesma imagem, e um segundo apply ouve "no committed update pending" |
| `main` → etapa 3 pelo ar (assinada pela bancada, marcada 2.8.3) | aceita e aplica; web de volta em 47 s; a versão lida é 2.8.3 | — |
| Etapa 3 → etapa 3 (marcada 2.8.4) | — | `202` em 2,01 s; web de volta em 49 s; versão 2.8.4 |

A passagem da v2.8.0 publicada para a primeira release assinada, com retenção da
configuração e do histórico, é a etapa 5.

## Etapas

1. **Este desenho**, com as medidas.
2. **Ferramenta e módulo** (feita, 01/10/2026): `tools/ota_sign.py` (raiz,
   chave de assinatura e certificado, assinar, conferir, vetores),
   `src/ota/signature.*`, a suíte `native_otasig` e os vetores. Ainda sem ligar
   ao stage.
3. **Ligar ao stage** (feita, 01/10/2026): as recusas 8 a 12 no stage e de novo
   no apply, o bloco de confiança compilado de `keys/` com os pisos, a raiz de
   bancada e a marca `ota_trust_bench`, a página, o log, as ferramentas de bancada
   e o manual. O rig prova a tabela acima. O simut-rx fica para um PR no
   repositório dele.
4. **Cerimônia e CI**: o mantenedor gera a raiz e a chave de assinatura; o
   Environment `release` com ele como revisor; o job de assinatura nos dois
   workflows; a candidata assinada.
5. **A primeira release assinada**, com o teste de retenção pelo ar a partir
   da v2.8.0.
