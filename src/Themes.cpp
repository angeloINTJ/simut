/**
 * @file Themes.cpp
 * @brief Theme palette definitions — core theme always on, categorias opcionais via #define.
 * @details Default build inclui apenas o tema simut_def. Para habilitar
 * categorias extras, descomente o `#define SIMUT_THEMES_<NAME>`
 * correspondente no bloco de configuracao abaixo e recompile.
 * Temas desabilitados nao consomem flash mas permanecem no codigo
 * para manutencao (novas cores/ajustes devem ser replicados em todos).
 *
 * @project SIMUT - Sistema Integrado de Monitoramento e Telemetria
 *          SIMUT - Integrated Monitoring and Telemetry System
 * @target Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @author Ângelo Moisés Alves
 * @license MIT License
 */

#include "Themes.h"

/* =========================================================================
 * THEME CATEGORY SELECTION — edit here to enable optional theme packs.
 * =========================================================================
 * The core palette (simut_def) is always compiled.
 * Descomente as categorias desejadas para adiciona-las ao firmware.
 * Cada tema custa ~70 bytes de flash.
 */
// #define SIMUT_THEMES_HEALTH // 12 temas — campanhas mensais (jan_branco..dez_laranja)
// #define SIMUT_THEMES_PRO // 5 temas — dark_pro, monochrome, clinical, corporate, minimal
// #define SIMUT_THEMES_MEDICAL // 6 temas — unimed, unimed_dark, xray, uti_monitor, scrubs, biohazard
// #define SIMUT_THEMES_SAFETY // 3 temas — danger, safety, fire
// #define SIMUT_THEMES_RETRO // 8 temas — matrix, cyberpunk, pipboy, nes, cmd, synth, gameboy, sith
// #define SIMUT_THEMES_NATURE // 8 temas — amber, vampire, magma, ocean, nature, outrun, midnight, forest
// #define SIMUT_THEMES_UTILITY // 7 temas — paper, blocks, blueprint, solarized, luxury, ubuntu, whiteboard
/* ========================================================================= */


ThemePalette currentTheme;

/**
 * Theme palette array. simut_def esta sempre no indice 0. Temas opcionais
 * sao inseridos apos, na ordem das categorias. Os indices podem deslocar
 * se o conjunto de categorias mudar — recomendado re-selecionar o tema
 * via 'conf system theme <id>' apos alterar os defines acima.
 */
