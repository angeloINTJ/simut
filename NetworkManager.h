/**
 * @file    NetworkManager.h
 * @brief   WiFi connectivity, NTP synchronization, and Virtual RTC management.
 * @details Handles the complete network lifecycle: WiFi STA/AP modes, exponential
 * backoff reconnection, NTP time sync, mDNS, and a Virtual RTC that
 * provides provisional timestamps from the last flash-stored epoch
 * until real NTP synchronization completes.
 *
 * @project SIMUT — Sistema Integrado de Monitoramento Universal de Temperatura
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <LEAmDNS.h>
#include <DNSServer.h>
#include "SystemDefs.h"
#include "LogManager.h"


typedef void (*TimeSyncCallback)(uint32_t bootProvisionalTs, int32_t deltaSeconds);

class NetworkManager {
public:
    NetworkManager();

    /* F-NET-TIME.2: flags adicionais lidas do overlay NetworkTimeData em
     * StorageManager. Passadas explicitamente (em vez de referência ao
     * StorageManager) para manter NetworkManager desacoplado. */
    void begin(const SystemConfig &cfg,
               bool dnsAuto = true,
               bool ntpEnabled = true,
               const char* dns2 = "");
    void beginAP(const char* deviceName);
    void update();


    void setProvisionalTime(uint32_t lastTs);
    void setTimeSyncCallback(TimeSyncCallback cb);

    /* F-NET-TIME.3a: set manual de RTC (via settimeofday) para quando NTP
     * está desabilitado. `epoch` é hora UTC em segundos (cliente converte
     * hora local para epoch usando o TZ acordado). No-op se epoch <= 0. */
    void setManualTime(time_t epoch);


    bool isConnected();
    bool isTimeSynced();
    int32_t getRssi();

    /**
     * @brief Verifica se a rede está saudável para operações pesadas.
     *
     * Combina estado de conexão WiFi com qualidade de sinal (RSSI).
     * Operações leves (NTP, mDNS) usam isConnected().
     * Operações pesadas (telemetria, uploads) devem usar isNetworkHealthy().
     *
     * @return true se conectado E sinal acima de RSSI_MIN_THRESHOLD.
     */
    bool isNetworkHealthy();


    String getIpAddress();
    void   getIpAddress(char* buf, size_t len);   /**< Buffer version (MEM-001). */
    String getMacAddress();
    void   getMacAddress(char* buf, size_t len);  /**< Buffer version (MEM-001). */
    String getSubnetMask();
    String getGateway();
    String getDns();


    String getFormattedTime();
    String getFormattedDate();
    time_t getEpoch();

    /**
     * @brief Aplica o fuso horário globalmente via setenv("TZ", ...).
     *
     * Após chamada, todo localtime_r() retorna hora local automaticamente.
     * Deve ser chamado no boot e sempre que o timezone for alterado.
     *
     * @param offset  Offset em horas relativo a UTC (ex: -3 para Brasil).
     */
    static void applyTimezone(int8_t offset);

private:
    enum NetState {
        NET_OFFLINE,
        NET_CONNECTING,
        NET_CONNECTED_WAIT_IP,
        NET_CONNECTED_WAIT_NTP,
        NET_READY,
        NET_AP_CONFIG,
        NET_SCANNING_RETRY,
        NET_DISCONNECT_PENDING
    };

    NetState _state;
    DNSServer _dnsServer;

    char _ssid[32];
    char _pass[32];
    char _deviceName[32];
    char _ntpServer[32];
    int8_t _tzOffset;

    /* F-NET-TIME.2 — flags runtime aplicadas no boot. */
    bool _dnsAuto     = true;
    bool _ntpEnabled  = true;
    bool _useDhcp     = true;
    char _staticDns1[16] = {0};
    char _staticDns2[16] = {0};

    uint32_t _stateTimer;
    uint32_t _retryTimer;
    uint32_t _reconnectTimer;
    uint32_t _reconnectDelay = 5000;
    static const uint32_t MAX_RECONNECT_DELAY = 120000;


    TimeSyncCallback _timeSyncCb = nullptr;
    uint32_t _provisionalBase = 0;
    uint32_t _provisionalBootMillis = 0;
    bool _provisionalActive = false;

    void handleConnecting();
    void syncNtp();
    void resetNtpBackoff();             /**< Reseta backoff após sucesso/reconnect */

    /* F-NET-TIME.2: aplica DNS manual (primário e/ou secundário) via lwIP
     * após IP adquirido. No-op quando dnsAuto=true e useDhcp=true. */
    void applyManualDnsIfNeeded();

    uint32_t _lastMdnsUpdate = 0;       /**< Throttle para MDNS.update()         */
    uint8_t  _connectCycles  = 0;        /**< Ciclos de reconexão consecutivos    */

    /* ── NTP retry com backoff exponencial + fallback ── */
    uint32_t _ntpRetryDelay = 20000;    /**< Delay atual entre retentativas (ms) */
    uint8_t  _ntpFailCount  = 0;        /**< Falhas consecutivas (reset em sync) */
    bool     _ntpFallbackDone = false;  /**< True após fallback p/ pool.ntp.org  */

    /* ── AP mode timeout ── */
    uint32_t _apStartTime = 0;          /**< millis() ao entrar em AP mode      */
};
