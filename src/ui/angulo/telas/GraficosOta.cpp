// The firmware-update screen (MODE_OTA_UPDATE). See Graficos.h.
//
// No plan drew it: it is the Ângulo's own, in family A's chrome. The old screen
// (DisplayManager_Ota.cpp) had a status line, a bar with its percentage, what
// to do about the end and "do not switch off"; this keeps all of it and adds
// the icon of each phase and, for a refusal, what to do about the reason.
#include <stdio.h>
#include <string.h>

#include "Graficos.h"
#include "GraficosComum.h"

#include "../Textos.h"   // UA_TXT( ): after every project header, because its pragmas hold to the end of the unit

namespace ua {
namespace tela {

using namespace graficos;

namespace {

// The block under the title bar is the same in every phase, so the screen does
// not jump as the update goes on: the icon, the status, then what the phase has.
constexpr int16_t Y_ICONE = 44;
constexpr int16_t Y_STATUS = 76;
constexpr int16_t Y_CORPO = 108;                          // where a phase's paragraphs start
constexpr int16_t X_BARRA = 60, Y_BARRA = 120, W_BARRA = 200, H_BARRA_OTA = 8;   // the loading screen's bar, a line lower
constexpr int16_t Y_PORCENTO = 160;                       // baseline of the percentage
constexpr int16_t LARGURA_CORPO = 320 - 2 * PAD;          // 296: the safe area less PAD at each side
constexpr int32_t MIL_POR_PCT = 10;                       // the kit's bar takes thousandths
// The foot: the strip that says "do not switch off" while the update is under
// way, or two lines about the ending once it is over.
constexpr int16_t Y_PE = 196, H_AVISO = 32;
constexpr int16_t Y_MANTIDA = 192, Y_TOQUE = 212;

enum class Cor : uint8_t { ACENTO, POSITIVO, ALERTA, PERIGO, TINTA };

uint16_t cor( const Tema &T, Cor c ) {
  switch ( c ) {
    case Cor::POSITIVO: return T.positivo;
    case Cor::ALERTA: return T.alerta;
    case Cor::PERIGO: return T.perigo;
    case Cor::TINTA: return T.tinta;
    default: return T.acento;
  }
}

// What happened and what to do about it, for each reason the validator gives
// (OtaScreen.h, otaWhyFor). The first half is the old panel's sentence, each
// kept to one line of the panel; the second is the advice the web page gives
// for the same verdict (fil_fw_v7 to fil_fw_v12), in short.
// The words are looked up when the layout is made and not kept in a table built at start-up: UA_TXT( ) answers from the pack the boot loads, so an
// initialiser would hold the English for good (Textos.h; S10c B5a made this table a switch, as B3 did with the panel's messages).
struct Motivo {
  const char *oQue;
  const char *comoResolver;
};
Motivo motivoDe( MotivoOta m ) {
  switch ( m ) {
    case MotivoOta::MODELO: return { UA_TXT( "The image is for another model." ), UA_TXT( "Use the file made for this device." ) };
    case MotivoOta::SEM_ASSINATURA: return { UA_TXT( "The image is not signed." ), UA_TXT( "Use a release file or one from the configurator." ) };
    case MotivoOta::ASSINATURA: return { UA_TXT( "The signature does not match." ), UA_TXT( "Download the file again." ) };
    case MotivoOta::CHAVE_APOSENTADA: return { UA_TXT( "Signed with a retired key." ), UA_TXT( "Use a newer release." ) };
    case MotivoOta::VERSAO_BLOQUEADA: return { UA_TXT( "Below the current security level." ), UA_TXT( "Use the current version or a newer one." ) };
    case MotivoOta::CHAVE_DE_BANCADA: return { UA_TXT( "Signed with the bench key." ), UA_TXT( "Use a release file." ) };
    case MotivoOta::DANIFICADA: return { UA_TXT( "Not a valid firmware image." ), UA_TXT( "Send the file again." ) };
    default: return { "", "" };   // NENHUM, or a number the validator never gives
  }
}

// A paragraph wrapped to at most two lines of the body's width.
struct Paragrafo {
  char linha[2][96];
  int n;
};

// Wraps as evenly as the greedy break allows: the old screen found that the
// text of "restarts by itself in about 30 s" left "30 s" alone on a second
// line, and a last line of two words reads as a mistake. Finds the narrowest
// width that still takes the text in two lines.
void quebrarEquilibrado( Paragrafo &par, const char *texto, Fonte f ) {
  par.n = 0;
  if ( !texto[0] ) return;
  const int16_t total = larguraTinta( texto, f );
  int16_t largura = LARGURA_CORPO;
  if ( total > LARGURA_CORPO ) {
    // A text that fits on a line stays on one; this is only for the longer ones.
    int16_t lo = (int16_t)( total / 2 ), hi = LARGURA_CORPO;
    char prova[3][96];
    while ( lo < hi ) {
      const int16_t meio = (int16_t)( ( lo + hi ) / 2 );
      if ( quebrar( texto, f, meio, &prova[0][0], sizeof prova[0], 3 ) <= 2 ) hi = meio;
      else lo = (int16_t)( meio + 1 );
    }
    largura = hi;
  }
  par.n = quebrar( texto, f, largura, &par.linha[0][0], sizeof par.linha[0], 2 );
}

struct LayoutOta {
  IconeNome icone;
  Cor corIcone;
  const char *status;
  Cor corStatus;
  bool barra;
  int32_t milesimos;
  bool porcentagem;
  char pct[8];
  Paragrafo par[2];             // the first in body ink, the second in support ink
  int16_t yPar[2];
  bool aviso;                   // "Não desligue o aparelho."
  bool pe;                      // "Continua na versão atual." and "Toque na tela para voltar."
  bool tudo;                    // the phase draws anything under the title
};

// Two paragraphs, the second under the first.
void doisParagrafos( LayoutOta &l, const char *oQue, const char *comoResolver ) {
  quebrarEquilibrado( l.par[0], oQue, tipo::corpo );
  quebrarEquilibrado( l.par[1], comoResolver, tipo::corpo );
  l.yPar[0] = Y_CORPO;
  l.yPar[1] = (int16_t)( Y_CORPO + H_CORPO * l.par[0].n + ( l.par[0].n ? ESP2 : 0 ) );
}

LayoutOta layoutOta( const VmOta &vm ) {
  LayoutOta l{ };
  l.tudo = true;
  l.corStatus = Cor::TINTA;
  l.corIcone = Cor::ACENTO;
  const uint8_t pct = vm.pct > 100 ? 100 : vm.pct;
  switch ( vm.fase ) {
    case FaseOta::RECEBENDO:
      l.icone = IconeNome::DOWNLOAD;
      l.status = UA_TXT( "Receiving the new version" );
      l.barra = true;
      l.milesimos = pct * MIL_POR_PCT;
      l.porcentagem = true;
      snprintf( l.pct, sizeof l.pct, "%u", (unsigned)pct );
      l.aviso = true;
      break;
    case FaseOta::CONFERINDO:
      l.icone = IconeNome::SHIELD_CHECK;
      l.status = UA_TXT( "Checking the signature" );
      l.barra = true;
      l.milesimos = 100 * MIL_POR_PCT;   // all of it is in
      l.aviso = true;
      break;
    case FaseOta::CONFERIDA:
      l.icone = IconeNome::CIRCLE_CHECK;
      l.corIcone = Cor::POSITIVO;
      l.status = UA_TXT( "Image verified" );
      l.barra = true;
      l.milesimos = 100 * MIL_POR_PCT;
      l.aviso = true;
      break;
    case FaseOta::INSTALANDO:
      l.icone = IconeNome::REFRESH_CW;
      l.status = UA_TXT( "Installing the new version" );
      quebrarEquilibrado( l.par[0], UA_TXT( "The device restarts by itself in about 30 s." ), tipo::corpo );
      l.yPar[0] = Y_CORPO;
      l.aviso = true;
      break;
    case FaseOta::RECUSADA: {
      l.icone = IconeNome::CIRCLE_X;
      l.corIcone = l.corStatus = Cor::PERIGO;
      l.status = UA_TXT( "Update refused" );
      const Motivo m = motivoDe( vm.motivo );
      doisParagrafos( l, m.oQue, m.comoResolver );
      l.pe = true;
      break;
    }
    case FaseOta::INTERROMPIDA:
      l.icone = IconeNome::CIRCLE_ALERT;
      l.corIcone = l.corStatus = Cor::ALERTA;
      l.status = UA_TXT( "Update interrupted" );
      doisParagrafos( l, UA_TXT( "The upload stopped before the end." ), UA_TXT( "Send the file again." ) );
      l.pe = true;
      break;
    case FaseOta::NENHUMA:
    default:
      l.tudo = false;
      break;
  }
  return l;
}

}  // namespace

void desenharOta( Pintor &p, const VmOta &vm ) {
  const Tema &T = p.t( );
  const LayoutOta l = layoutOta( vm );
  barraTitulo( p, UA_TXT( "Firmware update" ), nullptr, 0 );
  if ( !l.tudo ) return;

  p.icone( l.icone, ICONE_G, (int16_t)( X_CENTRO - ICONE_G / 2 ), Y_ICONE, cor( T, l.corIcone ) );
  textoCentrado( p, l.status, X_CENTRO, Y_STATUS, H_CORPO, tipo::subtitulo, cor( T, l.corStatus ), 'c' );

  if ( l.barra ) progresso( p, X_BARRA, Y_BARRA, W_BARRA, H_BARRA_OTA, l.milesimos );
  if ( l.porcentagem ) numero( p, X_CENTRO, Y_PORCENTO, l.pct, "%", Fonte::DISP18, T.tinta, 'c' );

  for ( int k = 0; k < 2; k++ )
    for ( int i = 0; i < l.par[k].n; i++ )
      textoCentrado( p, l.par[k].linha[i], X_CENTRO, (int16_t)( l.yPar[k] + H_CORPO * i ), H_CORPO, tipo::corpo,
                     k == 0 ? T.tinta : T.tinta2, 'c' );

  if ( l.aviso ) {
    // The one thing to remember while it goes on, in a strip of the warning colour.
    p.fillRoundRect( ESP2, Y_PE, 320 - 2 * ESP2, H_AVISO, RAIO_CARTAO, T.alertaSuave );
    const char *const aviso = UA_TXT( "Do not switch the device off." );   // looked up once: the width below and the draw read this one pointer
    const int16_t w = (int16_t)( ICONE_P + ESP2 + larguraTinta( aviso, tipo::corpo ) );
    const int16_t x = (int16_t)( X_CENTRO - rd2( w ) );
    p.icone( IconeNome::POWER, ICONE_P, x, (int16_t)( Y_PE + rd2( H_AVISO - ICONE_P ) ), T.alerta );
    textoCentrado( p, aviso, (int16_t)( x + ICONE_P + ESP2 ), Y_PE, H_AVISO, tipo::corpo, T.alerta, 'l' );
  }
  if ( l.pe ) {
    textoCentrado( p, UA_TXT( "Still on the current version." ), X_CENTRO, Y_MANTIDA, H_CORPO, tipo::corpo, T.tinta, 'c' );
    textoCentrado( p, UA_TXT( "Tap the screen to go back." ), X_CENTRO, Y_TOQUE, H_APOIO, tipo::apoio, T.tinta2, 'c' );
  }
}

int alvosOta( const VmOta &, Alvo * ) {
  // Nothing on this screen is a button: while the update runs the panel is deaf
  // (a touch would open the priority window that makes the apply answer 503),
  // and a tap anywhere on a refusal or a cut screen goes back, which the glue
  // handles by itself (DisplayManager_Touch.cpp:1684).
  return 0;
}

}  // namespace tela
}  // namespace ua
