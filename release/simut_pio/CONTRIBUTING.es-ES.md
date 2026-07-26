# Contribuir a SIMUT

[English](CONTRIBUTING.md) | [Português](CONTRIBUTING.pt-BR.md) | **Español**

¡Gracias por tu interés en contribuir! SIMUT es un firmware IoT de código abierto para la Raspberry Pi Pico W. Así es como puedes ayudar.

[English](CONTRIBUTING.md) | [Português](CONTRIBUTING.pt-BR.md) | **Español**

## Antes de empezar

- **Abre un issue primero** para discutir tu idea antes de escribir código. Esto evita el esfuerzo en vano si el cambio no encaja en la hoja de ruta del proyecto.
- Consulta la [Política de Seguridad](SECURITY.md) si tu contribución afecta la autenticación, la red o el manejo de datos.

## Encontrar algo en qué trabajar

Busca los issues etiquetados como [`good first issue`](https://github.com/angeloINTJ/simut/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22) — estos están seleccionados para nuevos contribuyentes y abarcan desde documentación hasta pruebas en sistemas embebidos (embedded testing). Cada uno tiene un alcance claro, criterios de aceptación y archivos de referencia para ayudarte a empezar.

| Habilidad | Ejemplos de issues |
|-------|---------------|
| C/C++ embedded | Pruebas de HistoryCodec, pruebas del analizador CLI, fuzz testing |
| Python / DevOps | Entorno de desarrollo Docker, integración continua (CI) con cppcheck, pre-commit hooks |
| Documentación / i18n | Traducción al español, insignias de validación social |
| Diseño | Nuevo tema de color integrado |

¿No estás seguro de por dónde empezar? Deja un comentario en cualquier `good first issue` y el mantenedor te ayudará a definir su alcance.

## Configuración del Entorno de Desarrollo
### Opción A — Docker (recomendado para nuevos contribuyentes)

No hay necesidad de instalar PlatformIO, Python o el toolchain de ARM localmente. Docker se encarga de todo.

**Requisitos previos:** [Docker Desktop](https://www.docker.com/products/docker-desktop/) (macOS / Windows) o [Docker Engine](https://docs.docker.com/engine/install/) (Linux)

```bash
# Clonar el repositorio
git clone https://github.com/angeloINTJ/simut.git
cd simut

# Compilar el firmware para la Raspberry Pi Pico W
docker compose run build

# Ejecutar todas las pruebas unitarias nativas
docker compose run test
```

> **Nota sobre la primera ejecución:** Docker construirá la imagen y descargará el toolchain de ARM (~500 MB). Esto solo ocurre una vez — las ejecuciones posteriores usan la imagen en caché y son rápidas.
>
> **Usuarios de Linux:** Exporten `UID` y `GID` antes de ejecutar para que los artefactos de compilación no pertenezcan al usuario root:
> ```bash
> export UID GID
> docker compose run build
> ```

| Comando | Comando equivalente en PlatformIO |
|---|---|
| `docker compose run build` | `pio run -e pico_w_release` |
| `docker compose run test` | `pio test -e native && pio test -e native_history` |

---

### Opción B — PlatformIO Local

### Requisitos Previos

- [PlatformIO Core](https://platformio.org/install/cli) 6.x o posterior
- Raspberry Pi Pico W
- Para pruebas de hardware: Pantalla ILI9341 TFT + Panel táctil XPT2046 + Sensor DS18B20

### Compilación

```bash
# Clonar el repositorio
git clone https://github.com/angeloINTJ/simut.git
cd simut

# Compilar el firmware
pio run -e pico_w_release

# Ejecutar pruebas unitarias
pio test -e native
```

### Flashear al Dispositivo

```bash
# Flashear el firmware
pio run -e pico_w_release -t upload

# Subir datos de LittleFS (paquetes de idiomas, favicon)
pio run -e pico_w_release -t uploadfs
```

## Convenciones de Código

- **Idioma:** Todos los comentarios, nombres de variables y documentación deben estar en inglés.
- **Nomenclatura:** `camelCase` para métodos y variables, `_underscorePrefix` para miembros privados, `UPPER_SNAKE_CASE` para constantes.
- **Indentación:** Tabulaciones para indentar, espacios para alinear.
- **Estilo de llaves:** K&R — la llave de apertura en la misma línea que la declaración.
- **Doxygen:** Todos los métodos públicos en los archivos de cabecera (headers) deben tener documentación `@brief`.
- **NULL:** Usa `nullptr` en código C++ (no `NULL`).
- **Comentarios:** Explica el *por qué*, no el *qué* — el código en sí mismo es el "qué".

## Presupuesto de Memoria Flash

El espacio en la memoria flash es críticamente ajustado (~98.7% en uso). Antes de añadir nuevas características, considera:

1. ¿Se puede optimizar para usar menos espacio?
2. ¿Puede reemplazar algo de menor valor?
3. ¿Puede residir en LittleFS en lugar del binario del firmware?

## Proceso de Pull Request

1. Abre un issue describiendo el cambio que quieres realizar.
2. Haz un fork del repositorio y crea una rama (`feature/my-feature`).
3. Escribe tu código y pruébalo en hardware si es posible.
4. Asegúrate de que `pio run -e pico_w_release` se compile con **cero advertencias**.
5. Asegúrate de que `pio test -e native` pase todas las pruebas.
6. Actualiza la documentación en `docs/` si tu cambio afecta el comportamiento de cara al usuario.
7. Envía el PR con una descripción clara, haciendo referencia al número de issue.
8. La lista de verificación de la plantilla de PR te guiará en los pasos restantes.

## Pruebas

- Las pruebas unitarias usan el framework [Unity](http://www.throwtheswitch.org/unity).
- Ejecútalas con `pio test -e native`.
- Añade pruebas para nueva lógica de validación, codificación/decodificación y rutas críticas de seguridad.
- Se requieren pruebas de hardware para cambios en la pantalla, sensores, WiFi y OTA.

## Comunidad

- Reporta errores a través de [GitHub Issues](https://github.com/angeloINTJ/simut/issues).
- Haz preguntas en [GitHub Discussions](https://github.com/angeloINTJ/simut/discussions).
- Sigue el [Código de Conducta](CODE_OF_CONDUCT.md).
- Vulnerabilidades de seguridad: sigue la [Política de Seguridad](SECURITY.md) — no abras un issue público.

## Herramientas de IA

Usamos asistentes de IA (Claude, Copilot, etc.) como **herramientas de ingeniería**, no como sustitutos del criterio humano. La IA ayuda con código repetitivo (boilerplate), borradores de documentación y la estructura base de las pruebas, pero las decisiones de arquitectura, los tiempos (timing) de PIO, el presupuesto de la memoria flash y el endurecimiento de la seguridad son trabajo humano. Si usas IA en tu contribución, está bien, solo revisa el resultado. El código generado es tu responsabilidad.

## Licencia

Al contribuir, aceptas que tus contribuciones estarán licenciadas bajo la Licencia MIT.