const ThemePalette availableThemes[] = {

 /* ------ CORE (always included) ------ */
 { "simut_def", "Simut Default",
 RGB565(18, 18, 20), RGB565(35, 38, 45), RGB565(245, 245, 245), RGB565(180, 180, 185), RGB565(90, 90, 100), RGB565(0, 150, 255), RGB565(50, 200, 255), RGB565(50, 50, 60),
 RGB565(255, 60, 60), RGB565(255, 170, 0), RGB565(0, 255, 2), RGB565(0, 200, 255), RGB565(0, 121, 255) , RGB565(180, 180, 185), RGB565(245, 245, 245), RGB565(245, 245, 245) , RGB565(0, 0, 0) },

#ifdef SIMUT_THEMES_HEALTH
 /* ------ HEALTH: 12 awareness-month campaigns ------ */
 { "jan_branco", "Janeiro Branco",
 RGB565(240, 245, 249), RGB565(255, 255, 255), RGB565(30, 30, 30), RGB565(120, 120, 120), RGB565(180, 180, 180), RGB565(150, 150, 150), RGB565(200, 200, 200), RGB565(220, 220, 220),
 RGB565(210, 50, 50), RGB565(230, 130, 0), RGB565(50, 150, 60), RGB565(30, 140, 200), RGB565(30, 140, 200) , RGB565(120, 120, 120), RGB565(30, 30, 30), RGB565(30, 30, 30) , RGB565(30, 30, 30) },

 { "fev_roxo", "Fevereiro Roxo",
 RGB565(25, 15, 35), RGB565(45, 30, 60), RGB565(240, 240, 240), RGB565(190, 150, 220), RGB565(100, 70, 130), RGB565(140, 80, 200), RGB565(180, 100, 255), RGB565(60, 40, 80),
 RGB565(255, 80, 80), RGB565(255, 180, 50), RGB565(80, 210, 100), RGB565(80, 200, 255), RGB565(160, 100, 240) , RGB565(190, 150, 220), RGB565(240, 240, 240), RGB565(240, 240, 240) , RGB565(240, 240, 240) },

 { "mar_lilas", "Marco Lilas",
 RGB565(250, 240, 255), RGB565(255, 255, 255), RGB565(60, 40, 80), RGB565(140, 110, 160), RGB565(200, 180, 210), RGB565(160, 100, 200), RGB565(200, 140, 240), RGB565(230, 210, 240),
 RGB565(220, 60, 60), RGB565(240, 150, 0), RGB565(60, 160, 80), RGB565(40, 150, 220), RGB565(140, 80, 180) , RGB565(140, 110, 160), RGB565(60, 40, 80), RGB565(60, 40, 80) , RGB565(60, 40, 80) },

 { "abr_verde", "Abril Verde",
 RGB565(15, 25, 15), RGB565(30, 45, 30), RGB565(240, 250, 240), RGB565(120, 180, 120), RGB565(60, 90, 60), RGB565(40, 180, 80), RGB565(80, 220, 120), RGB565(45, 65, 45),
 RGB565(255, 80, 80), RGB565(255, 190, 50), RGB565(80, 220, 100), RGB565(80, 200, 255), RGB565(60, 200, 100) , RGB565(120, 180, 120), RGB565(240, 250, 240), RGB565(240, 250, 240) , RGB565(240, 250, 240) },

 { "mai_amarelo", "Maio Amarelo",
 RGB565(25, 25, 15), RGB565(45, 45, 25), RGB565(250, 250, 240), RGB565(200, 190, 120), RGB565(100, 95, 60), RGB565(240, 200, 0), RGB565(255, 220, 50), RGB565(65, 65, 40),
 RGB565(255, 70, 70), RGB565(255, 160, 0), RGB565(90, 210, 90), RGB565(90, 210, 255), RGB565(250, 210, 50) , RGB565(200, 190, 120), RGB565(250, 250, 240), RGB565(250, 250, 240) , RGB565(250, 250, 240) },

 { "jun_vermelho", "Junho Vermelho",
 RGB565(25, 10, 10), RGB565(45, 20, 20), RGB565(250, 240, 240), RGB565(200, 120, 120), RGB565(100, 50, 50), RGB565(220, 40, 40), RGB565(255, 80, 80), RGB565(70, 30, 30),
 RGB565(255, 100, 100), RGB565(255, 180, 50), RGB565(80, 220, 100), RGB565(100, 200, 255), RGB565(255, 120, 120) , RGB565(200, 120, 120), RGB565(250, 240, 240), RGB565(250, 240, 240) , RGB565(250, 240, 240) },

 { "jul_amarelo", "Julho Amarelo",
 RGB565(250, 245, 230), RGB565(255, 255, 255), RGB565(80, 70, 40), RGB565(160, 140, 90), RGB565(210, 200, 170), RGB565(220, 160, 0), RGB565(255, 190, 0), RGB565(230, 220, 190),
 RGB565(220, 60, 60), RGB565(240, 140, 0), RGB565(60, 160, 80), RGB565(40, 140, 210), RGB565(200, 150, 20) , RGB565(160, 140, 90), RGB565(80, 70, 40), RGB565(80, 70, 40) , RGB565(80, 70, 40) },

 { "ago_dourado", "Agosto Dourado",
 RGB565(20, 18, 12), RGB565(38, 34, 25), RGB565(250, 245, 230), RGB565(190, 170, 110), RGB565(90, 80, 50), RGB565(210, 170, 50), RGB565(255, 210, 80), RGB565(60, 55, 40),
 RGB565(255, 80, 80), RGB565(255, 190, 60), RGB565(80, 220, 100), RGB565(100, 210, 255), RGB565(240, 190, 70) , RGB565(190, 170, 110), RGB565(250, 245, 230), RGB565(250, 245, 230) , RGB565(250, 245, 230) },

 { "set_amarelo", "Setembro Amarelo",
 RGB565(24, 24, 16), RGB565(42, 42, 28), RGB565(245, 245, 235), RGB565(185, 185, 130), RGB565(95, 95, 65), RGB565(230, 210, 40), RGB565(255, 235, 80), RGB565(65, 65, 45),
 RGB565(255, 85, 85), RGB565(255, 180, 55), RGB565(85, 215, 105), RGB565(95, 205, 250), RGB565(245, 225, 60) , RGB565(185, 185, 130), RGB565(245, 245, 235), RGB565(245, 245, 235) , RGB565(245, 245, 235) },

 { "out_rosa", "Outubro Rosa",
 RGB565(25, 15, 20), RGB565(45, 28, 38), RGB565(250, 240, 245), RGB565(200, 140, 170), RGB565(100, 60, 80), RGB565(230, 80, 150), RGB565(255, 130, 190), RGB565(70, 40, 55),
 RGB565(255, 90, 90), RGB565(255, 170, 60), RGB565(90, 220, 110), RGB565(110, 210, 255), RGB565(240, 110, 170) , RGB565(200, 140, 170), RGB565(250, 240, 245), RGB565(250, 240, 245) , RGB565(250, 240, 245) },

 { "nov_azul", "Novembro Azul",
 RGB565(12, 18, 28), RGB565(25, 35, 50), RGB565(240, 245, 250), RGB565(120, 150, 190), RGB565(50, 70, 100), RGB565(40, 120, 220), RGB565(80, 160, 255), RGB565(45, 60, 85),
 RGB565(255, 80, 80), RGB565(255, 180, 60), RGB565(80, 220, 110), RGB565(100, 200, 255), RGB565(60, 150, 240) , RGB565(120, 150, 190), RGB565(240, 245, 250), RGB565(240, 245, 250) , RGB565(240, 245, 250) },

 { "dez_laranja", "Dezembro Laranja",
 RGB565(26, 16, 10), RGB565(46, 30, 20), RGB565(250, 245, 240), RGB565(210, 150, 100), RGB565(110, 70, 40), RGB565(240, 110, 30), RGB565(255, 150, 60), RGB565(75, 50, 35),
 RGB565(255, 80, 80), RGB565(255, 180, 50), RGB565(80, 220, 100), RGB565(100, 210, 255), RGB565(250, 130, 50) , RGB565(210, 150, 100), RGB565(250, 245, 240), RGB565(250, 245, 240) , RGB565(250, 245, 240) },
#endif /* SIMUT_THEMES_HEALTH */

#ifdef SIMUT_THEMES_PRO
 /* ------ PRO: professional/corporate working-set ------ */
 { "dark_pro", "Dark Professional",
 RGB565(15, 15, 15), RGB565(30, 30, 30), RGB565(240, 240, 240), RGB565(150, 150, 150), RGB565(80, 80, 80), RGB565(0, 150, 136), RGB565(0, 200, 180), RGB565(50, 50, 50),
 RGB565(255, 80, 80), RGB565(255, 170, 0), RGB565(76, 175, 80), RGB565(3, 169, 244), RGB565(0, 188, 212) , RGB565(150, 150, 150), RGB565(240, 240, 240), RGB565(240, 240, 240) , RGB565(240, 240, 240) },

 { "monochrome", "High Contrast BW",
 RGB565(10, 10, 10), RGB565(35, 35, 35), RGB565(250, 250, 250), RGB565(180, 180, 180), RGB565(90, 90, 90), RGB565(200, 200, 200), RGB565(255, 255, 255), RGB565(60, 60, 60),
 RGB565(220, 220, 220), RGB565(190, 190, 190), RGB565(250, 250, 250), RGB565(170, 170, 170), RGB565(200, 200, 200) , RGB565(180, 180, 180), RGB565(250, 250, 250), RGB565(250, 250, 250) , RGB565(250, 250, 250) },

 { "clinical", "Light Clinical",
 RGB565(245, 248, 250), RGB565(255, 255, 255), RGB565(30, 40, 50), RGB565(120, 130, 140), RGB565(180, 190, 200), RGB565(30, 136, 229), RGB565(60, 160, 240), RGB565(220, 225, 230),
 RGB565(220, 50, 50), RGB565(240, 140, 30), RGB565(60, 160, 80), RGB565(50, 160, 220), RGB565(40, 150, 210) , RGB565(120, 130, 140), RGB565(30, 40, 50), RGB565(30, 40, 50) , RGB565(30, 40, 50) },

 { "corporate", "Office Corporate",
 RGB565(230, 235, 240), RGB565(250, 252, 255), RGB565(40, 45, 55), RGB565(130, 140, 150), RGB565(190, 195, 205), RGB565(50, 100, 180), RGB565(80, 130, 210), RGB565(210, 215, 225),
 RGB565(220, 70, 70), RGB565(240, 150, 50), RGB565(60, 170, 90), RGB565(60, 140, 220), RGB565(50, 110, 190) , RGB565(130, 140, 150), RGB565(40, 45, 55), RGB565(40, 45, 55) , RGB565(40, 45, 55) },

 { "minimal", "Apple Minimal",
 RGB565(245, 245, 247), RGB565(255, 255, 255), RGB565(29, 29, 31), RGB565(134, 134, 139), RGB565(210, 210, 215), RGB565(0, 122, 255), RGB565(50, 150, 255), RGB565(229, 229, 234),
 RGB565(255, 59, 48), RGB565(255, 149, 0), RGB565(52, 199, 89), RGB565(90, 200, 250), RGB565(0, 122, 255) , RGB565(134, 134, 139), RGB565(29, 29, 31), RGB565(29, 29, 31) , RGB565(29, 29, 31) },
#endif /* SIMUT_THEMES_PRO */

#ifdef SIMUT_THEMES_MEDICAL
 /* ------ MEDICAL: healthcare/hospital ------ */
 { "unimed", "Unimed Standard",
 RGB565(245, 250, 245), RGB565(255, 255, 255), RGB565(30, 40, 30), RGB565(120, 140, 120), RGB565(190, 200, 190), RGB565(0, 153, 93), RGB565(30, 180, 120), RGB565(220, 230, 220),
 RGB565(220, 50, 50), RGB565(240, 140, 30), RGB565(0, 153, 93), RGB565(40, 150, 220), RGB565(0, 153, 93) , RGB565(120, 140, 120), RGB565(30, 40, 30), RGB565(30, 40, 30) , RGB565(30, 40, 30) },

 { "unimed_dark", "Unimed Night",
 RGB565(20, 25, 20), RGB565(35, 45, 35), RGB565(245, 250, 245), RGB565(140, 160, 140), RGB565(80, 100, 80), RGB565(0, 180, 110), RGB565(40, 210, 140), RGB565(50, 65, 50),
 RGB565(255, 80, 80), RGB565(255, 170, 50), RGB565(0, 180, 110), RGB565(80, 200, 255), RGB565(0, 180, 110) , RGB565(140, 160, 140), RGB565(245, 250, 245), RGB565(245, 250, 245) , RGB565(245, 250, 245) },

 { "xray", "X-Ray Digital",
 RGB565(10, 12, 15), RGB565(25, 30, 35), RGB565(220, 230, 240), RGB565(130, 150, 170), RGB565(60, 80, 100), RGB565(100, 180, 255), RGB565(150, 220, 255), RGB565(40, 50, 60),
 RGB565(255, 100, 100), RGB565(255, 180, 80), RGB565(120, 220, 150), RGB565(100, 210, 255), RGB565(100, 190, 255) , RGB565(130, 150, 170), RGB565(220, 230, 240), RGB565(220, 230, 240) , RGB565(220, 230, 240) },

 { "uti_monitor", "ICU Vital Signs",
 RGB565(5, 5, 5), RGB565(20, 20, 20), RGB565(255, 255, 255), RGB565(150, 150, 150), RGB565(70, 70, 70), RGB565(0, 255, 100), RGB565(100, 255, 150), RGB565(40, 40, 40),
 RGB565(255, 50, 50), RGB565(255, 200, 0), RGB565(0, 255, 100), RGB565(0, 200, 255), RGB565(0, 255, 200) , RGB565(150, 150, 150), RGB565(255, 255, 255), RGB565(255, 255, 255) , RGB565(255, 255, 255) },

 { "scrubs", "Surgical Blue",
 RGB565(20, 40, 45), RGB565(35, 60, 65), RGB565(240, 250, 250), RGB565(140, 170, 175), RGB565(70, 100, 105), RGB565(40, 160, 170), RGB565(80, 200, 210), RGB565(50, 80, 85),
 RGB565(255, 100, 100), RGB565(255, 180, 60), RGB565(90, 220, 120), RGB565(100, 210, 255), RGB565(60, 180, 190) , RGB565(140, 170, 175), RGB565(240, 250, 250), RGB565(240, 250, 250) , RGB565(240, 250, 250) },

 { "biohazard", "Biohazard Lab",
 RGB565(20, 15, 25), RGB565(40, 30, 50), RGB565(200, 255, 50), RGB565(140, 180, 60), RGB565(70, 90, 40), RGB565(160, 50, 200), RGB565(200, 100, 240), RGB565(60, 45, 75),
 RGB565(255, 70, 70), RGB565(255, 170, 40), RGB565(100, 230, 80), RGB565(100, 210, 255), RGB565(180, 80, 220) , RGB565(140, 180, 60), RGB565(200, 255, 50), RGB565(200, 255, 50) , RGB565(200, 255, 50) },
#endif /* SIMUT_THEMES_MEDICAL */

#ifdef SIMUT_THEMES_SAFETY
 /* ------ SAFETY: industrial/alert ------ */
 { "danger", "High Voltage",
 RGB565(15, 15, 10), RGB565(35, 35, 20), RGB565(255, 220, 0), RGB565(180, 160, 40), RGB565(100, 90, 20), RGB565(255, 50, 0), RGB565(255, 100, 50), RGB565(50, 50, 30),
 RGB565(255, 60, 60), RGB565(255, 180, 0), RGB565(100, 220, 50), RGB565(100, 200, 255), RGB565(255, 150, 0) , RGB565(180, 160, 40), RGB565(255, 220, 0), RGB565(255, 220, 0) , RGB565(255, 220, 0) },

 { "safety", "Safety Orange",
 RGB565(25, 20, 15), RGB565(45, 38, 30), RGB565(250, 245, 240), RGB565(170, 150, 140), RGB565(90, 75, 65), RGB565(255, 110, 0), RGB565(255, 150, 60), RGB565(65, 55, 45),
 RGB565(255, 80, 80), RGB565(255, 180, 40), RGB565(90, 220, 100), RGB565(110, 200, 255), RGB565(255, 130, 30) , RGB565(170, 150, 140), RGB565(250, 245, 240), RGB565(250, 245, 240) , RGB565(250, 245, 240) },

 { "fire", "Fire Brigade",
 RGB565(30, 10, 10), RGB565(55, 20, 20), RGB565(255, 250, 250), RGB565(200, 150, 150), RGB565(120, 70, 70), RGB565(255, 200, 0), RGB565(255, 230, 100), RGB565(80, 35, 35),
 RGB565(255, 60, 60), RGB565(255, 160, 30), RGB565(100, 220, 100), RGB565(120, 200, 255), RGB565(255, 210, 50) , RGB565(200, 150, 150), RGB565(255, 250, 250), RGB565(255, 250, 250) , RGB565(255, 250, 250) },
#endif /* SIMUT_THEMES_SAFETY */

#ifdef SIMUT_THEMES_RETRO
 /* ------ RETRO: gaming/nostalgia ------ */
 { "matrix", "Matrix Terminal",
 RGB565(10, 15, 10), RGB565(20, 35, 20), RGB565(100, 255, 100), RGB565(50, 180, 50), RGB565(20, 80, 20), RGB565(0, 255, 0), RGB565(150, 255, 150), RGB565(30, 50, 30),
 RGB565(255, 80, 80), RGB565(255, 200, 50), RGB565(0, 255, 0), RGB565(100, 200, 255), RGB565(50, 220, 100) , RGB565(50, 180, 50), RGB565(100, 255, 100), RGB565(100, 255, 100) , RGB565(100, 255, 100) },

 { "cyberpunk", "Cyberpunk Neon",
 RGB565(15, 15, 25), RGB565(30, 30, 45), RGB565(255, 255, 0), RGB565(200, 200, 50), RGB565(100, 100, 50), RGB565(255, 0, 255), RGB565(0, 255, 255), RGB565(50, 50, 70),
 RGB565(255, 50, 100), RGB565(255, 150, 0), RGB565(50, 255, 100), RGB565(0, 200, 255), RGB565(255, 0, 255) , RGB565(200, 200, 50), RGB565(255, 255, 0), RGB565(255, 255, 0) , RGB565(255, 255, 0) },

 { "pipboy", "Wasteland 3000",
 RGB565(10, 15, 10), RGB565(20, 35, 20), RGB565(50, 255, 100), RGB565(30, 180, 60), RGB565(15, 90, 30), RGB565(80, 255, 150), RGB565(120, 255, 180), RGB565(30, 55, 30),
 RGB565(255, 100, 100), RGB565(255, 200, 80), RGB565(50, 255, 100), RGB565(100, 220, 255), RGB565(60, 220, 120) , RGB565(30, 180, 60), RGB565(50, 255, 100), RGB565(50, 255, 100) , RGB565(50, 255, 100) },

 { "nes", "8-Bit Console",
 RGB565(120, 120, 120), RGB565(180, 180, 180), RGB565(20, 20, 20), RGB565(80, 80, 80), RGB565(200, 200, 200), RGB565(220, 40, 40), RGB565(255, 80, 80), RGB565(150, 150, 150),
 RGB565(200, 40, 40), RGB565(220, 130, 30), RGB565(40, 150, 60), RGB565(40, 140, 200), RGB565(220, 50, 50) , RGB565(80, 80, 80), RGB565(20, 20, 20), RGB565(20, 20, 20) , RGB565(20, 20, 20) },

 { "cmd", "C:\\> Prompt",
 RGB565(5, 5, 5), RGB565(15, 15, 15), RGB565(200, 200, 200), RGB565(120, 120, 120), RGB565(60, 60, 60), RGB565(255, 255, 255), RGB565(255, 255, 255), RGB565(30, 30, 30),
 RGB565(255, 80, 80), RGB565(255, 180, 50), RGB565(100, 220, 100), RGB565(100, 200, 255), RGB565(200, 200, 200) , RGB565(120, 120, 120), RGB565(200, 200, 200), RGB565(200, 200, 200) , RGB565(200, 200, 200) },

 { "synth", "Synthwave 84",
 RGB565(35, 15, 45), RGB565(55, 25, 70), RGB565(0, 255, 255), RGB565(255, 0, 255), RGB565(120, 50, 150), RGB565(255, 0, 128), RGB565(255, 100, 200), RGB565(75, 35, 95),
 RGB565(255, 50, 100), RGB565(255, 150, 50), RGB565(50, 255, 150), RGB565(0, 220, 255), RGB565(255, 0, 200) , RGB565(255, 0, 255), RGB565(0, 255, 255), RGB565(0, 255, 255) , RGB565(0, 255, 255) },

 { "gameboy", "Retro 8-Bit",
 RGB565(139, 149, 109), RGB565(196, 207, 161), RGB565(25, 50, 25), RGB565(70, 100, 70), RGB565(170, 180, 140), RGB565(50, 80, 50), RGB565(30, 60, 30), RGB565(160, 170, 130),
 RGB565(80, 110, 80), RGB565(100, 130, 100), RGB565(40, 70, 40), RGB565(60, 90, 60), RGB565(40, 70, 40) , RGB565(70, 100, 70), RGB565(25, 50, 25), RGB565(25, 50, 25) , RGB565(25, 50, 25) },

 { "sith", "Red Alert",
 RGB565(20, 5, 5), RGB565(40, 15, 15), RGB565(255, 80, 80), RGB565(180, 50, 50), RGB565(100, 30, 30), RGB565(220, 40, 40), RGB565(255, 100, 100), RGB565(60, 25, 25),
 RGB565(255, 50, 50), RGB565(255, 150, 40), RGB565(100, 220, 100), RGB565(100, 200, 255), RGB565(255, 40, 40) , RGB565(180, 50, 50), RGB565(255, 80, 80), RGB565(255, 80, 80) , RGB565(255, 80, 80) },
#endif /* SIMUT_THEMES_RETRO */

#ifdef SIMUT_THEMES_NATURE
 /* ------ NATURE: natural/ambient palettes ------ */
 { "amber", "Industrial Amber",
 RGB565(20, 15, 10), RGB565(35, 28, 20), RGB565(255, 190, 0), RGB565(180, 130, 0), RGB565(80, 60, 0), RGB565(255, 150, 0), RGB565(255, 220, 100), RGB565(55, 45, 30),
 RGB565(255, 80, 80), RGB565(255, 200, 50), RGB565(100, 220, 100), RGB565(100, 200, 255), RGB565(255, 180, 50) , RGB565(180, 130, 0), RGB565(255, 190, 0), RGB565(255, 190, 0) , RGB565(255, 190, 0) },

 { "vampire", "Dracula Theme",
 RGB565(40, 42, 54), RGB565(68, 71, 90), RGB565(248, 248, 242), RGB565(142, 148, 172), RGB565(98, 114, 164), RGB565(255, 121, 198), RGB565(189, 147, 249), RGB565(85, 88, 112),
 RGB565(255, 85, 85), RGB565(241, 250, 140), RGB565(80, 250, 123), RGB565(139, 233, 253), RGB565(255, 121, 198) , RGB565(142, 148, 172), RGB565(248, 248, 242), RGB565(248, 248, 242) , RGB565(248, 248, 242) },

 { "magma", "Magma Gaming",
 RGB565(25, 10, 15), RGB565(45, 20, 25), RGB565(250, 240, 240), RGB565(200, 100, 120), RGB565(100, 40, 50), RGB565(255, 50, 50), RGB565(255, 120, 100), RGB565(70, 30, 40),
 RGB565(255, 80, 80), RGB565(255, 160, 40), RGB565(90, 220, 100), RGB565(100, 200, 255), RGB565(255, 80, 80) , RGB565(200, 100, 120), RGB565(250, 240, 240), RGB565(250, 240, 240) , RGB565(250, 240, 240) },

 { "ocean", "Deep Ocean",
 RGB565(10, 20, 30), RGB565(20, 35, 50), RGB565(240, 245, 250), RGB565(120, 160, 200), RGB565(50, 80, 110), RGB565(0, 180, 255), RGB565(100, 220, 255), RGB565(35, 55, 75),
 RGB565(255, 100, 100), RGB565(255, 180, 50), RGB565(80, 220, 120), RGB565(120, 210, 255), RGB565(60, 190, 240) , RGB565(120, 160, 200), RGB565(240, 245, 250), RGB565(240, 245, 250) , RGB565(240, 245, 250) },

 { "nature", "Eco Nature",
 RGB565(240, 248, 240), RGB565(255, 255, 255), RGB565(30, 50, 30), RGB565(110, 140, 110), RGB565(180, 200, 180), RGB565(60, 160, 80), RGB565(90, 190, 110), RGB565(220, 230, 220),
 RGB565(210, 60, 60), RGB565(230, 150, 40), RGB565(50, 150, 70), RGB565(60, 150, 210), RGB565(50, 160, 90) , RGB565(110, 140, 110), RGB565(30, 50, 30), RGB565(30, 50, 30) , RGB565(30, 50, 30) },

 { "outrun", "Miami Sunset",
 RGB565(30, 10, 35), RGB565(50, 20, 55), RGB565(255, 200, 100), RGB565(200, 120, 200), RGB565(100, 50, 120), RGB565(255, 100, 0), RGB565(255, 150, 50), RGB565(70, 30, 80),
 RGB565(255, 50, 100), RGB565(255, 150, 50), RGB565(50, 255, 150), RGB565(0, 200, 255), RGB565(255, 100, 200) , RGB565(200, 120, 200), RGB565(255, 200, 100), RGB565(255, 200, 100) , RGB565(255, 200, 100) },

 { "midnight", "Midnight Blue",
 RGB565(10, 15, 25), RGB565(25, 35, 55), RGB565(220, 230, 240), RGB565(130, 150, 180), RGB565(60, 80, 110), RGB565(80, 160, 240), RGB565(120, 190, 255), RGB565(40, 55, 80),
 RGB565(255, 90, 90), RGB565(255, 180, 60), RGB565(90, 220, 120), RGB565(110, 210, 255), RGB565(80, 170, 250) , RGB565(130, 150, 180), RGB565(220, 230, 240), RGB565(220, 230, 240) , RGB565(220, 230, 240) },

 { "forest", "Deep Forest",
 RGB565(15, 25, 15), RGB565(30, 45, 30), RGB565(230, 240, 230), RGB565(140, 170, 140), RGB565(70, 100, 70), RGB565(120, 180, 80), RGB565(160, 210, 110), RGB565(45, 65, 45),
 RGB565(255, 100, 80), RGB565(255, 190, 60), RGB565(100, 210, 100), RGB565(120, 220, 255), RGB565(130, 190, 90) , RGB565(140, 170, 140), RGB565(230, 240, 230), RGB565(230, 240, 230) , RGB565(230, 240, 230) },
#endif /* SIMUT_THEMES_NATURE */

#ifdef SIMUT_THEMES_UTILITY
 /* ------ UTILITY: neutral/specialty displays ------ */
 { "paper", "E-Paper Reader",
 RGB565(235, 235, 230), RGB565(250, 250, 245), RGB565(40, 40, 35), RGB565(120, 120, 110), RGB565(180, 180, 170), RGB565(80, 80, 75), RGB565(20, 20, 15), RGB565(210, 210, 200),
 RGB565(150, 50, 50), RGB565(180, 120, 30), RGB565(60, 140, 70), RGB565(50, 130, 180), RGB565(80, 80, 75) , RGB565(120, 120, 110), RGB565(40, 40, 35), RGB565(40, 40, 35) , RGB565(40, 40, 35) },

 { "blocks", "Toy Blocks",
 RGB565(240, 240, 240), RGB565(255, 255, 255), RGB565(30, 30, 30), RGB565(120, 120, 120), RGB565(190, 190, 190), RGB565(220, 40, 40), RGB565(40, 120, 220), RGB565(220, 220, 220),
 RGB565(220, 50, 50), RGB565(240, 180, 20), RGB565(50, 180, 70), RGB565(50, 150, 220), RGB565(240, 190, 30) , RGB565(120, 120, 120), RGB565(30, 30, 30), RGB565(30, 30, 30) , RGB565(30, 30, 30) },

 { "blueprint", "Blueprint CAD",
 RGB565(15, 35, 80), RGB565(25, 55, 110), RGB565(240, 245, 250), RGB565(150, 180, 210), RGB565(80, 110, 150), RGB565(255, 255, 255), RGB565(200, 220, 255), RGB565(40, 75, 140),
 RGB565(255, 100, 100), RGB565(255, 180, 80), RGB565(100, 220, 120), RGB565(120, 210, 255), RGB565(220, 240, 255) , RGB565(150, 180, 210), RGB565(240, 245, 250), RGB565(240, 245, 250) , RGB565(240, 245, 250) },

 { "solarized", "Solarized Dark",
 RGB565(0, 43, 54), RGB565(7, 54, 66), RGB565(131, 148, 150), RGB565(88, 110, 117), RGB565(42, 63, 70), RGB565(38, 139, 210), RGB565(42, 161, 152), RGB565(20, 65, 75),
 RGB565(220, 50, 47), RGB565(181, 137, 0), RGB565(133, 153, 0), RGB565(38, 139, 210), RGB565(42, 161, 152) , RGB565(88, 110, 117), RGB565(131, 148, 150), RGB565(131, 148, 150) , RGB565(131, 148, 150) },

 { "luxury", "Black & Gold",
 RGB565(12, 12, 12), RGB565(28, 28, 28), RGB565(255, 215, 0), RGB565(180, 150, 50), RGB565(90, 75, 30), RGB565(218, 165, 32), RGB565(255, 225, 100), RGB565(45, 45, 45),
 RGB565(255, 80, 80), RGB565(255, 180, 50), RGB565(100, 220, 100), RGB565(120, 200, 255), RGB565(240, 190, 60) , RGB565(180, 150, 50), RGB565(255, 215, 0), RGB565(255, 215, 0) , RGB565(255, 215, 0) },

 { "ubuntu", "Ubuntu Terminal",
 RGB565(48, 10, 36), RGB565(75, 20, 55), RGB565(240, 240, 240), RGB565(180, 180, 180), RGB565(100, 70, 90), RGB565(233, 84, 32), RGB565(255, 120, 60), RGB565(95, 35, 70),
 RGB565(255, 80, 80), RGB565(255, 180, 50), RGB565(100, 220, 100), RGB565(100, 210, 255), RGB565(240, 100, 50) , RGB565(180, 180, 180), RGB565(240, 240, 240), RGB565(240, 240, 240) , RGB565(240, 240, 240) },

 { "whiteboard", "Marker Board",
 RGB565(250, 250, 250), RGB565(255, 255, 255), RGB565(20, 20, 20), RGB565(100, 100, 100), RGB565(180, 180, 180), RGB565(30, 60, 200), RGB565(60, 100, 255), RGB565(230, 230, 230),
 RGB565(220, 40, 40), RGB565(240, 140, 20), RGB565(40, 160, 60), RGB565(40, 150, 220), RGB565(50, 80, 220) , RGB565(100, 100, 100), RGB565(20, 20, 20), RGB565(20, 20, 20) , RGB565(20, 20, 20) },
#endif /* SIMUT_THEMES_UTILITY */

};

