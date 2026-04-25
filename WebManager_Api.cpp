/**
 * @file    WebManager_Api.cpp
 * @brief   REST API handlers: status, config, network, users, themes, alarms, set_time.
 * @project SIMUT
 * @target  Raspberry Pi Pico W (RP2040) — Arduino Framework
 * @license MIT License
 */
#include "WebManager.h"
#include "WebUI_GZ.h"
#include "LogManager.h"
#include "MetricsManager.h"
#include "Themes.h"
#include "TouchPriority.h"
#include <LittleFS.h>
#include <time.h>

using ReadGuard = StorageManager::ReadGuard;

void WebManager::handleApiPerms() {
    uint16_t perms = getAuthPerms();
    if (perms == 0) { _server.send(401, "application/json", "{\"error\":\"Unauthorized\"}"); return; }

    bool ntpOk = _netRef->isTimeSynced();
    time_t now = time(nullptr);
    char json[224];
    snprintf(json, sizeof(json),
             "{\"user\":\"%s\",\"perms\":%u,\"ntp\":%d,\"time\":%lu,\"version\":\"%s\"}",
             _currentUserName.c_str(), perms, ntpOk ? 1 : 0, (unsigned long)now, SIMUT_VERSION);
    _server.send(200, "application/json", json);
}

void WebManager::handleApiNetwork() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_NET_CONFIG)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    SystemConfig& cfg = _storageRef->getConfig();

    WebConfigData* w = reinterpret_cast<WebConfigData*>(
        cfg.reserved + WEB_CONFIG_OFFSET);
    uint16_t currentPort = (w->port > 0) ? w->port : WEB_DEFAULT_PORT;

    /* F-NET-TIME.3a: expõe flags do overlay NetworkTimeData para a UI. */
    char json[640];
    char ipBuf[16], macBuf[18];
    _netRef->getIpAddress(ipBuf, sizeof(ipBuf));
    _netRef->getMacAddress(macBuf, sizeof(macBuf));
    snprintf(json, sizeof(json),
        "{\"connected\":%s,\"ip\":\"%s\",\"mask\":\"%s\",\"gw\":\"%s\","
        "\"dns\":\"%s\",\"mac\":\"%s\",\"ssid\":\"%s\",\"use_dhcp\":%s,"
        "\"static_ip\":\"%s\",\"static_mask\":\"%s\",\"static_gw\":\"%s\","
        "\"static_dns\":\"%s\",\"dns_auto\":%s,\"dns2\":\"%s\","
        "\"ntp_server\":\"%s\",\"ntp_enabled\":%s,\"web_port\":%u}",
        _netRef->isConnected() ? "true" : "false",
        ipBuf,
        _netRef->getSubnetMask().c_str(),
        _netRef->getGateway().c_str(),
        _netRef->getDns().c_str(),
        macBuf,
        cfg.wifiSsid,
        cfg.useDhcp ? "true" : "false",
        cfg.staticIp, cfg.staticMask, cfg.staticGateway, cfg.staticDns,
        _storageRef->isDnsAuto() ? "true" : "false",
        _storageRef->getSecondaryDns(),
        cfg.ntpServer,
        _storageRef->isNtpEnabled() ? "true" : "false",
        (unsigned)currentPort);

    _server.send(200, "application/json", json);
}


String WebManager::jsonEscape(const char* src) {
    String out;
    out.reserve(strlen(src) + 16);
    while (*src) {
        switch (*src) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += *src;   break;
        }
        src++;
    }
    return out;
}
void WebManager::handleApiConfig() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    SystemConfig& cfg = _storageRef->getConfig();

    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");

    char buf[256];

    snprintf(buf, sizeof(buf),
        "{\"name\":\"%s\",\"tz\":%d,\"log\":%s,\"res\":%d,\"s_int\":%lu,"
        "\"t_transport\":%d,\"t_sec\":%s,"
        "\"ntp_enabled\":%s,\"now_epoch\":%lu,",
        jsonEscape(cfg.deviceName).c_str(), cfg.timezoneOffset,
        cfg.loggingEnabled ? "true" : "false", cfg.ds18Resolution,
        (unsigned long)cfg.sampleIntervalMs, cfg.telTransport,
        cfg.telEncryption ? "true" : "false",
        _storageRef->isNtpEnabled() ? "true" : "false",
        (unsigned long)time(nullptr));
    if (!safeSend(buf)) return;

    snprintf(buf, sizeof(buf),
        "\"t_srv\":\"%s\",\"t_port\":%u,\"t_path\":\"%s\",\"t_key\":\"%s\",",
        jsonEscape(cfg.telServer).c_str(), cfg.telPort,
        jsonEscape(cfg.telPath).c_str(), jsonEscape(cfg.telApiKey).c_str());
    if (!safeSend(buf)) return;

    snprintf(buf, sizeof(buf),
        "\"m_topic\":\"%s\",\"m_cid\":\"%s\",\"m_user\":\"%s\","
        "\"m_qos\":%d,\"m_retain\":%s,\"m_ka\":%u,",
        jsonEscape(cfg.mqttTopic).c_str(), jsonEscape(cfg.mqttClientId).c_str(),
        jsonEscape(cfg.mqttUser).c_str(), cfg.mqttQos,
        cfg.mqttRetain ? "true" : "false", cfg.mqttKeepAlive);
    if (!safeSend(buf)) return;

    snprintf(buf, sizeof(buf),
        "\"t_int\":%lu,\"t_bat\":%d,\"t_mode\":%d,",
        (unsigned long)cfg.telInterval, cfg.telBatchSize, cfg.telMode);
    if (!safeSend(buf)) return;

    safeSend("\"t_glob\":\"");
    if (!safeSend(jsonEscape(cfg.telGlobalTemplate).c_str())) return;
    safeSend("\",\"t_line\":\"");
    if (!safeSend(jsonEscape(cfg.telLineTemplate).c_str())) return;
    safeSend("\",\"t_sep\":\"");
    if (!safeSend(jsonEscape(cfg.telLineSeparator).c_str())) return;
    safeSend("\",\"serial\":\"");
    if (!safeSend(jsonEscape(_storageRef->getBoardSerialNumber().c_str()).c_str())) return;
    safeSend("\",\"sensors\":[");
    for (int i = 0; i < MAX_SENSORS; i++) {
        snprintf(buf, sizeof(buf), "%s{\"hwid\":\"%s\",\"active\":%s}",
                 i == 0 ? "" : ",",
                 jsonEscape(cfg.sensors[i].hwId).c_str(),
                 cfg.sensors[i].active ? "true" : "false");
        if (!safeSend(buf)) return;
    }
    safeSend("]}");
}

