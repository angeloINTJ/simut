/* test_flash_raw — testa flash direto via XIP read + flash_range_program/erase
 * SEM LittleFS. Identifica se o problema é (a) flash QSPI HW, (b) LittleFS,
 * ou (c) ambos.
 */
#include <Arduino.h>
#include <hardware/flash.h>
#include <hardware/sync.h>

#define TEST_OFFSET 0x100000  /* 1MB into flash — fora da app slot atual */

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 30 && !Serial; i++) delay(100);
  delay(500);

  Serial.println();
  Serial.println(F("=== test_flash_raw ==="));
  Serial.println(F("[FRAW] alive — Serial.begin OK"));
  Serial.flush();

  /* (1) XIP read — confirma que mapeamento de flash está OK */
  uint32_t *xip = (uint32_t*)(0x10000000 + TEST_OFFSET);
  Serial.print(F("[FRAW] XIP read @0x"));
  Serial.print(0x10000000 + TEST_OFFSET, HEX);
  Serial.print(F(": 0x"));
  Serial.println(*xip, HEX);
  Serial.flush();

  /* (2) Erase 1 sector (4KB) */
  Serial.println(F("[FRAW] flash_range_erase..."));
  Serial.flush();
  uint32_t irqs = save_and_disable_interrupts();
  flash_range_erase(TEST_OFFSET, 4096);
  restore_interrupts(irqs);
  Serial.print(F("[FRAW] erase done. XIP read: 0x"));
  Serial.println(*xip, HEX);  /* should be 0xFFFFFFFF */
  Serial.flush();

  /* (3) Program pattern */
  Serial.println(F("[FRAW] flash_range_program..."));
  Serial.flush();
  uint8_t pattern[256];
  for (int i = 0; i < 256; i++) pattern[i] = (uint8_t)(i ^ 0x55);
  irqs = save_and_disable_interrupts();
  flash_range_program(TEST_OFFSET, pattern, 256);
  restore_interrupts(irqs);

  /* (4) Verify */
  uint8_t *xb = (uint8_t*)(0x10000000 + TEST_OFFSET);
  bool match = true;
  for (int i = 0; i < 256; i++) if (xb[i] != pattern[i]) { match = false; break; }
  Serial.print(F("[FRAW] verify: "));
  Serial.println(match ? "OK" : "MISMATCH");
  Serial.flush();

  /* (5) Cleanup — erase pattern */
  irqs = save_and_disable_interrupts();
  flash_range_erase(TEST_OFFSET, 4096);
  restore_interrupts(irqs);
  Serial.println(F("[FRAW] cleanup erase done"));
  Serial.flush();

  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  static uint32_t tick = 0;
  static uint32_t lastMs = 0;
  if (millis() - lastMs >= 1000) {
    lastMs = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    Serial.print(F("[FRAW] (loop) heartbeat tick="));
    Serial.println(tick++);
    Serial.flush();
  }
}
