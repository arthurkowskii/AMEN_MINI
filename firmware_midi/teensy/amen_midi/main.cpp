// AMEN MIDI — Teensy 4.1 entry point.
// Lives outside the portable core so src/ never sees Arduino headers.
// Built with PlatformIO (firmware_midi/platformio.ini).

#include "teensy_port.h"

static amen::PerformanceEngine engine;
static amen::teensy::TeensyPort port;

void setup() {
  Serial.begin(115200);
  delay(200);  // let the USB stack settle
  port.begin();
  Serial.println("AMEN MIDI boot");
}

void loop() {
  const uint32_t now = millis();
  // scan inputs -> tick scheduler -> drain USB-MIDI -> resync if the host
  // queue saturated (drain again). No I/O happens inside tick() itself.
  amen::teensy::service(port, engine, now);
  port.refreshDisplay(engine, now);
}
