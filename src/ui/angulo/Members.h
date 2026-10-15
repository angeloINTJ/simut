/* The Ângulo driver's members of class DisplayManager (pico_wiretft_angulo only).
 * Included INSIDE the class, as its last lines, by src/ui/UiDriverMembers.h. It has no include guard and no
 * #include of its own: a second include would declare every member twice, and the types it names (UaQuadro,
 * UaToque, UaEstado, ua::) have to be complete before the class, so src/ui/UiDriver.h brings them. */
public:
	/* GET /api/ui: a consistent copy of the frame on the glass (any core).
	 * 1 copied, 0 no frame on the glass yet, -1 Core 1 kept rewriting the
	 * record for 2 ms (the caller answers "busy" rather than "none"). */
	int8_t uaEstado( UaEstado &e ) const;
private:
	/* The Ângulo build (src/ui/angulo/DisplayManager_UiA*.cpp). Last among
	 * the members, like the update screen's, so they move no other one. */
	volatile bool _uaSujo = true;   /**< an old draw entry point ran: repaint if the frame changed */
	bool _uaPintando = false;       /**< the Ângulo renderer is pushing pixels: the old primitives may run */
	bool _uaTudo = true;            /**< repaint every strip (theme, alignment, a screen that drew under us) */
	uint32_t _uaVisto = 0;          /**< millis of the last look at the frame */
	void uaSujo( ) { _uaSujo = true; }
	void uaQuadro( );
	bool uaPintar( const UaQuadro &q, UiMode modo, uint32_t hash );
	bool uaToque( int16_t &x, int16_t &y );
	void uaTemaAplicado( );
	void uaOta( );
	const ua::Tema &uaTema( );
	bool uaRelogio( char hora[6], int &h, bool &provisoria );
	void uaVmStatus( ua::VmStatus &s );
	bool uaMontar( UaQuadro &q );
	bool uaMontarPendente( UaQuadro &q );
	UaToque uaToquePendente( const ua::Alvo *a );
	/* One pair per group of screens (ESPEC_UI_A.md §4), each in its file. */
	bool uaMontarBase( UaQuadro &q );
	UaToque uaToqueBase( const ua::Alvo *a, int16_t &x, int16_t &y );
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
	bool uaMontarOta( UaQuadro &q );   /**< the update screen whatever _uiMode says (Core 0 paints it) */
