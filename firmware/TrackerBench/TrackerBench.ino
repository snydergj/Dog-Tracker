#include <Arduino.h>

// Wireless Tracker V2 GNSS wiring, as used by Heltec's factory example.
constexpr int kGnssPower = 3;
constexpr int kGnssRx = 33;
constexpr int kGnssTx = 34;
constexpr unsigned long kBaud = 115200;

void setup() {
  Serial.begin(kBaud);
  pinMode(kGnssPower, OUTPUT);
  digitalWrite(kGnssPower, HIGH);
  Serial1.begin(kBaud, SERIAL_8N1, kGnssRx, kGnssTx);
}

void loop() {
  // Forward the GNSS module's NMEA stream over USB. The web app validates fixes.
  for (int remaining = 256; remaining > 0 && Serial1.available(); --remaining) {
    Serial.write(static_cast<uint8_t>(Serial1.read()));
  }
  delay(1);
}
