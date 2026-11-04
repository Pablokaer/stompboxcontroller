// Unit tests for the ESP32-C3 firmware logic, run on a computer (no board needed):
//     cd esp32-c3 && pio test -e native
// Same footswitch scenarios as the Raspberry Pi tests (tests/test_engine.py),
// settings validation, and the BLE-MIDI parser with packets recorded from the pedal.
#include <unity.h>
#include <LittleFS.h>
#include <mutex>
#include <string>
#include <vector>
#include "../../src/blemidi.h"
#include "../../src/common.h"

int fakePinLevel[64];
FakeSerial Serial;
FakeFS LittleFS;

// ---- fake MIDI side: records what would be sent to the pedal --------------
static std::mutex m;
static std::vector<std::string> sent;
static uint8_t cc[128];
static int program = 98;

bool midiConnected() { return true; }
void midiCC(uint8_t c, uint8_t v) {
  char b[16];
  snprintf(b, sizeof b, "B0 %02X %02X", c, v);
  std::lock_guard<std::mutex> l(m);
  sent.push_back(b);
  cc[c] = v;
}
void midiProgram(int p) {
  p = p < 0 ? 0 : p > 127 ? 127 : p;
  char b[16];
  snprintf(b, sizeof b, "C0 %02X", p);
  std::lock_guard<std::mutex> l(m);
  sent.push_back(b);
  program = p;
}
int midiProgramNow() { return program; }
bool midiEffectOn(int i) { return cc[EFFECTS[i].cc] & 64; }
void midiSetEffect(int i, bool on) {   // same rule as pedal.cpp: keep the effect-type bits
  uint8_t cur = cc[EFFECTS[i].cc];
  midiCC(EFFECTS[i].cc, on ? (cur | 64) : (cur & ~64));
}

// ---- helpers ---------------------------------------------------------------
static std::vector<std::string> collect(void (*fn)()) {
  { std::lock_guard<std::mutex> l(m); sent.clear(); }
  fn();
  delay(250);
  std::lock_guard<std::mutex> l(m);
  return sent;
}
static void expect(const std::vector<std::string>& got, std::vector<std::string> want) {
  std::string g, w;
  for (auto& x : got) g += x + "|";
  for (auto& x : want) w += x + "|";
  TEST_ASSERT_EQUAL_STRING(w.c_str(), g.c_str());
}
static void tap(int i, int ms) { engineSimulate(i, true); delay(ms); engineSimulate(i, false); }

static Settings testSettings() {
  const char* json = R"({"active_profile":"4","hold_ms":600,"combo_ms":120,"profiles":{
    "4":{"buttons":[{"gpio":3,"press":{"type":"toggle","effect":"mod"},"hold":{"type":"none"}},
                    {"gpio":4,"press":{"type":"toggle","effect":"delay"},"hold":{"type":"preset_next"}},
                    {"gpio":5,"press":{"type":"momentary","effect":"reverb"},"hold":{"type":"none"}},
                    {"gpio":6,"press":{"type":"toggle","effect":"eq"},"hold":{"type":"none"}}],
         "combos":[{"buttons":[0,3],"action":{"type":"all_off"}}]},
    "5":{"buttons":[{"gpio":3,"press":{"type":"toggle","effect":"mod"}},{"gpio":4,"press":{"type":"toggle","effect":"delay"}},
                    {"gpio":5,"press":{"type":"toggle","effect":"reverb"}},{"gpio":6,"press":{"type":"toggle","effect":"eq"}},
                    {"gpio":7,"press":{"type":"toggle","effect":"gate"}}],"combos":[]}}})";
  JsonDocument d;
  deserializeJson(d, json);
  Settings s;
  String err;
  TEST_ASSERT_TRUE_MESSAGE(parseSettings(d.as<JsonVariantConst>(), s, err), err.c_str());
  return s;
}

// ---- footswitch engine (same scenarios as tests/test_engine.py) -------------
void test_1_tap_toggles() { expect(collect([] { tap(0, 60); }), {"B0 03 00"}); }
void test_2_tap_on_button_with_hold_fires_press() { expect(collect([] { tap(1, 100); }), {"B0 04 00"}); }
void test_3_hold_fires_hold_action() { expect(collect([] { tap(1, 800); }), {"C0 63"}); }
void test_4_momentary_on_while_held() { expect(collect([] { tap(2, 400); }), {"B0 05 41", "B0 05 01"}); }
void test_5_combo() {
  expect(collect([] {
           engineSimulate(0, true); delay(40); engineSimulate(3, true);
           delay(300); engineSimulate(0, false); engineSimulate(3, false);
         }),
         {"B0 03 00", "B0 04 00", "B0 05 01", "B0 06 00", "B0 07 00"});
}
void test_6_effect_type_bits_are_kept() {
  cc[3] = 69;   // MOD on, effect type 5
  expect(collect([] { tap(0, 60); }), {"B0 03 05"});
}
void test_7_real_pin_press() {
  cc[7] = 64;   // EQ on
  expect(collect([] { fakePinLevel[6] = LOW; delay(80); fakePinLevel[6] = HIGH; }), {"B0 07 00"});
}
void test_8_stuck_pin_ignored_until_released() {
  fakePinLevel[6] = LOW;                     // FS4 already "pressed" at start
  engineApply(testSettings());
  expect(collect([] { delay(100); }), {});
  cc[7] = 0;
  expect(collect([] { fakePinLevel[6] = HIGH; delay(60); fakePinLevel[6] = LOW; delay(80); fakePinLevel[6] = HIGH; }),
         {"B0 07 40"});
}

