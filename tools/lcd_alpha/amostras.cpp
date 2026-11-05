/**
 * @file    tools/lcd_alpha/amostras.cpp
 * @brief   The manual's 17 LCD figures as samples of src/display/AlphaFrame.h on the host, and the link glyph of each RSSI (2026-11-04).
 * @details Each sample is a state the alpha's loop (DisplayManager_Alpha.cpp, loopCore1( )) reaches, named after its figure: an AlphaFrameIn, drawn by
 *          the alpha's own alphaDrawFrame( ) over a FakeHd44780 (test/native_stubs), after the frames that really come before it where the screen does
 *          not clear (the boot keeps the cells of row 0 past the version; the network screen and the setup network's pages write all thirty-two cells,
 *          and the sensor screen clears). Nothing of the composition is copied here: the headers come from src/ and what this file adds is the order of a
 *          pass, the loop's own (the link glyph, one screen, the blit), and the data of each state.
 *          The program prints, for every sample, the glass after its last blit( ): the DDRAM's two rows and the eight CGRAM slots, as hex, in the JSON
 *          that tools/lcd_alpha/lcd.py reads, and each sample's object is the one GET /api/lcd sends: the shadow of the fake LCD (the driver's own,
 *          display/LcdGlass.h) read by readGlass( ) and put through lcdGlassJson( ) (display/LcdGlassJson.h), the formatter the device runs. The outer
 *          object around the samples is this program's. Under "icones", for each RSSI of RSSIS, CGRAM slot 7 after alphaLoadLinkGlyph( ) on a fresh glass (the
 *          signal icon's figure, 12-wifi-niveis.png, and the table of chapter 12 that host_lcd.py --selftest holds it to). tools/lcd_alpha/host_lcd.py builds it twice (the radio's, and the cable's with -DSIMUT_NET_ENC28J60=1),
 *          runs it and draws the frames; the manual's figures are drawn by tools/lcd_alpha/manual_lcd.py from the same program.
 *
 *          usage: amostras VERSION          the version the splash and the boot screens print after "SIMUT " (data of the sample, not SIMUT_VERSION: the
 *                                           loop builds that banner from SIMUT_VERSION, DisplayManager_Alpha.cpp, and AlphaFrameIn takes it as a string)
 *                 amostras VERSION SAMPLE   only that sample's object, exactly what lcdGlassJson( ) wrote and a newline: the file lcd.py --quadro reads
 *                                           (exit 2 for a name no sample has)
 *
 *          The AP samples exist only in the radio's build: a wired build has no setup network and alphaDrawFrame( ) draws nothing for that screen.
 *
 * @project SIMUT
 * @license MIT License
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include "display/AlphaFrame.h"   /* its first include is net/NetCaps.h: SIMUT_NET_LAN and SIMUT_NET_WIFI come from the -D of the build */
#include "FakeHd44780.h"
#include "display/LcdGlassJson.h"   /* the formatter the device's GET /api/lcd runs */

namespace simut_native { uint32_t fake_millis_value = 0; }   /* declared by the Arduino.h stub; nothing here reads the clock */

