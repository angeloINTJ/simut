// Ports of the plan's family A screens: see the group in the table below.
//
// The settings screens (renderizador/src/telas.js): the menu and its pages,
// theme, alarms, language, sounds, touch calibration, sensitivity and display
// alignment, system status and the licence. Each function is its tela_<name>
// with `fam === 'A'` folded in, in the same order of drawing.
#include <stdio.h>

#include "PlanoComum.h"

using namespace ua;

namespace plano {

namespace {

/* ------------------------------------------------------------------ */
/* Configurações: the menu, four rows to a page                         */
/* ------------------------------------------------------------------ */

// telas.js MENU: the label and the icon of each row.
struct LinhaMenu { const char *texto; IconeNome icone; };
const LinhaMenu kMenu[] = {
  { "Temas", IconeNome::PALETTE }, { "Alarmes", IconeNome::BELL },
  { "Idioma", IconeNome::LANGUAGES }, { "Senha do painel", IconeNome::KEY_ROUND },
  { "Sons", IconeNome::VOLUME_2 }, { "Calibrar o toque", IconeNome::CROSSHAIR },
  { "Sensibilidade do toque", IconeNome::POINTER }, { "Alinhar o display", IconeNome::MOVE },
  { "Status do sistema", IconeNome::ACTIVITY }, { "Licença", IconeNome::FILE_TEXT },
};
const int kMenuN = sizeof kMenu / sizeof kMenu[0];

// tela_configuracoes: MENU.slice( (pag - 1) * 4, pag * 4 ), a chevron on each.
void configuracoes( Pintor &p, int16_t pag ) {
  barraTituloVoltar( p, "Configurações" );
  Item itens[4];
  int16_t n = 0;
  for ( int i = ( pag - 1 ) * 4; i < pag * 4 && i < kMenuN; i++, n++ ) {
    itens[n].texto = kMenu[i].texto;
    itens[n].icone = kMenu[i].icone;
    itens[n].chevron = true;
  }
  lista( p, Y0, itens, n, 40 );
  paginador( p, pag, 3 );
}
void configuracoes_p1( Pintor &p ) { configuracoes( p, 1 ); }
void configuracoes_p2( Pintor &p ) { configuracoes( p, 2 ); }

/* ------------------------------------------------------------------ */
/* Tema                                                                 */
/* ------------------------------------------------------------------ */

// tela_temas: the current theme is the one being drawn.
void temas_lista( Pintor &p ) {
  const Tema &T = p.t( );
  barraTituloVoltar( p, "Tema" );
  const int atual = T.escuro ? 1 : 0;
  Item itens[3];
  itens[0].texto = "Claro";
  itens[0].icone = IconeNome::SUN;
  itens[0].marcado = atual == 0;
  itens[1].texto = "Escuro";
  itens[1].icone = IconeNome::MOON;
  itens[1].marcado = atual == 1;
  itens[2].texto = "Automático (escuro das 19 às 7)";
  itens[2].icone = IconeNome::SUN_MOON;
  const int16_t fim = lista( p, Y0, itens, 3, 40 );
  textoCentrado( p, "A cor de destaque é uma só: muda com o tema.", ESP2, fim + ESP2, 16, tipo::apoio, T.tinta2, 'l' );
}

/* ------------------------------------------------------------------ */
/* Alarmes                                                              */
/* ------------------------------------------------------------------ */

// tela_alarmes: one row per sensor, the range in `rotulo`, a badge and a chevron.
void alarmes_lista( Pintor &p ) {
  barraTituloVoltar( p, "Alarmes" );
  char faixa[4][40];
  Item itens[4];
  for ( int i = 0; i < 4; i++ ) {
    const Sensor &o = SENSORES[i];
    char lo[16], hi[16];
    snprintf( faixa[i], sizeof faixa[i], "%s a %s °C", n1( o.lo, lo ), n1( o.hi, hi ) );
    itens[i].texto = o.nome;
    itens[i].valor = faixa[i];
    itens[i].valorFonte = tipo::rotulo;
    itens[i].chevron = true;
    itens[i].selo = i == 3 ? "Desligado" : "Ativo";
    itens[i].seloTom = i == 3 ? Tom::NEUTRO : Tom::POSITIVO;
  }
  lista( p, Y0, itens, 4, 40 );
  paginador( p, 1, 2 );
}

// tela_alarme_editar's stepper: a card with the label, the value in display
// type and the minus and plus buttons; `foco` puts the accent on the card line.
void stepper( Pintor &p, int16_t y, const char *rotulo, const char *valor, bool foco ) {
  const Tema &T = p.t( );
  const int16_t x = ESP2, w = 304;
  p.fillRoundRect( x, y, w, 56, RAIO_CARTAO, T.superficie );
  p.roundRect( x, y, w, 56, RAIO_CARTAO, foco ? T.acento : T.linha );
  textoCentrado( p, rotulo, x + PAD, y + 8, 16, tipo::rotulo, T.tinta2, 'l' );
  p.texto( valor, x + PAD, y + 48, tipo::titulo, T.tinta, 'l' );
  botao( p, x + w - PAD - 40 - ESP2 - 40, y + 8, 40, 40, "", Botao::SECUNDARIO );
  p.icone( IconeNome::MINUS, 20, x + w - PAD - 40 - ESP2 - 40 + 10, y + 18, T.tinta );
  botao( p, x + w - PAD - 40, y + 8, 40, 40, "", Botao::SECUNDARIO );
  p.icone( IconeNome::PLUS, 20, x + w - PAD - 40 + 10, y + 18, T.tinta );
}

// tela_alarme_editar: the first sensor's limits, the maximum being edited.
void alarme_editar_editar( Pintor &p ) {
  const Sensor &o = SENSORES[0];
  char titulo[40], lo[16], hi[16];
  snprintf( titulo, sizeof titulo, "%s · limites", o.nome );
  barraTituloVoltar( p, titulo );
  const int16_t y0 = Y0, passo = 64;
  stepper( p, y0, "Mínimo (°C)", n1( o.lo, lo ), false );
  stepper( p, y0 + passo, "Máximo (°C)", n1( o.hi, hi ), true );
  Item ativo;
  ativo.texto = "Alarme ativo";
  ativo.interruptor = 1;
  lista( p, y0 + 2 * passo, &ativo, 1, 28 );
  rodapeAcoes( p, "Salvar limites" );
}

/* ------------------------------------------------------------------ */
/* Idioma                                                               */
/* ------------------------------------------------------------------ */

// tela_idioma: four of the eight languages, the first one marked.
void idioma_lista( Pintor &p ) {
  barraTituloVoltar( p, "Idioma" );
  Item itens[4];
  itens[0].texto = "Português (Brasil)";
  itens[0].marcado = true;
  itens[1].texto = "English";
  itens[2].texto = "Español";
  itens[3].texto = "Français";
  lista( p, Y0, itens, 4, 40 );
  paginador( p, 1, 2 );
}

/* ------------------------------------------------------------------ */
/* Sons                                                                 */
/* ------------------------------------------------------------------ */

// tela_sons, page 1: four switches.
void sons_p1( Pintor &p ) {
  barraTituloVoltar( p, "Sons" );
  Item itens[4];
  itens[0].texto = "Toque na tela";
  itens[0].interruptor = 1;
  itens[1].texto = "Confirmação";
  itens[1].interruptor = 1;
  itens[2].texto = "Erro";
  itens[2].interruptor = 1;
  itens[3].texto = "Acessos pela web";
  itens[3].interruptor = 0;
  lista( p, Y0, itens, 4, 40 );
  paginador( p, 1, 2 );
}

// tela_sons' slider: a card with the label, the value in %, the track, the
// filled part and the knob. `pct` is the fraction in percent.
void deslizante( Pintor &p, int16_t y, const char *rotulo, int16_t pct ) {
  const Tema &T = p.t( );
  const int16_t x = ESP2, w = 304;
  p.fillRoundRect( x, y, w, 56, RAIO_CARTAO, T.superficie );
  p.roundRect( x, y, w, 56, RAIO_CARTAO, T.linha );
  textoCentrado( p, rotulo, x + PAD, y + 8, 16, tipo::rotulo, T.tinta2, 'l' );
  char valor[16];
  snprintf( valor, sizeof valor, "%d %%", (int)pct );
  textoCentrado( p, valor, x + w - PAD, y + 8, 16, tipo::dado, T.tinta, 'r' );
  const int16_t tx = x + PAD, tw = w - 2 * PAD;
  const int16_t cheio = rdiv( (int32_t)tw * pct, 100 );
  p.fillRoundRect( tx, y + 36, tw, 4, RAIO_TOTAL, T.superficie2 );
  p.fillRoundRect( tx, y + 36, cheio, 4, RAIO_TOTAL, T.acento );
  p.fillCircle( tx + cheio, y + 38, 8, T.acento );
}

// tela_sons, page 2: two sliders, the melody and the pager with "Ouvir".
void sons_p2( Pintor &p ) {
  barraTituloVoltar( p, "Sons" );
  const int16_t y0 = Y0, passo = 64;
  deslizante( p, y0, "Volume do alarme", 80 );
  deslizante( p, y0 + passo, "Volume do sistema", 40 );
  Item melodia;
  melodia.texto = "Melodia do alarme";
  melodia.valor = "Sirene 2";
  melodia.chevron = true;
  lista( p, y0 + 2 * passo, &melodia, 1, 28 );
  Acao ouvir;
  ouvir.rotulo = "Ouvir";
  ouvir.tipo = Botao::SECUNDARIO;
  ouvir.w = 72;
  paginador( p, 2, 2, &ouvir );
}

/* ------------------------------------------------------------------ */
/* Toque e display                                                      */
/* ------------------------------------------------------------------ */

// tela_calibrar: the sight at (20, 20) and the count. No title bar.
void calibrar_p1( Pintor &p ) {
  const Tema &T = p.t( );
  textoCentrado( p, "Toque no centro do alvo", 160, 104, 16, tipo::corpo, T.tinta, 'c' );
  textoCentrado( p, "Ponto 1 de 4", 160, 124, 16, tipo::apoio, T.tinta2, 'c' );
  p.circle( 20, 20, 10, T.acento );
  p.circle( 20, 20, 11, T.acento );
  p.hline( 4, 20, 32, T.acento );
  p.vline( 20, 4, 32, T.acento );
  p.fillCircle( 20, 20, 2, T.acento );
  botao( p, 320 - ESP3 - 88, 196, 88, 40, "Cancelar", Botao::DISCRETO );
}

// tela_sensibilidade: the instructions and the vertical stability bar.
void sensibilidade_medindo( Pintor &p ) {
  const Tema &T = p.t( );
  barraTituloVoltar( p, "Sensibilidade do toque" );
  textoCentrado( p, "Toque na tela várias vezes, como", PAD, 48, 16, tipo::corpo, T.tinta, 'l' );
  textoCentrado( p, "faria no dia a dia.", PAD, 68, 16, tipo::corpo, T.tinta, 'l' );
  textoCentrado( p, "A barra mostra onde o toque fica estável.", PAD, 92, 16, tipo::apoio, T.tinta2, 'l' );
  textoCentrado( p, "Toques: 7 · estável em 3 de 5", PAD, 116, 16, tipo::apoio, T.tinta2, 'l' );
  const int16_t bx = 320 - ESP3 - 24, by = 36, bh = 152;
  p.fillRoundRect( bx, by, 24, bh, RAIO_CONTROLE, T.superficie2 );
  p.roundRect( bx, by, 24, bh, RAIO_CONTROLE, T.linha );
  p.fillRoundRect( bx + 4, by + 56, 16, 48, RAIO_CONTROLE, T.positivoSuave );
  p.fillRect( bx, by + 80, 24, 3, T.acento );
  textoCentrado( p, "alto", bx - ESP2, by, 16, tipo::apoio, T.tinta2, 'r' );
  textoCentrado( p, "baixo", bx - ESP2, by + bh - 16, 16, tipo::apoio, T.tinta2, 'r' );
  rodapeAcoes( p, "Salvar ajuste" );
}

// tela_alinhar's arrow: a square secondary button with a 20 px icon.
void seta( Pintor &p, int16_t x, int16_t y, IconeNome ic ) {
  botao( p, x, y, 40, 40, "", Botao::SECUNDARIO );
  p.icone( ic, 20, x + 10, y + 10, p.t( ).tinta );
}

// tela_alinhar: the reference frame, the cross, four arrows and the offset.
void alinhar_ajuste( Pintor &p ) {
  const Tema &T = p.t( );
  barraTituloVoltar( p, "Alinhar o display" );
  p.rect( ESP2, 36, 304, 152, T.linhaForte );
  p.hline( 150, 112, 20, T.tinta2 );
  p.vline( 160, 102, 20, T.tinta2 );
  seta( p, 140, 44, IconeNome::CHEVRON_UP );
  seta( p, 140, 140, IconeNome::CHEVRON_DOWN );
  seta( p, 92, 92, IconeNome::CHEVRON_LEFT );
  seta( p, 188, 92, IconeNome::CHEVRON_RIGHT );
  textoCentrado( p, "A moldura deve", ESP2 + PAD, 44, 16, tipo::apoio, T.tinta2, 'l' );
  textoCentrado( p, "encostar na borda.", ESP2 + PAD, 60, 16, tipo::apoio, T.tinta2, 'l' );
  textoCentrado( p, "x +1 · y 0", ESP2 + PAD, 160, 16, tipo::dado, T.tinta, 'l' );
  rodapeAcoes( p, "Salvar", Botao::PRIMARIO, "Redefinir" );
}

/* ------------------------------------------------------------------ */
/* Status do sistema                                                    */
/* ------------------------------------------------------------------ */

// A row of tela_status: key, value and whether the value is in `positivo`.
struct Dado { const char *k, *v; bool positivo; };

// tela_status: six rows of 24 px in a card, the page name in the title.
void status( Pintor &p, int16_t pag ) {
  const Tema &T = p.t( );
  static const char *const nomes[] = { "Sistema", "Rede", "Sensores", "Telemetria" };
  static const Dado sistema[6] = {
    { "Aparelho", "simuttft", false }, { "Firmware", "2.4.7-dev", false }, { "Tempo ligado", "3 d 04:12:55", false },
    { "Memória livre", "112,4 kB", true }, { "Flash", "61,4 de 2048 kB", false }, { "Placa", "41,2 °C · GMT−3", false },
  };
  static const Dado rede[6] = {
    { "Wi-Fi", "HOSP-IOT · −58 dBm", true }, { "Endereço IP", "192.168.3.24", false },
    { "MAC", "28:cd:c1:15:4e:99", false }, { "NTP", "Sincronizado", true },
    { "Web", "porta 8081 · 1 sessão", false }, { "Telemetria", "0 pendentes", true },
  };
  char titulo[32];
  snprintf( titulo, sizeof titulo, "Status · %s", nomes[pag - 1] );
  barraTituloVoltar( p, titulo );
  const Dado *dados = pag == 1 ? sistema : rede;
  Item itens[6];
  for ( int i = 0; i < 6; i++ ) {
    itens[i].texto = dados[i].k;
    itens[i].valor = dados[i].v;
    itens[i].temValorCor = true;
    itens[i].valorCor = dados[i].positivo ? T.positivo : T.tinta;
  }
  lista( p, Y0, itens, 6, 24 );
  paginador( p, pag, 4 );
}
void status_sistema( Pintor &p ) { status( p, 1 ); }
void status_rede( Pintor &p ) { status( p, 2 ); }

/* ------------------------------------------------------------------ */
/* Licença                                                              */
/* ------------------------------------------------------------------ */

// tela_licenca: seven lines of the licence in a card, the scroll bar and a note.
void licenca_p1( Pintor &p ) {
  const Tema &T = p.t( );
  barraTituloVoltar( p, "Licença MIT" );
  static const char *const linhas[] = {
    "Copyright (c) 2026 Angelo",
    "Permission is hereby granted, free of charge,",
    "to any person obtaining a copy of this software",
    "and associated documentation files (the",
    "\"Software\"), to deal in the Software without",
    "restriction, including without limitation the",
    "rights to use, copy, modify, merge, publish,",
  };
  const int16_t x = ESP2, w = 304;
  p.fillRoundRect( x, 36, w, 152, RAIO_CARTAO, T.superficie );
  p.roundRect( x, 36, w, 152, RAIO_CARTAO, T.linha );
  for ( int i = 0; i < 7; i++ )
    textoCentrado( p, linhas[i], x + PAD, 44 + i * 16, 16, tipo::apoio, i == 0 ? T.tinta : T.tinta2, 'l' );
  const int16_t sx = x + w - PAD - 4;
  p.fillRoundRect( sx, 44, 4, 112, RAIO_TOTAL, T.superficie2 );
  p.fillRoundRect( sx, 44, 4, 36, RAIO_TOTAL, T.linhaForte );
  textoCentrado( p, "O texto legal fica no original (inglês).", x + PAD, 164, 16, tipo::apoio, T.tinta2, 'l' );
  paginador( p, 1, 3 );
}

}  // namespace

const Entrada kTelasConfig[] = {
  { "configuracoes_p1", configuracoes_p1 },
  { "configuracoes_p2", configuracoes_p2 },
  { "temas_lista", temas_lista },
  { "alarmes_lista", alarmes_lista },
  { "alarme_editar_editar", alarme_editar_editar },
  { "idioma_lista", idioma_lista },
  { "sons_p1", sons_p1 },
  { "sons_p2", sons_p2 },
  { "calibrar_p1", calibrar_p1 },
  { "sensibilidade_medindo", sensibilidade_medindo },
  { "alinhar_ajuste", alinhar_ajuste },
  { "status_sistema", status_sistema },
  { "status_rede", status_rede },
  { "licenca_p1", licenca_p1 },
};
const int kTelasConfigN = sizeof kTelasConfig / sizeof kTelasConfig[0];

}  // namespace plano
