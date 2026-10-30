// Ports of the plan's family A screens: the panel's own (boot, dashboard,
// sensors, loading, history, detail, calendar, alarm action), line for line
// from renderizador/src/telas.js with the plan's scenario. host_ui.py holds
// each one to its reference, pixel for pixel.
#include <stdio.h>

#include "PlanoComum.h"
#include "PlanoSerie.h"

using namespace ua;

namespace plano {

namespace {

// telas.js barraStatus( ) with no p.aviso: the clock and the Wi-Fi and
// Bluetooth icons, right to left.
Status statusComHora( ) {
  Status s;
  s.hora = HORA;
  s.icones[0] = IconeNome::WIFI;
  s.icones[1] = IconeNome::BLUETOOTH;
  return s;
}

// telas.js navegacao( ), family A: the slot shortcuts (ativo = the lit one;
// alarme = the one in alarm, -1 none; fase = the red phase, which floods it)
// and the gear.
void navegacao( Pintor &p, int ativo, int alarme, bool fase ) {
  const Tema &T = p.t( );
  const char *const rotulos[] = { "AMB", "S1", "S2", "S3", "S4" };
  const int16_t y = 196, h = 40, w = 44, g = ESP2;
  int16_t x = ESP3;
  for ( int i = 0; i < 5; i++ ) {
    const bool on = i == ativo;
    const bool emAlarme = i == alarme;
    uint16_t fundo = on ? T.acento : T.superficie2, cor = on ? T.acentoTinta : T.tinta;
    bool temBorda = !on;
    if ( emAlarme && fase ) { fundo = T.perigo; temBorda = false; cor = T.perigoTinta; }
    p.fillRoundRect( x, y, w, h, RAIO_CONTROLE, fundo );
    if ( temBorda ) p.roundRect( x, y, w, h, RAIO_CONTROLE, T.linha );
    textoCentrado( p, rotulos[i], x + rd2( w ), y, h, tipo::rotulo, cor, 'c' );
    if ( emAlarme && !fase ) p.fillCircle( x + w - 8, y + 8, 3, T.perigo );
    x += w + g;
  }
  p.fillRoundRect( x, y, 36, h, RAIO_CONTROLE, T.superficie2 );
  p.roundRect( x, y, 36, h, RAIO_CONTROLE, T.linha );
  p.icone( IconeNome::SETTINGS, 20, x + 8, y + 10, T.tinta );
}

// tela_boot: brand, version, progress bar and the current step.
void boot_wifi( Pintor &p ) {
  const Tema &T = p.t( );
  textoCentrado( p, "SIMUT", 160, 56, 40, Fonte::DISP24, T.tinta, 'c' );
  textoCentrado( p, "Monitor de temperatura e umidade", 160, 96, 16, tipo::apoio, T.tinta2, 'c' );
  progresso( p, 60, 128, 200, 8, 600 );
  textoCentrado( p, "Conectando ao Wi-Fi…", 160, 144, 16, tipo::apoio, T.tinta2, 'c' );
  textoCentrado( p, "simuttft · 2.4.7-dev", 160, 196, 16, tipo::dado, T.tinta2, 'c' );
  textoCentrado( p, "28:cd:c1:15:4e:99", 160, 212, 16, tipo::dado, T.tinta2, 'c' );
}

// tela_dashboard: ambient, the selected sensor and the navigation.
enum class Fase : uint8_t { NORMAL, ALARME, VERMELHA, SILENCIADO, WEB, OCUPADO };

void tela_dashboard( Pintor &p, Fase fase ) {
  const Tema &T = p.t( );
  Sensor o = SENSORES[0];
  const bool alarme = fase == Fase::ALARME || fase == Fase::VERMELHA || fase == Fase::SILENCIADO;
  if ( alarme ) o.t = 8.7;
  const bool vermelha = fase == Fase::VERMELHA;
  Status st = statusComHora( );
  if ( fase == Fase::WEB ) { st.aviso = "admin entrou pela web"; st.avisoTom = Tom::NEUTRO; }
  else if ( fase == Fase::SILENCIADO ) { st.aviso = "Silenciado · 87 s"; st.avisoTom = Tom::ALERTA; }
  barraStatus( p, st );

  char b1[16], b2[16], linha[64];

  // Ambient. superficie( ) hands back the inner box: x + PAD, w - 2 * PAD.
  superficie( p, ESP2, 36, 304, 64 );
  const int16_t c1x = ESP2 + PAD;
  textoCentrado( p, "Ambiente", c1x, 44, 16, tipo::rotulo, T.tinta2, 'l' );
  p.icone( IconeNome::THERMOMETER, 20, c1x, 64, T.tinta2 );
  numero( p, c1x + 28, 92, n1( AMB.t, b1 ), "°C", Fonte::DISP24, T.tinta );
  p.icone( IconeNome::DROPLETS, 20, 172, 68, T.tinta2 );
  snprintf( b2, sizeof b2, "%d", (int)AMB.h );
  numero( p, 200, 92, b2, "%UR", Fonte::DISP24, T.tinta );

  // Selected sensor: c2x is the inner x, c2r the inner right edge (c2.x + c2.w).
  superficie( p, ESP2, 108, 304, 80, vermelha, T.perigo, fase == Fase::SILENCIADO, T.perigo );
  const int16_t c2x = ESP2 + PAD, c2r = ESP2 + 304 - PAD;
  const uint16_t tinta = vermelha ? T.perigoTinta : T.tinta, tinta2 = vermelha ? T.perigoTinta : T.tinta2;
  textoCentrado( p, o.nome, c2x, 116, 16, tipo::subtitulo, tinta, 'l' );
  const Situacao sit = situacao( o );
  if ( vermelha ) {
    const int16_t w = larguraTinta( sit.texto, tipo::rotulo ) + 2 * ESP2;
    p.fillRoundRect( c2r - w, 116, w, 20, RAIO_TOTAL, T.perigoTinta );
    textoCentrado( p, sit.texto, c2r - rd2( w ), 116, 20, tipo::rotulo, T.perigo, 'c' );
  } else {
    selo( p, c2r - larguraSelo( sit.texto ), 116, 20, sit.texto, sit.tom );
  }
  numero( p, c2x, 176, n1( o.t, b1 ), "°C", Fonte::DISP32, vermelha ? T.perigoTinta : corSituacao( T, o ) );
  snprintf( linha, sizeof linha, "mín %s · máx %s", n1( o.min, b1 ), n1( alarme ? 8.7 : o.max, b2 ) );
  textoCentrado( p, linha, c2r, 144, 16, tipo::apoio, tinta2, 'r' );
  snprintf( linha, sizeof linha, "faixa %s a %s °C", n1( o.lo, b1 ), n1( o.hi, b2 ) );
  textoCentrado( p, linha, c2r, 160, 16, tipo::apoio, tinta2, 'r' );
  navegacao( p, 1, alarme ? 1 : -1, vermelha );

  if ( fase == Fase::OCUPADO ) {
    // The web-busy notice: a floating card with a strong border (the TFT has no shadow).
    const int16_t y = 128, h = 60;
    p.fillRoundRect( ESP2, y, 320 - 2 * ESP2, h, RAIO_CARTAO, T.superficie );
    p.roundRect( ESP2, y, 320 - 2 * ESP2, h, RAIO_CARTAO, T.linhaForte );
    p.icone( IconeNome::USER, 20, ESP2 + PAD, y + 20, T.tinta2 );
    textoCentrado( p, "admin está gravando pela web.", ESP2 + PAD + 28, y + 12, 16, tipo::corpo, T.tinta, 'l' );
    textoCentrado( p, "Aguarde para tocar na tela.", ESP2 + PAD + 28, y + 32, 16, tipo::apoio, T.tinta2, 'l' );
  }
}

void dashboard_normal( Pintor &p ) { tela_dashboard( p, Fase::NORMAL ); }
void dashboard_alarme( Pintor &p ) { tela_dashboard( p, Fase::ALARME ); }
void dashboard_vermelha( Pintor &p ) { tela_dashboard( p, Fase::VERMELHA ); }
void dashboard_silenciado( Pintor &p ) { tela_dashboard( p, Fase::SILENCIADO ); }
void dashboard_web( Pintor &p ) { tela_dashboard( p, Fase::WEB ); }
void dashboard_ocupado( Pintor &p ) { tela_dashboard( p, Fase::OCUPADO ); }

// tela_sensores: ambient strip and the 2x2 grid of sensor cards.
void sensores_todos( Pintor &p ) {
  const Tema &T = p.t( );
  barraStatus( p, statusComHora( ) );
  char b1[16], b2[16];
  superficie( p, ESP2, 36, 304, 32 );
  const int16_t cx = ESP2 + PAD;
  p.icone( IconeNome::THERMOMETER, 20, cx, 42, T.tinta2 );
  numero( p, cx + 28, 58, n1( AMB.t, b1 ), "°C", Fonte::DISP9, T.tinta );
  p.icone( IconeNome::DROPLETS, 20, 172, 42, T.tinta2 );
  snprintf( b2, sizeof b2, "%d", (int)AMB.h );
  numero( p, 200, 58, b2, "%UR", Fonte::DISP9, T.tinta );
  for ( int i = 0; i < 4; i++ ) {
    const Sensor &o = SENSORES[i];
    const int16_t x = ESP2 + ( i % 2 ) * ( 148 + ESP2 ), y = 76 + ( i / 2 ) * ( 56 + ESP2 );
    const bool sel = i == 0;
    superficie( p, x, y, 148, 56, false, 0, sel, T.acento );
    const int16_t ccx = x + PAD;
    textoCentrado( p, o.nome, ccx, y + 8, 16, tipo::rotulo, T.tinta2, 'l' );
    numero( p, ccx, y + 48, n1( o.t, b1 ), "°C", Fonte::DISP18, corSituacao( T, o ) );
    p.fillCircle( x + 148 - PAD - 3, y + 16, 3, corSituacao( T, o ) );
  }
  navegacao( p, 1, -1, false );
}

// tela_carregando: title bar, progress bar and the step.
void carregando_lendo( Pintor &p ) {
  const Tema &T = p.t( );
  barraTituloVoltar( p, "Vacinas · 24 h" );
  progresso( p, 60, 112, 200, 8, 600 );
  textoCentrado( p, "Lendo o histórico… 60 %", 160, 128, 16, tipo::apoio, T.tinta2, 'c' );
}

// tela_historico: the chart box. The plan's series is computed by its own
// JavaScript (Math.sin may differ from the C++ one in the last bit), so the
// points and the y of the grid, the mean and the limit come from the generated
// PlanoSerie.h; the box below must be the one it was generated for.
constexpr int16_t kGx = ESP2, kGy = 68, kGw = 304, kGh = 112;
constexpr int16_t kPx0 = kGx + PAD + 28, kPx1 = kGx + kGw - PAD, kPy0 = kGy + ESP2, kPy1 = kGy + kGh - ESP4;
static_assert( kPx0 == kSeriePx0 && kPx1 == kSeriePx1 && kPy0 == kSeriePy0 && kPy1 == kSeriePy1,
               "PlanoSerie.h was generated for another chart box" );
static_assert( sizeof kSerieY / sizeof kSerieY[0] == (unsigned)( kPx1 - kPx0 ), "one y per pixel column" );

void tela_historico( Pintor &p, bool seteDias ) {
  const Tema &T = p.t( );
  const Sensor &o = SENSORES[0];
  const char *periodo = seteDias ? "7 d" : "24 h";
  char b1[16], b2[16], t[64];
  snprintf( t, sizeof t, "%s · %s", o.nome, periodo );
  const Acao acoes[2] = {
    { "Dia", Botao::SECUNDARIO, IconeNome::CALENDAR, 0 },
    { "Voltar", Botao::DISCRETO, IconeNome::ARROW_LEFT, 0 },
  };
  barraTitulo( p, t, acoes, 2 );

  // Two lines of label + datum: the extremes with their time, then mean,
  // deviation, change and now.
  struct Stat { int16_t x, y; const char *k; char v[40]; } stats[6] = {
    { PAD, 32, "máx", "" }, { 164, 32, "mín", "" },
    { PAD, 48, "média", "4,4 °C" }, { 108, 48, "σ", "0,6" }, { 164, 48, "Δ", "+0,3 °C" }, { 236, 48, "agora", "" },
  };
  snprintf( stats[0].v, sizeof stats[0].v, "%s °C às %s", n1( o.max, b1 ), o.maxAt );
  snprintf( stats[1].v, sizeof stats[1].v, "%s °C às %s", n1( o.min, b1 ), o.minAt );
  snprintf( stats[5].v, sizeof stats[5].v, "%s °C", n1( o.t, b1 ) );
  for ( const Stat &s : stats ) {
    textoCentrado( p, s.k, s.x, s.y, 16, tipo::apoio, T.tinta2, 'l' );
    textoCentrado( p, s.v, s.x + larguraTinta( s.k, tipo::apoio ) + ESP1, s.y, 16, tipo::rotulo, T.tinta, 'l' );
  }

  // Chart area: the card, the grid, the dashed mean and limit, the series.
  p.fillRoundRect( kGx, kGy, kGw, kGh, RAIO_CARTAO, T.superficie );
  p.roundRect( kGx, kGy, kGw, kGh, RAIO_CARTAO, T.linha );
  for ( int i = 0; i < 4; i++ ) {                 // 3, 4, 5 and 6 °C
    const int16_t y = kGradeY[i];
    if ( y > kPy0 && y < kPy1 ) {
      p.hline( kPx0, y, kPx1 - kPx0, T.linha );
      p.texto( n1( 3.0 + i, b2 ), kPx0 - ESP1, y + 4, tipo::apoio, T.tinta2, 'r' );
    }
  }
  tracejada( p, kPx0, kPx1, kMediaY, T.tinta2 );
  // The 8 °C limit is above the chart (y 40 against the top at 76, 2026-10-14),
  // so the plan draws no line for it; the test is the plan's, kept.
  if ( kLimiteY > kPy0 ) tracejada( p, kPx0, kPx1, kLimiteY, T.perigo );
  int16_t prevX = 0, prevY = 0;
  for ( int i = 0; i < kPx1 - kPx0; i++ ) {
    const int16_t x = kPx0 + i, y = kSerieY[i];
    if ( i > 0 ) {
      p.line( prevX, prevY, x, y, T.acento );
      p.line( prevX, prevY + 1, x, y + 1, T.acento );
    }
    prevX = x;
    prevY = y;
  }
  p.fillCircle( prevX, prevY, 3, T.acento );
  p.circle( prevX, prevY, 4, T.superficie );

  // Time axis
  textoCentrado( p, seteDias ? "-7 d" : "-24 h", kPx0, kPy1, 16, tipo::apoio, T.tinta2, 'l' );
  textoCentrado( p, seteDias ? "-3 d" : "-12 h", rd2( kPx0 + kPx1 ), kPy1, 16, tipo::apoio, T.tinta2, 'c' );
  textoCentrado( p, "agora", kPx1, kPy1, 16, tipo::apoio, T.tinta2, 'r' );

  // Period chips
  const char *const nomes[] = { "1 h", "6 h", "12 h", "24 h", "7 d" };
  const int ativo = seteDias ? 4 : 3;
  const int16_t w = 56, g = ESP1;
  for ( int i = 0; i < 5; i++ ) {
    const int16_t x = ESP2 + i * ( w + g );
    const bool on = i == ativo;
    p.fillRoundRect( x, 196, w, 40, RAIO_CONTROLE, on ? T.acento : T.superficie2 );
    if ( !on ) p.roundRect( x, 196, w, 40, RAIO_CONTROLE, T.linha );
    textoCentrado( p, nomes[i], x + rd2( w ), 196, 40, tipo::rotulo, on ? T.acentoTinta : T.tinta, 'c' );
  }
}

void historico_24h( Pintor &p ) { tela_historico( p, false ); }
void historico_7d( Pintor &p ) { tela_historico( p, true ); }

// tela_detalhe: six big values, three per row, and the pager.
void detalhe_temp( Pintor &p ) {
  const Tema &T = p.t( );
  const Sensor &o = SENSORES[0];
  barraTituloVoltar( p, "Detalhe · 24 h" );
  char b1[16], b2[16], b3[16];
  struct Valor { const char *k; char v[24]; const char *sub; } itens[6] = {
    { "Máximo", "", o.maxAt }, { "Mínimo", "", o.minAt }, { "Média", "4,4 °C", "120 leituras" },
    { "Desvio (σ)", "0,6 °C", "estável" }, { "Variação", "+0,3 °C", "início → fim" }, { "Agora", "", HORA },
  };
  snprintf( itens[0].v, sizeof itens[0].v, "%s °C", n1( o.max, b1 ) );
  snprintf( itens[1].v, sizeof itens[1].v, "%s °C", n1( o.min, b2 ) );
  snprintf( itens[5].v, sizeof itens[5].v, "%s °C", n1( o.t, b3 ) );
  for ( int i = 0; i < 6; i++ ) {
    const int16_t x = ESP2 + ( i % 3 ) * ( 96 + ESP2 ), y = 36 + ( i / 3 ) * ( 72 + ESP2 );
    superficie( p, x, y, 96, 72 );
    const int16_t cx = x + PAD;
    textoCentrado( p, itens[i].k, cx, y + 8, 16, tipo::rotulo, T.tinta2, 'l' );
    p.texto( itens[i].v, cx, y + 48, Fonte::DISP9, T.tinta, 'l' );
    textoCentrado( p, itens[i].sub, cx, y + 52, 16, tipo::apoio, T.tinta2, 'l' );
  }
  paginador( p, 1, 2 );
}

// tela_calendario: October 2026, the days with data, today (the 13th).
void calendario_out( Pintor &p ) {
  const Tema &T = p.t( );
  const Acao acoes[3] = {
    { "", Botao::SECUNDARIO, IconeNome::CHEVRON_LEFT, 36 },
    { "", Botao::SECUNDARIO, IconeNome::CHEVRON_RIGHT, 36 },
    { "Voltar", Botao::DISCRETO, IconeNome::ARROW_LEFT, 0 },
  };
  barraTitulo( p, "Outubro 2026", acoes, 3 );
  // 1 Oct 2026 is a Thursday (index 4); days 1..13 have data and the 13th is today.
  const int hoje = 13, inicio = 4, ultimoComDados = 13;
  const int16_t x0 = ESP2, w = 304, cw = w / 7, y0 = 36;
  p.fillRoundRect( x0, y0, w, 152, RAIO_CARTAO, T.superficie );
  p.roundRect( x0, y0, w, 152, RAIO_CARTAO, T.linha );
  const char *const dias[] = { "D", "S", "T", "Q", "Q", "S", "S" };
  for ( int i = 0; i < 7; i++ )
    textoCentrado( p, dias[i], x0 + i * cw + rd2( cw ), y0 + 4, 16, tipo::rotulo, T.tinta2, 'c' );
  p.hline( x0 + PAD, y0 + 24, w - 2 * PAD, T.linha );
  for ( int d = 1; d <= 31; d++ ) {
    const int idx = inicio + d - 1, col = idx % 7, lin = idx / 7;
    const int16_t cx = x0 + col * cw + rd2( cw ), cy = y0 + 28 + lin * 24;
    const bool eh = d == hoje, dados = d <= ultimoComDados;
    if ( eh ) p.fillRoundRect( cx - 14, cy, 28, 24, RAIO_CONTROLE, T.acento );
    char num[16];   // room for any int: with 8, a -fsanitize build fails -Wformat-truncation
    snprintf( num, sizeof num, "%d", d );
    textoCentrado( p, num, cx, cy, 24, dados ? tipo::botao : tipo::corpo,
                   eh ? T.acentoTinta : dados ? T.tinta : T.tinta2, 'c' );
    if ( dados && !eh ) p.fillCircle( cx, cy + 21, 2, T.acento );
  }
  botao( p, 320 - ESP2 - 64, 196, 64, 40, "Hoje", Botao::SECUNDARIO );
  textoCentrado( p, "Ponto = dia com histórico.", ESP2, 196, 40, tipo::apoio, T.tinta2, 'l' );
}

// tela_alarme_acao: the danger strip and three stacked actions.
void alarme_acao_acoes( Pintor &p ) {
  const Tema &T = p.t( );
  Sensor o = SENSORES[0];
  o.t = 8.7;
  barraStatus( p, statusComHora( ) );
  const int16_t h = 56;
  p.fillRoundRect( ESP2, 36, 304, h, RAIO_CARTAO, T.perigoSuave );
  const int16_t yx = 36, ix = PAD + ESP2;
  char b1[16], b2[16], linha[80];
  p.icone( IconeNome::TRIANGLE_ALERT, 24, ix, yx + 16, T.perigo );
  snprintf( linha, sizeof linha, "%s acima do limite", o.nome );
  textoCentrado( p, linha, ix + 32, yx + 8, 20, tipo::subtitulo, T.perigo, 'l' );
  snprintf( linha, sizeof linha, "%s °C agora · máx %s °C · desde 11:21", n1( o.t, b1 ), n1( o.hi, b2 ) );
  textoCentrado( p, linha, ix + 32, yx + 32, 16, tipo::apoio, T.perigo, 'l' );
  const int16_t x = ESP2, w = 320 - 2 * x;
  botao( p, x, 100, w, 40, "Silenciar por 120 s", Botao::PRIMARIO, false, IconeNome::BELL_OFF );
  botao( p, x, 148, w, 40, "Ver mínimo e máximo", Botao::SECUNDARIO, false, IconeNome::HISTORY );
  botao( p, x, 196, w, 32, "Desativar os alarmes (pede o PIN)", Botao::DESTRUTIVO );
}

}  // namespace

const Entrada kTelasPainel[] = {
  { "boot_wifi", boot_wifi },
  { "dashboard_normal", dashboard_normal },
  { "dashboard_alarme", dashboard_alarme },
  { "dashboard_vermelha", dashboard_vermelha },
  { "dashboard_silenciado", dashboard_silenciado },
  { "dashboard_web", dashboard_web },
  { "dashboard_ocupado", dashboard_ocupado },
  { "sensores_todos", sensores_todos },
  { "carregando_lendo", carregando_lendo },
  { "historico_24h", historico_24h },
  { "historico_7d", historico_7d },
  { "detalhe_temp", detalhe_temp },
  { "calendario_out", calendario_out },
  { "alarme_acao_acoes", alarme_acao_acoes },
};
const int kTelasPainelN = sizeof kTelasPainel / sizeof kTelasPainel[0];

}  // namespace plano
