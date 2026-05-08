/* Minimal recovery firmware for Pico W brick recovery.
 * Blinks onboard LED + echoes serial input. */
void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  delay(500);
  Serial.println();
  Serial.println("=== pico_blink_echo recovery firmware ===");
  Serial.println("Press any key to verify USB CDC RX works");
}
unsigned long last = 0;
bool led = false;
void loop() {
  if (millis() - last > 500) {
    last = millis();
    led = !led;
    digitalWrite(LED_BUILTIN, led ? HIGH : LOW);
    Serial.printf("[%lu] tick led=%d\n", millis(), led ? 1 : 0);
  }
  while (Serial.available()) {
    int c = Serial.read();
    Serial.printf("rx 0x%02X '%c'\n", c, (c>=32&&c<127)?(char)c:'.');
  }
}
