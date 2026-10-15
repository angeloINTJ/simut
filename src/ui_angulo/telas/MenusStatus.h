#pragma once
// The two read-only screens of the menu: the system status (four pages) and
// the licence (pages of seven lines). ESPEC_UI_A.md §6.2. Neither has a row
// to touch: their targets are "Voltar" and the pager's arrows (AlvoLista:
// LST_VOLTAR, LST_PAG_ANTERIOR, LST_PAG_PROXIMA), see MenusLista.h.
#include "MenusLista.h"

namespace ua {
namespace tela {

/* ------------------------------------------------------------------ */
/* Status do sistema (MODE_SETTINGS_STATUS)                             */
/* ------------------------------------------------------------------ */

enum PaginaStatus : uint8_t { PS_SISTEMA, PS_REDE, PS_SENSORES, PS_TELEMETRIA };
constexpr int STATUS_PAGINAS = 4;
constexpr int STATUS_SENSORES_MAX = 5;     // sensors the page lists; more than that and the last row says how many are left

// What the sensors page says of each sensor. The first four say where its
// reading stands against its limits; the panel's Core 1 reads only the selected
// and the pinned sensor's value, so the glue of the functional build cannot
// tell them apart for the others and speaks the alarm system's own words
// (ESS_ALARME: a limit alarm is firing; ESS_ERRO: the sensor is failing;
// ESS_MANUTENCAO: a window is open; ESS_DESLIGADO: its alarms are off; else
// ESS_NA_FAIXA: alarms on and none firing).
enum EstadoSensor : uint8_t { ESS_NA_FAIXA, ESS_NO_LIMITE, ESS_ACIMA, ESS_ABAIXO, ESS_ERRO, ESS_MANUTENCAO, ESS_ALARME, ESS_DESLIGADO };

struct SensorStatus {
  char nome[32];
  uint8_t estado;                          // EstadoSensor
};

// The numbers are the old screen's SystemStatusData (SystemDefs_Records.h),
// as they come: the screen turns them into text, so the rules for it (kB, the
// uptime, the colours) are here and the host can draw them.
struct VmStatusSistema {
  uint8_t pagina;                          // PaginaStatus
  // Sistema
  char aparelho[32];                       // cfg.deviceName
  char firmware[16];                       // SIMUT_VERSION
  uint32_t ligadoSeg;                      // uptime, seconds
  uint32_t memLivre;                       // free heap, bytes
  uint32_t flashUsada;                     // the file system's used bytes (the WebManager's cache)
  uint32_t flashTotal;                     // its size, bytes
  int16_t placaDecimos;                    // the board's temperature, tenths of a degree
  int8_t fuso;                             // cfg.timezoneOffset, hours from GMT
  // Rede
  bool cabo;                               // the wired build: a "Cabo" row where the radio build has Wi-Fi and signal
  bool conectado;                          // the cable has link / Wi-Fi is associated
  bool ntpSincronizado;
  int16_t rssi;                            // dBm; the radio build only
  char ssid[33];
  char ip[16];
  char mac[18];
  char ntpServidor[48];
  // Sensores
  uint8_t ativos;                          // active sensors
  uint8_t slots;                           // the build's sensor slots: the "11" of "4 de 11"
  uint8_t nSensores;                       // entries filled below, in slot order, at most STATUS_SENSORES_MAX
  SensorStatus sensores[STATUS_SENSORES_MAX];
  // Telemetria
  bool mqtt;                               // the transport is MQTT (else HTTP)
  bool mqttConectado;
  uint16_t pendentes;                      // records waiting to be sent
  uint32_t lote;                           // the minimum batch, records; 0: off
  char servidor[64];                       // cfg.telServer
};
void desenharStatusSistema( Pintor &p, const VmStatusSistema &vm );
int alvosStatusSistema( const VmStatusSistema &vm, Alvo *out );

/* ------------------------------------------------------------------ */
/* Licença (MODE_SETTINGS_LICENSE)                                      */
/* ------------------------------------------------------------------ */

constexpr int LICENCA_LINHAS = 7;          // lines to a page
constexpr int LICENCA_BYTES = 96;          // bytes of one wrapped line (UTF-8)
constexpr int16_t LICENCA_PASSO = 16;      // the 7 pt text's line pitch
constexpr int16_t LICENCA_BARRA = 4;       // the scroll bar's width
// The ink the glue may give each line when it wraps the text with quebrar( ) in
// tipo::apoio: the card's text column, up to the scroll bar.
constexpr int16_t LICENCA_LARGURA = W_TELA - 2 * ESP2 - 2 * PAD - LICENCA_BARRA - ESP2;

struct VmLicenca {
  uint16_t pagina;                         // 0-based
  uint16_t paginas;                        // pages in all
  uint8_t n;                               // lines on this page, at most LICENCA_LINHAS (the last page may have fewer)
  char linhas[LICENCA_LINHAS][LICENCA_BYTES];   // already wrapped to LICENCA_LARGURA; "" is a blank line
};
void desenharLicenca( Pintor &p, const VmLicenca &vm );
int alvosLicenca( const VmLicenca &vm, Alvo *out );

}  // namespace tela
}  // namespace ua
