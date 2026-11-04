// Footswitch engine – same behaviour as the Raspberry Pi version (stompboxd.py):
// debounce, press / hold / momentary / combos, stuck-pin protection at start.
#include <LittleFS.h>
#include <mutex>
#include "common.h"

static std::recursive_mutex mtx;
static Settings S;
static bool raw[MAX_BUTTONS], stable[MAX_BUTTONS], sim[MAX_BUTTONS];
static uint32_t changedAt[MAX_BUTTONS];
struct St { bool down = false, resolved = false, consumed = false, momentary = false; uint32_t t = 0; };
static St st[MAX_BUTTONS];

// ------------------------------------------------------------------ actions
void runAction(const Action& a, Phase phase) {
  int e = a.effect;
  switch (a.type) {
    case A_NONE: return;
    case A_TOGGLE: {
      bool on = !midiEffectOn(e);
      midiSetEffect(e, on);
      logf("%s %s", EFFECTS[e].name, on ? "ON" : "OFF");
      return;
    }
    case A_MOMENTARY:
      midiSetEffect(e, phase != UP);
      logf("%s %s", EFFECTS[e].name, phase == UP ? "OFF (released)" : "ON (held)");
      return;
    case A_ON: case A_OFF:
      midiSetEffect(e, a.type == A_ON);
      logf("%s %s", EFFECTS[e].name, a.type == A_ON ? "ON" : "OFF");
      return;
    case A_ALL_OFF: case A_ALL_ON:
      for (int i = 0; i < 5; i++) midiSetEffect(i, a.type == A_ALL_ON);
      logf("all effects %s", a.type == A_ALL_ON ? "ON" : "OFF");
      return;
    case A_PRESET_NEXT: case A_PRESET_PREV: {
      int cur = midiProgramNow() < 0 ? 0 : midiProgramNow();
      midiProgram(cur + (a.type == A_PRESET_NEXT ? 1 : -1));
      logf("preset %d", midiProgramNow());
      return;
    }
    case A_PRESET:
      midiProgram(a.preset);
      logf("preset %d", a.preset);
      return;
  }
}

// ------------------------------------------------------------------- engine
static bool readPin(int i) { return digitalRead(S.prof().b[i].gpio) == LOW; }

static bool inCombo(int i) {
  const Profile& p = S.prof();
  for (int j = 0; j < p.nc; j++) if (p.c[j].mask & (1 << i)) return true;
  return false;
}

static void onDown(int i, uint32_t now) {
  const Profile& p = S.prof();
  st[i] = St();
  st[i].down = true;
  st[i].t = now;
  uint8_t live = 0;
  for (int j = 0; j < p.n; j++)
    if (st[j].down && !st[j].resolved && !st[j].consumed && now - st[j].t <= S.comboMs) live |= 1 << j;
  for (int c = 0; c < p.nc; c++) {
    if (p.c[c].mask == live) {
      String names;
      for (int j = 0; j < p.n; j++) if (live & (1 << j)) { st[j].consumed = true; names += (names.length() ? "+" : "") + String(j + 1); }
      logf("combo %s", names.c_str());
      runAction(p.c[c].action);
      return;
    }
  }
  if (p.b[i].press.type == A_MOMENTARY && !inCombo(i)) {
    st[i].resolved = st[i].momentary = true;
    runAction(p.b[i].press, DOWN);
  }
}

static void tick(uint32_t now) {
  const Profile& p = S.prof();
  for (int i = 0; i < p.n; i++) {
    St& s = st[i];
    if (!s.down || s.resolved || s.consumed) continue;
    uint32_t age = now - s.t;
    if (p.b[i].hold.type != A_NONE) {
      if (age >= S.holdMs) {
        s.resolved = true;
        logf("button %d hold", i + 1);
        runAction(p.b[i].hold);
      }
    } else if (!inCombo(i) || age >= S.comboMs) {
      s.resolved = true;
      if (p.b[i].press.type == A_MOMENTARY) { s.momentary = true; runAction(p.b[i].press, DOWN); }
      else runAction(p.b[i].press);
    }
  }
}

static void onUp(int i) {
  const Profile& p = S.prof();
  St s = st[i];
  st[i] = St();
  if (!s.down || s.consumed) return;
  if (s.momentary) runAction(p.b[i].press, UP);
  else if (!s.resolved) {
    if (p.b[i].press.type == A_MOMENTARY) { runAction(p.b[i].press, DOWN); runAction(p.b[i].press, UP); }
    else runAction(p.b[i].press);
  }
}

static void engineTask(void*) {
  for (;;) {
    {
      std::lock_guard<std::recursive_mutex> lock(mtx);
      uint32_t now = millis();
      const Profile& p = S.prof();
      for (int i = 0; i < p.n; i++) {
        bool r = readPin(i) || sim[i];
        if (r != raw[i]) { raw[i] = r; changedAt[i] = now; }
        if (r != stable[i] && now - changedAt[i] >= DEBOUNCE_MS) {
          stable[i] = r;
          if (r) onDown(i, now); else onUp(i);
        }
      }
      tick(now);
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

void engineApply(const Settings& s) {
  std::lock_guard<std::recursive_mutex> lock(mtx);
  S = s;
  const Profile& p = S.prof();
  for (int i = 0; i < p.n; i++) pinMode(p.b[i].gpio, INPUT_PULLUP);
  delay(5);
  String stuck;
  for (int i = 0; i < MAX_BUTTONS; i++) {
    st[i] = St();
    sim[i] = false;
    // a pin already reading "pressed" (stuck or mis-wired) must not fire:
    // start from its current state so it only acts after a release + press
    raw[i] = stable[i] = i < p.n && readPin(i);
    changedAt[i] = millis();
    if (raw[i]) stuck += (stuck.length() ? "," : "") + String(i + 1);
  }
  if (stuck.length()) logf("warning: footswitch [%s] reads pressed at start (check wiring) - ignored until released", stuck.c_str());
  logf("profile %d buttons active", S.active);
}

void engineBegin() {
  Settings s = defaultSettings();
  JsonDocument doc;
  String err;
  if (readJson("/config.json", doc)) {
    if (!parseSettings(doc.as<JsonVariantConst>(), s, err)) {
      logf("config.json invalid (%s) - using defaults", err.c_str());
      s = defaultSettings();
    }
  }
  engineApply(s);
  xTaskCreate(engineTask, "engine", 4096, nullptr, 5, nullptr);
}

Settings engineSettings() {
  std::lock_guard<std::recursive_mutex> lock(mtx);
  return S;
}

void engineSimulate(int i, bool down) {
  std::lock_guard<std::recursive_mutex> lock(mtx);
  if (i >= 0 && i < S.prof().n) sim[i] = down;
}

void enginePressed(JsonArray out) {
  std::lock_guard<std::recursive_mutex> lock(mtx);
  for (int i = 0; i < S.prof().n; i++) out.add(stable[i]);
}

void enginePinsLow(JsonArray out) {
  std::lock_guard<std::recursive_mutex> lock(mtx);
  for (int i = 0; i < S.prof().n; i++) out.add(readPin(i));
}