const int NUM_THEMES = sizeof(availableThemes) / sizeof(ThemePalette);

/* ────────────────────────────────────────────────────────────────────────
 * Custom themes carregados de arquivos .thm em /themes/ no LittleFS.
 * RAM-resident (até MAX_CUSTOM_THEMES * sizeof(CustomTheme)).
 * Strings idName/displayName ficam embutidas no struct (sem heap).
 * ──────────────────────────────────────────────────────────────────────── */
#include <LittleFS.h>

#define MAX_CUSTOM_THEMES 8
#define THM_DIR "/themes"
#define THM_ID_MAX 16
#define THM_NAME_MAX 24

struct CustomTheme {
 bool used;
 char idBuf[THM_ID_MAX];
 char nameBuf[THM_NAME_MAX];
 ThemePalette pal; /* idName/displayName apontam pra idBuf/nameBuf */
};

static CustomTheme _customThemes[MAX_CUSTOM_THEMES];
static int _customCount = 0;

/* Parser de #RRGGBB ou 0xRRGGBB → RGB565. Retorna -1 em erro. */
static int32_t parseHexRgb565(const char* s) {
 if (!s) return -1;
 while (*s == ' ' || *s == '\t') s++;
 if (*s == '#') s++;
 else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
 uint32_t v = 0;
 int n = 0;
 while (*s && n < 6) {
 char c = *s++;
 int d;
 if (c >= '0' && c <= '9') d = c - '0';
 else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
 else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
 else break;
 v = (v << 4) | d;
 n++;
 }
 if (n != 6) return -1;
 uint8_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
 return RGB565(r, g, b);
}

