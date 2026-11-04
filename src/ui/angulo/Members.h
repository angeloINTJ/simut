/* The Ângulo driver's members of class DisplayManager (pico_wiretft_angulo only).
 * Included INSIDE the class, as its last lines, by src/ui/UiDriverMembers.h. It has no include guard and no
 * #include of its own: a second include would declare every member twice, and the types it names (UaQuadro,
 * UaToque, UaEstado, ua::) have to be complete before the class, so src/ui/UiDriver.h brings them. */
public:
	/* GET /api/ui: a consistent copy of the frame on the glass (any core).
	 * 1 copied, 0 no frame on the glass yet, -1 Core 1 kept rewriting the
	 * record for 2 ms (the caller answers "busy" rather than "none"). */
	int8_t uaEstado( UaEstado &e ) const;
	/* The language pack has finished loading at boot (AppManager_Boot.cpp, Core 0, once): the panel reads the pack's @ANGULO section into ua::textosCarregar( ).
	 * Returns 0, or the ua::TextosResultado of the refusal, which the core logs. DisplayManager_UiA_Textos.cpp; the original's hook of the same name returns 0 (UiDriverMembers.h). */
	int uiLangLoaded( );
	/* GET /api/keypad (WebManager::handleApiKeypad( ), Core 0) sends the faces, "up", "kb" and the policy and, in place of the original panel's grid and popup geometry (which
	 * would miss this panel's keys), this route: the one that lists where the keys of the frame on the glass are (WebManager_UiA.cpp registers it). Defined here, in the class and
	 * not in a unit of its own, like uaSujo( ) below: the handler is another unit, and what it asks has to be a constant it can see, so that the original's branch folds out of the Angulo's
	 * images (S12, F4). The original's hook of the same name answers nullptr (UiDriverMembers.h). */
	const char *uiKeypadTargets( ) const { return "/api/ui"; }
private:
	/* The Ângulo build (src/ui/angulo/DisplayManager_UiA*.cpp). Last among
	 * the members, like the update screen's, so they move no other one. */
	volatile bool _uaSujo = true;   /**< a hook or a tick said the frame may have changed: repaint if it did */
	bool _uaTudo = true;            /**< repaint every strip (theme, alignment, a screen that drew under us) */
	int8_t _uaPrevia = -1;          /**< the Tema screen's preview, a themeIndex as ua::indiceDaEscolha( ) writes it; -1 none. Core 1's alone, dropped when the mode is not MODE_SETTINGS_THEMES (S10c, A'3). It sits in the padding after the two flags: after _uaVisto the class grew by 8 B, which AppManager's constructor showed as sizeof( DisplayManager ) 8584 -> 8592 (2026-10-22) */
	uint32_t _uaVisto = 0;          /**< millis of the last look at the frame */
	void uaSujo( ) { _uaSujo = true; }
	void uaQuadro( );
	bool uaPintar( const UaQuadro &q, UiMode modo, uint32_t hash );
	bool uaEmpurrar( uint16_t *buf, int16_t x, int16_t y, int16_t w, int16_t h );   // DisplayManager_UiA_Empurrar.cpp
	void uaEmpurrarEsperar( );
	void uiTouch( int16_t x, int16_t y );   /**< the touch lands on the frame on the glass; handleTouch( ) calls it where the original runs its mode chain */
	const ua::Tema &uaTema( UiMode modo );
	bool uaRelogio( char hora[6], int &h, bool &provisoria );
	void uaVmStatus( ua::VmStatus &s );
	bool uaMontar( UaQuadro &q );
	bool uaMontarPendente( UaQuadro &q );
	UaToque uaToquePendente( const ua::Alvo *a );
	/* One pair per group of screens (ESPEC_UI_A.md §4), each in its file. */
	bool uaMontarBase( UaQuadro &q );
	UaToque uaToqueBase( const ua::Alvo *a, int16_t &x, int16_t &y );
	/* MODE_CONFIRM_AP's frame and touch, the Base group's third dialog (S10c, D7): defined in DisplayManager_UiA.cpp inside one #if SIMUT_NET_WIFI, with stubs (false, NAO_E_MEU) for the wired image. */
	bool uaMontarConfirmaAp( UaQuadro &q );
	UaToque uaToqueConfirmaAp( const ua::Alvo *a );
	bool uaMontarPainel( UaQuadro &q );
	UaToque uaToquePainel( const ua::Alvo *a, int16_t &x, int16_t &y );
	bool uaMontarMenus( UaQuadro &q );
	UaToque uaToqueMenus( const ua::Alvo *a, int16_t &x, int16_t &y );
	bool uaMontarAjustes( UaQuadro &q );
	UaToque uaToqueAjustes( const ua::Alvo *a, int16_t &x, int16_t &y );
	bool uaMontarIdentidade( UaQuadro &q );
	UaToque uaToqueIdentidade( const ua::Alvo *a, int16_t &x, int16_t &y );
	bool uaMontarGraficos( UaQuadro &q );
	UaToque uaToqueGraficos( const ua::Alvo *a, int16_t &x, int16_t &y );
	void tickWebNotify( );    /**< the web login notice expires (the old top bar cleared it as it drew it) */
	void tickMinMaxIdle( );   /**< the core's min/max return (minMaxIdleDue( )) for a pass that has no render( ) to do it */
	bool uaMontarOta( UaQuadro &q );   /**< the update screen whatever _uiMode says (Core 0 paints it) */
