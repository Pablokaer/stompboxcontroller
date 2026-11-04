// Settings model, validation and JSON – same format as the Raspberry Pi version
// (/etc/stompbox/config.json there, /config.json in LittleFS here).
#include <LittleFS.h>
#include <mutex>
#include "common.h"

static const int ALLOWED[] = ALLOWED_PINS;
static const int DEFAULTS[] = DEFAULT_PINS;
static const char* TYPE_NAMES[] = {"none", "toggle", "momentary", "on", "off", "all_off", "all_on",
                                   "preset_next", "preset_prev", "preset"};

int effectIndex(const char* key) {
  for (int i = 0; i < 5; i++)
    if (key && !strcmp(key, EFFECTS[i].key)) return i;
  return -1;
}

bool pinAllowed(int gpio) {
  for (int p : ALLOWED) if (p == gpio) return true;
  return false;
}

bool parseAction(JsonVariantConst v, Action& out, bool allowMomentary, String& err) {
  const char* t = v["type"] | "";
  int type = -1;
  for (int i = 0; i < 10; i++) if (!strcmp(t, TYPE_NAMES[i])) type = i;
  if (type < 0) { err = String("unknown action ") + t; return false; }
  out = Action();
  out.type = (ActType)type;
  if (out.type == A_MOMENTARY && !allowMomentary) { err = "momentary is only allowed as a press action"; return false; }
  if (out.type == A_TOGGLE || out.type == A_MOMENTARY || out.type == A_ON || out.type == A_OFF) {
    out.effect = effectIndex(v["effect"] | "");
    if (out.effect < 0) { err = String("action ") + t + " needs an effect"; return false; }
  }
  if (out.type == A_PRESET) {
    int p = v["preset"] | -1;
    if (p < 0 || p > 127) { err = "preset must be 0-127"; return false; }
    out.preset = p;
  }
  return true;
}

void actionToJson(const Action& a, JsonObject o) {
  o["type"] = TYPE_NAMES[a.type];
  if (a.effect >= 0) o["effect"] = EFFECTS[a.effect].key;
  if (a.type == A_PRESET) o["preset"] = a.preset;
}

static Profile defaultProfile(int n) {
  Profile p{};
  p.n = n;
  const int order[5] = {0, 1, 2, 4, 3};   // MOD, DELAY, REVERB, EQ, GATE
  for (int i = 0; i < n; i++) {
    p.b[i].gpio = DEFAULTS[i];
    p.b[i].press.type = A_TOGGLE;
    p.b[i].press.effect = order[i];
  }
  return p;
}

Settings defaultSettings() {
  Settings s;
  s.p4 = defaultProfile(4);
  s.p5 = defaultProfile(5);
  return s;
}

static bool parseProfile(JsonVariantConst v, int n, Profile& p, String& err) {
  p = Profile{};
  p.n = n;
  JsonArrayConst buttons = v["buttons"];
  if (buttons.size() != (size_t)n) { err = "profile " + String(n) + " needs " + String(n) + " buttons"; return false; }
  for (int i = 0; i < n; i++) {
    JsonVariantConst b = buttons[i];
    int g = b["gpio"] | -1;
    if (!pinAllowed(g)) { err = "GPIO" + String(g) + " is not allowed"; return false; }
    for (int j = 0; j < i; j++)
      if (p.b[j].gpio == g) { err = "profile " + String(n) + ": GPIO" + String(g) + " used twice"; return false; }
    p.b[i].gpio = g;
    if (!parseAction(b["press"], p.b[i].press, true, err)) return false;
    if (!b["hold"].isNull() && !parseAction(b["hold"], p.b[i].hold, false, err)) return false;
    if (p.b[i].press.type == A_MOMENTARY && p.b[i].hold.type != A_NONE) {
      err = "button " + String(i + 1) + ": a momentary button cannot also have a hold action"; return false;
    }
  }
  for (JsonVariantConst c : v["combos"].as<JsonArrayConst>()) {
    if (p.nc >= MAX_COMBOS) { err = "too many combos"; return false; }
    uint8_t mask = 0;
    for (JsonVariantConst x : c["buttons"].as<JsonArrayConst>()) {
      int i = x | -1;
      if (i < 0 || i >= n) { err = "a combo needs 2 or more valid buttons"; return false; }
      mask |= 1 << i;
    }
    if (__builtin_popcount(mask) < 2) { err = "a combo needs 2 or more valid buttons"; return false; }
    for (int j = 0; j < p.nc; j++) if (p.c[j].mask == mask) { err = "a combo is defined twice"; return false; }
    p.c[p.nc].mask = mask;
    if (!parseAction(c["action"], p.c[p.nc].action, false, err)) return false;
    p.nc++;
  }
  return true;
}

