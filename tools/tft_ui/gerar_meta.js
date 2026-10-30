'use strict';
/**
 * Generates src/ui/angulo/AnguloMeta.h for the Ângulo UI driver (transitional, bench image
 * pico_wiretft_angulo):
 *
 *   - the EXTRA table (the GFX code of every non-ASCII character the delivered
 *     fonts carry, from fontes.json), so UTF-8 text can be drawn;
 *   - cap and x heights per font, as the delivery's angulo.js metricas( )
 *     measures them (motor_gfx.js: the 'H' and 'x' glyph boxes, or fontes.json
 *     for the digit-only fonts), which textoCentrado( ) centres by;
 *   - the icons: the 42 delivered in icones.h plus the ones the firmware's real
 *     screens need, rasterized by the delivery's own engine (iconMask( ):
 *     Lucide 0.468.0 through @napi-rs/canvas 1.0.11, the versions of its
 *     package.json; the same environment gives the 42 delivered ones back byte
 *     for byte, checked 2026-10-14).
 *
 * Usage: node gerar_meta.js <entrega> <renderer env with harness/node_modules>
 *
 * It also needs motor_gfx.js, the glyph engine of the synthetic-capture study
 * (tools/tft_synth/ on the branch study/tft-angulo-ui-a), which this tree does
 * not carry: copy it to ../tft_synth/ to regenerate. The two headers it writes
 * are in the tree.
 */
const fs = require('fs');
const path = require('path');

const [entrega, rend] = process.argv.slice(2).map((p) => path.resolve(p));
process.env.TFT_ENTREGA = entrega;
const M = require(path.join(__dirname, '..', 'tft_synth', 'motor_gfx.js'));

/* The delivery's engine, for iconMask( ) only: it needs FreeFont TTFs at load,
 * which the renderer env carries (rend/freefont), and its harness. */
const Module = require('module');
const motorEntrega = path.join(rend, 'src', 'engine.js');
const E = require(motorEntrega);

const RAIZ = path.resolve(__dirname, '..', '..');
const SAIDA = path.join(RAIZ, 'src', 'ui', 'angulo', 'AnguloMeta.h');
const SAIDA_ICONES = path.join(RAIZ, 'src', 'ui', 'angulo', 'AnguloIcones.h');

/* Icons the real screens need beyond the delivered 42 (name, size). */
const EXTRAS = [
  ['activity', 20], ['file-text', 20], ['users', 20], ['user-plus', 20], ['shield', 20], ['clock', 20],
  ['router', 20], ['cable', 16], ['ethernet-port', 16], ['info', 20], ['trash-2', 20], ['lock', 20],
  ['lock-open', 20], ['wrench', 20], ['bell-ring', 20], ['timer', 20], ['x', 20], ['wifi-off', 16],
  ['chevron-left', 24], ['chevron-right', 24], ['arrow-left', 20], ['download', 24], ['circle-alert', 24],
  ['check', 16], ['bell-off', 16], ['gauge', 20], ['calendar', 20],
  // The functional build's screens (2026-10-14): status bar, pinned panel,
  // result screens, the update screen, the chart's navigation, steppers.
  ['upload', 16], ['pin', 16], ['circle-check', 24], ['circle-x', 24], ['shield-check', 24], ['refresh-cw', 24],
  ['chevrons-left', 20], ['chevrons-right', 20], ['thermometer', 16], ['droplets', 16], ['gauge', 16],
  ['user', 16], ['lock', 16], ['wrench', 16], ['bell', 16], ['power', 16], ['chevron-up', 24],
  ['chevron-down', 24], ['rotate-ccw', 20],
  // The status bar's Wi-Fi levels (2026-10-30, "B, barras de sinal"): the original panel's four bars as Lucide's signal bars; no signal is wifi-off, above.
  ['signal-low', 16], ['signal-medium', 16], ['signal-high', 16], ['signal', 16],
];

/* ---------------- EXTRA table and metrics ---------------- */
const { extra } = JSON.parse(fs.readFileSync(path.join(entrega, 'fontes', 'fontes.json'), 'utf8'));
const pares = Object.entries(extra).sort((a, b) => a[1] - b[1]);
pares.forEach(([, code], i) => { if (code !== 0x7f + i) throw new Error('EXTRA codes are not contiguous'); });
const cps = pares.map(([ch]) => ch.codePointAt(0));

// The delivery's SANS7 and SANSB7 are left out: the panel's smallest type is 9 pt since 2026-10-15
// (tipo::apoio and tipo::rotulo), nothing draws 7 pt, and the two fonts cost 4,939 B of the image.
const FONTES = ['SANS9', 'SANSB9', 'MONO9', 'MONO12', 'DISP9', 'DISP12', 'DISP18', 'DISP24', 'DISP32'];
const met = FONTES.map((n) => {
  const f = M[n];
  const H = M.glyphs('H', f), x = M.glyphs('x', f);
  return [n, H.inkB - H.inkT, x.inkB - x.inkT];
});

