/**
 * @file BuildIdentity.cpp
 * @brief The variant tag in .rodata and the scanner that reads one back.
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */
#include "BuildIdentity.h"
#include <string.h>

/* `used` não basta, e descobrir isso custou um .bin.
 *
 * `used` impede o COMPILADOR de descartar o símbolo; quem apaga a seção
 * depois é o LINKER, com --gc-sections, e para ele um `(void)SIMUT_ENV_TAG;`
 * não é referência. Medido: a primeira versão compilou, linkou e produziu um
 * .bin **sem a etiqueta** — a funcionalidade inteira de recusar a variante
 * errada dependia de uma string que não estava na imagem. `retain`
 * (SHF_GNU_RETAIN) resolveria, mas este toolchain a ignora e o projeto compila
 * com -Werror=attributes.
 *
 * O que segura a string é uma **leitura de verdade**, em outra unidade de
 * compilação: `ota_validate_staging` lê esta etiqueta para saber qual é a
 * variante em execução. Sem LTO o compilador não pode dobrar a chamada nem
 * provar que ninguém a usa, então o endereço é materializado e o linker
 * mantém. A conferência que pega uma regressão disto não é de compilação: é
 * procurar a etiqueta no .bin, e é o que tools/release_manifest.py faz antes
 * de publicar qualquer imagem.
 *
 * No RP2350 essa leitura não existe. A imagem recusa o OTA até os slots A/B
 * existirem (src/ota/ota_layout.h), o validador fica fora do link, e o .bin sai
 * sem a etiqueta: medido em 2026-10-03, a pico2_w_release do passo 2 não a tem.
 * Nada a lê nessa imagem, porque o `env` de /api/perms, /api/status e do mDNS
 * vem do macro. E nada a assina ou publica por engano: tools/ota_sign.py e
 * tools/release_manifest.py recusam uma imagem sem etiqueta. Ela volta com o
 * stage no slot inativo (docs/analysis/OTA_AB_RP2350.md, etapa 4). */
const char SIMUT_ENV_TAG[] __attribute__((used)) =
    SIMUT_ENV_TAG_PREFIX SIMUT_ENV_NAME ";v=" SIMUT_VERSION ";";

bool simut_env_tag_scan(const unsigned char* buf, unsigned len, char* out, unsigned outLen) {
    static const unsigned char pfx[] = SIMUT_ENV_TAG_PREFIX;
    const unsigned plen = sizeof(pfx) - 1;
    if (!buf || !out || outLen == 0 || len < plen + 1) return false;
    for (unsigned i = 0; i + plen < len; i++) {
        if (memcmp(buf + i, pfx, plen) != 0) continue;
        unsigned j = i + plen, k = 0;
        while (j < len && buf[j] != ';' && k + 1 < outLen) {
            unsigned char c = buf[j];
            /* The env name is [a-z]; anything else is a coincidence in the
             * binary, not a tag. */
            if (c < 'a' || c > 'z') { k = 0; break; }
            out[k++] = (char)c;
            j++;
        }
        if (k && j < len && buf[j] == ';') { out[k] = '\0'; return true; }
    }
    out[0] = '\0';
    return false;
}
