// The alarm limits list and one sensor's alarm menu. See MenusAlarmes.h.
#include "MenusAlarmes.h"

#include <stdio.h>
#include <string.h>

#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

namespace ua {
namespace tela {

namespace {

// The kit's switch (interruptor( )): the size lista( ) draws it at.
constexpr int16_t W_INTERRUPTOR = 40, H_INTERRUPTOR = 24;

/* ------------------------------------------------------------------ */
/* The list of sensors                                                  */
/* ------------------------------------------------------------------ */

// A row holds two lines, the name over its range (desenharDuasLinhas( )), in the
// 48 px of LISTA_H_FILA_2, three to a page. One line would put the range between
// the name and the badge, and "Sala de coleta" (107 px) would be cut on a row
// that has 280 px inside its padding. The row was 40 px, four to a page, until
// the panel's text floor rose to 9 pt (2026-10-15): two lines of 14 px caps left
// 4 px between them, and the name's descenders touched the range.
// The badge sits on the name's line (its 20 px box is the name's, so the two share
// a baseline), which leaves the range the whole width under it: beside the badge,
// the widest band of the samples ("-327,6 a 327,6 °C") ran into "Manutenção".
constexpr int16_t Y_SELO = ESP1;

struct LinhaAl {
  char nome[32];       // cut to the room the badge leaves
  char faixa[32];      // "2,0 a 8,0 °C"
  const char *selo;    // one badge per row (see layoutAlarmes)
  Tom seloTom;
  int16_t xSelo;       // the badge's left edge
};

struct LayoutAlarmes {
  LayoutLista lista;
  LinhaAl linhas[LISTA_FILAS];
};

LayoutAlarmes layoutAlarmes( const VmAlarmes &vm ) {
  LayoutAlarmes l{ };
  l.lista = layoutLista( vm.n < ALARMES_MAX ? vm.n : ALARMES_MAX, vm.pagina, LISTA_FILAS_2, LISTA_H_FILA_2 );
  for ( int i = 0; i < l.lista.n; i++ ) {
    const LinhaAlarme &s = vm.sensores[l.lista.primeira + i];
    const Caixa16 &c = l.lista.linhas[i];
    LinhaAl &r = l.linhas[i];
    // An open maintenance window takes the badge's place: "Ligado" or
    // "Desligado" plus "Manutenção" is 70 + 106 px of badges and the 28 px of
    // chevron on a row that has 280 px inside its padding, and while the
    // window is open the alarms are suspended whatever the switch says.
    if ( s.manutencao ) { r.selo = UA_TXT( "Maintenance" ); r.seloTom = Tom::ALERTA; }
    else if ( s.ligado ) { r.selo = UA_TXT( "On" ); r.seloTom = Tom::POSITIVO; }
    else { r.selo = UA_TXT( "Off" ); r.seloTom = Tom::NEUTRO; }
    // Right to left, as lista( ) lays a row out: the padding, the chevron and its gap, the badge.
    r.xSelo = (int16_t)( c.x + c.w - PAD - ( ICONE_CONTROLE + ESP2 ) - larguraSelo( r.selo ) );
    cortar( s.nome, tipo::corpo, (int16_t)( r.xSelo - ESP2 - ( c.x + PAD ) ), r.nome, sizeof r.nome );
    char lo[16], hi[16];
    snprintf( r.faixa, sizeof r.faixa, "%s %s %s °C", n1( s.lo, lo ), UA_TXT( "to" ), n1( s.hi, hi ) );
  }
  return l;
}

}  // namespace

void desenharAlarmes( Pintor &p, const VmAlarmes &vm ) {
  const Tema &T = p.t( );
  const LayoutAlarmes l = layoutAlarmes( vm );
  // The title echoes the menu item that opened this list.
  barraTituloVoltar( p, UA_TXT( "Alarm limits" ) );
  if ( l.lista.total == 0 ) {
    // Two texts the dashboard's empty card has as well, joined by a literal format (a looked-up text is never a format); bounded by the buffer.
    char vazio[160];
    snprintf( vazio, sizeof vazio, "%s. %s", UA_TXT( "No active sensor" ), UA_TXT( "Activate a sensor on the web." ) );
    desenharVazio( p, vazio );
    return;
  }
  // lista( ) gives the card, the dividers and the chevrons; what a row says is
  // laid over it (two lines and a badge, which lista( ) cannot do).
  Item itens[LISTA_FILAS_2];
  for ( int i = 0; i < l.lista.n; i++ ) itens[i].chevron = true;
  lista( p, Y0, itens, (int16_t)l.lista.n, LISTA_H_FILA_2 );
  for ( int i = 0; i < l.lista.n; i++ ) {
    const Caixa16 &c = l.lista.linhas[i];
    const LinhaAl &r = l.linhas[i];
    selo( p, r.xSelo, c.y + Y_SELO, H_SELO, r.selo, r.seloTom );
    desenharDuasLinhas( p, c, c.x + PAD, r.nome, T.tinta, r.faixa, tipo::apoio, T.tinta2 );
  }
  desenharPaginador( p, l.lista.pag );
}

int alvosAlarmes( const VmAlarmes &vm, Alvo *out ) { return alvosLista( layoutAlarmes( vm ).lista, out, true ); }

/* ------------------------------------------------------------------ */
/* One sensor's menu                                                    */
/* ------------------------------------------------------------------ */

namespace {

constexpr int SA_LINHAS = 3;           // Limites, Alarmes, Manutenção

struct LayoutSensorAlarme {
  char titulo[40];
  Caixa16 voltar;
  Caixa16 linhas[SA_LINHAS];
  bool ativa[SA_LINHAS];               // the account may use the row
  bool manutencaoAberta;
  char manutencao[40];                 // the maintenance row's value
};

// "1 h 30 min left", "2 h left", "1 h left", "45 min left", "1 min left" or "Inactive"
// (pt-BR: "1 h 30 min restantes", "2 h restantes", "1 h restante", "45 min restantes",
// "1 min restante", "Desligada"). Rounded up to the minute: 30 s left is one minute, never
// zero. From 100 h on the minutes go: they are noise. The singular is a text of its own,
// because a language's word changes with the number (pt-BR "restante" and "restantes");
// the plural is the word "left" after the number and its unit, in a literal format.
void textoManutencao( uint32_t seg, char *out, int cap ) {
  if ( seg == 0 ) { copiar( out, UA_TXT( "Inactive" ), cap ); return; }
  const unsigned min = (unsigned)( ( seg + 59 ) / 60 );
  const unsigned h = min / 60, m = min % 60;
  if ( h == 1 && !m ) copiar( out, UA_TXT( "1 h left" ), cap );   // the rig read "1 h restantes" (2026-10-14)
  else if ( h >= 100 || ( h && !m ) ) snprintf( out, (size_t)cap, "%u h %s", h, UA_TXT( "left" ) );
  else if ( h ) snprintf( out, (size_t)cap, "%u h %u min %s", h, m, UA_TXT( "left" ) );
  else if ( m == 1 ) copiar( out, UA_TXT( "1 min left" ), cap );
  else snprintf( out, (size_t)cap, "%u min %s", m, UA_TXT( "left" ) );
}

LayoutSensorAlarme layoutSensorAlarme( const VmSensorAlarme &vm ) {
  LayoutSensorAlarme l{ };
  tituloCortado( vm.nome[0] ? vm.nome : UA_TXT( "Alarms" ), l.titulo, sizeof l.titulo );
  l.voltar = alvoVoltar( );
  for ( int i = 0; i < SA_LINHAS; i++ )
    l.linhas[i] = Caixa16{ ESP2, (int16_t)( Y0 + i * LISTA_H_FILA_2 ), (int16_t)( W_TELA - 2 * ESP2 ), LISTA_H_FILA_2 };
  l.ativa[0] = vm.podeLimites;
  l.ativa[1] = vm.podeBloquear;
  l.ativa[2] = vm.podeManutencao;
  l.manutencaoAberta = vm.manutencaoSeg != 0;
  textoManutencao( vm.manutencaoSeg, l.manutencao, sizeof l.manutencao );
  return l;
}

}  // namespace

void desenharSensorAlarme( Pintor &p, const VmSensorAlarme &vm ) {
  const Tema &T = p.t( );
  const LayoutSensorAlarme l = layoutSensorAlarme( vm );
  barraTituloVoltar( p, l.titulo );

  Item itens[SA_LINHAS];
  itens[0].texto = UA_TXT( "Limits" );
  itens[0].icone = IconeNome::GAUGE;
  itens[0].chevron = l.ativa[0];
  itens[1].texto = UA_TXT( "Alarms" );
  itens[1].icone = IconeNome::BELL;
  // A locked row shows its switch disabled, below; lista( ) only draws live ones.
  itens[1].interruptor = l.ativa[1] ? ( vm.alarmesLigados ? 1 : 0 ) : -1;
  // The maintenance row is the one of two lines, the label over the window's state; lista( )
  // draws its icon and chevron, and the lines are laid over it below. Beside the label the
  // state ("1 h 30 min restantes", 150 px) did not fit the 224 px between icon and chevron
  // at 9 pt (the label is 93).
  itens[2].icone = IconeNome::WRENCH;
  itens[2].chevron = l.ativa[2];
  // A row the account has no bit for: a lock instead of its icon, everything
  // in the disabled colour, nothing to open.
  for ( int i = 0; i < SA_LINHAS; i++ ) {
    if ( l.ativa[i] ) continue;
    itens[i].icone = IconeNome::LOCK;
    itens[i].temTom = true;
    itens[i].tom = desab( T, T.tinta );
  }
  const int16_t fim = lista( p, Y0, itens, SA_LINHAS, LISTA_H_FILA_2 );
  {
    uint16_t cTexto = T.tinta, cEstado = l.manutencaoAberta ? T.alerta : T.tinta2;
    if ( !l.ativa[2] ) { cTexto = desab( T, cTexto ); cEstado = desab( T, cEstado ); }
    // "Maintenance mode" and not "Maintenance", the alarm list's badge above: es-ES shortens that one to "Mantenim." for the badge's room,
    // and this row has the width for "Mantenimiento" (the review of S10c B6).
    desenharDuasLinhas( p, l.linhas[2], ESP2 + PAD + ICONE_CONTROLE + ESP2, UA_TXT( "Maintenance mode" ), cTexto, l.manutencao,
                        tipo::apoio, cEstado );
  }
  if ( !l.ativa[1] ) {
    const Caixa16 &c = l.linhas[1];
    interruptor( p, c.x + c.w - PAD - W_INTERRUPTOR, c.y + rd2( c.h - H_INTERRUPTOR ), vm.alarmesLigados, true );
  }
  // The switch saves on the spot, unlike the screens that end in "Salvar".
  desenharNota( p, fim + ESP2, UA_TXT( "Turning alarms on or off applies at once." ) );
}

int alvosSensorAlarme( const VmSensorAlarme &vm, Alvo *out ) {
  const LayoutSensorAlarme l = layoutSensorAlarme( vm );
  int n = addAlvoVoltar( out, 0, l.voltar, SA_VOLTAR );
  n = addAlvoSe( out, n, l.ativa[0], l.linhas[0], SA_LIMITES );
  n = addAlvoSe( out, n, l.ativa[1], l.linhas[1], SA_ALARMES );
  return addAlvoSe( out, n, l.ativa[2], l.linhas[2], SA_MANUTENCAO );
}

}  // namespace tela
}  // namespace ua