/* ---------------- icons ---------------- */
const icH = fs.readFileSync(path.join(entrega, 'icones', 'icones.h'), 'utf8');
const entregues = [...icH.matchAll(/\{ "([^"]+)", (\d+), (\w+)_bits \}/g)].map((m) => [m[1], Number(m[2]), m[3]]);
const nomes = new Set();
for (const [n] of entregues) nomes.add(n);
for (const [n] of EXTRAS) nomes.add(n);
const ident = (n) => n.toUpperCase().replace(/-/g, '_');

let extrasBits = '';
const registro = [];
for (const [n, t, id] of entregues) registro.push(`  { IconeNome::${ident(n)}, ${t}, ::entrega::${id}_bits },`);
for (const [n, t] of EXTRAS) {
  if (entregues.some(([en, et]) => en === n && et === t)) throw new Error(`${n}@${t} is delivered already`);
  const m = E.iconMask(n, t);
  const bpr = Math.ceil(t / 8), bytes = [], arte = [];
  for (let j = 0; j < t; j++) {
    for (let b = 0; b < bpr; b++) {
      let v = 0;
      for (let k = 0; k < 8; k++) { const i = b * 8 + k; v = (v << 1) | (i < t && m.mask[j * t + i] ? 1 : 0); }
      bytes.push(v);
    }
    let a = '';
    for (let i = 0; i < t; i++) a += m.mask[j * t + i] ? '#' : '.';
    arte.push('//   ' + a);
  }
  const nome = `EXTRA_${ident(n)}_${t}`;
  extrasBits += `// ${n} @ ${t} px (Lucide 0.468.0, the delivery's iconMask( ))\n${arte.join('\n')}\nstatic const uint8_t ${nome}_bits[] = {\n`;
  for (let i = 0; i < bytes.length; i += bpr * 2) extrasBits += '  ' + bytes.slice(i, i + bpr * 2).map((v) => '0x' + v.toString(16).padStart(2, '0')).join(', ') + ',\n';
  extrasBits += '};\n\n';
  registro.push(`  { IconeNome::${ident(n)}, ${t}, ${nome}_bits },`);
}
const listaNomes = [...nomes].sort();

const h = `// GENERATED by tools/tft_ui/gerar_meta.js from the TFT plan's delivery. Do not edit.
//
// The EXTRA table and the cap/x heights the text engine needs, and the icon
// registry: the 42 icons of icones.h plus ${EXTRAS.length} the firmware's real screens
// need, rasterized by the delivery's own engine.
#pragma once
#include <stdint.h>

namespace ua {
namespace meta {

// Unicode code point of GFX codes 0x7F.. (fontes.json "extra"), in code order.
static const uint32_t kExtra[${cps.length}] = {
${cps.map((c, i) => `  0x${c.toString(16).padStart(4, '0')},  // 0x${(0x7f + i).toString(16).toUpperCase()} ${pares[i][0]}`).join('\n')}
};

// Cap height and x height (ink box of 'H' and 'x'; fontes.json for the digit
// fonts), in the order of ua::Fonte.
static const uint8_t kCapX[${met.length}][2] = {
${met.map(([n, c, x]) => `  { ${c}, ${x} },  // ${n}`).join('\n')}
};

}  // namespace meta

enum class IconeNome : uint8_t {
${listaNomes.map((n) => `  ${ident(n)},`).join('\n')}
  NENHUM
};

}  // namespace ua

`;
fs.writeFileSync(SAIDA, h);
const hi = `// GENERATED by tools/tft_ui/gerar_meta.js. Do not edit.
//
// The icon registry: (name, size) -> drawBitmap rows. The delivered 42 are
// icones.h's (::entrega, included by Angulo.cpp just before this file); the
// ${EXTRAS.length} extra ones are rasterized here by the delivery's iconMask( ).
// Included by Angulo.cpp only.
#pragma once

namespace ua {
namespace icones {

${extrasBits}struct Reg { IconeNome nome; uint8_t tam; const uint8_t *bits; };
static const Reg kIcones[] = {
${registro.join('\n')}
};

}  // namespace icones
}  // namespace ua
`;
fs.writeFileSync(SAIDA_ICONES, hi);
console.log(`AnguloMeta.h: ${cps.length} EXTRA codes, ${met.length} fonts, ${registro.length} icons (${entregues.length} delivered + ${EXTRAS.length} extra)`);
