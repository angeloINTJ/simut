# Segurança {#cap-29}

O que o aparelho protege, como ele faz isso e o que fica por conta de quem instala. Este capítulo é para quem responde pela TI e pela conformidade da instalação; os detalhes de cada mecanismo estão nos capítulos indicados em cada seção.

## Quem pode falar com o aparelho {#cap-29-caminhos}

Todo acesso ao aparelho passa por um destes caminhos, e cada um tem a sua própria autenticação.

| Caminho | Imagens | Como a pessoa se identifica | O que esse caminho alcança |
|---|---|---|---|
| Interface web e API, pela rede | todas (o Air só acordado) | conta e senha; depois, cookie de sessão ou token Bearer | o que as permissões da conta liberam |
| Painel | [release]{.img} | conta e PIN | o que as permissões da conta liberam no painel |
| Console USB | todas | acesso físico ao cabo | os comandos de recuperação, que só existem aqui |
| Console Bluetooth | [alpha]{.img} [air]{.img} | senha do admin da web | o console, menos os comandos de recuperação |
| Ponto de acesso de configuração | todas | chave WPA2 própria do aparelho | a interface web, enquanto o AP estiver no ar |

O console USB é o caminho de maior confiança: quem tem o cabo tem a placa na mão. Por isso os quatro comandos de recuperação (`system factory`, `system format`, `system admin reset` e `system https off`) são recusados pelo Bluetooth, mesmo com a senha certa. Uma recuperação que funciona pelo rádio só ajudaria quem já entrou.

## Contas e permissões {#cap-29-contas}

