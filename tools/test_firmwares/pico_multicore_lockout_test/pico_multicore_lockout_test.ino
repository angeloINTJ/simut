/* Multicore lockout latency test — replica condições do SIMUT::setup
 * pra medir quanto tempo Core 1 leva pra responder ao IRQ de lockout.
 *
 * Cenário real (alpha11 capturou):
 *   Core 0 setup:
 *     1. Serial init
 *     2. Display.begin (Core 0 side?)
 *     3. multicore_launch_core1 (loopCore1 Core 1 side)
 *     4. core1 multicore_lockout_victim_init() → _core1Ready=true
 *     5. core1 segue: TFT init, touch init, canvas alloc, render loop
 *     6. Core 0 chama multicore_lockout_start_timeout_us(500ms)
 *     7. ESPERADO: Core 1 responde em <1ms
 *     8. OBSERVADO: Core 1 não responde por 10s
 *
 * Test firmware: simula passos 3-8, mede latência de cada lockout call.
 * Sem TFT/touch real (não tem hardware) — Core 1 faz busy work mais
 * representative possível: SPI dummy bursts + delays.
 */
#include <Arduino.h>
#include "pico/multicore.h"
#include "pico/lock_core.h"
#include "hardware/sync.h"

#include <SPI.h>

static volatile bool g_core1_ready = false;
static volatile uint32_t g_core1_iter = 0;

void core1Entry() {
  /* Same setup as DisplayManager::loopCore1 */
  multicore_lockout_victim_init();
  g_core1_ready = true;

  /* Simulate TFT/touch heavy init like real DisplayManager */
  delay(50);  /* TFT begin would be here — busy work */
  for (int i = 0; i < 100; i++) {
    /* Simulate TFT register writes */
    delayMicroseconds(50);
  }

  /* Render loop simulation: SPI bursts + delays */
  while (1) {
    /* Burst of "pixels" — busy CPU work */
    volatile uint32_t sum = 0;
    for (int i = 0; i < 1000; i++) sum += i;
    (void)sum;
    g_core1_iter++;
    delayMicroseconds(100);
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  delay(2000);
  Serial.println();
  Serial.println("=== multicore_lockout latency test ===");

  /* Start Core 1 */
  multicore_launch_core1(core1Entry);
  Serial.print("Core 1 launched @ "); Serial.println(millis());

  /* Wait for victim_init complete */
  unsigned long t0 = millis();
  while (!g_core1_ready && millis() - t0 < 5000) {
    tight_loop_contents();
  }
  Serial.print("Core 1 ready @ "); Serial.print(millis());
  Serial.print(" (wait="); Serial.print(millis() - t0);
  Serial.println("ms)");

  if (!g_core1_ready) {
    Serial.println("TIMEOUT: Core 1 not ready");
    return;
  }

  /* Now hammer Core 1 with lockout calls and measure latency */
  Serial.println();
  Serial.println("=== latency measurements (10 iterations) ===");
  for (int i = 0; i < 10; i++) {
    delay(500);  /* Let Core 1 do work between attempts */
    uint32_t before = g_core1_iter;
    unsigned long lock_t0 = micros();

    bool ok = multicore_lockout_start_timeout_us(500000);
    unsigned long lock_dt = micros() - lock_t0;

    if (ok) {
      uint32_t during = g_core1_iter;
      multicore_lockout_end_blocking();
      Serial.printf("[%d] lockout OK in %lu us  (Core1 iter %lu->%lu, frozen)\n",
                    i, lock_dt, before, during);
    } else {
      Serial.printf("[%d] lockout TIMEOUT after %lu us  (Core1 iter %lu)\n",
                    i, lock_dt, g_core1_iter);
    }
  }
  Serial.println("=== test complete ===");
}

unsigned long last_blink = 0;
bool led = false;
void loop() {
  if (millis() - last_blink > 500) {
    last_blink = millis();
    led = !led;
    digitalWrite(LED_BUILTIN, led ? HIGH : LOW);
  }
}