void WebManager::handleApiUsers() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_USER_MGR)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    SystemConfig& cfg = _storageRef->getConfig();

    char json[512];
    int pos = 0;
    json[pos++] = '[';

    bool first = true;
    for (int i = 0; i < MAX_USERS; i++) {
        if (!cfg.users[i].active) continue;
        if (!first) json[pos++] = ',';
        first = false;
        pos += snprintf(json + pos, sizeof(json) - pos,
            "{\"id\":%d,\"name\":\"%s\",\"perms\":%u}",
            i, cfg.users[i].username, cfg.users[i].permissions);
    }
    json[pos++] = ']';
    json[pos] = '\0';

    _server.send(200, "application/json", json);
}

void WebManager::handleApiThemes() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_DASHBOARD)) {
        _server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
        return;
    }

    _server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");

    safeSend("[");
    char entry[64];

    for (int i = 0; i < getThemeCount(); i++) {
        char cleanName[32];
        int cn = 0;
        const char* p = availableThemes[i].displayName;
        if (p) {
            while (*p && cn < (int)sizeof(cleanName) - 1) {
                unsigned char c = (unsigned char)(*p);
                if (c >= 32 && c != '\"' && c != '\\') cleanName[cn++] = (char)c;
                p++;
            }
        }
        if (cn == 0) cn = snprintf(cleanName, sizeof(cleanName), "Tema %d", i);
        cleanName[cn] = '\0';

        snprintf(entry, sizeof(entry), "%s{\"id\":%d,\"name\":\"%s\"}", i > 0 ? "," : "", i, cleanName);
        if (!safeSend(entry)) return;
    }

    safeSend("]");
}


