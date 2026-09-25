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
| **Release actual** | **v2.7.2** (24/09/2026). La línea 2.7 salió de beta con la v2.7.0, sobre mediciones: un soak de 8,18 h sin ningún reinicio y 6 de 6 actualizaciones por el aire sin perder nada. La v2.7.2 corrige el reloj del Air y los lotes largos de la telemetría, y dos pantallas del panel, y cada imagen queda 16 kB más pequeña. |
| **Imágenes publicadas** | Tres imágenes, cada una en `.uf2` y `.bin`: `release` (panel táctil TFT), `alpha` (LCD 16×2 con consola Bluetooth) y `air` (registrador a batería sin pantalla). Junto a ellas van los packs de idioma pt-BR y es-ES y un manifiesto de OTA. |
| **En `main`, aún sin release** | <ul><li>El Air conserva el reloj a través del sueño: las marcas de tiempo quedan en ±0,09 s en lugar de retrasarse 0,8 s por despertar.</li><li>La telemetría construye el payload un registro entero cada vez, así que una cola larga ya no sale como JSON inválido ni se salta registros.</li><li>Con un sensor, el LCD del alpha muestra el número de envíos de telemetría pendientes, y el icono de Wi-Fi crece de izquierda a derecha.</li></ul> |
| **Madurez** | <ul><li>`release`: **estable**.</li><li>`alpha`: publicado y probado en el banco, salvo la salida del LCD, que cubren los tests en el host — el banco no tiene HD44780.</li><li>`air`: **experimental**. Su único soak largo falló: un sueño en el ciclo 119 nunca despertó (F28). Hoy lo mitiga un watchdog a lo largo del despertar; la causa raíz no está confirmada.</li></ul> |
| **Pruebas** | Cada pull request ejecuta 408 casos de test en el host en 7 suites, 60 s de fuzzing y análisis estático, y compila las seis imágenes de firmware con la caché fría. El comportamiento en hardware real se verifica en un banco — ver [Verificación en hardware](#verificación-en-hardware). |

**Limitaciones conocidas.** Cada una está documentada donde aplica.
- **Actualización.** La actualización por el aire reformatea el sistema de archivos:
  - Wi-Fi, cuentas y slots de sensor se conservan. El histórico, los archivos de calibración y los packs de idioma no, así que la página web descarga un backup antes de empezar, y restaurarlo los recupera.
  - Hay un único slot de firmware y ningún rollback. Un flasheo fallido se recupera con BOOTSEL y un cable USB.
- **Reinicios sin explicación.** Un reinicio de watchdog con la traza vacía (`ctx=209`/`ctx=455`) apareció tres veces en la imagen de banco el 20–21 de septiembre, y no desde entonces. Los dos núcleos están ahora instrumentados para explicar el siguiente.
- **Conexiones inactivas.** Durante el soak de la v2.7.0, el 7,1 % de las respuestas en una conexión keep-alive inactiva llegaron cortadas. El dispositivo corta un flujo que no puede enviar durante 4 s.
- **Lista de usuarios.** Cada guardado de la lista de usuarios reinicia el dispositivo, unos 25 s cada vez.
- **Cursor de telemetría.** El cursor es una única marca de tiempo, así que un registro grabado fuera de orden en la flash se salta: 6 de 75.778 registros en una medición.
- **No es un instrumento certificado.** SIMUT no es un instrumento metrológico certificado. Valídalo contra tu propia referencia antes de confiar en él para almacenamiento regulado.

## ¿Por qué SIMUT?

| Necesidad | Sketch Arduino DIY | ESPHome / Tasmota | **SIMUT** |
|------|:---:|:---:|:---:|
| Autónomo con pantalla | Programación manual | Sin soporte TFT | UI táctil integrada, o LCD 16×2 |
| Entornos regulados | Sin traza de auditoría | Sin RBAC de usuarios | 32 cuentas, PIN de panel por cuenta, traza de auditoría firmada |
| Cadena de frío (sondas hasta −50 °C) | Lecturas básicas | Monitoreo básico | Multisensor calibrado, ventanas de mantenimiento |
| Operación offline | Sí | A menudo depende de la nube | Web local completa + pantalla |
| Actualización OTA | Reflasheo manual | OTA | OTA + backup/restore |
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

## Características principales

### Sensores y alarmas
- **16 slots universales de sensor** — GP0–GP15. Cada slot acepta DS18B20, DHT22 o BMP280/BME280; el BMx280 se reclasifica solo por el ID del chip. Tipo y pines se asignan en tiempo de ejecución, sin recompilar.
- **Temperatura, humedad y presión** como canales de primera clase.
- **Calibración** — offsets por sensor y curvas de hasta 5 puntos por canal, lineales o suaves.
- **Pipeline de sensores de confianza cero:**
  - verificación de la ROM del DS18B20, con la sonda cambiada en cuarentena hasta que vuelva la correcta;
  - histéresis de error: 3 fallos para entrar, 5 éxitos para salir;
  - lecturas fuera de rango descartadas.
- **Alarmas en todos los canales:**
  - límites mínimo y máximo por canal;
  - una alarma de fallo que salta aunque los límites del sensor estén desactivados;
  - un silencio de 120 s y un silencio global;
  - melodías en el zumbador y aviso visual en la pantalla.
- **Ventanas de mantenimiento** — por sensor, hasta 30 días, fijadas desde el panel o por un servidor. Mientras una está abierta, las alarmas quedan suprimidas, y su inicio y su fin se notifican como `maint_on` / `maint_off`.

### Panel táctil (`release`)
- **Panel táctil ILI9341 320×240** — dashboard, gráficos de histórico con banda mín/máx, estadísticas, calendario, ajustes.
- **Identidad en el panel** — el operador elige una cuenta y luego teclea el PIN de esa cuenta:
  - 32 cuentas, cada una con su PIN;
  - política de PIN configurable: longitud mínima, 1 a 3 glifos por tecla, dígitos o 0-9A-Z;
  - un teclado que se vuelve a barajar tras cada toque;
  - bloqueo por cuenta: el sexto fallo bloquea la cuenta, y 20 fallos en total bloquean el panel.
- **Administración en la pantalla:**
  - un elemento Usuarios crea cuentas y fija sus bits de permiso y sus PIN;
  - las 12 filas de Ajustes se filtran según lo que la cuenta puede hacer;
  - Ajustes → 12 arranca el punto de acceso de configuración.
- **Gestos en el panel superior** — un toque alterna mín/máx, mantener 3 s fija la selección.
- **Renderizado rápido con DMA** — composición en canvas sobre SPI a 62,5 MHz.
- **Área segura de 4 px en toda la UI** — el offset de alineación de pantalla (±4 px por eje) nunca recorta contenido.
- **Temas** — hasta 8 cargados desde LittleFS (11 vienen en `data/themes/`); el editor en `tools/theme-editor/` muestra la vista previa en un dispositivo real.
- **Sistema de sonidos** — clases Toque, Confirmación, Error, Alarma y Atención, 6 melodías cada una, con volúmenes separados para sistema y alarma.

### LCD de caracteres (`alpha`)
- **Lecturas** — recorre todos los slots y canales activos cada 3 s, con dígitos grandes para temperatura y humedad y una etiqueta `S<n>` que nombra el slot.
- **Punto de acceso de configuración** — muestra la dirección, el SSID y la clave, desplazando los valores largos.
- **Consola Bluetooth** — ver la nota de seguridad en [Entornos](#entornos).
- **En `main`, aún sin release** — con un único sensor, la esquina inferior izquierda muestra el número de envíos pendientes (`N`, o `Nk` a partir de mil), y el icono de Wi-Fi crece de izquierda a derecha.

### Interfaz web
- **11 páginas** — comprimidas con gzip (zopfli) en la flash, con temas claro y oscuro que siguen la preferencia del sistema, gestor de archivos y sesiones multiusuario que caducan tras 15 minutos de inactividad.
- **Espejo del panel en vivo** (`release`) — el fotograma actual del panel en el navegador, 213 ms por fotograma. Un clic sobre él es un toque en la pantalla.
- **Cada cambio dice cuánto cuesta** — tres botones:
  - *Probar*: se aplica sin guardar;
  - *Aplicar ahora*: se guarda sin reiniciar;
  - *Guardar y reiniciar*.

  El propio dispositivo clasifica cada cambio con un ensayo (dry run) antes de que la página ofrezca los botones.
- **Búsqueda de redes Wi-Fi** — elige la red de una lista, incluso desde dentro del punto de acceso de configuración.
- **Gráficos de histórico y exportación CSV en el navegador** — la página descarga los archivos binarios crudos de cada día y ella misma los decodifica, agrupa en cubetas (mín/máx/media) y exporta. La hora reciente, aún sin sellar, viene de `/api/history/open`. El renderizador de gráficos va embebido — sin CDN.
- **API HTTP** — 61 rutas. Cada una está protegida por un permiso o es pública por diseño, y el CI lo comprueba.

### Telemetría e integraciones
- **Cuatro transportes** — HTTP, HTTPS, MQTT y MQTTS:
  - payload en JSON, CSV o una plantilla personalizada;
  - TLS 1.2 (ECDHE con AES-GCM), con el certificado del servidor comprobado contra un `/cert.pem` subido al dispositivo.
- **Lotes por cantidad:**
  - `t_int` es el lote mínimo: la radio sigue apagada hasta que esperan tantos registros (0 = desactivado);
  - `t_bat` es el máximo por petición, un techo que la memoria libre puede bajar;
  - el tamaño del lote se adapta a éxitos y fallos, y el tiempo de respuesta del servidor marca el ritmo del siguiente.
- **Una segunda línea para alarmas:**
  - los eventos de alarma, fallo y mantenimiento viajan en su propia cola (32 por defecto, hasta 64);
  - cada evento sale de la cola solo cuando el servidor lo confirma (HTTP 2xx o un ack MQTT);
  - cada uno lleva el nombre de la cuenta que actuó.
- **Integraciones** — MQTT Discovery de Home Assistant (opcional), `/metrics` de Prometheus (sesión o HTTP Basic) y syslog remoto (RFC 5424 sobre UDP).
- **Enganches de flota:**
  - cabeceras de identidad `X-SIMUT-*` en los envíos;
  - en la imagen `release`, un servicio mDNS `_simut._tcp` con id, versión, imagen y TLS en su registro TXT;
  - tokens Bearer y un origen CORS configurable.

### Red y hora
- **Wi-Fi que se reconecta solo:**
  - una escalera de reintentos: 5 s, doblando hasta 120 s, luego latencia y una nueva ronda;
  - SSID ocultos y comprobaciones de la calidad de la señal;
  - IP estática, dos servidores DNS, un servidor NTP propio o un reloj manual, y puerto web configurable.
- **Punto de acceso de configuración:**
  - se llama `<nombre del dispositivo>_SETUP` — `simut_SETUP` de fábrica;
  - WPA2, con una clave por dispositivo que se muestra en la consola USB y en la pantalla de arranque del TFT;
  - portal cautivo en `http://192.168.4.1`.

  Cinco formas de entrar:
  - una unidad sin red configurada lo abre sola (salvo el Air);
  - un fallback automático lo abre cuando se pierde la red (salvo el Air);
  - Ajustes → 12 en el panel;
  - el comando `ap` de la consola (USB, o Bluetooth en el alpha y el Air);
  - mantener el panel pulsado 3 s durante el arranque.
- **NTP** — el intervalo entre reintentos crece de 20 s a 15 min, con fallback a `pool.ntp.org`. Hasta que sincroniza, un reloj provisional parte del registro más reciente guardado.

### Almacenamiento e histórico
- **Histórico binario compacto (V5)** — codificación delta + ancla a 5,38 bytes/registro, unos 116 días en el sistema de archivos de 1 MB (11 canales con cadencia de 1 minuto, medido en archivos de banco el 31/07/2026):
  - bloques de 60 registros, cada uno con su CRC;
  - el bloque abierto se guarda tras cada registro;
  - al superar el 86 % de ocupación, se borra el día más antiguo.
- **Configuración** — con CRC32, escrita en un archivo temporal y renombrada, con un `.bak` de reserva. Los secretos se guardan ofuscados.
- **Log de eventos** — 2 × 800 registros y 155 códigos de evento:
  - los eventos rutinarios se guardan en los cambios de estado, con un latido por hora y un recuento de lo suprimido;
  - los registros de seguridad, configuración y fallo fatal nunca se filtran.

### Seguridad
- **Cuentas y permisos:**
  - 32 cuentas, 13 bits de permiso;
  - nadie puede conceder un bit que no tiene;
  - backup, restauración, OTA e instalación de certificado exigen la máscara de admin completa.
- **Contraseñas:**
  - HMAC-SHA256, 5000 rondas, un salt aleatorio de hardware de 8 bytes por usuario y un pepper ligado a la placa;
  - una unidad recién salida de fábrica genera una contraseña de admin aleatoria de 8 caracteres, la imprime una sola vez en la consola USB y obliga a cambiarla en el primer inicio de sesión.
- **Límites contra fuerza bruta:**
  - bloqueo de login de 2 s a 300 s por cliente, con `429` cuando todos los slots de bloqueo están ocupados;
  - limitación por IP en las rutas pesadas;
  - la consola Bluetooth tiene su propio bloqueo exponencial y deja de anunciarse 5 minutos después del arranque.
- **Sesiones** — una cookie `HttpOnly; SameSite=Strict` (`Secure` sobre HTTPS), o un token Bearer.
- **Subidas** — path traversal, percent-encoding, bytes de control y nombres reservados se rechazan, y `/config` queda fuera del alcance del gestor de archivos.
- **HTTPS opcional** (`release`) — el par de certificado se instala con `POST /api/tls`. TLS 1.2, ECDHE con AES-GCM.
- **Auditorías** — las auditorías del 16/08/2026, de la v2.3.6-beta y del 07/09/2026 están cerradas. El último hallazgo, V-09 (una cuenta restringida podía crear otra con más bits de los que tenía), se corrigió en la v2.7.0, y tanto él como las correcciones del 07/09 se verificaron en hardware. Ver **[SECURITY.md](SECURITY.md)**.

### Resiliencia y forense
- **Autopsia de cuelgues en cada arranque** — los registros scratch del watchdog indican qué módulo se colgó en cada núcleo. Desde la v2.7.0, tres registros más guardan también el módulo del Core 1, el heap libre y el tiempo en marcha en el momento del cuelgue.
- **Disciplina de flash entre núcleos** — el Core 1 se pausa en cada escritura de flash (medido, no supuesto).
- **Disciplina de watchdog** — el watchdog se alimenta en cada operación de archivos, así que los clientes HTTP lentos no bloquean el bucle.

### Actualización, backup y recuperación
- **OTA desde la página web:**
  - solo admin;
  - la imagen se comprueba antes de grabarla (tamaño, CRC del boot2, variante de imagen) y de nuevo en el arranque siguiente;
  - Wi-Fi, cuentas y slots de sensor se conservan, y el resto del sistema de archivos se reformatea, así que la página descarga antes un backup;
  - el dispositivo vuelve en menos de un minuto: 52–56 s en la campaña de la v2.7.0.
- **Backup y restauración** — todo el sistema de archivos en un archivo, con CRC32 y ligado al chip.
- **[Guía de recuperación](docs/RECOVERY.md)** — rutas por BOOTSEL, picotool y 1200 bps para cada modo de fallo.

### SIMUT Air (experimental)
- **Dos modos, sin pantalla ni zumbador:**
  - **M0** es el despierto: web, consola, Bluetooth y sensores;
  - **M1** es el ciclo: duerme hasta la alarma del RTC, despierta, lee, graba el histórico y vuelve a dormir.
- **La radio solo cuando compensa** — solo se enciende cuando esperan `t_int` registros. Un despertar de lectura tarda 9,31 s con un DS18B20, y con un intervalo de 60 s el dispositivo está despierto cerca del 13 % del tiempo.
- **Pin del cargador** — un pin de detección de cargador (GP17 por defecto) lo mantiene despierto mientras está enchufado.
- **Consola completa** — es la única imagen publicada con la consola completa.

### Internacionalización
- **3 idiomas de interfaz** — inglés integrado; portugués (pt-BR) y español (es-ES) llegan como packs `.lng` en el sistema de archivos. Un dispositivo funciona en inglés más el pack instalado.

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

¿Prefieres no compilar? Cada [release](https://github.com/angeloINTJ/simut/releases/latest) incluye `simut_vX.Y.Z_release.uf2`, `_alpha.uf2` y `_air.uf2` (arrastrar y soltar con BOOTSEL presionado), el `.bin` correspondiente para la actualización por el aire y los packs de idioma pt-BR y es-ES.

### Primer arranque
1. **Apunta la contraseña del admin.** Una unidad recién salida de fábrica imprime una contraseña de admin aleatoria de 8 caracteres **una sola vez en la consola serie USB** (115200 baudios). Nunca se guarda en texto plano. Si la pierdes, `system admin reset confirm` por USB imprime una nueva.
2. **Conéctalo a tu red.** Una unidad sin red configurada abre sola su punto de acceso de configuración. El Air no: escribe `ap` en su consola.
   - Conéctate a `<nombre>_SETUP` (`simut_SETUP` de fábrica). Es WPA2, y su clave por dispositivo se imprime en la consola USB y en el terminal de arranque del TFT — al arrancar y, desde la v2.7.2, también cuando el AP se abre en operación. En un alpha, léela en la consola USB o en la respuesta del comando `ap`: según el código de la v2.7.2, el LCD no llega a sus páginas del AP.
   - El portal se abre en `http://192.168.4.1`.
   - Mientras el punto de acceso está activo, el dispositivo no mide: en la v2.7.2 no lee sensores, no comprueba alarmas ni graba histórico hasta entrar en una red.

   Sin pantalla, puedes usar la consola: `system ssid <nombre>`, `system pass <clave>` y luego `reload confirm`. La consola corta en el primer espacio: una red o una clave con espacio solo por la página web.
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
├── test/                   # Tests unitarios nativos (Unity), siete suites
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
| siete entornos `native*` | Tests unitarios en el host — ver [Pruebas](#pruebas) | — |

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

> No hay entorno de depuración. `pico_w_debug` se eliminó en la v2.4.1 tras no enlazar nunca: en `-Og` la imagen desbordaba el slot de 1020 KB en ~100 KB. La flash va justa. La imagen release usa el 97,2 % del slot de programa de 1.044.480 B, y su `.bin` queda 13.196 B por debajo del techo de actualización por el aire, de 1.040.384 B. Un objetivo de GDB habría que montarlo recortando funcionalidades. Para el tripwire de concurrencia en hardware, usa `pico_w_asserts`.

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

- **La consola de emergencia** funciona en las imágenes `release` y `alpha`. Sus 14 comandos:
  - `show net status`, `show system info`, `show system log`
  - `debug on|off`
  - `system admin reset`, `system format`, `system factory`, `system https off`
  - `system ssid <nombre>`, `system pass <clave>`, `system cors <origen|off>`
  - `ap`, `reload`, `help`

  Los comandos destructivos piden `confirm`, y las cuatro recuperaciones (`system factory`, `system format`, `system admin reset`, `system https off`) se rechazan por Bluetooth.
- **La consola completa estilo Cisco** (`enable` / `configure terminal`) funciona en la imagen `air` y en las imágenes de banco `pico_w_test` — ver el [manual de la CLI](docs/CLI-Manual.md) (en portugués). El Air añade `air status | hibernate | stop | idle <seg> | charger <gpio|off>`.

**Dónde se configura cada cosa:**
- **La interfaz web** es la herramienta del día a día.
- **El panel táctil** cubre lo que el operador necesita junto al dispositivo: temas, alarmas, sonidos, idioma, su propio PIN, usuarios, la política de PIN, la calibración del táctil, el ajuste de la pantalla, el estado y el punto de acceso de configuración.

### API Web
El dispositivo expone una API REST en `http://<ip-del-dispositivo>/api/`:
- **61 rutas** — 51 protegidas por un permiso, 10 públicas por diseño, ninguna sin protección, comprobadas por `tools/check_authz.py` en el CI;
- la tabla de rutas está en el [manual de usuario](docs/MANUAL.md);
- [docs/AUTHORIZATION.md](docs/AUTHORIZATION.md) asocia cada ruta con su permiso;
- [docs/API_POST.md](docs/API_POST.md) documenta los cuerpos de los POST.

## Pruebas

### Tests en el host

```bash
pio test -e native             # validadores, cursor de telemetría, etiquetas, parsers (185 casos)
pio test -e native_history_v5  # códec del histórico V5 (63)
pio test -e native_cli         # parser de la CLI (31)
pio test -e native_logpolicy   # persistencia de log por transición (45)
pio test -e native_alarmqueue  # cola de la telemetría de alarmas (39)
pio test -e native_network     # máquina de estados de la reconexión Wi-Fi (29)
pio test -e native_air         # configuración persistente del SIMUT Air (16)

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
- **firmware** — las seis imágenes, compiladas con la caché fría:
  - cada una se comprueba contra su presupuesto de flash y el techo de actualización por el aire;
  - la propia compilación aplica el `-Werror` y las puertas de la interfaz web, la ayuda de la CLI, los códigos de log, la tabla de canales y los packs de idioma.
- **fuzz** — 60 s de libFuzzer contra los validadores de la API web, con oráculos de contrato.
- **análisis estático** — cppcheck, en una versión fijada.

`main` está protegida: ocho de estas comprobaciones tienen que pasar antes de cualquier merge.

### Verificación en hardware

**El banco:**
- una Pico W con el panel TFT y el táctil;
- una segunda Pico, la *PicoHand*, que acciona las líneas RESET y BOOTSEL del objetivo, cronometra su línea de despierto/dormido y simula un cargador (ver [AGENTS.md](AGENTS.md), en portugués);
- suites de banco en `tools/` para la API web, el panel, la telemetría, la OTA, las caídas de Wi-Fi y el ciclo del Air.

Lo que se ha medido en hardware real, de lo más reciente a lo más antiguo:

| Fecha | Qué | Resultado |
|---|---|---|
| 24/09/2026 | Panel: Seguridad del PIN y Modo de Configuración (v2.7.2) | Las flechas del pie se quedan en la pantalla (la v2.7.1 la cerraba); un toque sin guardar ya no cambia la política grabada; Confirmar muestra la red, la clave y 192.168.4.1 (la v2.7.1 se quedaba en la confirmación, con el AP ya activo) |
| 23/09/2026 | Reloj del Air a través del sueño (v2.7.2) | Marcas de tiempo entre −0,085 y +0,030 s en 10 despertares (la v2.7.1 perdía 0,8 s por despertar); la corrección del NTP bajó de 9–10 s a 0,08 s |
| 23/09/2026 | Colas largas de telemetría en el Air (v2.7.2) | 0 cuerpos inválidos; 13.681 de 13.682 registros entregados despierto, 13.670 de 13.671 hibernando (v2.7.1: 68 de 69 cuerpos eran JSON inválido) |
| 22/09/2026 | Soak de la v2.7.0 | 8,18 h, 0 reinicios; el mayor bloque libre del heap varió −42 B |
| 22/09/2026 | Actualizaciones por el aire de la v2.7.0 | 6 de 6 aplicadas; 57 archivos restaurados, 0 registros perdidos |
| 22/09/2026 | Punto de acceso de configuración (v2.7.1) | Un cliente entra en 4,1 s, en `release` y en `alpha` con el Bluetooth activo, también con MAC aleatoria. El fallback automático se abre tras 6–7 min sin red |
| 22/09/2026 | Corrección del V-09 | 10 de 10 veredictos, con controles positivos |
| 21/09/2026 | Colector caído durante 3 h 58 min | 237 registros en cola, 0 reinicios; vaciada en una ronda con 0 perdidos, más 25 registros de la línea de alarmas |
| 21/09/2026 | Suites web | 67/67 como admin, 87/87 como cuenta restringida; 500 commits que escriben en la flash, 0 reinicios |
| 21/09/2026 | Búsqueda de redes Wi-Fi | 18 de 18, 0,94 s por barrido, también desde dentro del punto de acceso |
| 20/09/2026 | Cuentas, PIN y política en el panel | 32/32 |
| 19/09/2026 | Espejo del panel | 613 → 213 ms por fotograma; idéntico al framebuffer píxel a píxel (0 de 76.800 distintos) |
| 11/09/2026 | Corte de corriente durante una actualización | Solo la ventana de aplicación, de ~25 s, deja el dispositivo necesitando BOOTSEL |
| 10/08/2026 | Histórico a través de reinicios | 10 de 10 reinicios por hardware y 10 de 10 reinicios perdieron 0 registros |

El LCD 16×2 es la única salida no validada en la pantalla real. El banco no tiene HD44780, así que los tests en el host comprueban lo que recibe.

## Documentación

| Documento | Descripción |
|----------|-------------|
| [Manual de usuario (EN)](docs/MANUAL.md) | Montaje, pantalla/web/consola, OTA, referencia de la API, resolución de problemas — mantenido al día |
| [Manual do usuário (pt-BR)](docs/MANUAL.pt-BR.md) | El mismo manual, en portugués |
| [Manual completo (pt-BR)](docs/MANUAL.pt-BR.html) | El manual del producto en portugués, actualizado para la v2.7.2: 31 capítulos sobre instalación, configuración, uso diario e integración con servidores. Las pantallas se están recapturando; cada una que falta está marcada en su lugar |
| [Guía de cableado](docs/WIRING.md) | Pinout completo y diagramas de conexión |
| [Actualización por el aire](docs/OTA_USAGE.md) | Actualizar desde la página web, y lo que sobrevive |
| [Guía de recuperación](docs/RECOVERY.md) | Recuperación de brick — BOOTSEL, picotool, reset 1200 bps |
| [Manual de la CLI](docs/CLI-Manual.md) | Referencia completa de la consola, la del Air incluida (en portugués) |
| [Matriz de autorización](docs/AUTHORIZATION.md) | Cada ruta HTTP y el permiso que exige |
| [Política de seguridad](SECURITY.md) | Modelo de amenazas, manejo de credenciales, respuesta a incidentes |
| [Índice de la documentación](docs/README.md) | Qué documentos se mantienen al día y cuáles son instantáneas |
| [Changelog](CHANGELOG.md) | Historial de versiones y cambios |

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

Copyright © 2026 Ângelo Moisés Alves
