# Glosario de Etiquetas — SIMUT

*Las etiquetas de versión como v3.x/v4.x en entradas históricas se refieren al esquema de numeración pre-1.0 del proyecto, conservadas por procedencia; el firmware actual sigue la línea 2.x.*

[English](GLOSSARY.md) | [Português](GLOSSARY.pt-BR.md) | **Español**

> Diccionario de etiquetas inline utilizadas en comentarios del código fuente. Consulta este archivo antes de interpretar cualquier etiqueta como `F-*`, `BUG-*`, `SEC-*`, `Patch X`, `Fase N`, `#N` encontrada en las fuentes.

## Cómo usar

Cada etiqueta aparece en comentarios con el formato:

```cpp
/* F-LOCKOUT-STUCK: explicación breve de lo que resuelve este bloque. */
/* Patch C: conversión con signo para tolerar condición de carrera entre núcleos. */
```

Este glosario decodifica el significado de cada etiqueta. Si encuentras una etiqueta no listada aquí, puede ser nueva — agrégala siguiendo el patrón a continuación.

---

## Etiquetas de Funcionalidad (F-*)

| Etiqueta | Descripción |
|-----|-----------|
| `F-LOCKOUT-STUCK` | Refactorización entre núcleos: Core 0 no podía adquirir `multicore_lockout` mientras Core 1 estaba en una ruta pesada. Solución: modo silencioso cooperativo con reinicio forzado de Core 1 (`multicore_reset_core1`) + relanzamiento. Reinicio táctil en cada lanzamiento, TFT begin solo en el primero. |
| `F-I18N-TRIM.1` | Reducción de 8 a 2 idiomas en la pantalla TFT (EN + PT) para ahorrar flash. Firmware desde v3.22.0. |
| `F-I18N-TRIM.2` | Eliminación de 6 idiomas obsoletos (es/de/fr/it/ru/zh) de `WebUI.h`, consistente con F-I18N-TRIM.1. |
| `F-NET-TIME.1` | Superposición `NetworkTimeData` en `reserved[28..47]` — datos de configuración de red/hora. |
| `F-NET-TIME.2` | Consumidor en `NetworkManager` — flags DNS/NTP leídos desde la superposición. |
| `F-NET-TIME.3a` | Back-end web: GET/POST `/api/set_time` + `setManualTime()`. |
| `F-NET-TIME.3b` | Front-end: página `/network` con DNS separado + `/config` con fecha/hora + i18n PT. |
| `F-NET-TIME.4` | CLI: comandos `conf ntp`, `conf time`, `conf net dns` + tokenizador 5→6 slots. |
| `F-NET-TIME.5a` | Auto-reset de cursor en el futuro + pista `t_int=0` en la UI. |
| `F-NET-TIME.5b` | Cierre — regresión acumulada a lo largo de la ruta F-NET-TIME. |
| `F-IP-FIX` | Corrección de firma de `WiFi.config`: arduino-pico usa `(ip, dns, gateway, subnet)`, diferente de ESP32. |
| `F-BT-LOGIN` | Escritura flash diferida en inicio de sesión Bluetooth: `LOG_CODE` almacenado en RAM durante `_btMgr.update()`, eliminando bloqueo de Core 1 + riesgo de reinicio WDT. |

## Etiquetas de Error (BUG-*)

| Etiqueta | Descripción |
|-----|-----------|
| `BUG-002` | Orden de publicación entre núcleos: el productor debe escribir **datos** antes de la **bandera**, con `__dmb()` entre ellos. El consumidor lee bandera + `__dmb()` + datos. |
| `BUG-003` | Fragmentación granular de `enterFlashSafeMode`/`exitFlashSafeMode`. Antes: 1 bloqueo cubría todo. Ahora: 1 bloqueo por operación LittleFS, Core 1 renderiza entre fragmentos. Macro `FLASH_OP(...)` en `StorageManager.cpp`. |
| `BUG-004` | El mutex `_stateMutex` puede fallar `mutex_try_enter`; usar `_lastWebBusy` como respaldo fijo — evita parpadeo del overlay "web busy". |
| `BUG-005` | `captureBootSnapshot()` público + llamada explícita en `LogManager::begin()`. `setModule` ya no captura oportunísticamente. Guarda `_autopsyPerformed` en `performCrashAutopsy()` evita autopsia falsa en llamadas subsecuentes a `begin()` (ej.: `clear log`). |

## Etiquetas de Seguridad (SEC-*)

