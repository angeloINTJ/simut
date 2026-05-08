/* Test isolado do applier_reboot pattern (Fix #3 v3.43.21).
 *
 * Reproduz EXATAMENTE a sequência MMIO usada em src/ota/applier.cpp:
 *   PSM_WDSEL → CLR_ENABLE → scratch[4]=0 → LOAD=0xFFFFFF → TRIGGER
 *
 * Cenário: setup() conta boots via scratch[1]. Após 5s no loop, dispara
 * o applier_reboot pattern. Próximo boot conta como 2º. Assim sabemos
 * se o reset funciona e se boot pós-reset é confiável.
 *
 * Sintoma a observar: USB CDC enumera + serial output continua através
 * dos resets? Se sim, applier_reboot OK. Se serial mute, bug.
 */
#include <Arduino.h>
#include "hardware/watchdog.h"
#include "hardware/structs/watchdog.h"

constexpr uint32_t TWD_BASE       = 0x40058000u;
constexpr uint32_t TWD_CTRL_OFF   = 0x00u;
constexpr uint32_t TWD_LOAD_OFF   = 0x04u;
constexpr uint32_t TWD_SCRATCH4   = 0x1Cu;
constexpr uint32_t TWD_SET_ALIAS  = 0x2000u;
constexpr uint32_t TWD_CLR_ALIAS  = 0x3000u;
constexpr uint32_t TWD_ENABLE_BIT = (1u << 30);
constexpr uint32_t TWD_TRIG_BIT   = (1u << 31);
constexpr uint32_t TPSM_BASE      = 0x40010000u;
constexpr uint32_t TPSM_WDSEL_OFF = 0x18u;
constexpr uint32_t TPSM_RESET_MASK = 0x0001FFFC;  /* all except ROSC/XOSC */

static void __not_in_flash_func(applier_reboot_pattern)() {
    *(volatile uint32_t*)(TPSM_BASE + TPSM_WDSEL_OFF) = TPSM_RESET_MASK;
    *(volatile uint32_t*)(TWD_BASE + TWD_CLR_ALIAS + TWD_CTRL_OFF) = TWD_ENABLE_BIT;
    *(volatile uint32_t*)(TWD_BASE + TWD_SCRATCH4) = 0;
    *(volatile uint32_t*)(TWD_BASE + TWD_LOAD_OFF) = 0xFFFFFFu;
    *(volatile uint32_t*)(TWD_BASE + TWD_SET_ALIAS + TWD_CTRL_OFF) = TWD_TRIG_BIT;
    __asm volatile("dsb");
    while (1) tight_loop_contents();
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  Serial.ignoreFlowControl(true);
  delay(2000);

  /* Read scratch[1] = boot counter. Survives watchdog reset but cleared
   * on power cycle. */
  uint32_t boot_count = watchdog_hw->scratch[1];
  if (boot_count > 100) boot_count = 0;  /* uninitialized = random; clamp */
  watchdog_hw->scratch[1] = boot_count + 1;

  Serial.println();
  Serial.println("=================================");
  Serial.print("=== applier_reboot test BOOT #"); Serial.println(boot_count + 1);
  Serial.println("=================================");
  Serial.print("watchdog_caused_reboot(): "); Serial.println(watchdog_caused_reboot() ? "YES" : "NO");
  Serial.print("scratch[4] (boot mode):   0x"); Serial.println(watchdog_hw->scratch[4], HEX);
  Serial.print("scratch[5] (panic mark):  0x"); Serial.println(watchdog_hw->scratch[5], HEX);
  Serial.println();

  if (boot_count >= 5) {
    Serial.println("Reached 5 boots — stopping reboot loop. Blink mode.");
    return;
  }

  Serial.printf("Will trigger applier_reboot in 5 seconds (this is boot #%lu)...\n", boot_count + 1);
  for (int i = 5; i > 0; i--) {
    Serial.printf("  countdown %d\n", i);
    Serial.flush();
    delay(1000);
  }

  Serial.println("Triggering reboot pattern NOW...");
  Serial.flush();
  delay(50);
  Serial.end();
  delay(100);

  applier_reboot_pattern();
  /* never reached */
}

unsigned long last_blink = 0;
bool led = false;
void loop() {
  if (millis() - last_blink > 250) {
    last_blink = millis();
    led = !led;
    digitalWrite(LED_BUILTIN, led ? HIGH : LOW);
  }
}
