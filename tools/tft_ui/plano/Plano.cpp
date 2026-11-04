// Ports of the plan's family A screens (renderizador/src/telas.js), line for
// line, with the plan's scenario. See Plano.h.
#include "Plano.h"

#include <string.h>

#include "PlanoComum.h"

using namespace ua;

namespace plano {

namespace {

/* telas.js interruptor( ) and progresso( ) are the kit's; these are the
 * plan's screens. */

// spec_componentes: buttons, fields, badges, chips, switches, progress,
// segmented control and keys.
void spec_componentes( Pintor &p ) {
  const Tema &T = p.t( );
  textoCentrado( p, "Componentes", PAD, 0, 28, tipo::titulo, T.tinta, 'l' );
  textoCentrado( p, "todo estado existe", 320 - PAD, 0, 28, tipo::apoio, T.tinta2, 'r' );
  p.hline( 0, 28, 320, T.linha );
  int16_t x = PAD;
  const struct { const char *r; Botao t; } bs[] = { { "Salvar", Botao::PRIMARIO }, { "Cancelar", Botao::SECUNDARIO }, { "Voltar", Botao::DISCRETO } };
  for ( const auto &b : bs ) {
    const int16_t w = larguraTinta( b.r, tipo::botao ) + 2 * ESP3;
    botao( p, x, 36, w, 28, b.r, b.t );
    x += w + ESP2;
  }
  botao( p, PAD, 72, 72, 28, "Salvar", Botao::PRIMARIO, true );
  botao( p, PAD + 80, 72, 80, 28, "Apagar", Botao::DESTRUTIVO );
  const char *est[] = { "foco", "erro" };
  for ( int i = 0; i < 2; i++ ) {
    const int16_t cx = PAD + 168 + i * 68;
    campo( p, cx, 72, 60, 28, i == 0 ? CampoEstado::FOCO : CampoEstado::ERRO );
    textoCentrado( p, est[i], cx + ESP3, 72, 28, tipo::apoio, T.tinta2, 'l' );
  }
  x = PAD;
  const struct { const char *t; Tom tom; } ss[] = { { "Na faixa", Tom::POSITIVO }, { "No limite", Tom::ALERTA }, { "Acima", Tom::PERIGO }, { "Mudo", Tom::NEUTRO } };
  for ( const auto &s : ss ) x += selo( p, x, 108, 20, s.t, s.tom ) + ESP1;
  x = PAD;
  x += chip( p, x, 136, 24, "HOSP-IOT", true ) + ESP2;
  x += chip( p, x, 136, 24, "Lab 2", false ) + ESP2;
  interruptor( p, x, 136, true ); x += 40 + ESP2;
  interruptor( p, x, 136, false ); x += 40 + ESP2;
  progresso( p, x, 144, 320 - PAD - x, 8, 600 );
  const char *seg[] = { "abc", "ABC", "123" };
  segmentado( p, PAD, 168, 140, 24, seg, 3, 0 );
  TeclaOpts q; q.legenda = "q"; q.alturaDeX = true; tecla( p, 172, 164, 26, 28, q );
  TeclaOpts w; w.legenda = "w"; w.alturaDeX = true; w.estado = TeclaEstado::PRESSIONADA; tecla( p, 202, 164, 26, 28, w );
  TeclaOpts sh; sh.tipo = TeclaTipo::FUNCAO; sh.icone = IconeNome::ARROW_BIG_UP; sh.estado = TeclaEstado::ATIVA; sh.tam = 20; tecla( p, 232, 164, 36, 28, sh );
  TeclaOpts dl; dl.tipo = TeclaTipo::FUNCAO; dl.icone = IconeNome::DELETE; dl.tam = 20; tecla( p, 272, 164, 36, 28, dl );
  textoCentrado( p, "botão · campo · selo · chip · interruptor", PAD, 204, 16, tipo::apoio, T.tinta2, 'l' );
  textoCentrado( p, "progresso · segmentado · tecla", PAD, 220, 16, tipo::apoio, T.tinta2, 'l' );
}

const Entrada kTelas[] = {
  { "spec_componentes_estados", spec_componentes },
};

}  // namespace

// Each group of ports lists its examples in its own table (PlanoTelas*.cpp,
// declared in PlanoComum.h).
namespace {
const Entrada *buscar( const char *nome ) {
  for ( const Entrada &e : kTelas )
    if ( strcmp( e.nome, nome ) == 0 ) return &e;
  for ( int i = 0; i < kTelasPainelN; i++ )
    if ( strcmp( kTelasPainel[i].nome, nome ) == 0 ) return &kTelasPainel[i];
  for ( int i = 0; i < kTelasConfigN; i++ )
    if ( strcmp( kTelasConfig[i].nome, nome ) == 0 ) return &kTelasConfig[i];
  for ( int i = 0; i < kTecladosN; i++ )
    if ( strcmp( kTeclados[i].nome, nome ) == 0 ) return &kTeclados[i];
  return nullptr;
}
}  // namespace

// Every example of family A in telas.js order (render_telas.js names without
// the family and theme); host_main.cpp skips the ones not ported yet.
const char *const kNomes[] = {
  "spec_componentes_estados", "boot_wifi", "dashboard_normal", "dashboard_alarme", "dashboard_vermelha",
  "dashboard_silenciado", "dashboard_web", "dashboard_ocupado", "sensores_todos", "carregando_lendo",
  "historico_24h", "historico_7d", "detalhe_temp", "calendario_out", "configuracoes_p1", "configuracoes_p2",
  "temas_lista", "alarmes_lista", "alarme_editar_editar", "idioma_lista", "sons_p1", "sons_p2", "calibrar_p1",
  "sensibilidade_medindo", "alinhar_ajuste", "status_sistema", "status_rede", "licenca_p1", "alarme_acao_acoes",
  nullptr,
};

const char *const kNomesTeclado[] = {
  "T1_vazio", "T1_digitando", "T1_shift", "T1_caps", "T1_simbolos", "T1_ver", "T1_erro", "T1_salvando", "T1_ssid",
  "P1_vazio", "P1_digitando", "P1_erro", "P1_aguarde", "P1_bloqueado",
  "P2_vazio", "P2_digitando", "P2_erro", "P2_aguarde", "P2_bloqueado",
  "P3_vazio", "P3_digitando", "P3_erro", "P3_aguarde", "P3_bloqueado",
  nullptr,
};

bool portado( const char *nome ) { return buscar( nome ) != nullptr; }

bool desenhar( Pintor &p, const char *nome ) {
  const Entrada *e = buscar( nome );
  if ( !e ) return false;
  p.clear( p.t( ).fundo );
  e->fn( p );
  return true;
}

}  // namespace plano
