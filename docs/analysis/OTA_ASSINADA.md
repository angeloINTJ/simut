# OTA assinada — desenho

Estado: **proposta**, 01/10/2026. Decisão do mantenedor, no mesmo dia: OTA só
com imagem assinada, e, nas escolhas de chave, bancada e build local, "o mais
profissional". Nada disto está no código ainda; as etapas estão no fim.

## O problema

Hoje uma OTA confere três coisas no `.bin` recebido (`src/ota/validation.cpp`):

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

Flash: o BearSSL já está em todas as imagens por causa do TLS, com a
verificação ECDSA, a curva P-256 e o SHA-256. O spike não acrescentou nenhum
símbolo `br_*` (161 antes, 161 depois). O custo é o módulo novo e 65 B por
chave pública. Medir na etapa 2.

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
(`tools/ota_keys.py`, etapa 2), com a senha digitada ali. A chave privada não
passa por nenhuma ferramenta de terceiros nem por sessão de agente.

**Rotação.** A chave de assinatura tem um número de série no certificado. Para
trocá-la, por rotina ou por suspeita, a raiz certifica uma nova com série
maior, e a próxima release sai assinada por ela. Cada aparelho guarda a maior
série que já instalou e recusa as menores: a chave antiga morre aparelho por
aparelho, à medida que se atualizam. Um aparelho que ainda não se atualizou
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

Cerca de 250 B. O `env` vai sem o prefixo para que o escâner da etiqueta, nos
aparelhos antigos e no cliente, não ache duas.

## A verificação no aparelho

No fim do stage, antes de responder que a imagem está pronta, junto das três
checagens de hoje (`ota_validate_staging`):

1. acha o rodapé `SIMUTSIG` nos últimos 16 bytes; sem ele, recusa;
2. confere a assinatura da raiz sobre o certificado, com a raiz compilada, e
   o escopo que esta imagem aceita;
3. confere a série do certificado contra a mínima guardada;
4. calcula o SHA-256 do `.bin` lido do staging, mais os campos do trailer, e
   confere a assinatura da imagem com a chave do certificado;
5. confere o `security_version` contra o mínimo guardado;
6. confere o `env` do trailer contra a etiqueta achada na imagem e contra a
   variante em execução.

Recusas novas, depois das sete de hoje, com o mesmo `v=` na resposta:
8 sem assinatura, 9 assinatura inválida (certificado ou imagem), 10 chave
revogada (série abaixo da mínima), 11 nível de segurança abaixo do mínimo,
12 escopo não aceito (imagem de bancada num aparelho de produção). A página,
o simut-rx e o console mostram o motivo, e o manual também: o cap. 17, da
atualização, e o cap. 18, da recuperação.

O applier não muda: ele já confere o CRC do que escreveu.

## Anti-rollback

`security_version` sobe só quando uma release corrige uma falha de segurança,
num arquivo do repositório que o CI lê. Entre versões com o mesmo nível, voltar
pelo ar continua possível, como hoje.

O aparelho guarda a série mínima e o nível mínimo num registro de confiança no
setor de metadados da OTA (`OTA_METADATA_OFFSET`, fora do LittleFS), gravado
junto com os metadados num só programa de setor. Fora do LittleFS porque uma
restauração de `.bkp`, que também é do admin completo, reescreve o LittleFS
inteiro: ali os mínimos voltariam a zero e o rollback voltaria a ser possível.
O registro sobe no apply, quando a imagem nova é aceita. Voltar abaixo dele só
pelo USB.

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
  assinados). O job de assinatura confere a própria saída antes de entregá-la.
- `build-custom.yml`: o mesmo job de assinatura, na mesma aprovação.
- Candidata do teste de retenção: um `workflow_dispatch` que compila e assina,
  sem publicar.
- O `.uf2` não leva assinatura: o USB não confere.

## A bancada

- Build local para o rig: `tools/ota_sign.py` com a chave de bancada, e a OTA
  de sempre. O rig com imagem de bancada aceita; com imagem de produção, não.
- Build local para um aparelho de campo: só USB.
- As ferramentas de bancada que sobem firmware (`tools/ota_test.py`) passam a
  assinar com a chave de bancada antes de subir.

## Testes, antes do código

**No host.** A ferramenta Python (`cryptography`) gera vetores com uma chave de
teste fixa, que nenhuma imagem aceita: imagem boa, um bit trocado em cada
campo, trailer cortado, rodapé ausente, série baixa, nível baixo, escopo
errado, `env` trocado. O módulo do aparelho (`src/ota/signature.*`) é escrito
sem Arduino, com a verificação ECDSA injetada. A suíte nativa confere a
decisão sobre cada vetor, com a verificação respondendo o veredito que a
ferramenta calculou. A matemática é a do BearSSL, que o rig exercita inteira.

**No rig**, antes e depois:

| Caso | Hoje | Com a assinatura |
|---|---|---|
| `.bin` sem trailer | aceita | recusa, `v=8` |
| Um byte do `.bin` trocado depois de assinado | aceita (quem troca recalcula o CRC do upload) | recusa, `v=9` |
| Imagem assinada com a chave de bancada, aparelho de produção | aceita | recusa, `v=12` |
| Imagem assinada, nível abaixo do instalado | aceita | recusa, `v=11` |
| Imagem assinada certa | aceita | aceita |
| v2.8.0 → primeira release assinada, pelo ar | — | aceita, com retenção |
| Energia cortada no meio do stage | sobe a anterior | sobe a anterior |

## Etapas

1. **Este desenho**, com as medidas.
2. **Ferramentas e módulo**: `tools/ota_keys.py` (raiz, chave de assinatura,
   certificado), `tools/ota_sign.py` (assinar, conferir), `src/ota/signature.*`
   com a suíte nativa e os vetores. Ainda sem ligar ao stage. Mede o custo em
   flash.
3. **Ligar ao stage**: as recusas 8 a 12, o registro de confiança, a página, o
   console, o simut-rx e o manual. A raiz de bancada provisória. O rig prova a
   tabela acima.
4. **Cerimônia e CI**: o mantenedor gera a raiz e a chave de assinatura; o
   Environment `release` com ele como revisor; o job de assinatura nos dois
   workflows; a candidata assinada.
5. **A primeira release assinada**, com o teste de retenção pelo ar a partir
   da v2.8.0.
