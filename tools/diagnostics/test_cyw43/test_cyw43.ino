/* test_cyw43 — valida CYW43 chip do Pico W via WiFi connect/disconnect cycle.
 *
 * Saída esperada na serial:
 *   [CYW43] alive
 *   [CYW43] init OK
 *   [CYW43] scan: <N> APs found
 *   [CYW43] (loop) heartbeat tick=N
 *
 * Se aparecer "alive" mas não "init OK" → CYW43 init falhou (chip travado).
 * Se aparecer "init OK" mas não scan → SPI bus bom mas radio prob.
 * Se nenhum output → USB CDC ou Serial.begin falhando (não é CYW43).
 */
#include <Arduino.h>
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  /* Espera USB CDC enumerar (até 3s). */
  for (int i = 0; i < 30 && !Serial; i++) delay(100);
  delay(500);

  Serial.println();
  Serial.println(F("=== test_cyw43 ==="));
  Serial.println(F("[CYW43] alive — Serial.begin OK"));
  Serial.flush();

  Serial.println(F("[CYW43] calling WiFi.mode(WIFI_STA)..."));
  Serial.flush();
  WiFi.mode(WIFI_STA);
  Serial.println(F("[CYW43] init OK"));
  Serial.flush();

  Serial.println(F("[CYW43] starting scan..."));
  Serial.flush();
  int n = WiFi.scanNetworks();
  Serial.print(F("[CYW43] scan: "));
  Serial.print(n);
  Serial.println(F(" APs found"));
  for (int i = 0; i < n && i < 10; i++) {
    Serial.print(F("  "));
    Serial.print(i);
    Serial.print(F(": "));
    Serial.print(WiFi.SSID(i));
    Serial.print(F(" ("));
    Serial.print(WiFi.RSSI(i));
    Serial.println(F("dBm)"));
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
    Serial.print(F("[CYW43] (loop) heartbeat tick="));
    Serial.println(tick++);
    Serial.flush();
  }
}