| Etiqueta | Descripción |
|-----|-----------|
| `SEC-001` | Path traversal en carga: `isSafeUploadFilename()` bloquea `..`, `\`, `:`, `<`, `>`, `|`, `?`, `*`, `%`, caracteres de control, len>64. |
| `SEC-002` | Elusión de `replace("..","")` con `....` o `%2e%2e`: rechazo por `indexOf("..")>=0` + `indexOf('%')>=0`. |
| `SEC-003` | Contraseña de admin aleatoria de 8 caracteres `[A-Z2-9]` vía `rp2040.hwrand32()` en factory reset. Texto plano en RAM (`_initialAdminPassword[9]`), nunca en flash. |
| `SEC-004` | PIN `1234` fuerza cambio: bandera `FLAG_MUST_CHANGE_PIN` en `reserved[26..27]`, superposición `SetupFlagsData` con magic `0xBE`. |
| `SEC-005` | Límite de línea CLI: `CLI_LINE_MAX=256`, helper `appendCharWithLimit()`, anti-spam de advertencias. |
| `SEC-006` | Desalojo LRU de `_loginStates[16]` preserva slots con bloqueo activo no expirado. |
| `SEC-007` | Hash de contraseña 120→128 bits (32 caracteres hex). Migración transparente: login detecta hash antiguo (30 chars), valida con truncado, re-hash silencioso. |
| `SEC-008` | `PASSWORD_HMAC_ROUNDS` — número de rondas HMAC-SHA256 para hashing de contraseñas. |
| `SEC-009` | Salt aleatorio por usuario (`UserAccount.salt[8]`) vía `hwrand32()`. Incremento de `CONFIG_VERSION` con rutina de migración. |

## Etiquetas de Consistencia (CON-*)

| Etiqueta | Descripción |
|-----|-----------|
| `CON-001` | Bloque autoritativo `SCRATCH REGISTER MAP` en `LogManager.cpp` — mapa único de los scratch registers del watchdog. |
| `CON-002` | Enum `LanguageCode` con `LANG_EN`, `LANG_PT`, centinela `LANG_COUNT` + `static_assert` contra `LANG_NAMES`. |
| `CON-003` | Encabezados "8 languages" → "2 languages (EN + PT)" en `DisplayManager.{h,cpp}`. |
| `CON-004` | `_lastSavedCrc` como miembro privado de `StorageManager` (era `static` local). Omite guardado cuando CRC32 es idéntico. |
| `CON-005a` | `LoginState.nonce` como `char[65]` en lugar de `String`. |
| `CON-005b` | `CliDemand.strVal1/strVal2` como `char[64]` con helpers `setStrVal1/setStrVal2`. |
| `CON-006` | `DS18B20_CONVERSION_TIME_MS`, `DHT22_READ_TIMEOUT_MS` movidos de macros locales a `SystemDefs.h`. |

## Etiquetas de Documentación (DOC-*)

| Etiqueta | Descripción |
|-----|-----------|
| `DOC-002` | Constantes de temporización con nombre: `BOOT_WAIT_DOT_INTERVAL_MS`, `ALARM_ROTATE_INTERVAL_MS`, `ALARM_FLASH_INTERVAL_MS`, `WEB_NOTIFY_DURATION_MS`. |
| `DOC-003` | `SECURITY.md` en la raíz con modelo de amenazas, defensas, operaciones y respuesta a incidentes. |

## Etiquetas de Refactorización (REF-*)

| Etiqueta | Descripción |
|-----|-----------|
| `REF-004` | Singleton `TouchPriority` con `setProvider`/`isActive` — reemplaza 3 setters + 3 miembros + 3 lambdas duplicadas en 5 managers. |
| `REF-007` | Descomposición de `handleApiLogin` (~130 líneas) en 6 helpers: `findLoginStateForIp`, `checkLockout`, `validateNonce`, `verifyPasswordFor`, `allocSessionSlot`, `completeLogin`. |

## Parches (Patch X)

| Etiqueta | Descripción |
|-----|-----------|
| `Patch A` | Autopsia: guarda `elapsed` real (tiempo desde último heartbeat) en `scratch[7]` en lugar de `now - moduleStartTime`. |
| `Patch B` | (Reservado — no implementado) |
| `Patch C` | Conversión con signo `(int32_t)(now - lastBeat)` en `checkCrossCoreHealth` y `AppManager:446`. Tolera condición de carrera entre núcleos donde `lastBeat` está ligeramente adelantado → subdesbordamiento ≈ UINT32_MAX causaba falso positivo de pánico suave. |

## Fases (Fase N)

| Etiqueta | Descripción |
|-----|-----------|
| `Fase 1` | Gráfico: eliminar puntos antiguos que salieron de la ventana de visualización. |
| `Fase 2` | Gráfico: leer nuevos registros del archivo binario de historial. |
| `Fase 3` | Gráfico: recalcular estadísticas (ignorando NaNs). |
| `Fase 4` | Diferimiento de CLI durante toque: comandos USB/BT encolados en buffer circular de 2 slots mientras el toque está activo. Drenaje 1-por-ciclo al inicio de `update()`. |
| `Fase 5` | Vaciado coordinado post-toque: en la transición toque-activo→toque-libre, dispara vaciado en serie de logs → registro hist → cursor. Cierra la ventana de "datos en RAM no en flash" de minutos a <100ms. |

## Tickets Numéricos (#N)

| Etiqueta | Descripción |
|-----|-----------|
| `#4` | Sufijo `confirm` obligatorio en CLI para comandos sensibles. |
| `#5` | Ofuscación de campos sensibles en logs y consola. |
| `#7` | Paridad CLI ↔ Web: comandos disponibles en ambos canales. |
| `#8` | Marca de agua alta del heap: seguimiento del mínimo histórico de heap libre. |
| `#11` | Bandera `intVal1Valid` en `CliDemand` para distinguir "no informado" de "valor 0". |

