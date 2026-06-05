/* =============================================================================
 *  pico_hand.ino
 *
 *  "Robotic hand" firmware to remotely drive the BOOTSEL and RESET buttons
 *  of a target Raspberry Pi Pico via USB serial commands.
 *
 *  Designed for the Arduino IDE — tested with "arduino-pico" core
 *  (Earle Philhower):  https://github.com/earlephilhower/arduino-pico
 *
 *  IDE board selection:
 *      Board    : Raspberry Pi Pico (or Pico W)
 *      USB Stack: Pico SDK (default)
 *
 *  Electrical principle
 *  --------------------
 *  Both BOOTSEL and RUN (reset) have pull-ups on the target Pico. The
 *  physical buttons simply short the line to GND. We emulate this with
 *  each GPIO in "emulated open-drain":
 *
 *      "Pressed": GPIO as OUTPUT LOW    → pulls line to GND
 *      "Released": GPIO as INPUT         → high impedance
 *
 *  NEVER drive the line HIGH — this avoids conflict with the target's
 *  pull-up and eliminates short-circuit risk if someone presses the
 *  physical button at the same time.
 *
 *  Expected wiring
 *  ---------------
 *      Pico "hand"               Target Pico
 *      ----------                 -----------
 *      GPIO PIN_BOOTSEL  -------- BOOTSEL button pad/pin (hot side)
 *      GPIO PIN_RESET    -------- RUN/RESET button pad/pin (hot side)
 *      GND               -------- GND  (mandatory!)
 * ============================================================================= */

#include <Arduino.h>
#include <ctype.h>
#include <string.h>

/* =============================================================================
 *  Hardware configuration (adjust to match your wiring)
 * ============================================================================= */

/** GPIO connected to the target Pico's BOOTSEL button. */
static const uint8_t PIN_BOOTSEL = 0;

/** GPIO connected to the target Pico's RUN (reset) pin. */
static const uint8_t PIN_RESET   = 1;

/** On-board LED, used as heartbeat to indicate firmware is alive.
 *  GP25 is the standard Pico on-board LED. Using a literal value avoids
 *  collision with the `PIN_LED` macro defined in pins_arduino.h of the
 *  arduino-pico core (which would turn the variable into `(25u) = LED_BUILTIN`
 *  after preprocessing). */
static const uint8_t LED_GPIO    = 25;

/* =============================================================================
 *  Sequence timing (milliseconds)
 * ============================================================================= */

/** Minimum RESET pulse width for the RP2040 to recognize it. */
static const uint32_t RESET_PULSE_MS      = 50;

/** Time BOOTSEL is held BEFORE applying reset. */
static const uint32_t BOOTSEL_HOLD_PRE_MS = 50;

/** Time BOOTSEL stays held AFTER RESET is released, ensuring the bootrom
 *  samples the pin low during initial check. */
static const uint32_t BOOTSEL_HOLD_POS_MS = 200;

/** LED heartbeat period. */
static const uint32_t HEARTBEAT_MS        = 500;

/** Debug: toggle verbose pin transition + timestamp logs.
 *  Runtime toggle via "DEBUG ON"/"DEBUG OFF" commands. Default OFF so
 *  automation output stays clean. Use "DEBUG ON" before commands you
 *  want to instrument. */
static bool g_debug_enabled = false;

/* =============================================================================
 *  Parser constants
 * ============================================================================= */

static const size_t LINE_BUFFER_SIZE = 64;
static const size_t ARG_BUFFER_SIZE  = 16;

/* =============================================================================
 *  Virtual button layer (emulated open-drain)
 * ============================================================================= */

/**
 * Initialize GPIO in safe state (button released): input without pull,
 * letting the target Pico control the line level.
 *
 * @param gpio  GPIO number to configure.
 */
