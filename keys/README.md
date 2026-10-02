# keys/

As chaves **públicas** da OTA assinada e os pisos que cada imagem compila
(`docs/analysis/OTA_ASSINADA.md`). Nenhuma chave privada mora aqui, nem em
outro lugar do repositório.

| Arquivo | O quê |
|---|---|
| `ota_root_release.pub` | A raiz de produção: P-256, ponto não comprimido, em hex. Gerada pelo mantenedor em 01/10/2026 com `tools/ota_sign.py root-new`, num terminal dele |
| `ota_root_bench.pub` | A raiz de bancada. Só confiam nela as imagens de perfis não publicados que pedem `ota_trust_bench` em `tools/features.toml` |
| `ota_policy.json` | O que cada imagem exige da seguinte: `security_version` mínimo e a menor série aceita por escopo |

`tools/ota_sign.py gen-trust` escreve `src/ota/ota_trust.h` a partir destes três
arquivos, e o CI roda `gen-trust --check`. Quem muda um arquivo daqui roda o
gerador no mesmo commit.

## Quando mexer

- **Uma release corrige uma falha de segurança:** suba `security_version`. Quem
  instalar essa release não volta pelo ar para uma anterior a ela. Entre versões
  com o mesmo número, voltar continua possível.
- **Trocar a chave de assinatura**, por rotina ou por suspeita: a raiz certifica
  uma nova com série maior (`signer-new --serial N+1`). Para a antiga deixar de
  valer, suba `lowest_serial.release` para a série nova na release que sai
  assinada por ela. Cada aparelho passa a recusar a antiga quando instala essa
  release; antes disso, ainda a aceita.
- **Trocar a raiz:** a pública nova entra numa release assinada pela cadeia
  antiga, e quem a instala passa a confiar só na nova. Com a raiz vazada, isso
  não basta: o desenho explica por que a frota volta para o USB.

## Onde moram as privadas

| Chave | Onde |
|---|---|
| Raiz de produção | Offline com o mantenedor: PKCS#8 cifrado com senha, em duas cópias |
| Assinatura de produção | Segredo do Environment `release` do GitHub, com o mantenedor como revisor |
| Raiz e assinatura de bancada | `~/.simut-ota/` no PC do mantenedor, sem senha, modo 600 |