void WebManager::handleApiAlarms() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_SYS_CONFIG)) {
        _server.send(403, "application/json", "{\"error\":\"Forbidden\"}");
        return;
    }

    SystemConfig& cfg = _storageRef->getConfig();


    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");


    if (!safeSend("{\"sensors\":[")) return;

    bool first = true;


    if (cfg.ambientSensor.active) {
        char buf[256];
        snprintf(buf, sizeof(buf),
            "{\"idx\":-1,\"name\":\"Ambient\",\"type\":\"DHT22\",\"gpio\":%d,"
            "\"has_hum\":true,"
            "\"tmin\":%.1f,\"tmax\":%.1f,\"hmin\":%.1f,\"hmax\":%.1f,"
            "\"active\":%s}",
            cfg.ambientSensor.gpio,
            cfg.ambientSensor.tempMin, cfg.ambientSensor.tempMax,
            cfg.ambientSensor.humMin,  cfg.ambientSensor.humMax,
            cfg.ambientSensor.alarmsActive ? "true" : "false");
        if (!safeSend(buf)) return;
        first = false;
    }


    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!cfg.sensors[i].active) continue;
        if (!first) { if (!safeSend(",")) return; }
        first = false;


        bool hasHum = (cfg.sensors[i].rom[0] != 0x28);
        const char* typeName = hasHum ? "DHT22" : "DS18B20";


        String sName = cfg.sensors[i].friendlyName;
        sName.replace("\"", "\\\"");

        char buf[320];
        snprintf(buf, sizeof(buf),
            "{\"idx\":%d,\"name\":\"%s\",\"type\":\"%s\",\"gpio\":%d,"
            "\"has_hum\":%s,"
            "\"tmin\":%.1f,\"tmax\":%.1f,\"hmin\":%.1f,\"hmax\":%.1f,"
            "\"active\":%s}",
            i, sName.c_str(), typeName, cfg.sensors[i].gpio,
            hasHum ? "true" : "false",
            cfg.sensors[i].tempMin, cfg.sensors[i].tempMax,
            cfg.sensors[i].humMin,  cfg.sensors[i].humMax,
            cfg.sensors[i].alarmsActive ? "true" : "false");
        if (!safeSend(buf)) return;
    }

    if (!safeSend("],")) return;


    SoundSettingsState snd = _soundRef->getSettingsState();
    char sndBuf[280];
    snprintf(sndBuf, sizeof(sndBuf),
        "\"sounds\":{\"touch\":%s,\"confirm\":%s,\"error\":%s,"
        "\"alarm\":%s,\"web\":%s,\"mute\":%s,\"volume\":%d,\"alarmVolume\":%d,"
        "\"melTouch\":%d,\"melConfirm\":%d,\"melError\":%d,\"melAlarm\":%d}}",
        snd.touchEnabled   ? "true" : "false",
        snd.confirmEnabled ? "true" : "false",
        snd.errorEnabled   ? "true" : "false",
        snd.alarmEnabled   ? "true" : "false",
        snd.webEnabled     ? "true" : "false",
        snd.muted          ? "true" : "false",
        snd.volume,
        snd.alarmVolume,
        snd.touchMelody, snd.confirmMelody,
        snd.errorMelody, snd.alarmMelody);
    safeSend(sndBuf);
    safeSend("");
}
void WebManager::handleApiStatus() {
    uint16_t perms = getAuthPerms();
    if (!(perms & PERM_DASHBOARD)) { _server.send(403, "application/json", "{\"error\":\"Forbidden\"}"); return; }

    _server.sendHeader("Cache-Control", "no-cache");

    SystemConfig& cfg = _storageRef->getConfig();
    String ipStr = _netRef->getIpAddress();

    uint32_t heapTot = rp2040.getTotalHeap();
    uint32_t heapFree = rp2040.getFreeHeap();

    if (timeSince(_lastFsInfoRefresh, 10000) || _cachedFsTotalBytes == 0) {

        ReadGuard rg(_storageRef);
        FSInfo fs_info;
        fs_info.totalBytes = 0;
        fs_info.usedBytes = 0;
        LittleFS.info(fs_info);
        _cachedFsTotalBytes = fs_info.totalBytes;
        _cachedFsUsedBytes = fs_info.usedBytes;
        _lastFsInfoRefresh = millis();
    }

    time_t now = time(nullptr);
    bool ntp = _netRef->isTimeSynced();


    int pending = _telemetryRef
                ? static_cast<int>(_telemetryRef->getPendingEstimate())
                : -1;

    _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server.send(200, "application/json", "");

    char buffer[1024];

    String devName = cfg.deviceName;
    devName.replace("\"", "\\\"");

    uint32_t heapLargest = MetricsManager::instance().data().heapLargestBlock;
    snprintf(buffer, sizeof(buffer), "{\"sys\":{\"name\":\"%s\",\"uptime\":%lu,\"rssi\":%d,\"ip\":\"%s\",\"theme\":%d,\"heap_f\":%lu,\"heap_t\":%lu,\"heap_lb\":%lu,\"fs_u\":%lu,\"fs_t\":%lu,\"time\":%lu,\"ntp\":%d,\"pending\":%d},",
        devName.c_str(), millis(), _netRef->getRssi(), ipStr.c_str(), cfg.themeIndex,
        (unsigned long)heapFree, (unsigned long)heapTot, (unsigned long)heapLargest,
        (unsigned long)_cachedFsUsedBytes, (unsigned long)_cachedFsTotalBytes,
        (unsigned long)now, ntp ? 1 : 0, pending);

    if (!safeSend(buffer)) return;
    if (!safeSend("\"sensors\":[")) return;

    const auto& sensors = _sensorRef->getRuntimeSensors();
    bool first = true;
    for (const auto &s : sensors) {
        if (!s.config.active) continue;
        if (!first) { if (!safeSend(",")) return; }
        first = false;

        String sName = s.config.friendlyName;
        sName.replace("\"", "\\\"");

        String sId = s.config.hwId;
        sId.replace("\"", "\\\"");

        char valBuffer[16]; char humBuffer[32] = "";
        if (s.inErrorState) snprintf(valBuffer, sizeof(valBuffer), "\"Error\"");
        else if (isnan(s.avgValue1)) snprintf(valBuffer, sizeof(valBuffer), "\"--\"");
        else snprintf(valBuffer, sizeof(valBuffer), "%.2f", s.avgValue1);

        if (s.type == TYPE_DHT22 && !s.inErrorState && !isnan(s.avgValue2)) {
            snprintf(humBuffer, sizeof(humBuffer), ",\"hum\":%.1f", s.avgValue2);
        }

        snprintf(buffer, sizeof(buffer), "{\"gpio\":%d,\"id\":\"%s\",\"name\":\"%s\",\"val\":%s%s}", s.config.gpio, sId.c_str(), sName.c_str(), valBuffer, humBuffer);
        if (!safeSend(buffer)) return;
    }

    safeSend("]}");
    safeSend("");
}