static void pin_init_released(uint8_t gpio)
{
    /* Pre-write LOW to the output register: when we switch to OUTPUT
       in pin_press(), the low level is already ready without glitch.
       v3 fix: INPUT_PULLUP instead of plain INPUT — empirically on
       arduino-pico, `pinMode(INPUT)` after a digitalWrite(LOW) does not
       always release the line (read_back=L observed in HW 2026-05-08).
       Internal pull-up (~50k) ensures HIGH even if OE register glitches. */
    digitalWrite(gpio, LOW);
    pinMode(gpio, INPUT_PULLUP);
}

/**
 * Debug log for pin transitions — format:
 *   [DBG t=<ms>] <action> GP<n>  read_back=<H|L>
 * read_back samples the actual state read after the operation (sanity check).
 */
static void dbg_pin(uint32_t t, const char *action, uint8_t gpio)
{
    if (!g_debug_enabled) return;
    int rb = digitalRead(gpio);
    Serial.printf("[DBG t=%lu] %s GP%u read_back=%c\n",
                  (unsigned long)t, action, (unsigned)gpio, rb ? 'H' : 'L');
    Serial.flush();
}

/**
 * Press the "button": GPIO becomes OUTPUT LOW, pulling the target
 * Pico's line to GND.
 *
 * @param gpio  GPIO number to press.
 */
static void pin_press(uint8_t gpio)
{
    uint32_t t0 = millis();
    digitalWrite(gpio, LOW);
    pinMode(gpio, OUTPUT);
    dbg_pin(t0, "PRESS", gpio);
}

/**
 * Release the "button": GPIO returns to INPUT (high impedance), allowing
 * the target's pull-up to raise the line.
 *
 * @param gpio  GPIO number to release.
 */
static void pin_release(uint8_t gpio)
{
    /* v3 fix: INPUT_PULLUP instead of plain INPUT. See pin_init_released. */
    uint32_t t0 = millis();
    pinMode(gpio, INPUT_PULLUP);
    dbg_pin(t0, "RELEASE", gpio);
}

/**
 * Internal state mirrored by firmware, used by the STATUS command.
 *
 * We track this in variables rather than inferring from hardware because
 * the arduino-pico core has no public API to read the direction register
 * (GPIO_OE) directly — and this small duplication keeps the code portable
 * across cores.
 */
static bool g_bootsel_pressed = false;
static bool g_reset_pressed   = false;

/* =============================================================================
 *  High-level sequences
 * ============================================================================= */

/**
 * Apply a reset pulse to the target Pico.
 *
 * Measures actual pulse time and reports in debug mode.
 */
static void sequence_reset(void)
{
    uint32_t t_press = millis();
    pin_press(PIN_RESET);
    g_reset_pressed = true;
    delay(RESET_PULSE_MS);

    uint32_t t_release = millis();
    pin_release(PIN_RESET);
    g_reset_pressed = false;

    if (g_debug_enabled) {
        Serial.printf("[DBG] RESET pulse: target=%lums actual=%lums\n",
                      (unsigned long)RESET_PULSE_MS,
                      (unsigned long)(t_release - t_press));
        Serial.flush();
    }
}

/**
 * Put the target Pico into BOOTSEL mode:
 *   1. Press BOOTSEL.
 *   2. Apply a full RESET pulse (with BOOTSEL still held).
 *   3. Keep BOOTSEL held a moment longer for the bootrom to sample
 *      the pin during initialization.
 *   4. Release BOOTSEL.
 *
 * Measures actual time for each phase and reports in debug mode.
 */
static void sequence_bootsel(void)
{
    uint32_t t_bp = millis();
    pin_press(PIN_BOOTSEL);
    g_bootsel_pressed = true;
    delay(BOOTSEL_HOLD_PRE_MS);

    uint32_t t_rp = millis();
    pin_press(PIN_RESET);
    g_reset_pressed = true;
    delay(RESET_PULSE_MS);

    uint32_t t_rr = millis();
    pin_release(PIN_RESET);
    g_reset_pressed = false;

    delay(BOOTSEL_HOLD_POS_MS);

    uint32_t t_br = millis();
    pin_release(PIN_BOOTSEL);
    g_bootsel_pressed = false;

    if (g_debug_enabled) {
        Serial.printf("[DBG] BOOTSEL seq: pre=%lums reset=%lums pos=%lums total=%lums\n",
                      (unsigned long)(t_rp - t_bp),
                      (unsigned long)(t_rr - t_rp),
                      (unsigned long)(t_br - t_rr),
                      (unsigned long)(t_br - t_bp));
        Serial.flush();
    }
}

