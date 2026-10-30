// The Ângulo's text lookup (see Textos.h).

#include "Textos.h"

#include <stdlib.h>
#include <string.h>

namespace ua {

namespace {

// The one allocation a pack keeps: this header, then n hashes (uint32_t, ascending), n offsets (uint16_t, into the text area, in the order of the
// hashes) and the text area, each text followed by its NUL. Every part starts at an address its type needs: malloc( ) gives 8, the header is 8 B,
// the hashes 4 B each and the offsets come after them.
struct Tabela {
  uint32_t n;       // entries
  uint32_t bytes;   // the allocation, this header included
};

// Where the parts of a table lie.
struct Vista {
  uint32_t *h;
  uint16_t *d;
  char *t;
  explicit Vista( Tabela *tab )
      : h( reinterpret_cast<uint32_t *>( tab + 1 ) ),
        d( reinterpret_cast<uint16_t *>( h + tab->n ) ),
        t( reinterpret_cast<char *>( d + tab->n ) ) {}
};

// The published table. Written once, last, by textosCarregar( ) on Core 0; read by txt( ) on either core. Null until a good section is loaded.
Tabela *gTabela = nullptr;

int hex( char c ) {
  if ( c >= '0' && c <= '9' ) return c - '0';
  if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
  if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
  return -1;
}

enum Linha : uint8_t { LINHA_BOA, LINHA_VAZIA, LINHA_MA };

// The lines of a section, one at a time, without their '\n'.
struct Linhas {
  const char *p;
  size_t n, i;
  bool proxima( const char *&ini, size_t &len ) {
    if ( i >= n ) return false;
    ini = p + i;
    size_t j = i;
    while ( j < n && p[j] != '\n' ) j++;
    len = j - i;
    i = j < n ? j + 1 : n;
    return true;
  }
};

// One line: 8 hex digits, a space and the text, which must show a character (a text of blanks would draw an empty label, which nobody
// notices) and hold no NUL (it is kept as a C string). A '\r' at the end is a CRLF line end and is not the text's.
Linha ler( const char *ini, size_t len, uint32_t &hash, const char *&texto, size_t &tam ) {
  if ( len && ini[len - 1] == '\r' ) len--;
  if ( len == 0 ) return LINHA_VAZIA;
  if ( len < 10 || ini[8] != ' ' ) return LINHA_MA;
  uint32_t h = 0;
  for ( int k = 0; k < 8; k++ ) {
    const int d = hex( ini[k] );
    if ( d < 0 ) return LINHA_MA;
    h = h << 4 | (uint32_t)d;
  }
  bool visivel = false;
  for ( size_t k = 9; k < len; k++ ) {
    if ( ini[k] == '\0' ) return LINHA_MA;
    if ( ini[k] != ' ' && ini[k] != '\t' ) visivel = true;
  }
  if ( !visivel ) return LINHA_MA;
  hash = h;
  texto = ini + 9;
  tam = len - 9;
  return LINHA_BOA;
}

// Shell sort of the hashes with their offsets: no recursion, no allocation, and a section of the largest size takes ~1,500 entries.
void ordenar( uint32_t *h, uint16_t *d, uint32_t n ) {
  static const uint16_t folgas[] = { 701, 301, 132, 57, 23, 10, 4, 1 };
  for ( const uint16_t folga : folgas ) {
    for ( uint32_t i = folga; i < n; i++ ) {
      const uint32_t hv = h[i];
      const uint16_t dv = d[i];
      uint32_t j = i;
      for ( ; j >= folga && h[j - folga] > hv; j -= folga ) {
        h[j] = h[j - folga];
        d[j] = d[j - folga];
      }
      h[j] = hv;
      d[j] = dv;
    }
  }
}

}  // namespace

TextosResultado textosCarregar( const char *secao, size_t n ) {
  if ( __atomic_load_n( &gTabela, __ATOMIC_ACQUIRE ) ) return TEXTOS_JA_CARREGADO;
  if ( n > UA_TEXTOS_MAX ) return TEXTOS_GRANDE;
  if ( !secao ) return TEXTOS_VAZIO;

  // Pass one: every line must be good. Nothing is allocated for a section that is going to be refused.
  uint32_t quantas = 0, bytesDeTexto = 0;
  Linhas l{ secao, n, 0 };
  const char *ini = nullptr;
  size_t len = 0;
  while ( l.proxima( ini, len ) ) {
    uint32_t h = 0;
    const char *tx = nullptr;
    size_t tam = 0;
    const Linha r = ler( ini, len, h, tx, tam );
    if ( r == LINHA_MA ) return TEXTOS_MALFORMADA;
    if ( r == LINHA_BOA ) {
      quantas++;
      bytesDeTexto += (uint32_t)tam + 1;
    }
  }
  if ( quantas == 0 ) return TEXTOS_VAZIO;

  const uint32_t bytes = (uint32_t)sizeof( Tabela ) + quantas * (uint32_t)( sizeof( uint32_t ) + sizeof( uint16_t ) ) + bytesDeTexto;
  Tabela *t = static_cast<Tabela *>( malloc( bytes ) );
  if ( !t ) return TEXTOS_SEM_MEMORIA;
  t->n = quantas;
  t->bytes = bytes;

  // Pass two: fill the table in the order of the file, then sort it.
  const Vista v( t );
  uint32_t i = 0, fim = 0;
  l = Linhas{ secao, n, 0 };
  while ( l.proxima( ini, len ) ) {
    uint32_t h = 0;
    const char *tx = nullptr;
    size_t tam = 0;
    if ( ler( ini, len, h, tx, tam ) != LINHA_BOA ) continue;
    v.h[i] = h;
    v.d[i] = (uint16_t)fim;
    memcpy( v.t + fim, tx, tam );
    v.t[fim + tam] = '\0';
    fim += (uint32_t)tam + 1;
    i++;
  }
  ordenar( v.h, v.d, quantas );
  for ( uint32_t k = 1; k < quantas; k++ ) {
    if ( v.h[k] == v.h[k - 1] ) {
      free( t );
      return TEXTOS_DUPLICADA;
    }
  }

  // Publish. The pointer is written last, by one release store: Core 0 built the table, Core 1 reads it, and the two cores are separate masters on
  // the bus, so the stores that fill the table could reach the other core after the pointer if nothing ordered them. On the Cortex-M0+ and the
  // Cortex-M33 a release store is a data memory barrier and then the store; the reader's acquire load (txt) is the load and then a barrier, so a
  // reader that sees the pointer sees the whole table, and one that does not sees nothing. The host test cannot prove this order (one core, and a
  // compiler barrier is all the PC needs): it checks the shape (tools/tft_ui/test_textos.py), and the bench is the proof (S10c plan, B7).
  __atomic_store_n( &gTabela, t, __ATOMIC_RELEASE );
  return TEXTOS_OK;
}

#ifdef UA_HOST
// Host tests only (see Textos.h): the table is freed under the reader's feet if Core 1 is drawing a text of it, so no device build compiles this.
void textosLimpar( ) {
  Tabela *t = __atomic_exchange_n( &gTabela, static_cast<Tabela *>( nullptr ), __ATOMIC_ACQ_REL );
  free( t );
}
#endif

size_t textosContagem( ) {
  const Tabela *t = __atomic_load_n( &gTabela, __ATOMIC_ACQUIRE );
  return t ? t->n : 0;
}

size_t textosBytes( ) {
  const Tabela *t = __atomic_load_n( &gTabela, __ATOMIC_ACQUIRE );
  return t ? t->bytes : 0;
}

const char *txt( const char *en ) {
  Tabela *t = __atomic_load_n( &gTabela, __ATOMIC_ACQUIRE );
  if ( !t ) return en;
  const uint32_t h = fnv1a( en );
  const Vista v( t );
  uint32_t lo = 0, hi = t->n;
  while ( lo < hi ) {
    const uint32_t meio = lo + ( hi - lo ) / 2;
    if ( v.h[meio] < h ) lo = meio + 1;
    else hi = meio;
  }
  if ( lo == t->n || v.h[lo] != h ) return en;
  return v.t + v.d[lo];
}

}  // namespace ua