namespace {

constexpr int32_t RSSI_BOM = -45;    /* above -50 dBm: the five-bar icon (BigFont_HD44780.h, showWiFi( )), and "online" (RSSI above -100) */
constexpr int32_t RSSI_SEM = -127;   /* no link: the X, and "offline" */

std::string g_banner;                /* "SIMUT " and the version: the string the caller of AlphaFrameIn owns */

/* The glass of the alpha as the loop starts it: begin( ), then the CGRAM load. A pass is the loop's: the link glyph into slot 7 first, then one
 * screen, then the blit (display/AlphaFrame.h says so; test/native_stubs/AlphaFrameCheck.h's AlphaPass does the same and needs Unity). */
struct Vidro {
	FakeHd44780     lcd;
	BigFont_HD44780 big;
	Vidro( ) { lcd.begin( ); big.begin(lcd); }
	void desenha(const AlphaFrameIn &in) { alphaDrawFrame(lcd, big, in); lcd.blit( ); }   /* the splash: before the loop, so no link glyph */
	void passe(int32_t rssi, const AlphaFrameIn &in) { alphaLoadLinkGlyph(lcd, big, rssi); desenha(in); }
};

AlphaFrameIn abertura( )  { AlphaFrameIn in(ALPHA_SCREEN_SPLASH); in.banner = g_banner.c_str( ); return in; }
AlphaFrameIn barra(uint8_t cheias) { AlphaFrameIn in(ALPHA_SCREEN_BOOT); in.banner = g_banner.c_str( ); in.bootBar = cheias; return in; }
AlphaFrameIn barra_cheia( ) { AlphaFrameIn in(ALPHA_SCREEN_BOOT_FULL); in.banner = g_banner.c_str( ); return in; }
AlphaFrameIn limpa( )     { return AlphaFrameIn(ALPHA_SCREEN_CLEAR); }

AlphaFrameIn rede(bool online, bool tem_ip, const char* ip) {
	AlphaFrameIn in(ALPHA_SCREEN_NET);
	in.online = online; in.hasIp = tem_ip; in.ip = ip;
	return in;
}

AlphaFrameIn leitura(uint8_t ativos, int8_t slot, uint8_t canal, bool valida, float valor, uint16_t pendentes) {
	AlphaFrameIn in(ALPHA_SCREEN_SENSOR);
	in.slots = ativos; in.slot = slot; in.channel = canal; in.valid = valida; in.value = valor; in.pending = pendentes;
	return in;
}

#if SIMUT_NET_WIFI
AlphaFrameIn pagina_do_ap(uint8_t pagina, const char* valor) {
	AlphaFrameIn in(ALPHA_SCREEN_AP);
	in.apPage = pagina; in.apValue = valor; in.apStep = 0;   /* step 0: the value as it is, a value of 16 cells or less does not scroll */
	return in;
}
#endif

/* The first seconds, as the loop runs them: the splash, then the boot bar, then the six full-bar passes that follow the boot. */
Vidro depois_da_abertura( ) { Vidro v; v.desenha(abertura( )); return v; }
Vidro depois_do_boot( ) {
	Vidro v = depois_da_abertura( );
	v.passe(RSSI_SEM, barra(7));
	v.passe(RSSI_SEM, barra_cheia( ));
	return v;
}
/* The network screen of the end of the boot, then its clear: the readings begin on a cleared screen. */
Vidro no_ar(int32_t rssi) {
	Vidro v = depois_do_boot( );
	const bool online = rssi > -100;
	v.passe(rssi, rede(online, online, online ? "192.0.2.10" : ""));
	v.passe(rssi, limpa( ));
	return v;
}
/* A setup network opened while the unit is on its readings: the frame before the first page is a reading (the pages write every cell anyway). */
Vidro nas_leituras(int32_t rssi) {
	Vidro v = no_ar(rssi);
	v.passe(rssi, leitura(1, 0, CH_TEMP, true, 23.4f, 0));
	return v;
}

/* Each sample takes the glass from the state before it, draws its screen and returns the glass after the blit. */
Vidro s_abertura( )       { return depois_da_abertura( ); }
Vidro s_boot( )           { Vidro v = depois_da_abertura( ); v.passe(RSSI_SEM, barra(7)); return v; }
Vidro s_conectado( )      { Vidro v = depois_do_boot( ); v.passe(RSSI_BOM, rede(true, true, "192.0.2.10")); return v; }
Vidro s_obtendo_ip( )     { Vidro v = depois_do_boot( ); v.passe(RSSI_BOM, rede(true, false, "")); return v; }
Vidro s_offline( )        { Vidro v = depois_do_boot( ); v.passe(RSSI_SEM, rede(false, false, "")); return v; }
Vidro s_temperatura( )    { Vidro v = no_ar(RSSI_BOM); v.passe(RSSI_BOM, leitura(2, 0, CH_TEMP, true, 23.4f, 0)); return v; }
Vidro s_umidade( )        { Vidro v = no_ar(RSSI_BOM); v.passe(RSSI_BOM, leitura(2, 1, CH_HUM, true, 58.0f, 0)); return v; }
Vidro s_pressao( )        { Vidro v = no_ar(RSSI_BOM); v.passe(RSSI_BOM, leitura(2, 2, CH_PRESS, true, 1013.2f, 0)); return v; }
Vidro s_negativa( )       { Vidro v = no_ar(RSSI_BOM); v.passe(RSSI_BOM, leitura(1, 0, CH_TEMP, true, -18.5f, 0)); return v; }
Vidro s_sem_sensor( )     { Vidro v = no_ar(RSSI_BOM); v.passe(RSSI_BOM, leitura(0, -1, CH_TEMP, false, NAN, 0)); return v; }
/* A DHT22 unplugged: the snapshot is not valid (AppManager_HistoryAlarm.cpp: valid = !inErrorState) and keeps the last average, so the value is a number
 * the screen must not show. */
Vidro s_lcd_erro( )       { Vidro v = no_ar(RSSI_BOM); v.passe(RSSI_BOM, leitura(1, 0, CH_TEMP, false, 23.4f, 0)); return v; }
Vidro s_pendentes( )      { Vidro v = no_ar(RSSI_BOM); v.passe(RSSI_BOM, leitura(1, 0, CH_TEMP, true, 23.4f, 37)); return v; }
Vidro s_pendentes_k( )    { Vidro v = no_ar(RSSI_BOM); v.passe(RSSI_BOM, leitura(1, 0, CH_TEMP, true, 23.4f, 2500)); return v; }
#if SIMUT_NET_WIFI
/* The setup network's three pages. Its name is the device's name and _SETUP, the key is a demonstration one (10 characters of the alphabet of ApPsk.h,
 * the chapter's own example): the key of no device. */
Vidro s_ap_1( )           { Vidro v = nas_leituras(RSSI_SEM); v.passe(RSSI_SEM, pagina_do_ap(0, "192.168.4.1")); return v; }
Vidro s_ap_2( )           { Vidro v = nas_leituras(RSSI_SEM); v.passe(RSSI_SEM, pagina_do_ap(1, "simut_SETUP")); return v; }
Vidro s_ap_3( )           { Vidro v = nas_leituras(RSSI_SEM); v.passe(RSSI_SEM, pagina_do_ap(2, "K7PX4MRW2A")); return v; }
#endif

struct Amostra { const char* nome; Vidro (*faz)( ); };

/* One per figure of the manual (tipo="lcd"), named after it; lcd-ap (chapter 4) and ap-2 (chapter 12) are the same page. */
const Amostra AMOSTRAS[] = {
	{ "splash",      s_abertura },
	{ "boot",        s_boot },
	{ "conectado",   s_conectado },
	{ "obtendo-ip",  s_obtendo_ip },
	{ "offline",     s_offline },
	{ "temperatura", s_temperatura },
	{ "umidade",     s_umidade },
	{ "pressao",     s_pressao },
	{ "negativa",    s_negativa },
	{ "sem-sensor",  s_sem_sensor },
	{ "lcd-erro",    s_lcd_erro },
	{ "pendentes",   s_pendentes },
	{ "pendentes-k", s_pendentes_k },
#if SIMUT_NET_WIFI
	{ "ap-1",        s_ap_1 },
	{ "ap-2",        s_ap_2 },
	{ "ap-3",        s_ap_3 },
	{ "lcd-ap",      s_ap_2 },
#endif
};

/* The signal icon's figure (12-wifi-niveis.png) and the table of chapter 12 ("O sinal do Wi-Fi"): the link glyph each RSSI puts in slot 7, at both
 * ends of every band of the table, the two values the samples use (RSSI_BOM, RSSI_SEM) and the cable's edge (-100). Each from a fresh glass, so every
 * value goes through the mapping: showWiFi( ) skips a level it already shows, and a glass carried over would keep the glyph of the value before. */
const int32_t RSSIS[] = { -45, -50, -51, -60, -61, -70, -71, -80, -81, -90, -91, -99, -100, -127 };

void hex(const uint8_t* p, size_t n) { for (size_t i = 0; i < n; i++) printf("%02x", (unsigned)p[i]); }

/* A sample's glass as GET /api/lcd sends it: the fake LCD's shadow, read the way the handler reads the driver's, through lcdGlassJson( ). The clock is the fake's
 * (nowMs 0, the stamp of every blit), so age_ms is 0 and `seq` counts the publishes the sample's history made: both are data of the sample. */
bool objeto(const Vidro& v, char (&buf)[LCD_GLASS_JSON_MAX]) {
	LcdGlass g;
	if (v.lcd.readGlass(g) != LCD_GLASS_OK) return false;   /* every sample ends in a blit( ): a glass with no frame is a bug of the sample */
	return lcdGlassJson(g, v.lcd.nowMs, buf, sizeof(buf)) != 0;
}

}  // namespace