/* =============================================================================
 *  String utilities
 * ============================================================================= */

/**
 * Convert string to uppercase in-place (ASCII only).
 */
static void str_upper(char *s)
{
    for (; *s != '\0'; ++s) {
        *s = (char)toupper((unsigned char)*s);
    }
}

/**
 * Remove leading/trailing spaces, CR, and LF in-place.
 *
 * @param s  string to trim (modified in-place).
 * @return   pointer to the first useful character within @p s.
 */
static char *str_trim(char *s)
{
    /* Advance past leading whitespace */
    while (*s != '\0' && isspace((unsigned char)*s)) {
        ++s;
    }
    /* Rewind past trailing whitespace */
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return s;
}

/* =============================================================================
 *  Command table
 *
 *  Each handler receives the arguments (already trimmed) as a string.
 *  Commands without arguments simply ignore the parameter.
 * ============================================================================= */

typedef void (*command_handler_t)(const char *args);

typedef struct {
    const char        *name;     /**< uppercase name, e.g. "RESET"          */
    const char        *help;     /**< short description shown by HELP       */
    command_handler_t  handler;  /**< function that executes the command    */
} command_t;

/* Forward declarations --------------------------------------------------- */
static void cmd_ping(const char *args);
static void cmd_reset(const char *args);
static void cmd_bootsel(const char *args);
static void cmd_hold(const char *args);
static void cmd_release(const char *args);
static void cmd_status(const char *args);
static void cmd_pinout(const char *args);
static void cmd_self_bootsel(const char *args);
static void cmd_debug(const char *args);
static void cmd_pulse_test(const char *args);
static void cmd_help(const char *args);

/* Dispatch table --------------------------------------------------------- */
static const command_t COMMANDS[] = {
    { "PING",         "responds PONG (connectivity test)",                    cmd_ping         },
    { "RESET",        "applies reset pulse to target Pico",                   cmd_reset        },
    { "BOOTSEL",      "puts target Pico into BOOTSEL mode",                   cmd_bootsel      },
    { "HOLD",         "HOLD <BOOTSEL|RESET>: holds button pressed",           cmd_hold         },
    { "RELEASE",      "RELEASE <BOOTSEL|RESET>: releases button",             cmd_release      },
    { "STATUS",       "shows current state of control pins",                  cmd_status       },
    { "PINOUT",       "shows which GPIO is assigned to which function",       cmd_pinout       },
    { "SELF_BOOTSEL", "puts this HAND into BOOTSEL (for reflashing)",         cmd_self_bootsel },
    { "DEBUG",        "DEBUG <ON|OFF|STATUS>: toggle verbose logs",           cmd_debug        },
    { "PULSE_TEST",   "PULSE_TEST <BOOTSEL|RESET> <ms> <count>: timed pulses",cmd_pulse_test   },
    { "HELP",         "lists all available commands",                         cmd_help         },
};

static const size_t N_COMMANDS = sizeof(COMMANDS) / sizeof(COMMANDS[0]);

/* =============================================================================
 *  Command implementations
 * ============================================================================= */

static void cmd_ping(const char *args)
{
    (void)args;
    Serial.println("PONG");
}

static void cmd_reset(const char *args)
{
    (void)args;
    sequence_reset();
    Serial.println("OK RESET");
}

static void cmd_bootsel(const char *args)
{
    (void)args;
    sequence_bootsel();
    Serial.println("OK BOOTSEL");
}

