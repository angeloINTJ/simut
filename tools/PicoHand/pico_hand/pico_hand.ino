/* =============================================================================
 *  pico_hand.ino — Dual-core "robotic hand" firmware
 *
 *  Remotely drives the BOOTSEL and RESET buttons of a target Raspberry Pi Pico
 *  via USB serial commands, with continuous hardware verification.
 *
 *  Architecture (RP2040 dual-core)
 *  -------------------------------
 *  Core 0 (Executor):
 *    - USB CDC serial command parser
 *    - Serial2 → USB bridge (SIMUT target debug forwarding)
 *    - GPIO control (BOOTSEL + RESET in emulated open-drain)
 *    - Updates expected pin state for Core 1
 *    - Reads verification results from Core 1
 *
 *  Core 1 (Logic Analyzer):
 *    - Continuously samples actual pin levels via digitalRead()
 *    - Compares against expected state set by Core 0
 *    - Detects faults: stuck-high (open circuit), stuck-low (short to GND)
 *    - Maintains heartbeat timestamp for watchdog
 *    - Drives on-board LED: slow blink = OK, fast blink = fault
 *
 *  Inter-core communication:
 *    Single-writer fields in a shared volatile struct. No mutex needed:
 *    each field has exactly one writer. Atomic reads on Cortex-M0+ for
 *    aligned 32-bit and byte types.
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
 *  BOOTSEL: emulated open-drain.
 *      "Pressed": GPIO as OUTPUT LOW    → pulls line to GND
 *      "Released": GPIO as INPUT         → high impedance
 *
 *  RESET: active-HIGH (inverted logic).
 *      "Pressed": GPIO as OUTPUT HIGH   → drives line HIGH
 *      "Released": GPIO as OUTPUT LOW   → drives line LOW
 *
 *  Expected wiring
 *  ---------------
 *      Pico "hand"               Target Pico
 *      ----------                 -----------
 *      GPIO PIN_BOOTSEL  -------- BOOTSEL button pad/pin (hot side)
 *      GPIO PIN_RESET    -------- RUN/RESET (via external inverter/driver)
 *      GND               -------- GND  (mandatory!)
 * ============================================================================= */

#include <Arduino.h>
#include <ctype.h>
#include <string.h>

/* =============================================================================
 *  Hardware configuration (adjust to match your wiring)
 * ============================================================================= */

/** GPIO connected to the target Pico's BOOTSEL button. */
static const uint8_t PIN_BOOTSEL = 22;

/** GPIO connected to the target Pico's RUN (reset) pin.
 *  Moved from GP26 to GP27 — GP26 had a hardware defect preventing
 *  proper tri-state (INPUT_PULLUP did not release the line). */
static const uint8_t PIN_RESET   = 27;

/** RESET uses active-HIGH logic: OUTPUT HIGH = pressed, OUTPUT LOW = released.
 *  BOOTSEL remains open-drain (OUTPUT LOW = pressed, INPUT = released). */
static const bool RESET_ACTIVE_HIGH = true;

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

/** LED heartbeat period (when no fault — Core 1). */
static const uint32_t HEARTBEAT_MS        = 500;

/** LED fault blink period (when fault active — Core 1). */
static const uint32_t FAULT_BLINK_MS      = 100;

/* =============================================================================
 *  Logic Analyzer timing (microseconds) — Core 1
 * ============================================================================= */

/** Sampling interval for the verifier loop. 100µs = 10 kHz. */
static const uint32_t VERIFY_SAMPLE_US      = 100;

/** Grace period after a command before fault detection begins.
 *  Allows the pin and external circuit to settle. */
static const uint32_t VERIFY_SETTLE_US      = 500;

/** How long a mismatch must persist before it is flagged as a fault.
 *  5 ms avoids false positives from transient noise. */
static const uint32_t VERIFY_FAULT_US       = 5000;

/** If Core 1 heartbeat is older than this, Core 0 considers it dead. */
static const uint32_t VERIFY_HB_TIMEOUT_US  = 1000000;

/* =============================================================================
 *  Fault codes — written by Core 1, read by Core 0
 * ============================================================================= */

