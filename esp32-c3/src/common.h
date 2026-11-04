// Shared types and helpers for the ESP32-C3 firmware.
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"

// ---------------------------------------------------------------- effects
struct Effect { const char* key; const char* name; uint8_t cc; uint8_t on; };
// Value bit 64 = on; the low bits are the effect type loaded in that slot.
static const Effect EFFECTS[5] = {
  {"mod", "MOD", 3, 64}, {"delay", "DELAY", 4, 64}, {"reverb", "REVERB", 5, 65},
  {"gate", "GATE", 6, 64}, {"eq", "EQ", 7, 64}};
int effectIndex(const char* key);

// ---------------------------------------------------------------- actions
enum ActType : uint8_t { A_NONE, A_TOGGLE, A_MOMENTARY, A_ON, A_OFF, A_ALL_OFF, A_ALL_ON,
                         A_PRESET_NEXT, A_PRESET_PREV, A_PRESET };
struct Action { ActType type = A_NONE; int8_t effect = -1; uint8_t preset = 0; };

bool parseAction(JsonVariantConst v, Action& out, bool allowMomentary, String& err);
void actionToJson(const Action& a, JsonObject o);
enum Phase { FIRE, DOWN, UP };
void runAction(const Action& a, Phase phase = FIRE);   // engine.cpp

// --------------------------------------------------------------- settings
#define MAX_BUTTONS 5
#define MAX_COMBOS 16
struct Button { uint8_t gpio; Action press, hold; };
struct Combo { uint8_t mask; Action action; };        // mask bit i = footswitch i
struct Profile { uint8_t n; Button b[MAX_BUTTONS]; uint8_t nc; Combo c[MAX_COMBOS]; };
struct Settings { uint8_t active = 4; uint16_t holdMs = DEFAULT_HOLD_MS, comboMs = DEFAULT_COMBO_MS;
                  Profile p4, p5;
                  const Profile& prof() const { return active == 5 ? p5 : p4; } };

Settings defaultSettings();
bool parseSettings(JsonVariantConst v, Settings& out, String& err);
void settingsToJson(const Settings& s, JsonObject o);
bool pinAllowed(int gpio);

// ------------------------------------------------------------ log + files
void logf(const char* fmt, ...);
void logToJson(JsonArray arr, int max);
bool readJson(const char* path, JsonDocument& doc);
bool writeJson(const char* path, const JsonDocument& doc);

// ------------------------------------------------------------- modules
// engine.cpp
void engineBegin();
void engineApply(const Settings& s);
Settings engineSettings();
void engineSimulate(int i, bool down);
void enginePressed(JsonArray out);
void enginePinsLow(JsonArray out);

// pedal.cpp
void pedalBegin();
void pedalStatus(JsonObject o);
bool pedalScan(JsonArray out, String& err);
bool pedalConnectNow(const String& mac, bool startup, const String& name, String& err);
bool pedalSetStartup(const String& mac, String& err);
void pedalForget(const String& mac);          // "" = the device in use
bool midiConnected();
void midiCC(uint8_t cc, uint8_t value);
void midiProgram(int program);
int midiProgramNow();                          // -1 = unknown
bool midiEffectOn(int i);
void midiSetEffect(int i, bool on);

// net.cpp
void netBegin();
void netLoop();