/**
 * Resolve the textual name ("BOOTSEL" or "RESET") to the corresponding
 * GPIO and internal state flag.
 *
 * @param name           uppercase string.
 * @param out_gpio       (output) resolved GPIO.
 * @param out_state_flag (output) pointer to the state flag to update.
 * @return               true if the name was valid, false otherwise.
 */
static bool resolve_target(const char *name, uint8_t *out_gpio, bool **out_state_flag)
{
    if (strcmp(name, "BOOTSEL") == 0) {
        *out_gpio       = PIN_BOOTSEL;
        *out_state_flag = &g_bootsel_pressed;
        return true;
    }
    if (strcmp(name, "RESET") == 0) {
        *out_gpio       = PIN_RESET;
        *out_state_flag = &g_reset_pressed;
        return true;
    }
    return false;
}

/**
 * Shared helper for HOLD/RELEASE: copies the argument into a local buffer
 * and resolves the target GPIO + state flag.
 *
 * @param args            raw argument received by the handler.
 * @param out_name        buffer where normalized (uppercase) name is placed.
 * @param out_size        size of @p out_name buffer.
 * @param out_gpio        (output) resolved GPIO.
 * @param out_state_flag  (output) corresponding state flag.
 * @return                true on success, false on invalid argument.
 */
static bool parse_target_arg(const char *args,
                             char       *out_name,
                             size_t      out_size,
                             uint8_t    *out_gpio,
                             bool      **out_state_flag)
{
    /* Guard against missing argument */
    if (args == NULL || *args == '\0') {
        return false;
    }
    strncpy(out_name, args, out_size - 1);
    out_name[out_size - 1] = '\0';
    str_upper(out_name);
    return resolve_target(out_name, out_gpio, out_state_flag);
}

static void cmd_hold(const char *args)
{
    char     target[ARG_BUFFER_SIZE];
    uint8_t  gpio;
    bool    *state_flag;

    if (!parse_target_arg(args, target, sizeof(target), &gpio, &state_flag)) {
        Serial.println("ERR: HOLD requires BOOTSEL or RESET");
        return;
    }
    pin_press(gpio);
    *state_flag = true;
    Serial.printf("OK HOLD %s\n", target);
}

static void cmd_release(const char *args)
{
    char     target[ARG_BUFFER_SIZE];
    uint8_t  gpio;
    bool    *state_flag;

    if (!parse_target_arg(args, target, sizeof(target), &gpio, &state_flag)) {
        Serial.println("ERR: RELEASE requires BOOTSEL or RESET");
        return;
    }
    pin_release(gpio);
    *state_flag = false;
    Serial.printf("OK RELEASE %s\n", target);
}

static void cmd_status(const char *args)
{
    (void)args;
    Serial.printf("STATUS BOOTSEL=%s RESET=%s\n",
                  g_bootsel_pressed ? "PRESSED" : "RELEASED",
                  g_reset_pressed   ? "PRESSED" : "RELEASED");
}

static void cmd_pinout(const char *args)
{
    (void)args;
    Serial.printf("PINOUT BOOTSEL=GP%u RESET=GP%u LED=GP%u\n",
                  (unsigned)PIN_BOOTSEL,
                  (unsigned)PIN_RESET,
                  (unsigned)LED_GPIO);
}

static void cmd_self_bootsel(const char *args)
{
    (void)args;
    Serial.println("OK SELF_BOOTSEL");
    Serial.flush();
    /* Small extra delay for USB to drain before the CPU re-enters the
       bootrom (the CDC port disappears at that moment). */
    delay(100);
    /* arduino-pico core API: puts the board itself into BOOTSEL mode. */
    rp2040.rebootToBootloader();
}