enum FaultCode : uint8_t {
    FAULT_NONE        = 0,  /**< Pin matches expected state.              */
    FAULT_STUCK_HIGH  = 1,  /**< Expected LOW (pressed) but reads HIGH
                                 (open circuit, broken wire).             */
    FAULT_STUCK_LOW   = 2,  /**< Expected HIGH (released) but reads LOW
                                 (short to GND, target pulling down).     */
    FAULT_GLITCH      = 3,  /**< Unexpected transient (reserved).         */
    FAULT_NO_VERIFIER = 4,  /**< Core 1 heartbeat timeout (internal use). */
};

/* =============================================================================
 *  Shared state between cores
 *
 *  Single-writer discipline: each field is written by exactly one core.
 *  volatile ensures visibility; no mutex needed because RP2040 SIO provides
 *  atomic reads for aligned ≤32-bit types and digitalRead/digitalWrite are
 *  single-cycle atomic operations on independent SIO registers.
 * ============================================================================= */

struct VerifierState {
    /* ---------- Core 0 writes, Core 1 reads ---------- */

    /** true = pin SHOULD be LOW (pressed). */
    volatile bool     bootsel_expected_low;
    volatile bool     reset_expected_low;

    /** micros() timestamp of the last press/release command for each pin. */
    volatile uint32_t bootsel_cmd_time_us;
    volatile uint32_t reset_cmd_time_us;

    /* ---------- Core 1 writes, Core 0 reads ---------- */

    /** Last sampled pin level (true = LOW). */
    volatile bool     bootsel_actual_low;
    volatile bool     reset_actual_low;

    /** Current fault code (FaultCode enum). */
    volatile uint8_t  bootsel_fault;
    volatile uint8_t  reset_fault;

    /** micros() timestamp when the current fault began. */
    volatile uint32_t bootsel_fault_time_us;
    volatile uint32_t reset_fault_time_us;

    /** Accumulated fault count (cleared by VERIFY CLEAR). */
    volatile uint32_t bootsel_fault_count;
    volatile uint32_t reset_fault_count;

    /** Core 1 heartbeat: updated every sample loop iteration (~100µs). */
    volatile uint32_t last_heartbeat_us;
};

/** Single global instance of the verifier shared state. */
static VerifierState g_vs;

/* =============================================================================
 *  Debug flag
 * ============================================================================= */

/** Debug: toggle verbose pin transition + timestamp logs.
 *  Runtime toggle via "DEBUG ON"/"DEBUG OFF" commands. Default OFF so
 *  automation output stays clean. */
static bool g_debug_enabled = false;

/* =============================================================================
 *  Parser constants
 * ============================================================================= */

static const size_t LINE_BUFFER_SIZE = 64;
static const size_t ARG_BUFFER_SIZE  = 16;

/* =============================================================================
 *  Fault code string helpers
 * ============================================================================= */

/**
 * Convert a FaultCode to its human-readable string.
 * @param code  FaultCode enum value.
 * @return      Static string (never NULL).
 */
static const char *fault_code_str(uint8_t code)
{
    switch (code) {
        case FAULT_NONE:        return "OK";
        case FAULT_STUCK_HIGH:  return "STUCK_HIGH";
        case FAULT_STUCK_LOW:   return "STUCK_LOW";
        case FAULT_GLITCH:      return "GLITCH";
        case FAULT_NO_VERIFIER: return "NO_VERIFIER";
        default:                return "UNKNOWN";
    }
}

/* =============================================================================
 *  Virtual button layer (emulated open-drain) — Core 0 only
 * ============================================================================= */

/**
 * Initialize GPIO in safe state (button released).
 *
 * BOOTSEL: INPUT_PULLUP (high impedance, target pull-up keeps line HIGH).
 * RESET:   OUTPUT LOW (active-HIGH logic: LOW = released).
 *
 * @param gpio  GPIO number to configure.
 */
