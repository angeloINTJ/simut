<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/logo-wordmark-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/images/logo-wordmark.svg">
    <img src="docs/images/logo-wordmark.svg" alt="SIMUT" height="64">
  </picture>
</p>

# SIMUT — Sistema Integrado de Monitoramento Universal e Telemetria

> Sistema Integrado de Monitoreo Universal y Telemetría

> Monitoreo de temperatura, humedad y presión para la Raspberry Pi Pico W, con o sin red

[English](README.md) | [Português](README.pt-BR.md) | [Español](README.es-ES.md)

[![License: MIT](https://img.shields.io/badge/Licencia-MIT-1f6355?style=flat-square&labelColor=5f5b54)](LICENSE)
[![Platform: RP2040](https://img.shields.io/badge/Plataforma-RP2040-1f6355?style=flat-square&labelColor=5f5b54)](https://www.raspberrypi.com/products/raspberry-pi-pico/)
[![Framework: Arduino](https://img.shields.io/badge/Framework-Arduino-1f6355?style=flat-square&labelColor=5f5b54)](https://arduino-pico.readthedocs.io/)
[![CI](https://img.shields.io/github/actions/workflow/status/angeloINTJ/simut/build.yml?branch=main&label=CI&style=flat-square&labelColor=5f5b54)](https://github.com/angeloINTJ/simut/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/angeloINTJ/simut?label=Release&color=1f6355&style=flat-square&labelColor=5f5b54)](https://github.com/angeloINTJ/simut/releases/latest)
[![Docs](https://img.shields.io/badge/Docs-GitHub_Pages-1f6355?style=flat-square&labelColor=5f5b54)](https://angelointj.github.io/simut/)
[![Contributors](https://img.shields.io/badge/Contribuidores-5-1f6355?style=flat-square&labelColor=5f5b54)](#contribuidores)
[![Contribuciones bienvenidas](https://img.shields.io/badge/Contribuciones-bienvenidas-1f6355?style=flat-square&labelColor=5f5b54)](CONTRIBUTING.es-ES.md)

<p align="center">
  <img src="docs/images/tft-tour.gif" alt="Tour del TFT de SIMUT — dashboard, gráficos de histórico, calendario y ajustes" width="400">
</p>

## Descripción general

SIMUT es un firmware IoT para la **Raspberry Pi Pico W** que monitorea temperatura, humedad y presión en hasta 16 sensores, y sigue funcionando con o sin red. Un único código fuente genera tres dispositivos publicados:

- un **monitor con panel táctil** (TFT 320×240);
- un **monitor con LCD de caracteres** (16×2, el *alpha*);
- un **registrador a batería** que hiberna entre lecturas (el *Air*, experimental).

Los tres comparten el mismo núcleo:
- histórico binario en el dispositivo y alarmas por canal;
- 32 cuentas de usuario con 13 bits de permiso;
- interfaz web embebida;
- telemetría por HTTP(S) o MQTT(S), con una línea aparte para las alarmas;
- MQTT Discovery de Home Assistant, ruta `/metrics` para Prometheus y syslog remoto (RFC 5424);
- actualización por el aire y consola serie.

## Estado del proyecto

| | |
|---|---|
| **Release actual** | **v2.9.0** (02/10/2026), la primera release firmada; el [changelog](CHANGELOG.md) dice qué cambió cada versión. SIMUT salió de beta con la v2.7.0, sobre mediciones: un soak de 8,18 h sin ningún reinicio y 6 de 6 actualizaciones por el aire sin perder nada. |
| **Imágenes publicadas** | Tres imágenes, cada una en `.uf2` y `.bin`: `release` (panel táctil TFT), `alpha` (LCD 16×2 con consola Bluetooth) y `air` (registrador a batería sin pantalla). Junto a ellas van los packs de idioma pt-BR y es-ES y un manifiesto de OTA. Una imagen con otro conjunto de funciones sale del [configurador de build](https://angelointj.github.io/simut/configurador/), y el CI la compila desde `main`. |
| **Madurez** | <ul><li>`release`: **estable**.</li><li>`alpha`: publicado y probado en el banco, con su LCD 16×2 incluido desde el 26/09/2026.</li><li>`air`: **experimental**. Su único soak largo falló: un sueño en el ciclo 119 nunca despertó (F28). Hoy lo mitiga un watchdog a lo largo del despertar; la causa raíz no está confirmada.</li></ul> |
| **Pruebas** | Cada pull request ejecuta 562 casos de test en el host en 9 suites, 60 s de fuzzing y análisis estático, y compila las siete imágenes de firmware con la caché fría. El comportamiento en hardware real se verifica en un banco — ver [Verificación en hardware](docs/VERIFICATION.es-ES.md). |

**Limitaciones conocidas.** Cada una está documentada donde aplica.
- **Actualización.** La actualización por el aire reformatea el sistema de archivos:
  - Wi-Fi, cuentas y slots de sensor se conservan. El histórico, los archivos de calibración y los packs de idioma no, así que la página web descarga un backup antes de empezar, y restaurarlo los recupera.
  - Hay un único slot de firmware y ningún rollback. Un flasheo fallido se recupera con BOOTSEL y un cable USB.
  - Desde la v2.9.0, solo una imagen que el proyecto firmó se instala por el aire: el `.bin` de una release, o una compilación del configurador. Una compilación propia va por USB.
- **Reinicios sin explicación.** Un reinicio de watchdog (`ctx=209` o `ctx=455`) apareció tres veces en la imagen de prueba el 20–21 de septiembre, y no desde entonces; el que se capturó con su contexto tenía el Core 0 en la consola (`ctx=209`). Los dos núcleos están ahora instrumentados para explicar el siguiente. Un `ctx=455` (traza vacía) en el primer arranque después de `picotool load -x` no es esto: ese reinicio pasa por el watchdog, y el registro apareció tras 11 de 11 grabaciones así y tras ninguno de 7 reinicios por el pin (30/09/2026).
- **Conexiones inactivas.** Durante el soak de la v2.7.0, el 7,1 % de las respuestas en una conexión keep-alive inactiva llegaron cortadas. El dispositivo corta un flujo que no puede enviar durante 4 s.
- **Respuestas chunked.** Leído en un bucle cerrado, el 0,15–0,6 % de las respuestas de `/api/status` llega con el encuadre chunked roto ([#189](https://github.com/angeloINTJ/simut/issues/189)). El dispositivo no se reinicia y la petición siguiente funciona; la página pierde una actualización.
- **Lista de usuarios.** Cada guardado de la lista de usuarios reinicia el dispositivo, unos 25 s cada vez.
- **No es un instrumento certificado.** SIMUT no es un instrumento metrológico certificado. Valídalo contra tu propia referencia antes de confiar en él para almacenamiento regulado.

## ¿Por qué SIMUT?

| Necesidad | Sketch Arduino DIY | ESPHome / Tasmota | **SIMUT** |
|------|:---:|:---:|:---:|
| Autónomo con pantalla | Programación manual | Sin soporte TFT | UI táctil integrada, o LCD 16×2 |
| Entornos regulados | Sin traza de auditoría | Sin RBAC de usuarios | 32 cuentas, PIN de panel por cuenta, registro de eventos persistente y syslog remoto |
| Cadena de frío (sondas hasta −50 °C) | Lecturas básicas | Monitoreo básico | Multisensor calibrado, ventanas de mantenimiento |
| Operación offline | Sí | A menudo depende de la nube | Web local completa + pantalla |
| Actualización OTA | Reflasheo manual | OTA | OTA firmada + backup/restore |
| Seguridad | Ninguna | Básica | HMAC-SHA256, RBAC de 13 bits, bloqueos, HTTPS opcional |
| Home Assistant | Integración manual | Nativa | MQTT Discovery (opcional) |
| Métricas Prometheus | Ninguna | Integrado | Ruta `/metrics` |
| Registro de auditoría remoto | Ninguno | Complemento | Syslog (RFC 5424 / UDP) |

**SIMUT es para ti si:** necesitas un sistema de monitoreo de temperatura autónomo, seguro y auditable que funcione con o sin internet — típico de laboratorios, farmacias, bancos de sangre, almacenamiento de vacunas y cadenas de frío alimentarias.

**ESPHome/Tasmota pueden ser mejores si:** no necesitas pantalla local y prefieres configuración YAML a una interfaz web integrada. (Si lo que te retenía allí era Home Assistant: SIMUT habla MQTT Discovery.)

## Arquitectura

```
┌──────────────────────────────────────────────────────────┐
│                    Raspberry Pi Pico W                   │
│  ┌──────────────────────┐  ┌────────────────────────────┐│
│  │      Core 0          │  │        Core 1              ││
│  │  (Main Loop)         │  │  (Display Loop)            ││
│  │                      │  │                            ││
│  │  ◆ AppManager ───────┼──┼─ state/snapshots ──────┐   ││
│  │  ◆ SensorManager     │  │  ◆ DisplayManager ◄────┘   ││
│  │  ◆ WebManager        │  │  ◆ TouchPriority           ││
│  │  ◆ TelemetryManager  │  │  ◆ DMA canvas renderer     ││
│  │  ◆ CommandManager    │  │  ◆ Themes                  ││
│  │  ◆ StorageManager    │  │  ◆ i18n (EN/PT/ES packs)   ││
│  │  ◆ NetworkManager    │  │                            ││
│  └──────────┬───────────┘  └────────────────────────────┘│
│             │                                            │
│  ┌──────────┴──────────────────────────────────────────┐ │
│  │  Hardware Interfaces                                │ │
│  │  ◆ SPI → ILI9341 TFT 320×240 + XPT2046 Touch        │ │
│  │    (alpha: HD44780 16×2 LCD · Air: no display)      │ │
│  │  ◆ GP0–GP15 → 16 universal sensor slots:            │ │
│  │      DS18B20 (1-Wire) · DHT22 · BMP280/BME280 (I2C) │ │
│  │  ◆ USB CDC → serial console (+ Bluetooth: alpha/Air)│ │
│  │  ◆ WiFi (CYW43439) → HTTP(S) server + telemetry     │ │
│  └─────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
         │                   │                   │
    ┌────┴────┐          ┌───┴────┐         ┌────┴───────┐
    │ Sensors │          │ Web UI │         │  Telemetry │
    │ DS18B20 │          │ Browser│         │ HTTP(S) /  │
    │  DHT22  │          │ (RBAC) │         │  MQTT(S)   │
    │ BMx280  │          └────────┘         └────────────┘
    └─────────┘
```

## Capturas de pantalla

| Dashboard TFT | Gráfico de histórico en TFT | Dashboard web | Alpha inicial |
|:---:|:---:|:---:|:---:|
| ![Dashboard TFT](docs/images/screens/dashboard.png) | ![Gráfico TFT](docs/images/screens/graph.png) | ![Dashboard web](docs/images/web-dashboard.png) | [![Video del alpha](https://img.youtube.com/vi/wLjghqId8nE/hqdefault.jpg)](https://youtu.be/wLjghqId8nE) |

> Todas las pantallas del display, capturadas del framebuffer del panel real: [docs/images/screens/screens.md](docs/images/screens/screens.md).
>
> El video del **alpha inicial** muestra el primer prototipo TFT + táctil — la interfaz fue rediseñada desde entonces.

## Hardware

| Componente | Especificación |
|-----------|---------------|
| MCU | Raspberry Pi Pico W (RP2040, doble núcleo) |
| Pantalla | `release`: TFT ILI9341 320×240 (SPI, con DMA) · `alpha`: LCD de caracteres HD44780 16×2 (4 bits) · `air`: ninguna |
| Táctil | Pantalla táctil resistiva XPT2046 (`release`) |
| Sensores | **16 slots universales en GP0–GP15** — cualquier mezcla de DS18B20 (1-Wire), DHT22 y BMP280/BME280. El BMx280 es I²C y ocupa dos pines; dos de ellos pueden compartir un par (0x76/0x77) |
| Zumbador | Piezo pasivo (por PIO) — no existe en el Air |
| Almacenamiento | Flash interna de 2 MB (slot de firmware de 1 MB + LittleFS de 1 MB) |

Consulta la **[guía de cableado](docs/WIRING.md)** para el pinout completo y los diagramas de conexión.

> **PCB del SIMUT — diseño disponible para descarga** — el diseño de la placa en KiCad (`.kicad_pcb`, `.kicad_sch`) está en [`PCB_test/`](PCB_test/), y el paquete de fabricación listo para enviar a la fábrica (Gerbers + taladros PTH/NPTH, sin capas de pasta) está publicado como release público: **[simut-pcb-v1.1 — `simut_pcb_fabrication.zip`](https://github.com/angeloINTJ/simut/releases/tag/simut-pcb-v1.1)**.

## Características

- **Sensores y alarmas** — 16 slots universales de sensor (DS18B20, DHT22, BMP280/BME280) asignados en tiempo de ejecución, curvas de calibración, límites por canal, alarma de fallo y ventanas de mantenimiento.
- **Panel táctil** (`release`) — panel principal, gráficos del histórico y calendario, e identidad en el panel: la cuenta y luego su PIN.
- **LCD de caracteres** (`alpha`) — cada slot por turno, el punto de acceso de configuración y una consola Bluetooth.
- **Interfaz web** — 11 páginas, el espejo del panel en vivo, gráficos del histórico y exportación CSV en el navegador.
- **API HTTP** — 62 rutas. Cada una está protegida por un permiso o es pública por diseño, y el CI lo comprueba.
- **Telemetría** — HTTP, HTTPS, MQTT y MQTTS, en lotes por cantidad, y una segunda línea, con confirmación, para las alarmas; Home Assistant, Prometheus y syslog.
- **Red y hora** — Wi-Fi que se reconecta solo, punto de acceso de configuración abierto a pedido, y NTP con un reloj provisional hasta sincronizar.
- **Almacenamiento** — el histórico binario V5 (unos 116 días en 1 MB), la configuración con CRC32 y `.bak`, y un log de eventos con 156 códigos de evento.
- **Seguridad** — 32 cuentas, 13 bits de permiso, HMAC-SHA256 con salt, bloqueos por intentos y HTTPS opcional.
- **Actualización** — actualización por el aire firmada, desde la página web, y backup y restauración de todo el sistema de archivos.
- **SIMUT Air** (experimental) — registrador a batería que hiberna entre lecturas.
- **Idiomas** — inglés integrado; pt-BR y es-ES como packs de idioma.

Cada característica en detalle, y cuáles puede dejar fuera una build propia: **[docs/FEATURES.es-ES.md](docs/FEATURES.es-ES.md)**.

## Inicio rápido

### Requisitos previos
- [PlatformIO](https://platformio.org/) (Core 6.x o superior)
- `pip install zopfli` — opcional; las páginas web comprimen 2.888 B menos con él, y los presupuestos de flash se miden con él
- Raspberry Pi Pico W
- ¿Sin toolchain local? `docker compose run build` compila en un contenedor — la vía que [CONTRIBUTING.es-ES.md](CONTRIBUTING.es-ES.md) recomienda para nuevos contribuyentes

### Compilar y flashear

```bash
# Clonar el repositorio
git clone https://github.com/angeloINTJ/simut.git
cd simut

# Compilar el firmware
pio run -e pico_w_release

# Flashear la Pico W (auto-reset por toque de 1200 bps; BOOTSEL también funciona)
pio run -e pico_w_release -t upload

# Solo en el primer flasheo: subir los datos de LittleFS (packs de idioma, temas, favicon).
# Atención: uploadfs reformatea la partición LittleFS — en un dispositivo ya en
# servicio destruye histórico, configuración y calibración. No lo repitas
# cuando el dispositivo tenga datos; los packs de idioma pueden subirse
# después desde el gestor de archivos web. Copia los dos packs de idioma, y el
# dispositivo carga el primero en orden alfabético (es-ES): borra el otro.
pio run -e pico_w_release -t uploadfs
```

¿Prefieres no compilar? Cada [release](https://github.com/angeloINTJ/simut/releases/latest) incluye `simut_vX.Y.Z_release.uf2`, `_alpha.uf2` y `_air.uf2` (arrastrar y soltar con BOOTSEL presionado), el `.bin` correspondiente para la actualización por el aire y los packs de idioma pt-BR y es-ES. Una imagen con otro conjunto de funciones sale del [configurador de build](https://angelointj.github.io/simut/configurador/).

### Primer arranque
1. **Apunta la contraseña del admin.** Una unidad recién salida de fábrica imprime una contraseña de admin aleatoria de 8 caracteres **una sola vez en la consola serie USB** (115200 baudios). Nunca se guarda en texto plano. Si la pierdes, `system admin reset confirm` por USB imprime una nueva.
2. **Conéctalo a tu red.** El punto de acceso de configuración se abre cuando lo pides: escribe `ap` en la consola (en el Air, después de `enable`), o usa Ajustes → 8 en el panel táctil. De la v2.7.1 a la v2.8.0, una unidad sin red configurada lo abría sola; ya no lo hace.
   - Conéctate a `<nombre>_SETUP` (`simut_SETUP` de fábrica). Es WPA2, y su clave por dispositivo está en la respuesta de `ap`, en la consola USB y en el terminal de arranque del TFT.
   - El portal se abre en `http://192.168.4.1`.
   - El dispositivo sigue midiendo con el punto de acceso activo; solo la telemetría y el syslog esperan a la red. De la v2.7.1 a la v2.8.0 no leía sensores, no comprobaba alarmas ni grababa histórico mientras el punto de acceso estaba activo.

   Sin pantalla, puedes usar la consola: `system ssid <nombre>`, `system pass <clave>` y luego `reload confirm`. La consola corta en el primer espacio: una red o una clave con espacio solo por la página web.

   Sin red configurada, la unidad pide la fecha y la hora al final del arranque, porque sin red no hay NTP: el panel táctil abre una pantalla de fecha y hora (**OMITIR** la cierra, y Ajustes → 4 fija el reloj después), y cada imagen escribe en la consola una línea con `conf time AAAA-MM-DD HH:MM:SS`. Ese comando funciona en la consola de todas las imágenes, por USB o por Bluetooth: en el alpha y el Air, que no tienen panel, es la forma de responder a la pregunta. La sección **Date & Time** de la página web también fija el reloj.
3. **Abre la interfaz web** en la dirección que obtuvo el dispositivo — en la imagen `release`, también en `http://simut.local` — y entra como `admin` con la contraseña del paso 1. Se te pedirá elegir una nueva.
4. **Añade sensores** en **Config → Sensors & GPIO**, o deja que *Scan for probes* los encuentre.
5. **En el panel táctil**, Ajustes pide una cuenta y su PIN. El PIN de fábrica del admin es `1234`, y hay que cambiarlo en el primer uso.

## Estructura del proyecto

```
simut/
├── src/                    # Código del firmware (C++17)
│   ├── main.cpp            # Punto de entrada
│   ├── AppManager*         # Máquina de estados, arranque, alarmas, ciclo del Air
│   ├── DisplayManager*     # Panel TFT y LCD del alpha (Core 1), táctil, temas
│   ├── WebManager*         # Servidor web, API HTTP, sesiones, OTA, TLS
│   ├── StorageManager*     # LittleFS, configuración, cuentas, archivos de histórico
│   ├── SensorManager*      # Drivers DS18B20 / DHT22 / BMx280
│   ├── NetworkManager*     # Wi-Fi, escalera de reconexión, AP de configuración, mDNS, NTP
│   ├── TelemetryManager*   # Telemetría HTTP(S)/MQTT(S) y línea de alarmas
│   ├── CommandManager*     # Consola serie y Bluetooth
│   ├── LogManager*         # Log de eventos y forense de cuelgues
│   ├── HistoryV5.*         # Códec del histórico V5
│   ├── air/                # Configuración del SIMUT Air
│   ├── display/            # Teclados, fuentes y etiquetas comunes a las pantallas
│   ├── sensors/            # Tabla de canales, curvas de calibración
│   ├── ota/                # Preparación, validación y aplicación de la actualización
│   └── SystemDefs*.h       # Constantes y límites del sistema
├── data/                   # Assets de LittleFS (packs de idioma, temas, favicon)
├── PCB_test/               # Diseño de la PCB en KiCad + archivos de fabricación (Gerber/DRL)
├── test/                   # Tests unitarios nativos (Unity), nueve suites
├── tools/                  # Puertas de build, suites de banco, PicoHand, scripts de release, editor de temas
├── docs/                   # Documentación + sitio GitHub Pages
├── WebUI.h                 # Fuente de la web UI (se convierte en src/WebUI_GZ.h al compilar)
├── AGENTS.md               # Manual del banco: flasheo, el Air, mediciones (en portugués)
└── platformio.ini          # Configuración de build
```

## Compilación

### Entornos

| Entorno | Propósito | Publicado |
|-------------|---------|:---:|
| `pico_w_release` | Imagen de producción para el panel TFT: consola de emergencia, servidor HTTPS, mDNS | `…_release` |
| `pico_w_alpha` | LCD de caracteres 16×2 (HD44780), sin táctil; consola de emergencia + consola Bluetooth | `…_alpha` |
| `pico_w_air` | **Experimental** — SIMUT Air: sin pantalla, sin zumbador, ciclo de hibernación (M0 despierto / M1 despierta-lee-envía-duerme); consola completa + Bluetooth. Ver el §17 del [Manual de Usuario](docs/MANUAL.md) | `…_air` |
| `pico_w_test` | Imagen de banco: la consola completa para las suites de prueba; sin HTTPS, sin mDNS | — |
| `pico_w_test_https` | `pico_w_test` más el servidor HTTPS, para validar TLS; tres de sus páginas se sirven desde LittleFS para que quepa | — |
| `pico_w_asserts` | Release + aserciones de concurrencia | — |
| `pico2_w_release` | La release compilada para el Pico 2 W (RP2350), para que el CI la vea compilar. Aún no ha corrido en una placa | — |
| ocho entornos `native*` | Tests unitarios en el host — ver [Pruebas](#pruebas) | — |

> **Nota de seguridad para `pico_w_alpha` y `pico_w_air`:** las dos compilan la
> consola Bluetooth SPP (`SIMUT_BLUETOOTH=1`), así que en esas dos imágenes es
> superficie de ataque real. Se autentica con la **contraseña del admin de la
> web**, con un bloqueo exponencial que sobrevive a una reconexión, y una ventana
> de descubrimiento que se cierra 5 minutos después del arranque. Los comandos de
> recuperación están restringidos al USB.
>
> El punto de acceso de configuración es WPA2 en todas las imágenes, con una
> clave por dispositivo que se muestra en la consola y, donde la hay, en la
> pantalla. Ver [SECURITY.md](SECURITY.md) §2 y §8.

> No hay entorno de depuración. `pico_w_debug` se eliminó en la v2.4.1 tras no enlazar nunca: en `-Og` la imagen desbordaba el slot de 1020 KB en ~100 KB. La flash va justa. La imagen release usa el 97,2 % del slot de programa de 1.044.480 B (el valor medido vive en `tools/flash_budget.json`), y el CI comprueba cada `.bin`, con sus 241 B de firma, contra el techo de actualización por el aire, de 1.040.384 B. Un objetivo de GDB habría que montarlo recortando funcionalidades. Para el tripwire de concurrencia en hardware, usa `pico_w_asserts`.

### Flags de compilación
- `-Os` — optimización por tamaño
- `-Wall -Wextra`, y `-Werror` en `src/` (las bibliotecas de terceros no entran)
- `-specs=nano.specs` — newlib-nano para un binario más pequeño
- `-DNDEBUG` en todas las imágenes
- LTO deshabilitado (limitación del toolchain con Arduino-Pico de earlephilhower)
- El framework está fijado en arduino-pico 5.6.1 y recibe parches de `tools/arduino_pico_overrides/patch.sh`

## Configuración

### Consola (CLI)
La consola serie está disponible por USB (115200 baudios) y, en el alpha y el Air, por Bluetooth SPP.

- **La consola de emergencia** funciona en las imágenes `release` y `alpha`. Sus 15 comandos:
  - `show net status`, `show system info`, `show system log`
  - `debug on|off`
  - `system admin reset`, `system format`, `system factory`, `system https off`
  - `system ssid <nombre>`, `system pass <clave>`, `system cors <origen|off>`
  - `ap`, `time <fecha> <hora>`, `reload`, `help`

  Los comandos destructivos piden `confirm`, y las cuatro recuperaciones (`system factory`, `system format`, `system admin reset`, `system https off`) se rechazan por Bluetooth.
- **La consola completa estilo Cisco** (`enable` / `configure terminal`) funciona en la imagen `air` y en las imágenes de banco `pico_w_test` — ver el [manual de la CLI](docs/CLI-Manual.md) (en portugués). El Air añade `air status | hibernate | stop | idle <seg> | charger <gpio|off>`.

**Dónde se configura cada cosa:**
- **La interfaz web** es la herramienta del día a día.
- **El panel táctil** cubre lo que el operador necesita junto al dispositivo: temas, alarmas, sonidos, idioma, su propio PIN, usuarios, la política de PIN, la calibración del táctil, el ajuste de la pantalla, el estado, el punto de acceso de configuración y la fecha y la hora.

### API Web
El dispositivo expone una API REST en `http://<ip-del-dispositivo>/api/`:
- **62 rutas** — 52 protegidas por un permiso, 10 públicas por diseño, ninguna sin protección, comprobadas por `tools/check_authz.py` en el CI;
- la tabla de rutas está en el [manual de usuario](docs/MANUAL.md);
- [docs/AUTHORIZATION.md](docs/AUTHORIZATION.md) asocia cada ruta con su permiso;
- [docs/API_POST.md](docs/API_POST.md) documenta los cuerpos de los POST.

## Pruebas

### Tests en el host

```bash
pio test -e native             # validadores, cursor de telemetría, etiquetas, parsers, paquetes de idioma, las pantallas Licencia y de actualización, el menú de Ajustes, la verificación de contraseña (271 casos)
pio test -e native_history_v5  # códec del histórico V5 (62)
pio test -e native_cli         # parser de la CLI (33)
pio test -e native_logpolicy   # persistencia de log por transición, franjas de la autopsia, lectura del táctil (57)
pio test -e native_alarmqueue  # cola de la telemetría de alarmas (47)
pio test -e native_network     # máquina de estados de la reconexión Wi-Fi (36)
pio test -e native_air         # configuración persistente del SIMUT Air (16)
pio test -e native_sensors     # tabla de tipos de sensor (13)
pio test -e native_otasig      # firma de la imagen de OTA (27)

# Comprobaciones de referencia del códec V5 (Python vs C++, 20 mil casos aleatorios)
python3 tools/check_history_v5_parity.py --cases 20000
python3 tools/history_v5.py --selftest --trials 200000
```

### Integración continua

Cada push y pull request a `main` ejecuta cuatro jobs:
- **gates** — las suites del host más estas comprobaciones:
  - escaneo de secretos, tablas de códigos de log, matriz de autorización;
  - consistencia de la licencia, guarda del sistema de archivos, consistencia del Air;
  - tests de la fusión de días del histórico.
- **firmware** — las siete imágenes, compiladas con la caché fría:
  - cada una se comprueba contra su presupuesto de flash y el techo de actualización por el aire;
  - la propia compilación aplica el `-Werror` y las puertas de la interfaz web, la ayuda de la CLI, los códigos de log, la tabla de canales y los packs de idioma.
- **fuzz** — 60 s de libFuzzer contra los validadores de la API web, con oráculos de contrato.
- **análisis estático** — cppcheck, en una versión fijada.

`main` está protegida: nueve de estas comprobaciones tienen que pasar antes de cualquier merge, todos los jobs salvo la imagen `pico_w_test_https`.

### Verificación en hardware

El comportamiento se comprueba en un banco: una Pico W con el panel TFT y el táctil, y una segunda Pico, la *PicoHand*, que acciona las líneas RESET y BOOTSEL del objetivo. Cada medición, con fecha y números: **[docs/VERIFICATION.es-ES.md](docs/VERIFICATION.es-ES.md)**.

## Documentación

| Documento | Descripción |
|----------|-------------|
| [Manual de usuario (EN)](docs/MANUAL.md) | Montaje, pantalla/web/consola, OTA, referencia de la API, resolución de problemas — mantenido al día |
| [Manual do usuário (pt-BR)](docs/MANUAL.pt-BR.md) | El mismo manual, en portugués |
| [Manual completo (pt-BR)](docs/MANUAL.pt-BR.html) | El manual del producto en portugués, actualizado para la v2.9.0: 31 capítulos sobre instalación, configuración, uso diario e integración con servidores. Las pantallas se están recapturando; cada una que falta está marcada en su lugar |
| [Guía de cableado](docs/WIRING.md) | Pinout completo y diagramas de conexión |
| [Actualización por el aire](docs/OTA_USAGE.md) | Actualizar desde la página web, y lo que sobrevive |
| [Guía de recuperación](docs/RECOVERY.md) | Recuperación de brick — BOOTSEL, picotool, reset 1200 bps |
| [Manual de la CLI](docs/CLI-Manual.md) | Referencia completa de la consola, la del Air incluida (en portugués) |
| [Matriz de autorización](docs/AUTHORIZATION.md) | Cada ruta HTTP y el permiso que exige |
| [Política de seguridad](SECURITY.md) | Modelo de amenazas, manejo de credenciales, respuesta a incidentes |
| [Características](docs/FEATURES.es-ES.md) | Cada característica en detalle |
| [Verificación en hardware](docs/VERIFICATION.es-ES.md) | Lo medido en el banco, con fecha |
| [Índice de la documentación](docs/README.md) | Qué documentos se mantienen al día y cuáles son instantáneas |
| [Changelog](CHANGELOG.md) | Historial de versiones y cambios |

## Cómo se desarrolla SIMUT

La mayor parte de los cambios la escribe un agente de IA (Claude Code) en sesiones que dirige el mantenedor: del 1 de septiembre al 2 de octubre de 2026, 275 de los 371 commits de `main` llevaban una línea `Co-Authored-By: Claude`. Las instrucciones que siguen esas sesiones son [CLAUDE.md](CLAUDE.md) y [AGENTS.md](AGENTS.md) (en portugués). Las reglas son las mismas para todo cambio, lo escriba quien lo escriba:
- llega a `main` solo por pull request, después de nueve checks obligatorios: las nueve suites de test en el host, también bajo AddressSanitizer y UBSan, 60 s de fuzzing, análisis estático y seis imágenes de firmware compiladas con la caché fría;
- los tests van primero: una corrección trae una reproducción que falla antes y pasa después, y una refactorización muestra que el comportamiento no cambió;
- una afirmación sobre bytes, velocidad o una corrección trae su medición, y lo que no se comprobó en el hardware lo dice;
- las decisiones de producto, y la de hacer merge, son del mantenedor.

## Contribuir

Las contribuciones son bienvenidas. Lee [CONTRIBUTING.es-ES.md](CONTRIBUTING.es-ES.md) para el setup de desarrollo, las convenciones de código y el proceso de pull request.

Todos los contribuidores deben seguir el [Código de Conducta](CODE_OF_CONDUCT.es-ES.md).

## Soporte

- **Reportes de bugs:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=bug_report.md)
- **Solicitudes de funciones:** [GitHub Issues](https://github.com/angeloINTJ/simut/issues/new?template=feature_request.md)
- **Vulnerabilidades de seguridad:** ver [SECURITY.md](SECURITY.md) — no abras una issue pública
- **Preguntas:** abre una discusión o una issue

## Contribuidores

Gracias a todas las personas que han contribuido:

<!-- ALL-CONTRIBUTORS-LIST:START - Do not remove or modify this section -->
<!-- prettier-ignore-start -->
<!-- markdownlint-disable -->
<table>
  <tbody>
    <tr>
      <td align="center" valign="top" width="14.28%"><a href="https://github.com/angeloINTJ"><img src="https://avatars.githubusercontent.com/u/117550822?v=4?s=100" width="100px;" alt="Ângelo Moisés Alves"/><br /><sub><b>Ângelo Moisés Alves</b></sub></a><br /><a href="https://github.com/angeloINTJ/simut/commits?author=angeloINTJ" title="Code">💻</a> <a href="https://github.com/angeloINTJ/simut/commits?author=angeloINTJ" title="Documentation">📖</a> <a href="#design-angeloINTJ" title="Design">🎨</a> <a href="#hardware-angeloINTJ" title="Hardware">🔌</a> <a href="#security-angeloINTJ" title="Security">🛡️</a> <a href="#maintenance-angeloINTJ" title="Maintenance">🚧</a></td>
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

Este proyecto sigue la especificación [all-contributors](https://allcontributors.org).

## Powered by SIMUT

¿Tu producto o proyecto usa SIMUT? Añade esta insignia a tu README, documentación o página de producto:

```markdown
[![Powered by SIMUT](docs/images/powered-by-simut.svg)](https://github.com/angeloINTJ/simut)
```

[![Powered by SIMUT](docs/images/powered-by-simut.svg)](https://github.com/angeloINTJ/simut)

**Versión grande** (para presentaciones, pósteres o embalaje de producto):

```markdown
[![Powered by SIMUT](docs/images/powered-by-simut-large.svg)](https://github.com/angeloINTJ/simut)
```

[![Powered by SIMUT](docs/images/powered-by-simut-large.svg)](https://github.com/angeloINTJ/simut)

---

## Licencia

Licencia MIT — ver [LICENSE](LICENSE) para los detalles.

Las imágenes de firmware también llevan software de terceros. El [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
cita cada componente, el titular del copyright, la licencia y las imágenes que lo llevan, y la
carpeta [LICENSES/](LICENSES) guarda los textos de las licencias. El equipo muestra la misma lista
en la pantalla Licencia del panel y en la página `/license`.

Copyright © 2026 Ângelo Moisés Alves