static void cmd_debug(const char *args)
{
    char buf[ARG_BUFFER_SIZE];
    if (args == NULL || *args == '\0') {
        Serial.printf("DEBUG STATUS: %s\n", g_debug_enabled ? "ON" : "OFF");
        return;
    }
    strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    str_upper(buf);
    if (strcmp(buf, "ON") == 0) {
        g_debug_enabled = true;
        Serial.println("OK DEBUG ON");
    } else if (strcmp(buf, "OFF") == 0) {
        g_debug_enabled = false;
        Serial.println("OK DEBUG OFF");
    } else if (strcmp(buf, "STATUS") == 0) {
        Serial.printf("DEBUG STATUS: %s\n", g_debug_enabled ? "ON" : "OFF");
    } else {
        Serial.printf("ERR: DEBUG requires ON, OFF or STATUS (received '%s')\n", buf);
    }
}

/**
 * PULSE_TEST <BOOTSEL|RESET> <duration_ms> <count>
 *
 * Applies N timed pulses to the specified pin. Useful for oscilloscope/LED
 * observation without triggering a full sequence. Each pulse reports actual
 * measured time. 200ms pause between pulses.
 */
static void cmd_pulse_test(const char *args)
{
    char target_str[ARG_BUFFER_SIZE];
    if (args == NULL || *args == '\0') {
        Serial.println("ERR: usage PULSE_TEST <BOOTSEL|RESET> <ms> <count>");
        return;
    }
    /* Parse "BOOTSEL 50 5" */
    const char *sp1 = strchr(args, ' ');
    if (!sp1) { Serial.println("ERR: missing arguments"); return; }
    size_t name_len = (size_t)(sp1 - args);
    if (name_len >= sizeof(target_str)) { Serial.println("ERR: name too long"); return; }
    memcpy(target_str, args, name_len);
    target_str[name_len] = '\0';
    str_upper(target_str);

    uint8_t gpio;
    bool *flag;
    if (!resolve_target(target_str, &gpio, &flag)) {
        Serial.println("ERR: target must be BOOTSEL or RESET");
        return;
    }

    /* Remainder: "<ms> <count>" */
    const char *rest = sp1 + 1;
    while (*rest == ' ') rest++;
    const char *sp2 = strchr(rest, ' ');
    if (!sp2) { Serial.println("ERR: missing ms+count"); return; }
    long ms_val = strtol(rest, NULL, 10);
    long n_val = strtol(sp2 + 1, NULL, 10);
    if (ms_val <= 0 || ms_val > 5000 || n_val <= 0 || n_val > 50) {
        Serial.println("ERR: ms in 1..5000, count in 1..50");
        return;
    }

    Serial.printf("OK PULSE_TEST GP%u %ld ms x %ld pulses\n",
                  (unsigned)gpio, ms_val, n_val);
    Serial.flush();

    for (long i = 0; i < n_val; i++) {
        uint32_t t0 = millis();
        pin_press(gpio);
        *flag = true;
        delay((uint32_t)ms_val);
        uint32_t t1 = millis();
        pin_release(gpio);
        *flag = false;
        Serial.printf("  pulse %ld: target=%ldms actual=%lums (started t=%lu)\n",
                      i + 1, ms_val, (unsigned long)(t1 - t0), (unsigned long)t0);
        Serial.flush();
        delay(200);
    }
    Serial.println("DONE PULSE_TEST");
}

static void cmd_help(const char *args)
{
    (void)args;
    Serial.println("Available commands:");
    for (size_t i = 0; i < N_COMMANDS; ++i) {
        Serial.printf("  %-13s - %s\n", COMMANDS[i].name, COMMANDS[i].help);
    }
}

/* =============================================================================
 *  Line parser
 * ============================================================================= */

/**
 * Process a complete line received over serial.
 *
 * Splits the line into "command" and "arguments" at the first space,
 * looks up the COMMANDS table, and dispatches. Empty lines are silently
 * ignored.
 */