static void pin_init_released(uint8_t gpio)
{
    if (gpio == PIN_RESET && RESET_ACTIVE_HIGH) {
        /* Active-HIGH: released = OUTPUT LOW. */
        digitalWrite(gpio, LOW);
        pinMode(gpio, OUTPUT);
    } else {
        /* Open-drain: released = INPUT (high impedance).
         * v3 fix: INPUT_PULLUP instead of plain INPUT — empirically on
         * arduino-pico, `pinMode(INPUT)` after a digitalWrite(LOW) does not
         * always release the line (read_back=L observed in HW 2026-05-08).
         * Internal pull-up (~50k) ensures HIGH even if OE register glitches. */
        digitalWrite(gpio, LOW);
        pinMode(gpio, INPUT_PULLUP);
    }
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
 * Update the verifier expected state for a given pin.
 *
 * @param gpio         GPIO that was changed.
 * @param expected_low true if pin should now be LOW (pressed).
 */
static void verifier_notify(uint8_t gpio, bool expected_low)
{
    uint32_t now = micros();
    if (gpio == PIN_BOOTSEL) {
        g_vs.bootsel_expected_low = expected_low;
        g_vs.bootsel_cmd_time_us  = now;
    } else if (gpio == PIN_RESET) {
        g_vs.reset_expected_low = expected_low;
        g_vs.reset_cmd_time_us  = now;
    }
}

/**
 * Press the "button".
 *
 * BOOTSEL (open-drain):  OUTPUT LOW  → pulls target line to GND.
 * RESET   (active-HIGH): OUTPUT HIGH → drives target line HIGH.
 *
 * @param gpio  GPIO number to press.
 */
static void pin_press(uint8_t gpio)
{
    uint32_t t0 = millis();
    bool expect_low;
    if (gpio == PIN_RESET && RESET_ACTIVE_HIGH) {
        digitalWrite(gpio, HIGH);
        pinMode(gpio, OUTPUT);
        expect_low = false;   /* active-HIGH: pressed = HIGH, not LOW */
    } else {
        digitalWrite(gpio, LOW);
        pinMode(gpio, OUTPUT);
        expect_low = true;    /* open-drain: pressed = LOW */
    }
    verifier_notify(gpio, expect_low);
    dbg_pin(t0, "PRESS", gpio);
}

/**
 * Release the "button".
 *
 * BOOTSEL (open-drain):  INPUT_PULLUP → high impedance, target pull-up wins.
 * RESET   (active-HIGH): OUTPUT LOW   → drives target line LOW.
 *
 * @param gpio  GPIO number to release.
 */
static void pin_release(uint8_t gpio)
{
    uint32_t t0 = millis();
    bool expect_low;
    if (gpio == PIN_RESET && RESET_ACTIVE_HIGH) {
        /* Active-HIGH: released = OUTPUT LOW. */
        digitalWrite(gpio, LOW);
        pinMode(gpio, OUTPUT);
        expect_low = true;    /* active-HIGH: released = LOW */
    } else {
        /* Open-drain: released = INPUT (high impedance). */
        pinMode(gpio, INPUT_PULLUP);
        expect_low = false;   /* open-drain: released = HIGH (pull-up) */
    }
    verifier_notify(gpio, expect_low);
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
 *  Verifier health check — Core 0
 * ============================================================================= */

/**
 * Check whether the logic analyzer (Core 1) has detected a fault on a
 * specific pin, or whether Core 1 itself has stopped responding.
 *
 * @param gpio           PIN_BOOTSEL or PIN_RESET.
 * @param out_fault_code (output) FaultCode if a fault is active.
 * @return               true if verification passed (no fault, verifier alive),
 *                       false if a fault is active or Core 1 is dead.
 */
static bool check_pin_verification(uint8_t gpio, uint8_t *out_fault_code)
{
    uint32_t now = micros();
    uint32_t hb_age = now - g_vs.last_heartbeat_us;

    /* Check Core 1 heartbeat first */
    if (hb_age > VERIFY_HB_TIMEOUT_US) {
        *out_fault_code = FAULT_NO_VERIFIER;
        return false;
    }

    /* Read pin-specific fault */
    uint8_t  fault;
    uint32_t fault_time;
    if (gpio == PIN_BOOTSEL) {
        fault      = g_vs.bootsel_fault;
        fault_time = g_vs.bootsel_fault_time_us;
    } else {
        fault      = g_vs.reset_fault;
        fault_time = g_vs.reset_fault_time_us;
    }

    /* If a fault is flagged but happened before the last command,
       the pin has since recovered — treat as OK. */
    uint32_t cmd_time = (gpio == PIN_BOOTSEL) ? g_vs.bootsel_cmd_time_us
                                              : g_vs.reset_cmd_time_us;
    if (fault != FAULT_NONE && fault_time < cmd_time) {
        fault = FAULT_NONE;
    }

    *out_fault_code = fault;
    return (fault == FAULT_NONE);
}

/**
 * Append verification suffix to a command response if a fault is active.
 *
 * Call after pin operations (RESET, BOOTSEL, HOLD, RELEASE).
 * If verification passed, the response remains "OK <CMD>".
 * If a fault is detected, appends " VFY:<PIN>_<FAULT>" to the response.
 *
 * @param gpio  PIN_BOOTSEL or PIN_RESET (the pin that was operated).
 * @param name  Human-readable pin name ("BOOTSEL" or "RESET").
 */
static void append_verification_suffix(uint8_t gpio, const char *name)
{
    uint8_t fault;
    if (check_pin_verification(gpio, &fault)) {
        return;  /* All good, response already printed */
    }

    if (fault == FAULT_NO_VERIFIER) {
        Serial.printf(" VFY:NO_VERIFIER");
    } else {
        Serial.printf(" VFY:%s_%s", name, fault_code_str(fault));
    }
}

/* =============================================================================
 *  High-level sequences
 * ============================================================================= */

/**
 * Apply a reset pulse to the target Pico.
 *
 * Measures actual pulse time and reports in debug mode.
 * Verification suffix is appended by the caller (cmd_reset).
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
static void cmd_verify(const char *args);
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
    { "VERIFY",       "VERIFY [CLEAR]: shows/resets logic analyzer status",   cmd_verify       },
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

    /* Brief settle delay so Core 1 has time to detect the transition
       before we check verification status. */
    delay(10);

    uint8_t fault;
    if (!check_pin_verification(PIN_RESET, &fault)) {
        if (fault == FAULT_NO_VERIFIER) {
            Serial.println("ERR RESET VFY:NO_VERIFIER");
        } else {
            Serial.printf("ERR RESET VFY:RESET_%s\n", fault_code_str(fault));
        }
        return;
    }
    Serial.println("OK RESET");
}

static void cmd_bootsel(const char *args)
{
    (void)args;
    sequence_bootsel();

    /* Brief settle delay for Core 1 detection. */
    delay(10);

    /* Check both pins since BOOTSEL sequence touches both. */
    uint8_t bfault, rfault;
    bool bok = check_pin_verification(PIN_BOOTSEL, &bfault);
    bool rok = check_pin_verification(PIN_RESET,   &rfault);

    if (bok && rok) {
        Serial.println("OK BOOTSEL");
    } else {
        Serial.print("ERR BOOTSEL");
        if (!bok) {
            Serial.printf(" VFY:BOOTSEL_%s", fault_code_str(bfault));
        }
        if (!rok) {
            Serial.printf(" VFY:RESET_%s", fault_code_str(rfault));
        }
        Serial.println();
    }
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

    delay(5);  /* Settle for Core 1 */

    uint8_t fault;
    if (!check_pin_verification(gpio, &fault)) {
        if (fault == FAULT_NO_VERIFIER) {
            Serial.printf("ERR HOLD %s VFY:NO_VERIFIER\n", target);
        } else {
            Serial.printf("ERR HOLD %s VFY:%s_%s\n",
                          target, target, fault_code_str(fault));
        }
        return;
    }
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

    delay(5);  /* Settle for Core 1 */

    uint8_t fault;
    if (!check_pin_verification(gpio, &fault)) {
        if (fault == FAULT_NO_VERIFIER) {
            Serial.printf("ERR RELEASE %s VFY:NO_VERIFIER\n", target);
        } else {
            Serial.printf("ERR RELEASE %s VFY:%s_%s\n",
                          target, target, fault_code_str(fault));
        }
        return;
    }
    Serial.printf("OK RELEASE %s\n", target);
}

static void cmd_status(const char *args)
{
    (void)args;
    /* Include verifier actual readings for richer status. */
    Serial.printf("STATUS BOOTSEL=%s RESET=%s "
                  "VFY:BOOTSEL_ACT=%s VFY:RESET_ACT=%s\n",
                  g_bootsel_pressed ? "PRESSED" : "RELEASED",
                  g_reset_pressed   ? "PRESSED" : "RELEASED",
                  g_vs.bootsel_actual_low ? "LOW" : "HIGH",
                  g_vs.reset_actual_low   ? "LOW" : "HIGH");
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

    /* Final verification check */
    delay(5);
    uint8_t fault;
    if (!check_pin_verification(gpio, &fault)) {
        Serial.printf("DONE PULSE_TEST VFY:%s_%s\n",
                      target_str, fault_code_str(fault));
    } else {
        Serial.println("DONE PULSE_TEST");
    }
}

/**
 * VERIFY [CLEAR]
 *
 * Without arguments: reports the current status of the logic analyzer
 * (Core 1) for both pins, including heartbeat age and fault counters.
 *
 * With "CLEAR": resets all fault counters and clears active faults.
 */
static void cmd_verify(const char *args)
{
    char buf[ARG_BUFFER_SIZE];
    if (args != NULL && *args != '\0') {
        strncpy(buf, args, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        str_upper(buf);

        if (strcmp(buf, "CLEAR") == 0) {
            /* Reset all fault counters and active faults.
               These are Core 1 writes but we're on Core 0 — safe because
               we're clearing counters; if Core 1 writes concurrently it
               will just set a new fault naturally on next mismatch. */
            g_vs.bootsel_fault       = FAULT_NONE;
            g_vs.reset_fault         = FAULT_NONE;
            g_vs.bootsel_fault_count = 0;
            g_vs.reset_fault_count   = 0;
            Serial.println("OK VFY CLEAR");
            return;
        }
        Serial.printf("ERR: VERIFY expects CLEAR or no argument (received '%s')\n", buf);
        return;
    }

    /* Report verifier status */
    uint32_t now    = micros();
    uint32_t hb_age = now - g_vs.last_heartbeat_us;

    /* Format each pin status */
    const char *bf = fault_code_str(g_vs.bootsel_fault);
    const char *rf = fault_code_str(g_vs.reset_fault);

    Serial.printf("VFY BOOTSEL=%s", bf);
    if (g_vs.bootsel_fault != FAULT_NONE) {
        /* Format: STUCK_HIGH(3,1234us) — fault(count,last_fault_age_us) */
        uint32_t fault_age = now - g_vs.bootsel_fault_time_us;
        Serial.printf("(%lu,%luus)",
                      (unsigned long)g_vs.bootsel_fault_count,
                      (unsigned long)fault_age);
    }
    Serial.printf(" RESET=%s", rf);
    if (g_vs.reset_fault != FAULT_NONE) {
        uint32_t fault_age = now - g_vs.reset_fault_time_us;
        Serial.printf("(%lu,%luus)",
                      (unsigned long)g_vs.reset_fault_count,
                      (unsigned long)fault_age);
    }
    Serial.printf(" HB=%luus", (unsigned long)hb_age);
    Serial.printf(" E:BOOTSEL=%s E:RESET=%s",
                  g_vs.bootsel_expected_low ? "LOW" : "HIGH",
                  g_vs.reset_expected_low   ? "LOW" : "HIGH");
    Serial.printf(" A:BOOTSEL=%s A:RESET=%s",
                  g_vs.bootsel_actual_low ? "LOW" : "HIGH",
                  g_vs.reset_actual_low   ? "LOW" : "HIGH");
    Serial.println();
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
 *  Core 0 — setup() / loop()
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

    /* Heartbeat LED — pin init only. Core 1 drives the blink pattern. */
    pinMode(LED_GPIO, OUTPUT);
    digitalWrite(LED_GPIO, LOW);

    /* Control lines start released (high impedance). */
    pin_init_released(PIN_BOOTSEL);
    pin_init_released(PIN_RESET);

    /* Core 1 launches automatically via the arduino-pico framework.
     * setup1() and loop1() are defined below — the framework detects them
     * (weak symbol override), calls main1() which runs setup1() once
     * followed by loop1() in an infinite loop on Core 1. */
}

void loop(void)
{
    /* LED heartbeat is now handled by Core 1.
     * Core 0 only does serial I/O and bridge forwarding. */

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

/* =============================================================================
 *  Core 1 — Logic Analyzer (setup1 + loop1)
 *
 *  The arduino-pico framework automatically detects setup1()/loop1()
 *  as overrides of weak symbols. Core 1 is launched via main1() which
 *  calls rp2040.begin(1) (systick) and rp2040.fifo.registerCore() before
 *  entering setup1() once and then loop1() in an infinite loop.
 *
 *  Core 1 continuously samples the BOOTSEL and RESET pins and compares
 *  their actual levels against the expected state written by Core 0.
 *  Faults are flagged when a mismatch persists beyond VERIFY_FAULT_US.
 * ============================================================================= */

void setup1(void)
{
    /* Initialize verifier state to match the pin_init_released() defaults.
     *
     * BOOTSEL: open-drain  → released = HIGH → expected_low = false
     * RESET:   active-HIGH → released = LOW  → expected_low = true  */
    g_vs.bootsel_expected_low = false;
    g_vs.reset_expected_low   = RESET_ACTIVE_HIGH;  /* true = LOW = released */
    g_vs.bootsel_actual_low   = false;
    g_vs.reset_actual_low     = false;
    g_vs.bootsel_fault        = FAULT_NONE;
    g_vs.reset_fault          = FAULT_NONE;
    g_vs.bootsel_fault_count  = 0;
    g_vs.reset_fault_count    = 0;
    g_vs.last_heartbeat_us    = micros();
}

void loop1(void)
{
    static uint32_t last_sample_us   = 0;
    static uint32_t last_led_ms      = 0;
    static uint32_t boots_mismatch_start_us = 0;
    static uint32_t reset_mismatch_start_us = 0;

    uint32_t now_us = micros();
    uint32_t now_ms = millis();

    /* ----- Heartbeat (unconditional — Core 0 watchdog depends on this) ----- */
    g_vs.last_heartbeat_us = now_us;

    /* ----- LED driver (always runs) -----
     * Slow blink (500ms) when all OK, fast blink (100ms) on any fault. */
    {
        bool any_fault = (g_vs.bootsel_fault != FAULT_NONE ||
                          g_vs.reset_fault   != FAULT_NONE);
        uint32_t period_ms = any_fault ? FAULT_BLINK_MS : HEARTBEAT_MS;

        if (now_ms - last_led_ms >= period_ms) {
            last_led_ms = now_ms;
            digitalWrite(LED_GPIO, !digitalRead(LED_GPIO));
        }
    }

    /* ----- Pin sampling (throttled to VERIFY_SAMPLE_US) ----- */
    if (now_us - last_sample_us < VERIFY_SAMPLE_US) {
        return;
    }
    last_sample_us = now_us;

    /* Read actual levels. digitalRead() returns HIGH (true) when the
       line is at logic high (released via pull-up). Convert to
       "actual_low" semantics where true = LOW (pressed). */
    bool boots_read = (digitalRead(PIN_BOOTSEL) == LOW);
    bool reset_read = (digitalRead(PIN_RESET)   == LOW);

    g_vs.bootsel_actual_low = boots_read;
    g_vs.reset_actual_low   = reset_read;

    /* ----- BOOTSEL verification ----- */
    {
        bool expected = g_vs.bootsel_expected_low;
        bool actual   = boots_read;
        uint32_t cmd_time = g_vs.bootsel_cmd_time_us;

        if (now_us - cmd_time >= VERIFY_SETTLE_US) {
            if (expected == actual) {
                if (g_vs.bootsel_fault != FAULT_NONE) {
                    g_vs.bootsel_fault = FAULT_NONE;
                }
                boots_mismatch_start_us = 0;
            } else {
                if (boots_mismatch_start_us == 0) {
                    boots_mismatch_start_us = now_us;
                } else if (now_us - boots_mismatch_start_us >= VERIFY_FAULT_US) {
                    if (g_vs.bootsel_fault == FAULT_NONE) {
                        g_vs.bootsel_fault_count++;
                    }
                    g_vs.bootsel_fault       = expected ? FAULT_STUCK_HIGH
                                                        : FAULT_STUCK_LOW;
                    g_vs.bootsel_fault_time_us = now_us;
                }
            }
        }
    }

    /* ----- RESET verification ----- */
    {
        bool expected = g_vs.reset_expected_low;
        bool actual   = reset_read;
        uint32_t cmd_time = g_vs.reset_cmd_time_us;

        if (now_us - cmd_time >= VERIFY_SETTLE_US) {
            if (expected == actual) {
                if (g_vs.reset_fault != FAULT_NONE) {
                    g_vs.reset_fault = FAULT_NONE;
                }
                reset_mismatch_start_us = 0;
            } else {
                if (reset_mismatch_start_us == 0) {
                    reset_mismatch_start_us = now_us;
                } else if (now_us - reset_mismatch_start_us >= VERIFY_FAULT_US) {
                    if (g_vs.reset_fault == FAULT_NONE) {
                        g_vs.reset_fault_count++;
                    }
                    g_vs.reset_fault         = expected ? FAULT_STUCK_HIGH
                                                        : FAULT_STUCK_LOW;
                    g_vs.reset_fault_time_us = now_us;
                }
            }
        }
    }
}
