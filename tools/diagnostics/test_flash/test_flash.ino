/* test_flash — valida flash QSPI + LittleFS mount/format/write/read.
 *
 * Saída esperada na serial:
 *   [FLASH] alive
 *   [FLASH] LittleFS.begin: OK | FORMAT_REQ
 *   [FLASH] info: total=N used=M
 *   [FLASH] write+read: OK
 *   [FLASH] (loop) heartbeat tick=N
 *
 * Se aparecer "alive" mas não "LittleFS.begin" → trava em mount (LFS corrompido).
 * Se mount OK mas write fail → flash QSPI com problema.
 * Se nenhum output → USB CDC falhando (não é flash).
 */
#include <Arduino.h>
#include <LittleFS.h>

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 30 && !Serial; i++) delay(100);
  delay(500);

  Serial.println();
  Serial.println(F("=== test_flash ==="));
  Serial.println(F("[FLASH] alive — Serial.begin OK"));
  Serial.flush();

  Serial.println(F("[FLASH] LittleFS.begin..."));
  Serial.flush();
  bool ok = LittleFS.begin();
  if (!ok) {
    Serial.println(F("[FLASH] LittleFS.begin: FORMAT_REQ — formatting"));
    Serial.flush();
    if (LittleFS.format()) {
      Serial.println(F("[FLASH] format OK"));
      ok = LittleFS.begin();
    }
  }
  if (!ok) {
    Serial.println(F("[FLASH] FATAL: LittleFS unmountable"));
    while (1) { delay(1000); }
  }
  Serial.println(F("[FLASH] LittleFS.begin: OK"));
  Serial.flush();

  FSInfo info;
  LittleFS.info(info);
  Serial.print(F("[FLASH] info: total="));
  Serial.print(info.totalBytes);
  Serial.print(F(" used="));
  Serial.println(info.usedBytes);
  Serial.flush();

  /* Write + read test no /test_diag.bin */
  const char *path = "/test_diag.bin";
  uint8_t pattern[64];
  for (int i = 0; i < 64; i++) pattern[i] = (uint8_t)(i ^ 0xA5);
  File f = LittleFS.open(path, "w");
  if (!f) {
    Serial.println(F("[FLASH] open(w) FAIL"));
  } else {
    f.write(pattern, 64);
    f.close();
    f = LittleFS.open(path, "r");
    if (!f) {
      Serial.println(F("[FLASH] open(r) FAIL"));
    } else {
      uint8_t buf[64];
      size_t n = f.read(buf, 64);
      f.close();
      bool match = (n == 64);
      for (int i = 0; i < 64 && match; i++) match = (buf[i] == pattern[i]);
      Serial.print(F("[FLASH] write+read: "));
      Serial.println(match ? "OK" : "MISMATCH");
    }
    LittleFS.remove(path);
  }
  Serial.flush();

  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  static uint32_t tick = 0;
  static uint32_t lastMs = 0;
  if (millis() - lastMs >= 1000) {
    lastMs = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    Serial.print(F("[FLASH] (loop) heartbeat tick="));
    Serial.println(tick++);
    Serial.flush();
  }
}
