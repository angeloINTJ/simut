/* CYW43 + WiFi reset test — isola se WiFi chip externo recupera do
 * watchdog reset (sem orchestrator complications).
 *
 * Sequência por boot (boot count via scratch[1]):
 *   1. WiFi.begin(SSID, PASS)
 *   2. Aguarda WiFi connect até 30s
 *   3. Reporta tempo de connect + status
 *   4. WiFi.end()
 *   5. Após 5s, applier_reboot pattern → próximo boot
 *
 * Resultado esperado: Boot 1 connecta em ~5-15s. Boot 2 (pós-watchdog
 * reset) connecta no mesmo tempo? Ou demora mais? Ou falha?
 */
#include <Arduino.h>
#include <WiFi.h>
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
constexpr uint32_t TPSM_RESET_MASK = 0x0001FFFC;

static const char* SSID = "ProcrastinationPLUS";
static const char* PASS = "A$AGzD3XeY7xSrwAg5JF";

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

  uint32_t boot_count = watchdog_hw->scratch[1];
  if (boot_count > 100) boot_count = 0;
  watchdog_hw->scratch[1] = boot_count + 1;

  Serial.println();
  Serial.print("=== CYW43 reset test BOOT #"); Serial.println(boot_count + 1);
  Serial.print("watchdog_caused_reboot: "); Serial.println(watchdog_caused_reboot() ? "YES" : "NO");
  Serial.flush();

  /* WiFi connect */
  unsigned long t0 = millis();
  Serial.println("WiFi.begin...");
  Serial.flush();
  WiFi.begin(SSID, PASS);

  unsigned long deadline = millis() + 30000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(500);
    Serial.print(".");
    Serial.flush();
  }
  Serial.println();
  unsigned long connect_dt = millis() - t0;

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi CONNECTED in %lu ms; IP=%s\n", connect_dt, WiFi.localIP().toString().c_str());
    Serial.printf("RSSI=%d  SSID=%s\n", WiFi.RSSI(), WiFi.SSID().c_str());
  } else {
    Serial.printf("WiFi FAILED after %lu ms; status=%d\n", connect_dt, WiFi.status());
  }
  Serial.flush();

  if (boot_count >= 5) {
    Serial.println("5 boots done — staying online for inspection");
    return;
  }

  Serial.println("\nWiFi.end + 5s sleep + applier_reboot");
  Serial.flush();
  WiFi.end();
  delay(2000);

  for (int i = 5; i > 0; i--) {
    Serial.printf("countdown %d\n", i);
    Serial.flush();
    delay(1000);
  }

  Serial.println("REBOOT NOW");
  Serial.flush();
  delay(50);
  Serial.end();
  delay(100);
  applier_reboot_pattern();
}

unsigned long last = 0;
bool led = false;
void loop() {
  if (millis() - last > 1000) {
    last = millis();
    led = !led;
    digitalWrite(LED_BUILTIN, led ? HIGH : LOW);
  }
}
