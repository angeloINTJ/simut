# Características de SIMUT

[English](FEATURES.md) | [Português](FEATURES.pt-BR.md) | [Español](FEATURES.es-ES.md)

Todo lo que hace el firmware, en detalle. El [README](../README.es-ES.md) tiene el resumen, y [Núcleo e interruptores](#núcleo-e-interruptores) dice cuáles de estas características puede dejar fuera una build propia.

## Sensores y alarmas
- **16 slots universales de sensor** — GP0–GP15. Cada slot acepta DS18B20, DHT22 o BMP280/BME280; el BMx280 se reclasifica solo por el ID del chip. Tipo y pines se asignan en tiempo de ejecución, sin recompilar.
- **Temperatura, humedad y presión** como canales de primera clase.
- **Calibración** — offsets por sensor y curvas de hasta 5 puntos por canal, lineales o suaves.
- **Validación de los sensores:**
  - verificación de la ROM del DS18B20, con la sonda cambiada en cuarentena hasta que vuelva la correcta;
  - histéresis de error: 3 fallos para entrar, 5 éxitos para salir;
  - lecturas fuera de rango descartadas.
- **Alarmas en todos los canales:**
  - límites mínimo y máximo por canal;
  - una alarma de fallo que salta aunque los límites del sensor estén desactivados;
  - un silencio de 120 s y un silencio global;
  - melodías en el zumbador y aviso visual en la pantalla.
- **Ventanas de mantenimiento** — por sensor, hasta 30 días, fijadas desde el panel o por un servidor. Mientras una está abierta, las alarmas quedan suprimidas, y su inicio y su fin se notifican como `maint_on` / `maint_off`.

## Panel táctil (`release`)
- **Panel táctil ILI9341 320×240** — dashboard, gráficos de histórico con banda mín/máx, estadísticas, calendario, ajustes.
- **Identidad en el panel** — el operador elige una cuenta y luego teclea el PIN de esa cuenta:
  - 32 cuentas, cada una con su PIN;
  - política de PIN configurable: longitud mínima, 1 a 3 glifos por tecla, dígitos o 0-9A-Z;
  - un teclado que se vuelve a barajar tras cada toque;
  - bloqueo por cuenta: el sexto fallo bloquea la cuenta, y 20 fallos en total bloquean el panel.
- **Administración en la pantalla:**
  - un elemento Usuarios crea cuentas y fija sus bits de permiso y sus PIN;
  - las 13 filas de Ajustes se filtran según lo que la cuenta puede hacer;
  - Ajustes → 8 arranca el punto de acceso de configuración;
  - Ajustes → 4 fija la fecha y la hora, y una unidad sin red configurada las pide al final del arranque.
- **Gestos en el panel superior** — un toque alterna mín/máx, mantener 3 s fija la selección.
- **Renderizado rápido con DMA** — composición en canvas sobre SPI a 62,5 MHz.
- **Área segura de 4 px en toda la UI** — el offset de alineación de pantalla (±4 px por eje) nunca recorta contenido.
- **Temas** — hasta 8 cargados desde LittleFS (11 vienen en `data/themes/`); el editor en `tools/theme-editor/` muestra la vista previa en un dispositivo real.
- **Sistema de sonidos** — clases Toque, Confirmación, Error, Alarma y Atención, 6 melodías cada una, con volúmenes separados para sistema y alarma.

## LCD de caracteres (`alpha`)
- **Lecturas** — recorre todos los slots y canales activos cada 3 s, con dígitos grandes para temperatura y humedad y una etiqueta `S<n>` que nombra el slot.
- **Punto de acceso de configuración** — muestra la dirección, el SSID y la clave, desplazando los valores largos.
- **Consola Bluetooth** — ver la nota de seguridad en [Entornos](../README.es-ES.md#entornos).
- **Envíos pendientes** — con un único sensor, la esquina inferior izquierda muestra el número de envíos pendientes (`N`, o `Nk` a partir de mil), y el icono de Wi-Fi crece de izquierda a derecha.

## Interfaz web
- **11 páginas** — comprimidas con gzip (zopfli) en la flash, con temas claro y oscuro que siguen la preferencia del sistema, gestor de archivos y sesiones multiusuario que caducan tras 15 minutos de inactividad.
- **Espejo del panel en vivo** (`release`) — el fotograma actual del panel en el navegador, 213 ms por fotograma. Un clic sobre él es un toque en la pantalla.
- **Cada cambio dice cuánto cuesta** — tres botones:
  - *Probar*: se aplica sin guardar;
  - *Aplicar ahora*: se guarda sin reiniciar;
  - *Guardar y reiniciar*.

  El propio dispositivo clasifica cada cambio con un ensayo (dry run) antes de que la página ofrezca los botones.
- **Reiniciar sin guardar** — un botón al final de la página de configuración reinicia el dispositivo y descarta lo que la página no guardó; vuelve la configuración grabada.
- **Versión en la página de inicio de sesión** — la versión del firmware aparece bajo el nombre antes de iniciar sesión.
- **Búsqueda de redes Wi-Fi** — elige la red de una lista, incluso desde dentro del punto de acceso de configuración.
- **Gráficos de histórico y exportación CSV en el navegador** — la página descarga los archivos binarios crudos de cada día y ella misma los decodifica, agrupa en cubetas (mín/máx/media) y exporta. La hora reciente, aún sin sellar, viene de `/api/history/open`. El renderizador de gráficos va embebido — sin CDN.
- **API HTTP** — 62 rutas. Cada una está protegida por un permiso o es pública por diseño, y el CI lo comprueba.

## Telemetría e integraciones
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

## Red y hora
- **Wi-Fi que se reconecta solo:**
  - una escalera de reintentos: 5 s, doblando hasta 120 s, luego latencia y una nueva ronda;
  - SSID ocultos y comprobaciones de la calidad de la señal;
  - IP estática, dos servidores DNS, un servidor NTP propio o un reloj manual, y puerto web configurable.
- **Punto de acceso de configuración:**
  - se llama `<nombre del dispositivo>_SETUP` — `simut_SETUP` de fábrica;
  - WPA2, con una clave por dispositivo que se muestra en la consola USB y en la pantalla de arranque del TFT;
  - portal cautivo en `http://192.168.4.1`.

  Solo se abre cuando alguien lo pide. Una unidad con la red caída sigue midiendo y reintentando la red. Tres formas de entrar:
  - Ajustes → 8 en el panel;
  - el comando `ap` de la consola (USB, o Bluetooth en el alpha y el Air);
  - mantener el panel pulsado 3 s durante el arranque.
- **NTP** — el intervalo entre reintentos crece de 20 s a 15 min, con fallback a `pool.ntp.org`. Hasta que el NTP sincroniza o alguien fija el reloj, un reloj provisional parte del registro más reciente guardado. El panel lo marca con `?` entre la fecha y la hora, y el primer ajuste después del arranque, por NTP o a mano, corrige los bloques del histórico que ese arranque empezó.

## Almacenamiento e histórico
- **Histórico binario compacto (V5)** — codificación delta + ancla a 5,38 bytes/registro, unos 116 días en el sistema de archivos de 1 MB (11 canales con cadencia de 1 minuto, medido en archivos de banco el 31/07/2026):
  - bloques de 60 registros, cada uno con su CRC;
  - el bloque abierto se guarda tras cada registro;
  - al superar el 86 % de ocupación, se borra el día más antiguo.
- **Configuración** — con CRC32, escrita en un archivo temporal y renombrada, con un `.bak` de reserva. Los secretos se guardan ofuscados, no cifrados: el acceso físico a la flash queda fuera del modelo de amenazas ([SECURITY.md §3](../SECURITY.md#3-secret-storage)).
- **Log de eventos** — 2 × 800 registros y 157 códigos de evento:
  - los eventos rutinarios se guardan en los cambios de estado, con un latido por hora y un recuento de lo suprimido;
  - los registros de seguridad, configuración y fallo fatal nunca se filtran.

## Seguridad
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
- **Auditorías** — las auditorías del 16/08/2026, de la v2.3.6-beta y del 07/09/2026 están cerradas. El último hallazgo, V-09 (una cuenta restringida podía crear otra con más bits de los que tenía), se corrigió en la v2.7.0, y tanto él como las correcciones del 07/09 se verificaron en hardware. Ver **[SECURITY.md](../SECURITY.md)**.

## Resiliencia y forense
- **Autopsia de cuelgues en cada arranque** — los registros scratch del watchdog indican qué módulo se colgó en cada núcleo. Desde la v2.7.0, tres registros más guardan también el módulo del Core 1, el heap libre y el tiempo en marcha en el momento del cuelgue.
- **Disciplina de flash entre núcleos** — el Core 1 se pausa en cada escritura de flash (medido, no supuesto).
- **Disciplina de watchdog** — el watchdog se alimenta en cada operación de archivos, así que los clientes HTTP lentos no bloquean el bucle.

## Actualización, backup y recuperación
- **OTA desde la página web:**
  - solo admin;
  - la imagen se comprueba antes de grabarla (tamaño, CRC del boot2, variante de imagen) y de nuevo en el arranque siguiente;
  - Wi-Fi, cuentas y slots de sensor se conservan, y el resto del sistema de archivos se reformatea, así que la página descarga antes un backup;
  - el dispositivo vuelve en menos de un minuto: 52–56 s en la campaña de la v2.7.0.
- **Backup y restauración** — todo el sistema de archivos en un archivo, con CRC32 y ligado al chip.
- **[Guía de recuperación](RECOVERY.md)** — rutas por BOOTSEL, picotool y 1200 bps para cada modo de fallo.

## SIMUT Air (experimental)
- **Dos modos, sin pantalla ni zumbador:**
  - **M0** es el despierto: web, consola, Bluetooth y sensores;
  - **M1** es el ciclo: duerme hasta la alarma del RTC, despierta, lee, graba el histórico y vuelve a dormir.
- **La radio solo cuando compensa** — solo se enciende cuando esperan `t_int` registros. Un despertar de lectura tarda 9,31 s con un DS18B20, y con un intervalo de 60 s el dispositivo está despierto cerca del 13 % del tiempo.
- **Pin del cargador** — un pin de detección de cargador (GP17 por defecto) lo mantiene despierto mientras está enchufado.
- **Consola completa** — es la única imagen publicada con la consola completa.

## Internacionalización
- **3 idiomas de interfaz** — inglés integrado; portugués (pt-BR) y español (es-ES) llegan como packs `.lng` en el sistema de archivos. Un dispositivo funciona en inglés más el pack instalado.

## Núcleo e interruptores

Toda imagen tiene lo que la tabla de abajo no lista: los slots de sensor, las alarmas y la línea de alarmas, el histórico grabado, las cuentas y los permisos, la interfaz web y la API HTTP por HTTP simple, la telemetría por HTTP simple, la consola de emergencia, el log de eventos, el backup y la restauración, y la actualización por el aire firmada. Dos cosas son una elección y no un interruptor: la pantalla (panel táctil, LCD 16×2 o ninguna) y el chip (el RP2040 de la Pico W, o el RP2350 de la Pico 2 W, que el CI compila y en el que aún no corre nada).

Todo lo demás es un interruptor en [`tools/features.toml`](../tools/features.toml). Esta tabla se genera de ese archivo con `tools/gen_features.py`, y el CI falla cuando se queda atrás. El [configurador de build](https://angelointj.github.io/simut/configurador/) compila una imagen con cualquier combinación que las reglas permiten y muestra cuánto cuesta cada interruptor en flash, medido.

<!-- BEGIN generated: switches — tools/gen_features.py -->
| Grupo | Interruptor | Qué hace | SIMUT | SIMUT Alpha | SIMUT Air |
|---|---|---|:---:|:---:|:---:|
| Red | Servidor web HTTPS | Sirve la interfaz web por TLS. Sin él, el dispositivo responde solo en HTTP. | sí | no | no |
|  | Encontrarlo por nombre en la red (mDNS) | Responde a &lt;nombre del dispositivo&gt;.local, para que el navegador lo encuentre sin la dirección IP. | sí | no | no |
|  | Consola serie por Bluetooth | Una consola protegida por contraseña, por Bluetooth clásico (SPP). Se anuncia durante los primeros 5 minutos después de encenderlo, así que llega a un dispositivo con el Wi-Fi aún sin configurar. | no | sí | sí |
| Telemetría e integraciones | Telemetría cifrada (HTTPS y MQTTS) | Envía al colector por TLS, y lo valida contra /cert.pem cuando hay uno. Sin ella, la telemetría va solo por HTTP o MQTT sin cifrar, y una configuración que pide cifrado se rechaza en vez de enviarse en claro. | sí | sí | sí |
|  | MQTT y Home Assistant | Publica en un broker MQTT (MQTT, o MQTTS con el interruptor de TLS), recibe las confirmaciones de la línea de alarmas en el tópico de ack y anuncia los sensores a Home Assistant por MQTT Discovery. Sin él, la telemetría va solo por HTTP. | sí | sí | sí |
|  | Métricas para Prometheus (/metrics) | Un endpoint /metrics en el formato de texto de Prometheus, con autenticación Basic para el recolector. | sí | sí | sí |
|  | Syslog remoto (registro de auditoría) | Reenvía los eventos del log a un colector syslog (RFC 5424 por UDP), para que el registro de auditoría sobreviva fuera del dispositivo. | sí | sí | sí |
| Interfaz | Página de histórico en la interfaz web | La página /history: gráficos, calendario, exportación CSV en el navegador y el visor de eventos. El dispositivo graba el histórico igual, y los archivos de histórico siguen descargables desde la página Archivos. | sí | sí | sí |
|  | API de exportación del histórico | Rutas que devuelven el histórico de varios sensores en una llamada (/api/history_multi) y exportan el histórico y el log como .simx. Ninguna página las usa; las herramientas de banco y las integraciones, sí. | sí | sí | sí |
|  | Línea de comandos completa | Todos los comandos por serie y Bluetooth. Sin ella queda solo la consola de emergencia. | no | no | sí |
|  | Gráfico y calendario en la pantalla táctil | El gráfico del histórico que se abre desde la franja de mín/máx, su pantalla de detalle y el calendario para elegir el día. Sin él, la franja no tiene el botón del gráfico; el histórico se sigue grabando y sigue en la web. | sí | no | no |
| Sensores | Sondas de temperatura DS18B20 | Sondas 1-Wire, una por pin, cada una reconocida por su número de serie. | sí | sí | sí |
|  | DHT22 temperatura y humedad | Un sensor por pin. | sí | sí | sí |
|  | BME280 / BMP280 temperatura, humedad y presión | Sensor I2C en un par de pines; la variante BMP280 no mide humedad. | sí | sí | sí |
| Sonido | Sonidos en el zumbador | Clics de toque, confirmaciones y sonidos de alarma en el zumbador (GP22). | sí | sí | no |
| Energía | Hibernación entre lecturas | El ciclo del Air: despierta, lee, envía, duerme. Solo existe sin pantalla. | no | no | sí |
| Banco y diagnóstico | Texto corto de la licencia en la pantalla táctil | La pantalla de Licencia del panel táctil muestra dos líneas que remiten al archivo LICENSE, en vez del texto MIT completo y los créditos. La página web /license mantiene siempre el texto completo. | no | no | sí |
|  | Trampa de concurrencia | Registra un error cuando una operación de flash empieza con el mutex de estado tomado. Para soaks, no para equipos en campo. | no | no | no |
<!-- END generated: switches -->