// ---- settings validation (same rules as the Pi) ----------------------------
static bool parseWith(void (*change)(JsonDocument&), String& err) {
  JsonDocument d;
  settingsToJson(defaultSettings(), d.to<JsonObject>());
  change(d);
  Settings s;
  return parseSettings(d.as<JsonVariantConst>(), s, err);
}
void test_default_is_valid() {
  String err;
  TEST_ASSERT_TRUE(parseWith([](JsonDocument&) {}, err));
}
void test_bad_hold_time() {
  String err;
  TEST_ASSERT_FALSE(parseWith([](JsonDocument& d) { d["hold_ms"] = 10; }, err));
}
void test_duplicate_gpio() {
  String err;
  TEST_ASSERT_FALSE(parseWith([](JsonDocument& d) { d["profiles"]["4"]["buttons"][1]["gpio"] = 3; }, err));
}
void test_boot_pin_not_allowed() {
  String err;
  TEST_ASSERT_FALSE(parseWith([](JsonDocument& d) { d["profiles"]["4"]["buttons"][0]["gpio"] = 9; }, err));
}
void test_momentary_cannot_have_hold() {
  String err;
  TEST_ASSERT_FALSE(parseWith([](JsonDocument& d) {
    d["profiles"]["4"]["buttons"][0]["press"]["type"] = "momentary";
    d["profiles"]["4"]["buttons"][0]["hold"]["type"] = "all_off";
  }, err));
}
void test_settings_roundtrip() {
  JsonDocument a, b;
  settingsToJson(testSettings(), a.to<JsonObject>());
  Settings s;
  String err;
  TEST_ASSERT_TRUE(parseSettings(a.as<JsonVariantConst>(), s, err));
  settingsToJson(s, b.to<JsonObject>());
  std::string sa, sb;
  serializeJson(a, sa);
  serializeJson(b, sb);
  TEST_ASSERT_EQUAL_STRING(sa.c_str(), sb.c_str());
}

// ---- BLE-MIDI parser: packets recorded from the JME STOMPBOX (btmon) ---------
static std::string parse(std::vector<uint8_t> p) {
  std::string out;
  parseBleMidi(p.data(), p.size(),
               [&](uint8_t c, uint8_t v) { out += "CC" + std::to_string(c) + "=" + std::to_string(v) + " "; },
               [&](uint8_t pr) { out += "PC" + std::to_string(pr) + " "; });
  return out;
}
void test_blemidi_recorded_packets() {
  TEST_ASSERT_EQUAL_STRING("PC95 ", parse({0xbe, 0xe4, 0xc0, 0x5f}).c_str());
  TEST_ASSERT_EQUAL_STRING("CC0=0 ", parse({0xbe, 0xee, 0xb0, 0x00, 0x00}).c_str());
  TEST_ASSERT_EQUAL_STRING("CC2=7 ", parse({0xbe, 0xf8, 0xb0, 0x02, 0x07}).c_str());
}
void test_blemidi_several_messages_running_status_sysex() {
  TEST_ASSERT_EQUAL_STRING("CC3=64 PC5 ", parse({0x80, 0x80, 0xb0, 0x03, 0x40, 0x81, 0xc0, 0x05}).c_str());
  TEST_ASSERT_EQUAL_STRING("CC4=0 CC5=1 ", parse({0x80, 0x80, 0xb0, 0x04, 0x00, 0x05, 0x01}).c_str());   // running status
  TEST_ASSERT_EQUAL_STRING("CC6=64 ", parse({0x80, 0x80, 0xf0, 0x7e, 0x01, 0xf7, 0x80, 0xb0, 0x06, 0x40}).c_str());
  TEST_ASSERT_EQUAL_STRING("", parse({0x80, 0x80}).c_str());          // too short / truncated
  TEST_ASSERT_EQUAL_STRING("", parse({0x80, 0x80, 0xb0, 0x03}).c_str());
}

void setUp() {}
void tearDown() {}

int main() {
  for (auto& p : fakePinLevel) p = HIGH;
  for (auto& e : EFFECTS) cc[e.cc] = e.on;
  engineBegin();
  engineApply(testSettings());
  UNITY_BEGIN();
  RUN_TEST(test_1_tap_toggles);
  RUN_TEST(test_2_tap_on_button_with_hold_fires_press);
  RUN_TEST(test_3_hold_fires_hold_action);
  RUN_TEST(test_4_momentary_on_while_held);
  RUN_TEST(test_5_combo);
  RUN_TEST(test_6_effect_type_bits_are_kept);
  RUN_TEST(test_7_real_pin_press);
  RUN_TEST(test_8_stuck_pin_ignored_until_released);
  RUN_TEST(test_default_is_valid);
  RUN_TEST(test_bad_hold_time);
  RUN_TEST(test_duplicate_gpio);
  RUN_TEST(test_boot_pin_not_allowed);
  RUN_TEST(test_momentary_cannot_have_hold);
  RUN_TEST(test_settings_roundtrip);
  RUN_TEST(test_blemidi_recorded_packets);
  RUN_TEST(test_blemidi_several_messages_running_status_sysex);
  return UNITY_END();
}