int main(int argc, char** argv) {
	if (argc < 2 || argc > 3 || !argv[1][0] || (argc == 3 && !argv[2][0])) { fprintf(stderr, "usage: amostras VERSION [SAMPLE]\n"); return 2; }
	g_banner = std::string("SIMUT ") + argv[1];
	char json[LCD_GLASS_JSON_MAX];
	if (argc == 3) {   /* one sample, as the device would send it and nothing around it */
		for (size_t i = 0; i < sizeof(AMOSTRAS) / sizeof(AMOSTRAS[0]); i++) {
			if (strcmp(AMOSTRAS[i].nome, argv[2])) continue;
			if (!objeto(AMOSTRAS[i].faz( ), json)) { fprintf(stderr, "amostras: sample %s has no glass to send\n", argv[2]); return 1; }
			puts(json);
			return 0;
		}
		fprintf(stderr, "amostras: no sample named %s\n", argv[2]);
		return 2;
	}
	printf("{\"rede\":\"%s\",\"amostras\":{", SIMUT_NET_LAN ? "cabo" : "wifi");
	for (size_t i = 0; i < sizeof(AMOSTRAS) / sizeof(AMOSTRAS[0]); i++) {
		if (!objeto(AMOSTRAS[i].faz( ), json)) { fprintf(stderr, "amostras: sample %s has no glass to send\n", AMOSTRAS[i].nome); return 1; }
		printf("%s\"%s\":%s", i ? "," : "", AMOSTRAS[i].nome, json);
	}
	printf("},\"icones\":{");
	for (size_t i = 0; i < sizeof(RSSIS) / sizeof(RSSIS[0]); i++) {
		Vidro v;
		alphaLoadLinkGlyph(v.lcd, v.big, RSSIS[i]);
		printf("%s\"%d\":\"", i ? "," : "", (int)RSSIS[i]); hex(v.lcd.cgram[7], 8); printf("\"");
	}
	printf("}}\n");
	return 0;
}