/* Parseia 1 arquivo .thm pra dest. true em sucesso. Aceita os 16 campos
 * (13 originais + btnText/titleText/sensorName); ausentes herdam textSub
 * ou textMain. */
static bool parseThm(const char* path, CustomTheme* dest) {
 File f = LittleFS.open(path, "r");
 if (!f) return false;

 dest->used = false;
 dest->idBuf[0] = '\0';
 dest->nameBuf[0] = '\0';

 bool inColors = false;
 bool gotAny = false;
 /* Defaults — sobrescritos por chaves no .thm */
 dest->pal.bgMain = RGB565(18, 18, 20);
 dest->pal.cardBg = RGB565(35, 38, 45);
 dest->pal.textMain = RGB565(245, 245, 245);
 dest->pal.textSub = RGB565(180, 180, 185);
 dest->pal.textOff = RGB565(90, 90, 100);
 dest->pal.accent = RGB565(0, 150, 255);
 dest->pal.accentHigh = RGB565(50, 200, 255);
 dest->pal.barBg = RGB565(50, 50, 60);
 dest->pal.tempHot = RGB565(255, 60, 60);
 dest->pal.tempWarm = RGB565(255, 170, 0);
 dest->pal.tempOk = RGB565(40, 200, 80);
 dest->pal.tempCold = RGB565(0, 200, 255);
 dest->pal.humidity = RGB565(0, 150, 255);
 /* Novos: defaults serão herdados após o parse loop se ficarem zerados */
 bool gotBtn = false, gotTitle = false, gotSensor = false, gotBtnActive = false;
 dest->pal.btnText = 0;
 dest->pal.titleText = 0;
 dest->pal.sensorName = 0;
 dest->pal.btnTextActive = 0;

 char line[80];
 while (f.available( )) {
 int n = f.readBytesUntil('\n', line, sizeof(line) - 1);
 if (n <= 0) continue;
 line[n] = '\0';
 /* Trim CR e espaços do fim */
 while (n > 0 && (line[n-1] == '\r' || line[n-1] == ' ' || line[n-1] == '\t')) {
 line[--n] = '\0';
 }
 if (n == 0 || line[0] == '#') continue;

 if (!strncmp(line, "@NAME ", 6)) {
 strncpy(dest->nameBuf, line + 6, THM_NAME_MAX - 1);
 dest->nameBuf[THM_NAME_MAX - 1] = '\0';
 } else if (!strncmp(line, "@CODE ", 6)) {
 strncpy(dest->idBuf, line + 6, THM_ID_MAX - 1);
 dest->idBuf[THM_ID_MAX - 1] = '\0';
 } else if (!strncmp(line, "@COLORS", 7)) {
 inColors = true;
 } else if (inColors) {
 char* eq = strchr(line, '=');
 if (!eq) continue;
 *eq = '\0';
 char* key = line;
 char* val = eq + 1;
 int32_t c = parseHexRgb565(val);
 if (c < 0) continue;
 uint16_t color = (uint16_t)c;
 gotAny = true;
 if (!strcmp(key, "bgMain")) dest->pal.bgMain = color;
 else if (!strcmp(key, "cardBg")) dest->pal.cardBg = color;
 else if (!strcmp(key, "textMain")) dest->pal.textMain = color;
 else if (!strcmp(key, "textSub")) dest->pal.textSub = color;
 else if (!strcmp(key, "textOff")) dest->pal.textOff = color;
 else if (!strcmp(key, "accent")) dest->pal.accent = color;
 else if (!strcmp(key, "accentHigh")) dest->pal.accentHigh = color;
 else if (!strcmp(key, "barBg")) dest->pal.barBg = color;
 else if (!strcmp(key, "tempHot")) dest->pal.tempHot = color;
 else if (!strcmp(key, "tempWarm")) dest->pal.tempWarm = color;
 else if (!strcmp(key, "tempOk")) dest->pal.tempOk = color;
 else if (!strcmp(key, "tempCold")) dest->pal.tempCold = color;
 else if (!strcmp(key, "humidity")) dest->pal.humidity = color;
 else if (!strcmp(key, "btnText")) { dest->pal.btnText = color; gotBtn = true; }
 else if (!strcmp(key, "titleText")) { dest->pal.titleText = color; gotTitle = true; }
 else if (!strcmp(key, "sensorName")) { dest->pal.sensorName = color; gotSensor = true; }
 else if (!strcmp(key, "btnTextActive")) { dest->pal.btnTextActive = color; gotBtnActive = true; }
 }
 }
 f.close( );

 if (!gotAny || dest->idBuf[0] == '\0') return false;
 if (dest->nameBuf[0] == '\0') strncpy(dest->nameBuf, dest->idBuf, THM_NAME_MAX - 1);

 /* Fallback dos novos campos pra cores existentes (compatibility) */
 if (!gotBtn) dest->pal.btnText = dest->pal.textSub;
 if (!gotTitle) dest->pal.titleText = dest->pal.textMain;
 if (!gotSensor) dest->pal.sensorName = dest->pal.textMain;
 if (!gotBtnActive) dest->pal.btnTextActive = dest->pal.textMain;

 dest->pal.idName = dest->idBuf;
 dest->pal.displayName = dest->nameBuf;
 dest->used = true;
 return true;
}