static void process_line(char *line)
{
    line = str_trim(line);
    if (*line == '\0') {
        return;   /* blank line — not an error */
    }

    if (g_debug_enabled) {
        Serial.printf("[DBG t=%lu] RX line: '%s'\n", (unsigned long)millis(), line);
        Serial.flush();
    }

    /* Split command name and arguments at the first space */
    char *args  = (char *)"";
    char *space = strchr(line, ' ');
    if (space != NULL) {
        *space = '\0';
        args   = str_trim(space + 1);
    }

    /* Commands are case-insensitive */
    str_upper(line);

    for (size_t i = 0; i < N_COMMANDS; ++i) {
        if (strcmp(line, COMMANDS[i].name) == 0) {
            COMMANDS[i].handler(args);
            return;
        }
    }
    Serial.printf("ERR: unknown command '%s' (type HELP)\n", line);
}

/**
 * Pump characters from USB serial into a line buffer. When a line break
 * arrives, the line is processed. Non-blocking: returns immediately if
 * no data is available.
 */
static void serial_pump(void)
{
    static char   buf[LINE_BUFFER_SIZE];
    static size_t len = 0;

    while (Serial.available() > 0) {
        int c = Serial.read();
        if (c < 0) {
            break;
        }
        if (c == '\r' || c == '\n') {
            /* End of line: process whatever is in the buffer */
            if (len > 0) {
                buf[len] = '\0';
                process_line(buf);
                len = 0;
            }
            /* \r\n sequences are handled naturally: on the second iteration,
               len is already 0 and the empty line is ignored. */
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = (char)c;
        } else {
            /* Line exceeded buffer: discard and report error */
            len = 0;
            Serial.printf("ERR: line too long (max %u)\n",
                          (unsigned)(sizeof(buf) - 1));
        }
    }
}

/* =============================================================================
 *  setup() / loop()
 * ============================================================================= */

void setup(void)
{
    /* USB CDC. Baud rate is ignored on CDC but kept by convention. */
    Serial.begin(115200);

    /* v2: Serial2 = UART1 default GP4(TX)/GP5(RX) — transparent serial bridge
     * for SIMUT target debug (capturing post-OTA boot when the SIMUT USB CDC
     * goes mute due to F-USB-CDC-DEAD). Crossover wiring:
     *   PicoHand GP4 (TX) -- SIMUT GP5 (RX)
     *   PicoHand GP5 (RX) -- SIMUT GP4 (TX)
     *   GND -- GND (mandatory)
     * Bytes from SIMUT arrive here and are forwarded to the USB CDC Serial. */
    Serial2.begin(115200);

    /* Heartbeat LED */
    pinMode(LED_GPIO, OUTPUT);
    digitalWrite(LED_GPIO, LOW);

    /* Control lines start released (high impedance). */
    pin_init_released(PIN_BOOTSEL);
    pin_init_released(PIN_RESET);
}

void loop(void)
{
    /* Non-blocking heartbeat: toggle LED every HEARTBEAT_MS. */
    static uint32_t last_blink_ms = 0;
    uint32_t now = millis();
    if (now - last_blink_ms >= HEARTBEAT_MS) {
        last_blink_ms = now;
        digitalWrite(LED_GPIO, !digitalRead(LED_GPIO));
    }

    /* Transparent serial bridge: forward bytes from UART1 (Serial2)
     * to USB CDC (Serial) — SIMUT logs appear on the host terminal.
     * Line-buffered with [S] prefix to distinguish from hand output.
     * Small buffer (200 B): fine for SIMUT boot logs. */
    static char rxbuf[200];
    static int  rxlen = 0;
    static bool need_prefix = true;
    while (Serial2.available()) {
        char c = (char)Serial2.read();
        if (need_prefix) {
            Serial.print("[S] ");
            need_prefix = false;
        }
        Serial.write((uint8_t)c);
        if (c == '\n') {
            need_prefix = true;
            rxlen = 0;
        } else if (rxlen < (int)sizeof(rxbuf) - 1) {
            rxbuf[rxlen++] = c;
        }
    }

    /* Process any serial commands that have arrived. */
    serial_pump();
}