bool parseSettings(JsonVariantConst v, Settings& s, String& err) {
  Settings out;
  String ap = v["active_profile"] | "4";
  if (ap != "4" && ap != "5") { err = "active_profile must be 4 or 5"; return false; }
  out.active = ap.toInt();
  int hold = v["hold_ms"] | DEFAULT_HOLD_MS, combo = v["combo_ms"] | DEFAULT_COMBO_MS;
  if (hold < 200 || hold > 3000) { err = "hold time must be 200-3000 ms"; return false; }
  if (combo < 30 || combo > 500) { err = "combo window must be 30-500 ms"; return false; }
  out.holdMs = hold;
  out.comboMs = combo;
  if (!parseProfile(v["profiles"]["4"], 4, out.p4, err)) return false;
  if (!parseProfile(v["profiles"]["5"], 5, out.p5, err)) return false;
  s = out;
  return true;
}

static void profileToJson(const Profile& p, JsonObject o) {
  JsonArray bs = o["buttons"].to<JsonArray>();
  for (int i = 0; i < p.n; i++) {
    JsonObject b = bs.add<JsonObject>();
    b["gpio"] = p.b[i].gpio;
    actionToJson(p.b[i].press, b["press"].to<JsonObject>());
    actionToJson(p.b[i].hold, b["hold"].to<JsonObject>());
  }
  JsonArray cs = o["combos"].to<JsonArray>();
  for (int j = 0; j < p.nc; j++) {
    JsonObject c = cs.add<JsonObject>();
    JsonArray idx = c["buttons"].to<JsonArray>();
    for (int i = 0; i < p.n; i++) if (p.c[j].mask & (1 << i)) idx.add(i);
    actionToJson(p.c[j].action, c["action"].to<JsonObject>());
  }
}

void settingsToJson(const Settings& s, JsonObject o) {
  o["active_profile"] = String(s.active);
  o["hold_ms"] = s.holdMs;
  o["combo_ms"] = s.comboMs;
  JsonObject ps = o["profiles"].to<JsonObject>();
  profileToJson(s.p4, ps["4"].to<JsonObject>());
  profileToJson(s.p5, ps["5"].to<JsonObject>());
}

// ------------------------------------------------------------------ log
static String LOG[40];
static int logHead = 0, logCount = 0;
static std::mutex logMux;

void logf(const char* fmt, ...) {
  char msg[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  uint32_t t = millis() / 1000;
  char line[180];
  snprintf(line, sizeof line, "%02lu:%02lu:%02lu %s", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60),
           (unsigned long)(t % 60), msg);
  Serial.println(line);
  std::lock_guard<std::mutex> lock(logMux);
  LOG[logHead] = line;
  logHead = (logHead + 1) % 40;
  if (logCount < 40) logCount++;
}

void logToJson(JsonArray arr, int max) {
  std::lock_guard<std::mutex> lock(logMux);
  int n = min(max, logCount);
  for (int i = 0; i < n; i++) arr.add(LOG[(logHead - 1 - i + 40) % 40]);
}

// ---------------------------------------------------------------- files
bool readJson(const char* path, JsonDocument& doc) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  return !e;
}

bool writeJson(const char* path, const JsonDocument& doc) {
  String tmp = String(path) + ".tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  LittleFS.remove(path);
  return LittleFS.rename(tmp, path);
}