void scanCustomThemes( ) {
 _customCount = 0;
 for (int i = 0; i < MAX_CUSTOM_THEMES; i++) _customThemes[i].used = false;

 /* mkdir REMOVIDO daqui — causava hang em boot pós-OTA.
 * LittleFS.mkdir usa flash_safe_execute interno que disparava
 * multicore_lockout que não respondia em boot states pós-watchdog.
 * Capturado via UART markers: hang no scanCustomThemes durante boot.
 * mkdir é necessário só pra UPLOAD de novos .thm — feito em
 * handleApiUpload (path seguro, Core 1 em loop normal). Se /themes
 * não existir aqui, openDir retorna empty + loop sai. _customCount=0. */

 Dir d = LittleFS.openDir(THM_DIR);
 while (d.next( ) && _customCount < MAX_CUSTOM_THEMES) {
 String name = d.fileName( );
 if (!name.endsWith(".thm")) continue;
 String full = String(THM_DIR) + "/" + name;
 if (parseThm(full.c_str( ), &_customThemes[_customCount])) {
 _customCount++;
 }
 }
}

const ThemePalette* getThemePalette(int index) {
 if (index < 0) return &availableThemes[0];
 if (index < NUM_THEMES) return &availableThemes[index];
 int ci = index - NUM_THEMES;
 if (ci < _customCount && _customThemes[ci].used) return &_customThemes[ci].pal;
 return &availableThemes[0];
}

/** @brief Load a theme by index into the active palette. */
void loadTheme(int index) {
 const ThemePalette* p = getThemePalette(index);
 currentTheme = *p;
}

int getThemeCount( ) { return NUM_THEMES + _customCount; }

String getThemeId(int index) {
 const ThemePalette* p = getThemePalette(index);
 return String(p->idName);
}

/** @brief Find a theme by name (exact match first, then substring search). */
int getThemeIndexByName(String name) {
 int total = getThemeCount( );
 for (int i = 0; i < total; i++) {
 if (String(getThemePalette(i)->idName).equalsIgnoreCase(name)) return i;
 }
 for (int i = 0; i < total; i++) {
 if (String(getThemePalette(i)->idName).indexOf(name) >= 0) return i;
 }
 return -1;
}
