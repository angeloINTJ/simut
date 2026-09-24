# Gestão de frota {#cap-27}

Este capítulo mostra como cuidar de muitos aparelhos a partir de um ponto central: encontrar cada um na rede, configurar todos com o mesmo modelo, acompanhar o estado, atualizar o firmware em ondas e fazer isso com segurança. É para quem escreve ou opera o servidor de gestão. As rotas e os formatos estão no [capítulo 26](#cap-26).

## O desenho de uma frota {#cap-27-desenho}

Numa frota, o tráfego corre nos dois sentidos ([capítulo 20](#cap-20-sentidos)):

- **Do aparelho para o servidor**, sem que ninguém peça: a telemetria ([capítulo 21](#cap-21)) e a linha de alarmes ([capítulo 22](#cap-22)). Todo POST HTTP leva a identidade do aparelho nos cabeçalhos `X-SIMUT-*`.
- **Do servidor para o aparelho**, quando o servidor quer: a interface de programação ([capítulo 26](#cap-26)) para ler o estado, configurar, criar contas e atualizar; e as métricas do Prometheus ([capítulo 24](#cap-24)).

O gestor de frota é o programa que faz o segundo sentido para todos os aparelhos. Ele pode ser um serviço no servidor, um script agendado ou uma página no navegador.

::: {.figura #fig-27-arquitetura tipo="diagrama" arquivo="27-arquitetura.png" captura="diagrama: vários aparelhos SIMUT numa rede local; setas saindo deles para o coletor (telemetria e linha de alarmes, com X-SIMUT-Uid/Ver/Env/Cfg); setas do Prometheus para cada aparelho (GET /metrics, HTTP Basic); setas do gestor de frota para cada aparelho (API REST com cookie ou Bearer); um anúncio mDNS _simut._tcp saindo dos aparelhos release para a rede local; o gestor lendo o manifest.json da release no GitHub"}
Os caminhos de uma frota. Os aparelhos empurram medições e alarmes; o gestor e o Prometheus perguntam.
:::

| Tarefa | Mecanismo | Seção |
|---|---|---|
| Saber quais aparelhos existem | mDNS, cabeçalhos da telemetria, `GET /api/status` | [Descobrir os aparelhos](#cap-27-descobrir) |
| Configurar vários de uma vez | `POST /api/commit_all`, com ensaio | [Configuração em lote](#cap-27-lote) |
| Cadastrar pessoas no painel | Seção `users` do `commit_all` | [Receita completa](#cap-27-receita) |
| Acompanhar a saúde | `/metrics`, `GET /api/status` | [Monitoramento](#cap-27-monitorar) |
| Receber alarmes na hora | Linha de alarmes | [Monitoramento](#cap-27-monitorar) |
| Atualizar o firmware | `manifest.json` da release e a sequência de atualização | [Atualização da frota](#cap-27-ota) |

## Descobrir os aparelhos {#cap-27-descobrir}

### A chave de cada aparelho {#cap-27-chave}

Use o `uid` como chave primária de cada aparelho no seu cadastro: são os 16 algarismos hexadecimais do número de série da placa, que não mudam com configuração, atualização nem restauração ([capítulo 20](#cap-20-identidade)). O IP vem do DHCP e muda; o nome é editável e pode se repetir.

O mesmo `uid` aparece em quatro lugares, e o gestor pode cruzá-los:

| Onde | Campo |
|---|---|
| Cada POST de telemetria e da linha de alarmes | Cabeçalho `X-SIMUT-Uid` |
| `GET /api/status` | `sys.uid` |
| `GET /api/config` | `serial` |
| [release]{.img} Anúncio mDNS | Campo `uid` do registro TXT |

### Pelo mDNS {#cap-27-mdns}

[release]{.img}

A imagem release se anuncia na rede local como o serviço `_simut._tcp`, com a porta do servidor web e quatro campos de texto ([capítulo 9](#cap-09-mdns)):

| Campo | Conteúdo |
|---|---|
| `uid` | Número de série da placa |
| `ver` | Versão do firmware |
| `env` | `release` |
| `tls` | `1` com HTTPS, `0` com HTTP |

Com HTTPS na porta de fábrica, o anúncio leva a porta 443. O endereço do aparelho sai do próprio anúncio.

```bash
avahi-browse -rpt _simut._tcp
```

Em Python, com a biblioteca `zeroconf`:

```python
import time
from zeroconf import ServiceBrowser, Zeroconf  # pip install zeroconf

class Ouvinte:
    def add_service(self, zc, tipo, nome):
        info = zc.get_service_info(tipo, nome)
        if not info:
            return
        txt = {k.decode(): v.decode() for k, v in info.properties.items() if v}
        esquema = "https" if txt.get("tls") == "1" else "http"
        for ip in info.parsed_addresses():
            print(txt.get("uid"), txt.get("ver"), "%s://%s:%d" % (esquema, ip, info.port))
    def update_service(self, zc, tipo, nome):
        pass
    def remove_service(self, zc, tipo, nome):
        pass

zc = Zeroconf()
ServiceBrowser(zc, "_simut._tcp.local.", Ouvinte())
time.sleep(5)
zc.close()
```

O mDNS só atravessa a rede local: ele não passa por roteadores nem VLANs diferentes, a não ser que a rede tenha um repetidor de mDNS. O nome `<nome>.local` vem do campo **Nome**: dê a cada aparelho um nome único, ou dois aparelhos vão disputar o mesmo endereço.

### Pela telemetria {#cap-27-telemetria}

As imagens alpha e Air não têm mDNS. Para elas, e para aparelhos em outras redes, o caminho é o coletor: cada POST de telemetria chega com `X-SIMUT-Uid`, `X-SIMUT-Ver`, `X-SIMUT-Env` e `X-SIMUT-Cfg`, e com o endereço IP de origem na conexão ([capítulo 20](#cap-20-cabecalhos)).

Guarde, para cada `uid`:

- o IP de origem do último POST, que é o endereço para falar com o aparelho, se não houver NAT no caminho;
- `X-SIMUT-Ver` e `X-SIMUT-Env`, para saber que imagem instalar;
- `X-SIMUT-Cfg`: se mudou desde o último POST, alguém mudou a configuração;
- a hora do último POST, para saber quem ficou em silêncio.

Um aparelho novo aparece no coletor no primeiro envio de telemetria. No MQTT não há cabeçalhos: o tópico de cada aparelho é que o identifica ([capítulo 20](#cap-20-identidade-mqtt)).

### Confirmar quem responde {#cap-27-confirmar}

Antes de mandar qualquer configuração para um endereço, confirme que ali está o aparelho certo: entre e compare `sys.uid` de `GET /api/status` com o cadastro. Um IP reaproveitado pelo DHCP pode apontar para outro aparelho.

```bash
curl -s -b jar "$H/api/status?quiet=1" | python3 -c \
  'import sys, json; s = json.load(sys.stdin)["sys"]; print(s["uid"], s["ver"], s["env"], s["cfg"])'
```

## Contas de serviço {#cap-27-servico}

### Uma conta para o gestor {#cap-27-conta}

Não use a conta `admin` para a automação do dia a dia. Crie em cada aparelho uma conta só para o gestor, com as permissões de que ele precisa e nada mais ([capítulo 8](#cap-08-criar)).

As permissões são bits independentes, sem hierarquia ([capítulo 8](#cap-08-permissoes)). Combinações úteis:

| Uso | Permissões | `perms` |
|---|---|---|
| Só monitorar (`/api/status`, `/metrics`) | [PERM_DASHBOARD]{.perm} | 1 |
| Configurar, cadastrar pessoas no painel e abrir manutenção | [PERM_DASHBOARD]{.perm} [PERM_SYS_CONFIG]{.perm} [PERM_USER_MGR]{.perm} [PERM_ALARM_LIMITS]{.perm} [PERM_ALARM_BLOCK]{.perm} [PERM_MAINT]{.perm} | 7433 |
| O mesmo, e também baixar o histórico | As de cima, mais [PERM_HISTORY]{.perm} e [PERM_FILE_READ]{.perm} | 7467 |
| Mudar a rede | Acrescente [PERM_NET_CONFIG]{.perm} | +16 |

A conta de 7433 não mexe na rede, não lê arquivos, não calibra e não vê o log de eventos. Acrescente só o que o gestor for de fato usar.

### Ninguém concede o que não tem {#cap-27-subconjunto}

Uma conta só cria outra com permissões que ela mesma tem ([capítulo 8](#cap-08-subconjunto)). Um pedido com qualquer bit a mais volta com status 200 e `"rejected":["users.perms"]`, e a conta não é criada.

É por isso que a conta de 7433 leva as três permissões de painel, **Limites**, **Bloqueio** e **Manutenção** (`0x0400`, `0x0800` e `0x1000`), mesmo sem nunca usar o painel. Elas estão ali para que o gestor possa repassá-las às pessoas que vai cadastrar. Uma conta de serviço sem elas cria contas, mas não cria a conta de painel que o gestor existe para cadastrar.

Consequências para o gestor:

- a tela que monta as permissões de uma conta nova deve oferecer no máximo os bits da conta de serviço, lidos em `GET /api/perms`;
- o PIN do `admin` só pode ser definido pelo próprio `admin` ou por um administrador completo; a conta de serviço recebe `"rejected":["users.id"]`;
- o nível de administrador completo nunca se concede pela API.

### O que só o administrador completo faz {#cap-27-admin}

Backup, restauração, atualização de firmware e instalação do certificado HTTPS exigem o administrador completo ([capítulo 8](#cap-08-admin)). Nas imagens release e alpha, só a conta `admin` tem esse nível. [air]{.img} No Air, o console completo pode dar o nível a outra conta com `user perm <nome> admin` ([capítulo 14](#cap-14)).

Na prática, a atualização da frota usa a conta `admin` de cada aparelho. Guarde essas senhas num cofre de segredos, uma por aparelho, e use-as só nas janelas de atualização.

### Sessões numa frota {#cap-27-sessoes}

Cada aparelho tem três vagas de sessão, uma por conta ([capítulo 26](#cap-26-sessoes)). Numa frota, isso pede disciplina:

- **Uma sessão por aparelho, reaproveitada.** Entre uma vez, guarde o token e entre de novo só quando a sessão morrer.
- **Uma conta por processo.** Dois processos com a mesma conta derrubam a sessão um do outro a cada entrada. Se o gestor tem vários trabalhadores, dê uma conta a cada um, ou faça um único trabalhador falar com cada aparelho.
- **Saia ao terminar.** `GET /logout` devolve a vaga. Sem isso, ela fica ocupada por 15 min.
- **Deixe vagas para as pessoas.** O gestor não deve ocupar mais de uma vaga por aparelho. As outras duas são dos operadores.
- **Monitorar pelo `/metrics` não ocupa vaga.** O HTTP Basic confere conta e senha a cada coleta, sem abrir sessão.

### A primeira entrada da conta de serviço {#cap-27-primeira}

Uma conta criada pela página **Usuários** ou pela API nasce com uma senha de uso único e precisa trocá-la ([capítulo 26](#cap-26-troca)). A troca pode ser feita pelo próprio gestor:

```bash
H=http://192.0.2.10
TEMP_HASH=$(printf '%s' "$SENHA_TEMPORARIA" | sha256sum | cut -d' ' -f1)
NOVA_HASH=$(printf '%s' "$SIMUT_SERVER_PASS" | sha256sum | cut -d' ' -f1)
NONCE=$(curl -s "$H/api/login_init" | sed 's/.*"nonce":"\([^"]*\)".*/\1/')
curl -s -c jar -X POST "$H/api/login" --data-urlencode user=servidor \
  --data-urlencode "pass=$TEMP_HASH" --data-urlencode "nonce=$NONCE"
# {"ok":true,"redirect":"/force_chpass"}
curl -s -b jar -X POST "$H/api/force_chpass" \
  --data-urlencode "p1=$NOVA_HASH" --data-urlencode "p2=$NOVA_HASH"
# {"status":"ok"}
```

Este exemplo vale para HTTP. Em HTTPS, `p1` e `p2` levam a senha nova em texto, com pelo menos 8 caracteres, letra e algarismo.

## Gestão central {#cap-27-central}

### Um serviço no servidor {#cap-27-servidor}

O caminho mais simples é um programa no servidor que fala com cada aparelho: um script em Python, um serviço agendado, uma rotina do seu sistema. Ele não depende de CORS, guarda o cookie ou o token como quiser e não tem as restrições do navegador. O cliente em Python do [capítulo 26](#cap-26-python) é um ponto de partida.

Regras para esse programa:

- fale com um aparelho de cada vez, ou no máximo com um pedido de cada vez por aparelho ([capítulo 26](#cap-26-um-por-vez));
- ponha um limite de tempo em cada pedido e siga para o próximo aparelho quando um não responder;
- trate `401` entrando de novo uma vez, e `403` perguntando `GET /api/perms` ([capítulo 26](#cap-26-401-403));
- registre cada gravação com o `uid`, o que foi pedido e a resposta inteira, inclusive `rejected`.

### Uma página no navegador {#cap-27-navegador}

Uma página hospedada no seu servidor, como `http://gestor.exemplo.com.br:8080`, também pode falar com os aparelhos direto do navegador do operador. Para isso:

1. Configure em cada aparelho a origem exata da página, pelo console serial ou pela chave `cors` do `commit_all` ([capítulo 9](#cap-09-cors)).
2. Reinicie cada aparelho: a origem só vale depois do reinício.
3. Na página, entre com `POST /api/login`. Como o pedido sai da origem autorizada, a resposta traz o token no corpo, em `token`.
4. Mande o token em `Authorization: Bearer` em cada pedido. A página não usa cookie.
5. Saia com `GET /logout` e o mesmo cabeçalho quando o operador fechar a página.

```js
async function entrar(base, usuario, senhaHash) {
  const ini = await (await fetch(base + '/api/login_init')).json();
  if (ini.locked) throw new Error('bloqueado por ' + ini.lockSec + ' s');
  const corpo = new URLSearchParams({ user: usuario, pass: senhaHash, nonce: ini.nonce });
  const r = await fetch(base + '/api/login', { method: 'POST', body: corpo });
  const j = await r.json();
  if (!j.ok) throw new Error('entrada recusada: ' + r.status);
  if (!j.token) throw new Error('sem token: a origem desta página não está no CORS do aparelho');
  return j.token;
}
```

A página calcula `senhaHash` como o SHA-256 hexadecimal da senha, do mesmo jeito que a página de entrada do aparelho ([capítulo 26](#cap-26-hash)).

O que o navegador impõe:

- **A origem precisa bater exatamente**: esquema, nome e porta. `https://gestor.exemplo.com.br` e `http://gestor.exemplo.com.br` são origens diferentes.
- **Página em HTTPS não fala com aparelho em HTTP.** O navegador bloqueia o conteúdo misto. Uma página servida em HTTPS exige aparelhos em HTTPS, com certificados em que o navegador confie. O jeito prático é uma autoridade certificadora da própria empresa, instalada nos navegadores dos operadores, assinando o certificado de cada aparelho ([capítulo 9](#cap-09-https)).
- **Cabeçalhos da resposta ficam invisíveis.** O aparelho não libera cabeçalhos extras para outras origens: a página lê status e corpo, mas não `Retry-After` nem os `X-Backup-*`.
- **Uma atualização de firmware apaga a origem CORS**, junto com o certificado HTTPS. Depois de atualizar, a página perde o acesso ao aparelho até o backup ser restaurado. Faça atualizações e restaurações por um serviço no servidor, ou pela página **Arquivos** do próprio aparelho ([Atualização da frota](#cap-27-ota)).

::: perigo
**A página do gestor guarda as chaves da frota inteira.** Um script malicioso injetado nela lê os tokens de todos os aparelhos abertos. Guarde os tokens só na memória da página, nunca em `localStorage`; sirva a página de um servidor que só a sua equipe controla; e não carregue nela scripts de terceiros.
:::

## Configuração em lote {#cap-27-lote}

### O modelo e os campos de cada aparelho {#cap-27-modelo}

Separe a configuração em duas partes:

| Parte | Exemplos | Como tratar |
|---|---|---|
| Modelo, igual em toda a frota | Servidor e caminho da telemetria, formato, lotes, modelos de payload, linha de alarmes, fuso, NTP, syslog, política de PIN | Um único JSON, validado uma vez |
| Próprio de cada aparelho | `name`, `t_key` (uma chave por aparelho), `m_topic` e `m_cid` no MQTT, `hwId` e nomes dos slots, limites de cada sensor | Tirado do cadastro, pelo `uid` |

Monte o `_payload` de cada aparelho juntando o modelo com os campos dele. Mande só as chaves que você quer definir: campo ausente mantém o valor ([capítulo 26](#cap-26-commit)).

### O ciclo por aparelho {#cap-27-ciclo}

1. Entre e confirme o `uid` ([Confirmar quem responde](#cap-27-confirmar)).
2. [release]{.img} [alpha]{.img} Mande o `_payload` com `_dry=1`. Leia `rejected`, `reboot` e `reboot_for`.
3. Se o ensaio recusou algum campo, pare e corrija o modelo ou o cadastro.
4. Mande o mesmo `_payload` sem `_dry`, ou com `_reboot=1` se você já espera reiniciar.
5. Se a resposta traz `"reboot":true`, espere o aparelho voltar e entre de novo.
6. Leia o estado de volta (`GET /api/config`, `GET /api/alarms` ou `GET /api/network`) e confira os campos.
7. Guarde o novo `cfg` de `GET /api/status`. Ele passa a ser a referência daquele aparelho.

::: atencao
**O ensaio tem pontos cegos.** Ele aceita só as seções `sys`, `net` e `alarms`; não enxerga `h_int`, `ntp_enabled`, `slog_*`, `m_had`, `dns_auto`, `dns2` e `web_ka`; e aplica de verdade o fuso e os sons ([capítulo 26](#cap-26-commit)). [air]{.img} O Air recusa o ensaio: nele, cada gravação é real e reinicia. Teste o modelo primeiro num aparelho de bancada da mesma imagem.
:::

### Um exemplo em Python {#cap-27-exemplo-lote}

O script usa a classe `Simut` do [capítulo 26](#cap-26-python), salva como `simut_status.py`. O inventário é um arquivo JSON com o endereço, o `uid` esperado e os campos próprios de cada aparelho. A senha da conta de serviço vem do ambiente.

```python
#!/usr/bin/env python3
"""Aplica um modelo de configuração a uma lista de aparelhos, um de cada vez."""
import json
import os
import sys
import time

import requests
from simut_status import Simut, SimutError

MODELO = {
    "sys": {
        "t_srv": "coletor.exemplo.com.br", "t_port": 443, "t_path": "/telemetria",
        "t_sec": 1, "t_mode": 0, "t_int": 10, "t_bat": 250, "tz": -3,
    },
    "alarms": {"sensors": []},
}

def esperar_volta(dev, limite=120):
    fim = time.time() + limite
    while time.time() < fim:
        time.sleep(3)
        try:
            if dev.http.get(dev.base + "/api/login_init", timeout=5).ok:
                return True
        except requests.RequestException:
            pass
    return False

def aplicar(item, senha):
    dev = Simut(item["url"], "servidor", senha)
    dev.login()
    try:
        s = dev.status(quiet=True)["sys"]
        if s["uid"] != item["uid"]:
            raise SimutError("uid %s no lugar de %s" % (s["uid"], item["uid"]))
        payload = json.loads(json.dumps(MODELO))
        payload["sys"].update(item.get("sys", {}))           # name, t_key...
        payload["alarms"]["sensors"] = item.get("limites", [])
        if s["env"] != "air":
            ensaio = dev.commit(payload, mode="dry")
            if ensaio.get("rejected"):
                raise SimutError("o ensaio recusou %s" % ensaio["rejected"])
        r = dev.commit(payload)
        if r.get("reboot"):
            if not esperar_volta(dev):
                raise SimutError("não voltou depois do reinício")
            dev.login()
        if r.get("rejected"):
            raise SimutError("gravado, mas recusou %s" % r["rejected"])
        return dev.status(quiet=True)["sys"]["cfg"], r
    finally:
        dev.logout()

if __name__ == "__main__":
    senha = os.environ["SIMUT_SERVER_PASS"]
    for item in json.load(open(sys.argv[1], encoding="utf-8")):
        try:
            cfg, r = aplicar(item, senha)
            print("%s ok cfg=%s reboot=%s" % (item["uid"], cfg, r.get("reboot")))
        except (SimutError, requests.RequestException) as e:
            print("%s FALHOU: %s" % (item["uid"], e))
```

Um item do inventário:

```json
{"uid": "E6614C311B7A2F2D", "url": "http://192.0.2.10",
 "sys": {"name": "camara-01", "t_key": "chave-desta-unidade"},
 "limites": [{"idx": 0, "temp": [2.0, 8.0]}]}
```

A seção `alarms` se aplica sem reiniciar. As chaves de `sys` deste modelo incluem `tz`, que reinicia (grupo `time`), e `t_sec`, que reinicia (grupo `mqtt`); a resposta diz isso em `reboot_for` ([capítulo 5](#cap-05-grupos)). Um aparelho que já tem os mesmos valores não reinicia, porque um campo igual ao atual não conta como mudança.

### Reinícios em lote {#cap-27-reinicios}

Uma gravação que reinicia tira o aparelho do ar por alguns segundos e encerra todas as sessões dele, inclusive as dos operadores. Numa frota:

- agrupe numa única gravação tudo o que vai para o mesmo aparelho: duas gravações que reiniciam custam dois reinícios;
- prefira os grupos que se aplicam sem reiniciar (`alarms`, `maint`, `alarm_tel`, `telemetry`) para as mudanças frequentes;
- faça as mudanças que reiniciam fora do horário de uso, e nunca em todos os aparelhos de uma vez;
- toda gravação de contas reinicia: cadastre várias pessoas num único pedido.

### Configuração que muda por fora {#cap-27-deriva}

Guarde o `cfg` de cada aparelho depois de cada gravação sua. Se o valor mudar sem uma gravação do gestor, alguém mudou a configuração pela página, pelo painel ou pelo console. O `cfg` chega também em cada POST de telemetria, no cabeçalho `X-SIMUT-Cfg`, então o coletor percebe a mudança sem perguntar nada ao aparelho.

Compare o `cfg` de um aparelho só com os valores anteriores dele mesmo. Dois aparelhos com a mesma configuração quase nunca têm o mesmo `cfg`, porque as contas levam sementes aleatórias ([capítulo 20](#cap-20-cabecalhos)). Para comparar aparelhos entre si, compare os campos de `GET /api/config`.

## Monitoramento {#cap-27-monitorar}

### Saúde pelo Prometheus {#cap-27-prometheus}

`GET /metrics` entrega os números de cada aparelho no formato do Prometheus, com HTTP Basic ([capítulo 24](#cap-24)). Para a frota:

- crie em cada aparelho uma conta só com **Painel** [PERM_DASHBOARD]{.perm} para o Prometheus;
- use a mesma conta e a mesma senha em toda a frota só se aceitar que vazar uma é vazar todas; o mais seguro é uma senha por aparelho;
- use um intervalo de coleta de 60 s, e nunca menos de 15 s. Cada coleta confere a senha do zero, e isso ocupa o aparelho por cerca de 0,69 s (medido na v2.2.13, 19/08/2026; [capítulo 24](#cap-24-intervalo));
- uma senha errada no Prometheus bloqueia o endereço dele, e o bloqueio vale também para a entrada na interface web a partir do mesmo endereço ([capítulo 26](#cap-26-bloqueio)).

A lista de métricas, a configuração do Prometheus e as regras de alerta estão no [capítulo 24](#cap-24).

::: atencao
**Coletar métricas mantém o Air acordado.** [air]{.img} Uma coleta com senha válida conta como uso e adia a hibernação ([capítulo 19](#cap-19)). Não ponha um Air na coleta periódica: acompanhe-o pela telemetria, que ele envia nos despertares.
:::

### Estado pela API {#cap-27-status}

Para um painel próprio, leia `GET /api/status?quiet=1` de cada aparelho ([capítulo 26](#cap-26-status)). Os campos que importam numa frota:

| Campo | Para quê |
|---|---|
| `sys.ver`, `sys.env` | Versão e imagem: quem precisa de atualização |
| `sys.cfg` | Configuração alterada por fora |
| `sys.uptime` | Reinícios inesperados: o valor volta a zero. Lembre que ele também volta a zero a cada 49,7 dias |
| `sys.rssi` | Sinal fraco. Com −78 dBm ou menos, o aparelho adia a telemetria ([capítulo 9](#cap-09-referencia)) |
| `sys.ntp` | Relógio sem acerto pela rede ([capítulo 10](#cap-10)) |
| `sys.pending` | Telemetria acumulada sem entrega |
| `sys.fs_u`, `sys.fs_t` | Sistema de arquivos enchendo |
| `sensors[].val` | `"Error"` indica sensor em falha |

`?quiet=1` não adia a hibernação do Air. Um intervalo de 60 s por aparelho basta para um painel de frota; a página **Painel de Controle** pergunta a cada 3 s porque mostra um aparelho só.

### Alarmes pela linha de alarmes {#cap-27-linha}

Para saber em segundos que um sensor passou do limite, falhou ou entrou em manutenção, não pergunte: deixe o aparelho avisar pela linha de alarmes ([capítulo 22](#cap-22)). Numa frota:

- aponte a linha de alarmes de todos os aparelhos para o mesmo coletor, com um caminho próprio (`a_path`) diferente do caminho da telemetria;
- identifique o aparelho pelo `X-SIMUT-Uid` do POST, ou pelo tópico no MQTT;
- responda `2xx` rápido: a fila da linha de alarmes vive na memória, e um reinício perde o que não foi confirmado ([capítulo 22](#cap-22-fila)).

### Aparelhos em silêncio {#cap-27-silencio}

Um aparelho que parou de mandar telemetria não avisa que parou. Para cada `uid`, compare a hora do último POST com o ritmo esperado dele ([capítulo 21](#cap-21-quando)). Um silêncio muito maior que o intervalo de envio pede uma verificação: rede, energia ou um aparelho parado. No Prometheus, a própria métrica `up` da coleta cumpre esse papel para os aparelhos que ele coleta.

## Atualização da frota {#cap-27-ota}

### O manifest.json {#cap-27-manifest}

Cada release do projeto publica, nos ativos da página de releases no GitHub (`https://github.com/angeloINTJ/simut/releases`), as imagens de cada variante, os pacotes de idioma e um `manifest.json` que as descreve:

```json
{
  "version": "2.7.1",
  "min_from": "1.6.2",
  "images": {
    "release": {
      "file": "simut_v2.7.1_release.bin",
      "size": 982844,
      "sha256": "9b1f…e07a",
      "uf2": {"file": "simut_v2.7.1_release.uf2", "size": 1966080, "sha256": "44c2…91d0"}
    }
  }
}
```

O exemplo mostra só a entrada `release`; as entradas `alpha` e `air` têm a mesma forma, com os arquivos `simut_v2.7.1_alpha.bin` e `simut_v2.7.1_air.bin`. Os tamanhos e os resumos são ilustrativos e aparecem abreviados; cada release traz os seus, com os 64 algarismos do SHA-256.

| Campo | Significado |
|---|---|
| `version` | A versão desta release |
| `min_from` | A versão mais antiga que consegue receber esta release pela rede |
| `images.<env>` | A imagem de cada variante, com a mesma chave que `sys.env` do aparelho: `release`, `alpha` ou `air` |
| `file`, `size`, `sha256` | Nome do `.bin`, tamanho em bytes e SHA-256 em hexadecimal |
| `uf2` | O mesmo firmware em `.uf2`, para gravar pelo cabo com o botão BOOTSEL ([capítulo 3](#cap-03)). Não serve para a atualização pela rede |

Os pacotes de idioma da mesma release, como `language_pt-BR.lng`, vêm nos mesmos ativos.

A release só publica o manifesto quando a imagem de cada variante traz a etiqueta da própria variante e da versão. O aparelho também confere a etiqueta: uma imagem de outra variante é recusada no envio com `"v":7` ([capítulo 26](#cap-26-ota)).

### Antes de atualizar um aparelho {#cap-27-antes}

Para cada aparelho, o gestor confere:

1. **A imagem certa.** Leia `sys.env` e escolha `images[env]`. Nunca escolha pela versão ou pelo nome do arquivo.
2. **O arquivo inteiro.** Calcule o SHA-256 do `.bin` baixado e compare com `sha256`. Confira também `size`.
3. **A etiqueta.** O `.bin` contém o texto `SIMUT-ENV:<env>;v=<versão>;`. Confira que `<env>` é o do aparelho e `<versão>` é a do manifesto.
4. **A versão de partida.** A versão atual do aparelho precisa ser igual ou mais nova que `min_from`. Firmwares anteriores à 1.6.2 tinham um aplicador que dizia ter atualizado sem instalar nada; eles precisam de uma gravação pelo cabo uma vez.
5. **A conta.** A atualização exige o administrador completo ([O que só o administrador completo faz](#cap-27-admin)).

```python
import hashlib, json, re

man = json.load(open("manifest.json"))
env = "release"                                  # sys.env do aparelho
img = man["images"][env]
dados = open(img["file"], "rb").read()
assert len(dados) == img["size"], "tamanho diferente do manifesto"
assert hashlib.sha256(dados).hexdigest() == img["sha256"], "SHA-256 diferente"
m = re.search(rb"SIMUT-ENV:([a-z]+);v=([^;]+);", dados)
assert m and m.group(1).decode() == env and m.group(2).decode() == man["version"], "etiqueta errada"
```

### A sequência em cada aparelho {#cap-27-sequencia}

A sequência é a do [capítulo 26](#cap-26-ota), com dois passos a mais no fim:

1. Baixe o backup `.bkp` e confira a integridade.
2. Envie a imagem com `POST /api/restore?op=stage&commit=1` e confira `"v":0` e `"committed":1`.
3. Aplique com `POST /api/ota/apply`.
4. Espere o aparelho voltar e confirme a versão nova em `sys.ver`.
5. Restaure o backup com `POST /api/restore?op=apply` e espere o reinício.
6. Envie os pacotes de idioma da release nova para `/lang` e reinicie, para que os textos novos apareçam traduzidos ([capítulo 13](#cap-13-idioma)).

::: atencao
**Sem o passo 5, o aparelho fica sem histórico, sem certificado HTTPS e sem origem CORS.** A atualização reformata o sistema de arquivos e só a configuração principal sobrevive ([capítulo 26](#cap-26-ota)). O backup restaurado traz os pacotes de idioma antigos: por isso o passo 6 vem depois dele.
:::

Uma imagem de 1 MB leva cerca de 30 s para chegar. Não desligue nem reinicie o aparelho entre os passos 2 e 4: há um único espaço de firmware, e uma falha na gravação exige o cabo USB ([capítulo 18](#cap-18)).

### Em ondas {#cap-27-ondas}

Nunca atualize a frota inteira de uma vez.

::: {.figura #fig-27-ondas tipo="diagrama" arquivo="27-ondas.png" captura="diagrama: a frota dividida em três ondas; onda 0 com um aparelho de bancada, onda 1 com poucos aparelhos de cada imagem, onda 2 com o resto; entre as ondas, uma barreira 'conferir versão, telemetria e alarmes por um período'; dentro de cada aparelho, a sequência backup, envio, aplicação, confirmação, restauração, idioma"}
A atualização em ondas. Cada onda só começa depois de a anterior se provar estável.
:::

1. **Onda 0.** Um aparelho de bancada de cada imagem em uso. Confira a versão, a telemetria, a linha de alarmes, o painel e o histórico restaurado.
2. **Onda 1.** Poucos aparelhos em uso real. Espere um período de trabalho normal e confira que todos voltaram a mandar telemetria.
3. **Onda 2.** O resto, em grupos pequenos, um aparelho de cada vez dentro de cada grupo.

Pare tudo ao primeiro aparelho que não voltar ou que voltar com a versão antiga. Um status HTTP de sucesso não prova nada: só a versão nova informada pelo aparelho prova a atualização.

[air]{.img} Um Air só atende enquanto está acordado em M0. Atualize um Air com ele na bancada, ligado ao carregador, ou dentro de uma janela em que ele esteja acordado ([capítulo 19](#cap-19)).

## Segurança numa frota {#cap-27-seguranca}

As recomendações gerais estão no [capítulo 29](#cap-29-recomendacoes). Numa frota, somam-se estas:

1. **Rede fechada.** Ponha aparelhos, coletor e gestor numa rede ou VLAN própria, sem rota para a internet. Nunca exponha um aparelho à internet, nem com porta redirecionada.
2. **HTTPS onde a rede não é sua.** Em HTTP, quem escuta a rede lê as leituras, os PINs enviados num `commit_all` e o token de sessão, e o resumo da senha basta para entrar ([capítulo 26](#cap-26-hash)). Instale um par de certificados em cada aparelho release ([capítulo 9](#cap-09-https)).
3. **Uma conta por função, com o mínimo de permissões.** Monitoramento com **Painel**; configuração com a conta de serviço; atualização com o `admin`, só nas janelas de atualização.
4. **Senhas diferentes em cada aparelho**, guardadas num cofre de segredos e lidas do ambiente pelos scripts, nunca escritas neles.
5. **Uma chave de telemetria por aparelho** (`t_key`), conferida pelo coletor. Os cabeçalhos `X-SIMUT-*` identificam, mas não autenticam: qualquer máquina da rede pode imitá-los ([capítulo 20](#cap-20-cabecalhos)).
6. **Backups são segredos.** Um `.bkp` contém a configuração inteira, com as contas e as chaves. Guarde-os cifrados e com acesso restrito.
7. **Trilha fora do aparelho.** Mande o log de eventos de cada aparelho para um servidor syslog ([capítulo 25](#cap-25)). As gravações do gestor aparecem ali com o código 303, e as entradas com o 300.
8. **Origem CORS exata.** Configure só a origem do gestor, com esquema e porta. Sem gestor no navegador, deixe o CORS desligado.
9. **Firmware em dia.** As correções de segurança saem só na release mais nova ([capítulo 29](#cap-29-recomendacoes)).

## Receita completa: uma pessoa no painel, do zero {#cap-27-receita}

[release]{.img}

Esta receita cadastra uma pessoa que vai bloquear alarmes e abrir janelas de manutenção no painel de um aparelho. Ela usa a conta de serviço de 7433 ([Contas de serviço](#cap-27-servico)). A senha vem do ambiente.

```bash
H=http://192.0.2.10
HASH=$(printf '%s' "$SIMUT_SERVER_PASS" | sha256sum | cut -d' ' -f1)

entrar() {
  NONCE=$(curl -s "$H/api/login_init" | sed 's/.*"nonce":"\([^"]*\)".*/\1/')
  curl -s -c jar -X POST "$H/api/login" --data-urlencode user=servidor \
    --data-urlencode "pass=$HASH" --data-urlencode "nonce=$NONCE"
  echo
}

# 1. sessão
entrar

# 2. a política de PIN: o PIN precisa servir no painel e na API
curl -s -b jar "$H/api/config" | python3 -c \
  'import sys, json; d = json.load(sys.stdin); print({k: d[k] for k in ("pin_min", "pin_max", "pin_kb", "pin_alpha")})'

# 3. criar a conta, com PIN e as permissões Bloqueio e Manutenção (6144)
curl -s -b jar -X POST "$H/api/commit_all" --data-urlencode \
  '_payload={"users":{"actions":[{"type":"add","name":"joao","perms":6144,"pin":"482913"}]}}'
echo

# 4. esperar o reinício e entrar de novo
sleep 3
until curl -s -o /dev/null -m 3 "$H/api/login_init"; do sleep 3; done
entrar

# 5. conferir
curl -s -b jar "$H/api/users"
echo
```

O que esperar em cada passo:

1. `{"ok":true,"redirect":"/"}`. Com `"redirect":"/force_chpass"`, a conta de serviço ainda precisa trocar a senha ([A primeira entrada](#cap-27-primeira)).
2. Por exemplo `{'pin_min': 4, 'pin_max': 8, 'pin_kb': 3, 'pin_alpha': 0}`. Escolha um PIN só de algarismos, com comprimento entre o maior de 4 e `pin_min` e o menor de 8 e `pin_max`. A API aceita só PINs de 4 a 8 algarismos, qualquer que seja a política ([capítulo 26](#cap-26-contas)).
3. `{"status":"ok","reboot":true,"reboot_for":["users"],"creds":[{"u":"joao","p":"K7M2QX9A"}]}`. **Guarde a senha de `creds` agora**: ela não aparece de novo. Se a resposta trouxer `rejected`, leia o motivo antes de seguir ([capítulo 26](#cap-26-contas)).
4. O aparelho reinicia logo depois de responder e volta em alguns segundos.
5. A conta `joao` aparece com `"perms":6144` e `"pin":true`.

Três variações comuns:

- **`"rejected":["users.perms"]` e nenhuma conta criada:** a conta de serviço não tem as permissões de painel. Ela precisa portar `0x0800` e `0x1000` para concedê-las.
- **`"rejected":["users.pin"]` e a conta criada sem PIN:** o PIN estava fora da regra ou já pertence a outra conta. Defina outro com `{"type":"pin","id":<id>,"pin":"..."}`, lendo o `id` em `GET /api/users`.
- **`"rejected":["users.dup"]`:** já existe uma conta `joao`. Para trocar as permissões dela, apague por `id` e crie de novo no mesmo pedido, com a exclusão antes.

A pessoa entra no painel com o PIN. A senha de `creds` só serve para a interface web, e pede troca na primeira entrada.

## Lista de aceitação do gestor {#cap-27-aceitacao}

Antes de pôr o gestor em produção, prove cada item:

- [ ] identifica cada aparelho pelo `uid`, e confere o `uid` antes de gravar;
- [ ] entra uma vez por aparelho, reaproveita a sessão e sai ao terminar;
- [ ] entra de novo em `401`, e em `403` pergunta `GET /api/perms` antes de desistir;
- [ ] não insiste durante um bloqueio: lê `locked` e `lockSec` no `login_init`;
- [ ] manda um pedido de cada vez a cada aparelho;
- [ ] lê `rejected` em toda resposta do `commit_all` e mostra ao operador;
- [ ] espera o reinício quando a resposta traz `"reboot":true`, e entra de novo;
- [ ] guarda `creds` na resposta que as traz;
- [ ] apaga e reseta contas por `id`, nunca por nome;
- [ ] oferece, para uma conta nova, no máximo as permissões da conta de serviço;
- [ ] manda texto com acento em UTF-8 direto no JSON, sem `\uXXXX`;
- [ ] escolhe a imagem de atualização pelo `env`, confere SHA-256, tamanho, etiqueta e `min_from`;
- [ ] atualiza em ondas, restaura o backup e confirma a versão nova informada pelo aparelho;
- [ ] não tenta silenciar alarmes pela rede: usa `active:false` ou uma janela de manutenção ([capítulo 26](#cap-26-alarmes)).
