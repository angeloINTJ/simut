// Tokens do Angulo para o TFT 320x240 RGB565 (gerado por gerar_tokens_h.js; nao editar a mao).
// Hex do guia -> RGB565 pela macro ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3).
#pragma once
#include <Arduino.h>

struct Tema {
  uint16_t fundo;
  uint16_t superficie;
  uint16_t superficie2;
  uint16_t tinta;
  uint16_t tinta2;
  uint16_t linha;
  uint16_t linhaForte;
  uint16_t acento;
  uint16_t acentoForte;
  uint16_t acentoTinta;
  uint16_t positivo;
  uint16_t positivoSuave;
  uint16_t alerta;
  uint16_t alertaSuave;
  uint16_t perigo;
  uint16_t perigoSuave;
  uint16_t perigoTinta;
};

// Tema claro
const Tema TEMA_CLARO PROGMEM = {
  /* fundo         #f6f6f4 */ 0xF7BE,
  /* superficie    #ffffff */ 0xFFFF,
  /* superficie2   #ecebe6 */ 0xEF5C,
  /* tinta         #201e1a */ 0x20E3,
  /* tinta2        #4f4c46 */ 0x4A68,
  /* linha         #85847f */ 0x842F,
  /* linhaForte    #6f6b65 */ 0x6B4C,
  /* acento        #1f6355 */ 0x1B0A,
  /* acentoForte   #174d42 */ 0x1268,
  /* acentoTinta   #f1faf6 */ 0xF7DE,
  /* positivo      #20784e */ 0x23C9,
  /* positivoSuave #e1f0e7 */ 0xE79C,
  /* alerta        #8a6116 */ 0x8B02,
  /* alertaSuave   #f3ead2 */ 0xF75A,
  /* perigo        #b3382e */ 0xB1C5,
  /* perigoSuave   #f7e3e0 */ 0xF71C,
  /* perigoTinta   #fff5f3 */ 0xFFBE,
};
// Tema claro, estado desabilitado: cada papel misturado a 45 % sobre o fundo
const Tema TEMA_CLARO_DESAB PROGMEM = {
  /* fundo         */ 0xF7BE,
  /* superficie    */ 0xFFDF,
  /* superficie2   */ 0xF79E,
  /* tinta         */ 0x94B2,
  /* tinta2        */ 0xAD54,
  /* linha         */ 0xC637,
  /* linhaForte    */ 0xBDB6,
  /* acento        */ 0x95B5,
  /* acentoForte   */ 0x8D54,
  /* acentoTinta   */ 0xF7DE,
  /* positivo      */ 0x95F5,
  /* positivoSuave */ 0xF7BE,
  /* alerta        */ 0xC5B1,
  /* alertaSuave   */ 0xF79D,
  /* perigo        */ 0xDD13,
  /* perigoSuave   */ 0xF77E,
  /* perigoTinta   */ 0xFFBE,
};

// Tema escuro
const Tema TEMA_ESCURO PROGMEM = {
  /* fundo         #161513 */ 0x10A2,
  /* superficie    #201e1b */ 0x20E3,
  /* superficie2   #2a2723 */ 0x2924,
  /* tinta         #ebe7df */ 0xEF3B,
  /* tinta2        #b5b0a6 */ 0xB594,
  /* linha         #73706d */ 0x738D,
  /* linhaForte    #918c86 */ 0x9470,
  /* acento        #5fb39a */ 0x5D93,
  /* acentoForte   #7cc7b2 */ 0x7E36,
  /* acentoTinta   #0e211b */ 0x0903,
  /* positivo      #6fbe8e */ 0x6DF1,
  /* positivoSuave #24352b */ 0x21A5,
  /* alerta        #d9a84e */ 0xDD49,
  /* alertaSuave   #38301c */ 0x3983,
  /* perigo        #e07862 */ 0xE3CC,
  /* perigoSuave   #382220 */ 0x3904,
  /* perigoTinta   #2b100c */ 0x2881,
};
// Tema escuro, estado desabilitado: cada papel misturado a 45 % sobre o fundo
const Tema TEMA_ESCURO_DESAB PROGMEM = {
  /* fundo         */ 0x10A2,
  /* superficie    */ 0x18C2,
  /* superficie2   */ 0x18C3,
  /* tinta         */ 0x738D,
  /* tinta2        */ 0x5ACA,
  /* linha         */ 0x39E7,
  /* linhaForte    */ 0x4A48,
  /* acento        */ 0x32C9,
  /* acentoForte   */ 0x432B,
  /* acentoTinta   */ 0x08C2,
  /* positivo      */ 0x3B09,
  /* positivoSuave */ 0x1903,
  /* alerta        */ 0x6AC5,
  /* alertaSuave   */ 0x2102,
  /* perigo        */ 0x7206,
  /* perigoSuave   */ 0x20C3,
  /* perigoTinta   */ 0x1881,
};

// Escala de espaco (grade de 4 px): toda margem, folga e padding e um destes
enum Espaco : uint8_t { ESP1 = 4, ESP2 = 8, ESP3 = 12, ESP4 = 16, ESP5 = 24, ESP6 = 32, ESP7 = 48 };

// Raios: controle (botao, tecla, campo, chip), cartao e total (pilula/circulo)
enum Raio : uint8_t { RAIO_CONTROLE = 6, RAIO_CARTAO = 12 };
#define RAIO_TOTAL(h) ((h) / 2)

// Mistura generica (para derivar qualquer cor desabilitada em tempo de execucao, se preciso)
static inline uint16_t misturar565(uint16_t fg, uint16_t bg, uint8_t pct) {
  uint8_t fr = (fg >> 11) & 31, fgc = (fg >> 5) & 63, fb = fg & 31;
  uint8_t br = (bg >> 11) & 31, bgc = (bg >> 5) & 63, bb = bg & 31;
  // expande para 8 bits (bits altos replicados), mistura e volta a quantizar
  auto ex5 = [](uint8_t v) { return (uint8_t)((v << 3) | (v >> 2)); };
  auto ex6 = [](uint8_t v) { return (uint8_t)((v << 2) | (v >> 4)); };
  uint8_t r = (ex5(fr) * pct + ex5(br) * (100 - pct) + 50) / 100;
  uint8_t g = (ex6(fgc) * pct + ex6(bgc) * (100 - pct) + 50) / 100;
  uint8_t b = (ex5(fb) * pct + ex5(bb) * (100 - pct) + 50) / 100;
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
