/**
 * @file    SystemDefs_Cli.h
 * @brief   CLI command architecture: DemandType + CliDemand (EXT-003 split).
 * @details Enum de comandos parseados pelo CommandManager (USB/BT) e struct
 *          CliDemand com payload tipado (intVal/strVal/rom/etc). Setters
 *          encapsulam safeCopy() para os campos char[]. Sub-header de
 *          SystemDefs.h (facade). EXT-003 / F17 etapa 4.
 *
 * @project SIMUT
 * @author  Ângelo Moisés Alves
 * @license MIT License
 */

#pragma once
#include <Arduino.h>
#include "SystemDefs_Time.h"  /* safeCopy */

/** CLI command types parsed from USB/Bluetooth input. */
enum DemandType {
    CMD_NONE = 0,
    CMD_UNKNOWN,
    CMD_HELP,
    CMD_SHOW_THEMES,
    CMD_SET_THEME,
    CMD_SHOW_LOGS,
    CMD_SHOW_SENSORS,
    CMD_SHOW_STORAGE,
    CMD_SHOW_SYSINFO,
    CMD_SHOW_NET,
    CMD_SHOW_METRICS,
    CMD_SET_DS_RES,
    CMD_SET_SYS_NAME,
    CMD_SET_WIFI_SSID,
    CMD_SET_WIFI_PASS,
    CMD_SET_TIMEZONE,
    CMD_SET_NTP,
    CMD_SET_TEL_SERVER,
    CMD_SET_TEL_PORT,
    CMD_SET_TEL_PATH,
    CMD_SET_TEL_BATCH,
    CMD_SET_TEL_INTERVAL,
    CMD_SET_TEL_CRYPTO,
    CMD_SET_TEL_MODE,
    CMD_SET_HISTORY_INTERVAL,  /**< intVal1 = minutos (1..1440). */
    CMD_RESET_ADMIN,
    CMD_RESET_TOUCH_CAL,
    CMD_FACTORY_RESET,
    CMD_SET_NTP_ENABLED,   /**< F-NET-TIME.4: intVal1 = 0 off, 1 on */
    CMD_SET_DNS_CFG,       /**< F-NET-TIME.4: intVal1 = 0 auto, 1 manual; strVal1=ip1, strVal2=ip2 */
    CMD_SET_TIME,          /**< F-NET-TIME.4: strVal1="YYYY-MM-DD", strVal2="HH:MM:SS" */
    CMD_DEFINE_SENSOR,
    CMD_WIPE_SENSOR,
    CMD_ACCEPT_SENSOR,
    CMD_SCAN_SENSORS,
    CMD_WRITE_MEMORY,
    CMD_CLEAR_LOGS,
    CMD_RELOAD,
    CMD_TEL_SYNC,
    CMD_TEL_DUMP,
    CMD_DEBUG,
    CMD_LANGUAGE,
    /* #7: paridade CLI↔Web */
    CMD_IP_CFG,       /**< IP estático: intVal1 = 0 dhcp, 1 static, 2 addr, 3 mask, 4 gw, 5 dns; strVal1 = valor */
    CMD_SENSOR_FIELD, /**< Limites/calib: intVal1 = gpio; strVal1 = field (tmin/tmax/hmin/hmax/alarm/calib); strVal2 = valor */
    CMD_USER_ADD,     /**< strVal1 = username; strVal2 = senha */
    CMD_USER_DEL,     /**< strVal1 = username (protege admin) */
    CMD_USER_PASS,    /**< strVal1 = username; strVal2 = nova senha */
    CMD_SET_WEB_PORT, /**< intVal1 = porta (1..65535) */

};

/** Parsed CLI command with typed payload fields.
 *
 * CON-005b: strVal1/strVal2 são char[] fixos (antes: String) para remover
 * alocações dinâmicas no parser CLI (path exercitado a cada comando).
 * 64 bytes cobrem os maiores destinos: cfg.telServer[64], telApiKey[64],
 * wifiSsid[32], etc. Strings maiores que 63 chars são truncadas
 * silenciosamente em safeCopy — mesmo comportamento anterior quando
 * chegavam em cfg.* também truncado.
 */
struct CliDemand {
    DemandType type;
    char strVal1[64] = {0};
    char strVal2[64] = {0};
    int intVal1;
    bool boolVal;
    uint8_t rom[8];
    bool confirmed = false;      /**< true se sufixo 'confirm' presente — gate p/ comandos destrutivos */
    bool intVal1Valid = true;    /**< #11: false se o token numérico não era um int bem-formado */

    /* Setters utilitários para migração char[] (CON-005b). Usar nos sites
     * que antes faziam `cmd.strVal1 = <String|const char*>`. */
    void setStrVal1(const char* s) { safeCopy(strVal1, s ? s : "", sizeof(strVal1)); }
    void setStrVal2(const char* s) { safeCopy(strVal2, s ? s : "", sizeof(strVal2)); }
};

