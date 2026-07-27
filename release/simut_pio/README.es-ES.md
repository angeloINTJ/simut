# SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria

> Sistema Integrado de Monitorización Universal y Telemetría

> Firmware IoT de grado profesional para Raspberry Pi Pico W

[English](README.md) | [Português](README.pt-BR.md) | **Español**

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Platform: RP2040](https://img.shields.io/badge/Platform-RP2040-green.svg)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-teal.svg)](https://arduino-pico.readthedocs.io/)
[![CI](https://github.com/angeloINTJ/simut/actions/workflows/build.yml/badge.svg)](https://github.com/angeloINTJ/simut/actions/workflows/build.yml)
[![Version](https://img.shields.io/badge/Version-v1.6.1--beta-blue.svg)](https://github.com/angeloINTJ/simut/releases)
[![Docs](https://img.shields.io/badge/Docs-GitHub_Pages-34D058.svg)](https://angelointj.github.io/simut/)
[![Contributors](https://img.shields.io/badge/All_Contributors-5-orange.svg)](CONTRIBUTORS.md)
[![Contributions Welcome](https://img.shields.io/badge/Contributions-Welcome-brightgreen.svg)](CONTRIBUTING.md)

<p align="center">
  <img src="docs/images/tft-demo.gif" alt="SIMUT TFT Demo" width="320">
</p>

## Resumen

SIMUT es un firmware IoT de grado profesional para la **Raspberry Pi Pico W** que proporciona monitoreo en tiempo real de temperatura y humedad a través de una arquitectura de doble núcleo. Cuenta con un panel táctil TFT local, una interfaz web integrada con control de acceso basado en roles (RBAC), subida de telemetría (HTTP/MQTT), una CLI accesible a través de USB y Bluetooth, y un sistema externo de paquetes de idiomas.

## ¿Por qué SIMUT?

| Necesidad | DIY Arduino Sketch | ESPHome / Tasmota | **SIMUT** |
|------|:---:|:---:|:---:|
| Independiente con pantalla | ⚠️ Programación manual | ❌ Sin soporte TFT | ✅ Interfaz táctil integrada |
| Entornos regulados | ❌ Sin rastro de auditoría | ❌ Sin RBAC de usuario | ✅ Multiusuario, registros de auditoría |
| Cadena de frío (-80°C a +45°C) | ⚠️ Lecturas básicas | ✅ Monitoreo básico | ✅ Multisensor calibrado |
| Funcionamiento sin conexión | ✅ Sí | ❌ A menudo dependiente de la nube | ✅ Web local completa + pantalla |
| Actualizaciones OTA | ❌ Flasheo manual | ✅ OTA | ✅ OTA + copia de seguridad/restauración |
| Seguridad | ❌ Ninguna | ⚠️ Básica | ✅ HMAC-SHA256, RBAC, rate limiting |

**SIMUT es para ti si:** necesitas un sistema de monitoreo de temperatura independiente, seguro y auditable que funcione con o sin internet — típico en laboratorios, farmacias, bancos de sangre, almacenamiento de vacunas y cadenas de frío de alimentos.

**ESPHome/Tasmota podría ser mejor si:** ya tienes Home Assistant, no necesitas una pantalla local y prefieres la configuración en YAML en lugar de una interfaz web integrada.

## Arquitectura

```
┌──────────────────────────────────────────────────────────┐
│                    Raspberry Pi Pico W                   │
│  ┌──────────────────────┐  ┌────────────────────────────┐│
│  │       Núcleo 0       │  │       Núcleo 1             ││
│  │   (Bucle Principal)  │  │     (Bucle de UI)          ││
│  │                      │  │                            ││
│  │  ◆ AppManager ───────┼──┼─ state/snapshots ──────┐   ││
│  │  ◆ SensorManager     │  │  ◆ DisplayManager ◄────┘   ││
│  │  ◆ WebManager        │  │  ◆ TouchPriority           ││
│  │  ◆ TelemetryManager  │  │  ◆ Themes (50 built-in)    ││
│  │  ◆ CommandManager    │  │  ◆ i18n (PT/EN/ES)         ││
│  │  ◆ StorageManager    │  │                            ││
│  │  ◆ NetworkManager    │  │                            ││
│  └──────────┬───────────┘  └────────────────────────────┘│
│             │                                            │
│  ┌──────────┴──────────────────────────────────────────┐ │
│  │  Interfaces de Hardware                             │ │
│  │  ◆ SPI → ILI9341 TFT 320×240 + XPT2046 Touch        │ │
│  │  ◆ 1-Wire (PIO) → DS18B20 (hasta 16)                │ │
│  │  ◆ Data → DHT22 (hasta 16)                           │ │
│  │  ◆ I2C → BME280 T+H+P (hasta 8)                      │ │
│  │  ◆ USB CDC → CLI Serial                             │ │
│  │  ◆ Bluetooth (BLE) → CLI Remoto                     │ │
│  │  ◆ WiFi (CYW43439) → Servidor HTTP + Telemetría     │ │
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
         │                   │                  │
    ┌────┴────┐          ┌───┴────┐         ┌────┴───────┐
    │ Sensores│          │ Web UI │         │ Telemetría │
    │ DS18B20 │          │Navegador│        │ Servidor   │
    │   DHT22 │          │ (RBAC) │         │  HTTP/MQTT │
    └─────────┘          └────────┘         └────────────┘
```

## Capturas de pantalla

| Panel TFT | Demo TFT | Interfaz Web | Versión Alpha Temprana |
|:---:|:---:|:---:|:---:|
| ![TFT](docs/images/tft-dashboard.png) | ![Demo](docs/images/tft-demo.gif) | ![Web](docs/images/web-dashboard.png) | [![Alpha video](https://img.youtube.com/vi/wLjghqId8nE/hqdefault.jpg)](https://youtu.be/wLjghqId8nE) |

> 📸 Consulta [docs/images/README.md](docs/images/README.md) para saber cómo hacer capturas de pantalla desde tu dispositivo.
>
> 🎥 El vídeo de la **Versión Alpha Temprana** muestra el primer prototipo TFT + táctil. La interfaz de usuario, los temas, la capacidad de respuesta y el nivel de pulido han evolucionado significativamente desde entonces — mira el GIF de la **Demo TFT** actual para ver la experiencia de hoy.

## Hardware

| Componente | Especificación |
|-----------|---------------|
| MCU | Raspberry Pi Pico W (RP2040) |
| Pantalla | ILI9341 320×240 TFT (SPI) |
| Panel táctil | Pantalla táctil resistiva XPT2046 |
| Sensores | DS18B20 (1-Wire, hasta 16), DHT22 (hasta 16), BME280 (I2C, hasta 8) — slots configurables |
| Zumbador | Piezoeléctrico pasivo (impulsado por PIO) |
| Almacenamiento | 2 MB de memoria flash interna |

Consulta la **[Guía de Cableado](docs/WIRING.md)** para ver los diagramas completos de pines y conexiones.

## Características Principales

### Detección y Control
* **Soporte multisensor** — hasta 16 sensores en slots configurables: DS18B20 (1-Wire), DHT22 (Data), BME280 (I2C, T+H+P)
* **Flujo de sensores Zero-trust** — verificación de ROM cada 5 lecturas, detección de discrepancias de hardware, histéresis de errores
* **Alarmas por sensor** — umbrales de temperatura/humedad con melodías en el zumbador y respuesta visual en la pantalla TFT
* **Interfaz web de calibración** — modo de calibración restringido por `PERM_CALIB`; la entrada de valor de referencia calcula el offset automáticamente
* **Calibración ambiental mediante picoUID** — `calib.csv` admite ID, nombre y offsets personalizados para el DHT22

### Pantalla e Interfaz de Usuario
* **Pantalla TFT ILI9341 de 320×240** — panel de control, gráficos en tiempo real, estadísticas, ajustes táctiles (XPT2046)
* **Programador con prioridad táctil** — la entrada de la interfaz de usuario siempre prevalece sobre las operaciones en segundo plano
* **50 temas integrados** + hasta 8 temas personalizados cargados desde LittleFS (editor sin conexión en `tools/theme-editor/`)
* **Diseño dinámico del panel** — basado en ranuras (slots), adaptable a temas
* **Renderizado atómico de pantalla** — composición fuera de pantalla basada en canvas, sin tearing
* **Sistema de sonido** — clases Táctil / Confirmación / Error / Alarma / Atención con melodías y volumen configurables
* **Temas claro y oscuro** para la interfaz web con persistencia en `localStorage`

### Conectividad y Web
* **Servidor web integrado** — sesiones multiusuario, RBAC (10 bits de permisos), administrador de archivos, panel en vivo
* **WebUI comprimida con gzip** — páginas integradas minificadas con CSS/JS compartido, almacenables en caché por el navegador
* **Cambio de contraseña de autoservicio** en la pantalla de inicio de sesión con medidor de seguridad
* **Telemetría** — HTTP POST y MQTT con JSON / CSV / plantillas personalizadas, soporte TLS/SSL, tamaño de lote adaptativo
* **Gráfico de historial multisensor** — endpoint que devuelve múltiples series en una sola respuesta; rango configurable
* **Exportación CSV** — paquete binario `.simx` con magic, versión, tabla de sensores, registros y tráiler CRC32
* **Exportación en fragmentos con reintento adaptativo** — división ante fallos con recuperación automática

### CLI y Bluetooth
* **CLI de doble canal** — USB Serial + Bluetooth con sesiones protegidas por contraseña
* **Nombre de dispositivo BT personalizado** — configurable vía web/CLI
* **Registro con volcado diferido** durante el inicio de sesión BT para evitar la contención de la flash

### Tiempo y Almacenamiento
* **Sincronización de hora NTP** — retroceso exponencial, respaldo multiservidor, RTC virtual con corrección automática
* **Entrada manual de tiempo** a través de la interfaz web cuando no hay NTP disponible
* **Códec de historial** — codificación delta + sensor-mask + anclaje para almacenamiento binario compacto
* **LittleFS** — configuración dual-bank con CRC32, archivos de historial, registro compacto rotativo

### Seguridad
* **Autenticación reforzada** — HMAC-SHA256 con sal aleatoria por usuario, 5000 iteraciones, hash de 128 bits
* **Contraseña de administrador aleatoria al restablecer de fábrica** — de 8 caracteres mostrada en TFT, nunca persistida en la flash
* **Limitador de tasa** — LRU de 16 ranuras con TTL de 15 min, expulsión consciente de bloqueos, retroceso exponencial
* **Subidas seguras contra salto de directorios** — `..`, codificación por porcentaje, bytes de control y caracteres reservados bloqueados
* **`SECURITY.md`** con modelo de amenazas, política de rotación y respuesta a incidentes

### Resiliencia y Análisis Forense
* **Análisis forense de bloqueos** — perfilador de caja negra con autopsia del registro scratch del watchdog
* **Ruta de reinicio seguro** — restablecimiento amigable con USB que mantiene el puerto serial accesible
* **Detección de pánico leve** — monitorización de estado entre núcleos
* **Disciplina del Watchdog** — se alimenta en cada operación de LittleFS y durante las operaciones de flash

### Actualizaciones OTA
* **Actualización de firmware OTA** — subida de nuevo firmware a través de la interfaz web, aplicado en el lugar preservando la configuración
* **Copia de seguridad y restauración** — copia de seguridad/restauración completa de LittleFS con verificación de integridad CRC32
* **Preservación de configuración basada en instantáneas** — las configuraciones críticas sobreviven a la aplicación del firmware

### Internacionalización
* **2 idiomas de visualización** — Inglés (integrado) + Portugués/Español mediante paquetes de idiomas externos
* **Paquetes de idiomas cargables en caliente** desde LittleFS
* **Respaldo i18n en línea** para claves no persistidas en los archivos de idioma del dispositivo

## Requisitos de Hardware

| Componente | Especificación |
|-----------|---------------|
| MCU | Raspberry Pi Pico W (RP2040) |
| Pantalla | ILI9341 TFT 320×240 (SPI) |
| Panel táctil | XPT2046 (SPI) |
| Sensores | DS18B20 (1-Wire) + DHT22 |
| Almacenamiento | 2 MB flash (1 MB firmware + 1 MB LittleFS) |
| Zumbador | Piezoeléctrico pasivo (impulsado por PIO) |

## Inicio Rápido

### Requisitos Previos
* [PlatformIO](https://platformio.org/) (Core 6.x o posterior)
* Raspberry Pi Pico W

### Compilar y Flashear

```bash
# Clone the repository
git clone https://github.com/angeloINTJ/SIMUT.git
cd SIMUT

# Build firmware
pio run -e pico_w_release

# Flash to Pico W (hold BOOTSEL, connect USB)
pio run -e pico_w_release -t upload

# Upload LittleFS data (language packs, favicon)
pio run -e pico_w_release -t uploadfs
```

### Primer Arranque
1. El dispositivo arranca y muestra la pantalla de configuración en la TFT.
2. Se muestra una contraseña de administrador aleatoria de 8 caracteres en la TFT.
3. Conéctate al punto de acceso WiFi de SIMUT o conéctate vía USB Serial a 115200 baudios.
4. Inicia sesión a través de la interfaz web (`http://simut.local` o la IP del dispositivo).

## Estructura del Proyecto

```
SIMUT/
├── src/                    # All source code
│   ├── main.cpp            # Entry point
│   ├── AppManager*.cpp/h   # Application state machine
│   ├── DisplayManager*.cpp/h  # TFT display, touch, themes
│   ├── WebManager*.cpp/h   # Web server, API, OTA endpoints
│   ├── StorageManager.cpp/h   # LittleFS, config, history
│   ├── SensorManager.cpp/h # DS18B20 and DHT22 drivers
│   ├── NetworkManager.cpp/h   # WiFi, mDNS
│   ├── TelemetryManager.cpp/h # MQTT and HTTP telemetry
│   ├── CommandManager.cpp/h   # CLI parser (USB + Bluetooth)
│   ├── LogManager.cpp/h    # Logging and crash forensics
│   ├── SystemDefs*.h       # System constants and limits
│   └── ota/                # OTA update subsystem
├── data/                   # LittleFS assets
│   ├── favicon.ico
│   └── lang/               # Language packs
├── test/                   # Unit tests (Unity framework)
├── tools/                  # Build and development tools
├── docs/                   # Documentation
├── platformio.ini          # Build configuration
├── WebUI.h                 # Web UI source (compressed at build time)
└── LICENSE
```

## Compilación

### Entornos

| Entorno | Descripción |
|-------------|-------------|
| `pico_w_release` | Firmware de producción (por defecto) |
| `pico_w_debug` | Compilación de depuración con registros adicionales |
| `native` | Pruebas unitarias en el host (Unity) |

### Opciones de Compilación 
* `-Os` — optimizar para tamaño (la flash está muy ajustada al ~98.7%)
* `-Wall -Wextra` — advertencias elevadas
* `-specs=nano.specs` — newlib-nano para un binario más pequeño
* LTO está desactivado (limitación del toolchain con earlephilhower Arduino-Pico)

## Configuración

### Comandos CLI
Una interfaz de línea de comandos está disponible a través de USB Serial (115200 baudios) y Bluetooth. Grupos de comandos principales:

* `help` — mostrar los comandos disponibles
* `conf system` — ver/editar la configuración del sistema
* `conf sensor` — ver/editar la configuración de los sensores
* `conf net` — ver/editar la configuración de red
* `conf user` — administrar cuentas de usuario
* `write memory` — guardar cambios en la flash
* `reload` — reiniciar el dispositivo

### API Web
El dispositivo expone una API REST en `http://<device-ip>/api/`. Consulta la [Guía de Uso de OTA](docs/OTA_USAGE.md) para ver los endpoints específicos de OTA.

## Documentación

| Documento | Descripción |
|----------|-------------|
| [Manual del Usuario](docs/MANUAL.md) | Configuración completa de hardware, guía de pantalla/web/CLI, resolución de problemas |
| [Guía de Actualización OTA](docs/OTA_USAGE.md) | Actualización de firmware de forma inalámbrica vía interfaz web o curl |
| [Guía de Recuperación](docs/RECOVERY.md) | Recuperación de un brickeo tras un fallo de OTA — BOOTSEL y picotool |
| [Política de Seguridad](SECURITY.md) | Modelo de amenazas, manejo de credenciales, respuesta a incidentes |
| [Registro de Cambios (Changelog)](CHANGELOG.md) | Historial de versiones y cambios en las funcionalidades |

## Pruebas

```bash
# Run unit tests (validators, CRC, float conversion, time logic)
pio test -e native
```

## Contribuir

¡Las contribuciones son bienvenidas! Por favor, lee [CONTRIBUTING.md](CONTRIBUTING.md) para conocer la configuración del entorno de desarrollo, las convenciones de código y el proceso de pull requests.

Todos los contribuyentes deben seguir el [Código de Conducta](CODE_OF_CONDUCT.md).

## Soporte

* **Reportes de errores:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=bug_report.md)
* **Peticiones de funciones:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=feature_request.md)
* **Vulnerabilidades de seguridad:** Consulta [SECURITY.md](SECURITY.md) — no abras un issue público
* **Preguntas:** Abre una discusión o un issue

## Contribuyentes ✨

Gracias a estas maravillosas personas:

<!-- ALL-CONTRIBUTORS-LIST:START - Do not remove or modify this section -->
<!-- prettier-ignore-start -->
<!-- markdownlint-disable -->
<table>
  <tbody>
    <tr>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/angeloINTJ"><img src="https://avatars.githubusercontent.com/u/117550822?v=4?s=100" width="100px;" alt="Angelo Moises Alves"/><br /><sub><b>Angelo Moises Alves</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=angeloINTJ" title="Code">💻</a> <a href="https://github.com/angeloINTJ/simut/commits?author=angeloINTJ" title="Documentation">📖</a> <a href="#design-angeloINTJ" title="Design">🎨</a> <a href="#hardware-angeloINTJ" title="Hardware">🔌</a> <a href="#security-angeloINTJ" title="Security">🛡️</a> <a href="#maintenance-angeloINTJ" title="Maintenance">🚧</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/LorenzoLongaretto"><img src="https://avatars.githubusercontent.com/u/165825895?v=4?s=100" width="100px;" alt="Lorenzo Longaretto"/><br /><sub><b>Lorenzo Longaretto</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=LorenzoLongaretto" title="Tests">🧪</a> <a href="https://github.com/angeloINTJ/simut/commits?author=LorenzoLongaretto" title="Code">💻</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/JohnMartin0301"><img src="https://avatars.githubusercontent.com/u/112761826?v=4?s=100" width="100px;" alt="John Martin"/><br /><sub><b>John Martin</b></sub></a><br /><a href="#infra-JohnMartin0301" title="Infrastructure">🚇</a> <a href="https://github.com/angeloINTJ/simut/commits?author=JohnMartin0301" title="Code">💻</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/f-p-0"><img src="https://avatars.githubusercontent.com/u/239882173?v=4?s=100" width="100px;" alt="f p"/><br /><sub><b>f p</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=f-p-0" title="Documentation">📖</a></td>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/drmikecrypto"><img src="https://avatars.githubusercontent.com/u/91358784?v=4?s=100" width="100px;" alt="Mike"/><br /><sub><b>Mike</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=drmikecrypto" title="Code">💻</a> <a href="https://github.com/angeloINTJ/simut/commits?author=drmikecrypto" title="Tests">🧪</a> <a href="https://github.com/angeloINTJ/simut/commits?author=drmikecrypto" title="Documentation">📖</a></td>
    </tr>
  </tbody>
</table>
<!-- markdownlint-restore -->
<!-- prettier-ignore-end -->
<!-- ALL-CONTRIBUTORS-LIST:END -->

Este proyecto sigue la especificación de [all-contributors](https://allcontributors.org).

## Impulsado por SIMUT

¿Está tu producto o proyecto utilizando SIMUT? Añade esta insignia a tu README, documentación o página del producto:

```markdown
[![Powered by SIMUT](docs/images/powered-by-simut.svg)](https://github.com/angeloINTJ/simut)
```

[![Powered by SIMUT](docs/images/powered-by-simut.svg)](https://github.com/angeloINTJ/simut)

**Versión grande** (para presentaciones, pósters o empaques de productos):

```markdown
[![Powered by SIMUT](docs/images/powered-by-simut-large.svg)](https://github.com/angeloINTJ/simut)
```

[![Powered by SIMUT](docs/images/powered-by-simut-large.svg)](https://github.com/angeloINTJ/simut)

---

## Licencia

Licencia MIT — consulta el archivo [LICENSE](LICENSE) para más detalles.

Copyright © 2026 Angelo Moises Alves