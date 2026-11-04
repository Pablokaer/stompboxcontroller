// =============================================================================
//  STOMPBOX Controller – ESP32-C3 firmware: board and default settings
//  Change the pins here if your board is not an ESP32-C3 SuperMini.
// =============================================================================
#pragma once

#define STOMPBOX_VERSION "1.3.0"
#define STOMPBOX_PLATFORM "esp32c3"

// ---- Footswitch pins (GPIO numbers) ------------------------------------------
// Default layout: FS1..FS5 on GPIO 3, 4, 5, 6, 7 (one row on the SuperMini header).
// Avoid GPIO 2, 8, 9 (boot "strapping" pins: a pressed switch could stop the
// board from starting), GPIO 18/19 (USB) and GPIO 11-17 (internal flash).
#define DEFAULT_PINS {3, 4, 5, 6, 7}
// Pins the settings page offers for footswitches:
#define ALLOWED_PINS {0, 1, 3, 4, 5, 6, 7, 10, 20, 21}

// ---- On-board LED and BOOT button (ESP32-C3 SuperMini) ------------------------
#define STATUS_LED_PIN 8          // blue LED, on = pedal connected, blinking = searching
#define STATUS_LED_ACTIVE_LOW 1
#define BOOT_BUTTON_PIN 9         // hold 10 s while running = factory reset

// ---- Hotspot defaults (used until changed) -----------------------------------
#define HOTSPOT_SSID "STOMPBOX-CTRL"           // can be changed on the settings page
#define HOTSPOT_PASSWORD "stompbox123"   // 8-63 characters, "" = open network
#define HOTSPOT_MAX_CLIENTS 4
#define MDNS_NAME "stompbox"             // http://stompbox.local on other networks

// ---- Pedal -------------------------------------------------------------------
#define PEDAL_NAME "JME STOMPBOX"
#define MIDI_SERVICE_UUID "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define MIDI_CHAR_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"
#define MIDI_CHANNEL 0   // 0 = MIDI channel 1

// ---- Timing defaults ---------------------------------------------------------
#define DEFAULT_HOLD_MS 600
#define DEFAULT_COMBO_MS 120
#define DEBOUNCE_MS 20
#define POLL_MS 5
