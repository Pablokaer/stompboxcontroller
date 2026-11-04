// =============================================================================
//  STOMPBOX Controller – ESP32-C3 firmware
//  https://github.com/Pablokaer/stompboxcontroller  (see esp32-c3/README.md)
//
//  Same features as the Raspberry Pi version: pedal over Bluetooth MIDI,
//  4/5 footswitches with press/hold/combos, own Wi-Fi hotspot with captive
//  portal and the same settings page.
// =============================================================================
#include <LittleFS.h>
#include "common.h"

static void led(bool on) { digitalWrite(STATUS_LED_PIN, (on ^ STATUS_LED_ACTIVE_LOW) ? HIGH : LOW); }

void setup() {
  Serial.begin(115200);
  pinMode(STATUS_LED_PIN, OUTPUT);
  led(true);
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  if (!LittleFS.begin(true)) Serial.println("LittleFS failed");
  logf("stompbox %s (%s) starting", STOMPBOX_VERSION, STOMPBOX_PLATFORM);
  pedalBegin();      // Bluetooth MIDI + pedal state (used by the engine)
  engineBegin();     // footswitches
  netBegin();        // hotspot, Wi-Fi, settings page
  led(false);
}

void loop() {
  netLoop();

  // status LED: on = pedal connected, slow blink = searching / connecting
  static uint32_t lastBlink = 0;
  if (midiConnected()) led(true);
  else if (millis() - lastBlink > 500) { lastBlink = millis(); led((millis() / 500) % 2); }

  // factory reset: hold the BOOT button for 10 seconds while running
  static uint32_t bootSince = 0;
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    if (!bootSince) bootSince = millis();
    if (millis() - bootSince > 10000) {
      logf("factory reset");
      for (const char* f : {"/config.json", "/pedal.json", "/wifi.json", "/hotspot.json"}) LittleFS.remove(f);
      delay(300);
      ESP.restart();
    }
  } else bootSince = 0;

  delay(2);
}