- O aparelho tem 32 contas. A conta 0 é a do administrador.
- Cada conta tem até 13 permissões, uma por bit. Uma conta só vê e altera o que os seus bits liberam, na web, no painel e na API. O [capítulo 8](#cap-08) descreve cada permissão.
- **Ninguém concede o que não tem.** Uma conta com a permissão de gerenciar usuários só pode dar a outra conta os bits que ela mesma tem. Um pedido que inclui um bit a mais é recusado inteiro, e a resposta diz qual campo foi recusado. A regra fecha a falha V-09 e vale desde a v2.7.0.
- **Quatro operações exigem o administrador completo** (todos os bits): fazer backup, restaurar, aplicar uma atualização e instalar o certificado HTTPS.

## Senhas, PIN e bloqueios {#cap-29-senhas}

### Senhas da web

- A senha nunca é gravada. O aparelho guarda um HMAC-SHA256 de 5000 rodadas, com um salt aleatório de 8 bytes por conta e um segundo segredo preso à placa. Hashes antigos, de 2500 rodadas, são convertidos no primeiro login bem-sucedido.
- **A senha inicial do administrador** tem 8 caracteres aleatórios. Ela aparece uma única vez, no console USB, e a interface web obriga a trocá-la no primeiro login.
- **Uma conta nova** recebe uma senha de 8 caracteres, mostrada uma vez para quem a criou.
- **Tentativas erradas bloqueiam o cliente**, com espera de 2 s a 300 s, crescendo a cada erro. A tabela de bloqueio tem 8 posições. Com todas ocupadas, o aparelho responde `429` até uma posição vencer.

### PIN do painel [release]{.img}

- O PIN também não é gravado: o aparelho guarda uma cadeia de hash com salt.
- A política de PIN (comprimento mínimo, glifos por tecla, alfabeto) é configurável. O teclado é sorteado de novo a cada toque, para que a posição dos dedos não entregue o PIN.
- **Erros bloqueiam primeiro a conta e depois o painel.** A sexta falha seguida bloqueia a conta. Vinte falhas no total bloqueiam o painel inteiro até o aparelho reiniciar.

### Bluetooth [alpha]{.img} [air]{.img}

- O login usa a senha do administrador da web.
- O bloqueio cresce de 2 s a 300 s e continua valendo depois de desconectar e conectar de novo.
- O aparelho deixa de ser encontrável 5 minutos depois de o Bluetooth ligar. Um aparelho já pareado continua conectando.

## Sessões e transporte {#cap-29-transporte}

- **Sessões da web:**
  - o cookie de sessão é `HttpOnly` e `SameSite=Strict`, e ganha `Secure` quando a conexão é HTTPS;
  - uma sessão vence depois de 15 minutos sem uso, e o aparelho guarda até 3 sessões;
  - um integrador pode usar um token Bearer no lugar do cookie ([capítulo 26](#cap-26)).
- **HTTPS**, só na imagem [release]{.img}:
  - o aparelho oferece apenas TLS 1.2, com troca de chaves ECDHE e cifra AES-GCM;
  - o certificado é seu: você o instala com `POST /api/tls` ([capítulo 9](#cap-09));
  - sem certificado instalado, a interface web funciona só em HTTP, e o conteúdo trafega sem cifra na rede local.
- **Telemetria por TLS:** o aparelho confere o certificado do seu servidor contra um arquivo `/cert.pem` que você envia a ele. Sem esse arquivo, a conexão é cifrada, mas ninguém confirma que do outro lado está o seu servidor, e o aparelho registra um aviso no log ([capítulo 21](#cap-21)).
- **Ponto de acesso de configuração:**
  - a rede é WPA2;
  - a chave tem 10 caracteres, é derivada do identificador da placa e sobrevive a um reset de fábrica;
  - ela aparece no console USB, na tela de boot do painel e no LCD do alpha.

## Limites contra abuso {#cap-29-abuso}

- **Toda rota da API tem dono.** São 62 rotas: 52 exigem uma permissão e 10 são públicas por projeto, como a página de login. Nenhuma fica sem proteção, e o CI confere isso a cada mudança.
- **As rotas pesadas têm intervalo mínimo por endereço de origem:** 200 ms em `/api/logs` e `/api/ls`, e 5 s em `/api/calib` e `/api/tls`.
- **O gerenciador de arquivos recusa:**
  - caminhos com `..`, codificação por `%`, bytes de controle e nomes reservados;
  - nomes com mais de 64 caracteres;
  - qualquer operação na pasta `/config`, onde ficam a configuração e as contas.

## Auditoria {#cap-29-auditoria}

- **Registros protegidos:** os de segurança, de configuração e de falha fatal sempre vão para o log de eventos. O filtro que reduz o volume do log só atua sobre eventos de rotina ([capítulo 16](#cap-16)).
- **Ações assinadas:** as ações feitas no painel e as mudanças de alarme levam o nome da conta que agiu. Na linha de alarmes, o nome é gravado no registro no momento do evento, então apagar a conta depois não muda quem aparece como autor ([capítulo 22](#cap-22)).
- **Cópia fora do aparelho:** para guardar o log fora do aparelho, envie-o a um servidor syslog ([capítulo 25](#cap-25)).

## O que o aparelho não faz {#cap-29-limites}

- **Não tem rollback.** Existe um único slot de firmware. Uma gravação interrompida na janela de aplicação, de cerca de 25 s, exige BOOTSEL e cabo USB ([capítulo 18](#cap-18)).
- **Não cifra o sistema de arquivos.** A senha do Wi-Fi, a senha do MQTT e a chave de API da telemetria são gravadas embaralhadas com uma chave derivada do próprio chip. Isso impede a leitura casual de um arquivo copiado, mas não é cifra: quem tem a placa na mão consegue recuperá-las. Proteja o acesso físico.
- **Não é um instrumento metrológico certificado.** Valide as leituras contra a sua própria referência antes de usá-lo como controle de um armazenamento regulado.

## Recomendações de instalação {#cap-29-recomendacoes}

1. Troque a senha do administrador no primeiro login (a interface obriga) e troque o PIN `1234` do administrador no painel.
2. Crie uma conta por pessoa, cada uma só com as permissões de que ela precisa. Reserve o administrador completo para quem faz backup e atualização.
3. Ajuste a política de PIN ao risco do local: um PIN mais longo e o alfabeto com letras (`0-9A-Z`) tornam um palpite mais difícil ([capítulo 8](#cap-08)).
4. Ponha o aparelho numa rede separada da rede de visitantes, ou use HTTPS com um certificado seu.
5. Se a telemetria usa TLS, envie ao aparelho o certificado do seu servidor (`/cert.pem`).
6. Envie o log de eventos a um servidor syslog, para que um registro sobreviva ao aparelho.
7. Baixe um backup depois de configurar e antes de cada atualização.
8. Proteja o acesso físico à porta USB: os comandos de recuperação estão nela.
9. Mantenha o firmware na versão mais recente. As correções de segurança só saem na release mais nova, sem retroativos para versões antigas.
10. No alpha e no Air, lembre que o console Bluetooth está ativo e aceita a senha do administrador.

## Auditorias e correções {#cap-29-auditorias}

| Data | Escopo | Resultado |
|---|---|---|
| 16/08/2026 | Revisão do código contra as diretrizes de segurança do projeto | Achados fechados na v2.2.5-beta |
| 29/08/2026 | Auditoria externa da v2.3.6-beta (ACH-01 a ACH-08) | Correções a partir da v2.3.9-beta; ACH-05 e ACH-08 foram feitos depois, na linha de setembro |
| 07/09/2026 | Auditoria V-01 a V-08 e O-1 a O-3 | Fechada na v2.4.1-beta e verificada no hardware em 08 e 09/09/2026 |
| 21/09/2026 | Achado V-09: uma conta restrita criava contas com permissões que não tinha | Corrigido na v2.7.0 e verificado no hardware, 10 de 10 vereditos com controles positivos |

## Relatar uma vulnerabilidade {#cap-29-relatar}

Não abra uma issue pública. Use o relato privado do GitHub no repositório do projeto: aba **Security** → **Report a vulnerability**. A política completa está no `SECURITY.md` do repositório.