## Otros

| Etiqueta | Descripción |
|-----|-----------|
| `U3` | Puerto web configurable (no hardcoded 80). |
| `U11` | Heartbeat de fallo suprimido: 1 log/hora después de supresión de logs de telemetría. |
| `U14` | Falso positivo del watchdog: subdesbordamiento en resta sin signo entre núcleos. Corregido con conversión con signo (Patch C). |
| `U15` | Alimentaciones WDT entre operaciones LittleFS en `writeCompactToFlash`/`flushPendingLogs`. |
| `U16` | Ráfagas de guardado web: watchdog 30s, omisión CRC, límite de frecuencia 1s, rastreador de cambios del lado del cliente. |
| `U17` | Notificación toast en UI web: infraestructura compartida en 9 páginas, i18n PT. |
| `U18` | Diferimiento de CLI durante toque (Fase 4): buffer circular de 2 slots, drenaje post-toque. |
| `U19` | Vaciado post-toque (Fase 5): logs, registro hist, cursor — cierra ventana de vulnerabilidad. |
| `U23` | Instrumentación de autopsia: `MOD_SAVE_CONFIG`, `MOD_LOG_FLASH`, `MOD_HIST_FLASH`, `MOD_CORE1_LOCK` + `TraceScope` RAII. |
| `U24` | Commit-all por lotes: interfaz web acumula cambios en `sessionStorage`, botón único "Guardar y Reiniciar". |
| `U25` | Escritura flash diferida en login Bluetooth: `setForceBuffer(true/false)` envolviendo `_btMgr.update()`. |
| `WEB-001` | Escape JSON en `/api/ls`: nombre de archivo y directorio escapados para bytes de control (0x00-0x1F/0x7F). |
| `N9` | Certificado TLS >16 KB rechazado en el arranque para evitar OOM. |
| `F12.1–F12.5` | Endurecimiento de seguridad (ver SEC-001 a SEC-005). |
| `F13.1–F13.4` | Errores latentes (ver BUG-002 a BUG-005). |
| `F15.1–F15.2.a` | Migración de hash: desalojo LRU (SEC-006), incremento de esquema v14→v15 (SEC-009). |

---

## Cómo agregar nuevas etiquetas

1. Elige un prefijo consistente con la categoría: `F-` para funcionalidades, `BUG-` para errores, `SEC-` para seguridad, `CON-` para consistencia, `DOC-` para documentación, `REF-` para refactorización, `EXT-` para hallazgos externos.
2. Agrega la etiqueta a este archivo con una línea describiendo lo que significa.
3. Usa la etiqueta en comentarios inline en el formato: `/* TAG: descripción breve. */`
4. Mantén este glosario como fuente autoritativa — si una etiqueta queda obsoleta, márcala con `[OBSOLETO]` en lugar de eliminarla.

> **Nota:** Las etiquetas `EXT-*` se refieren a hallazgos de auditoría externa. Se convierten al prefijo apropiado (`F-`, `SEC-`, etc.) cuando se implementan.
