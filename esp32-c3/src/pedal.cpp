// Bluetooth MIDI: saved devices, startup device, keeping the pedal connected,
// sending/receiving MIDI. Same behaviour and JSON format as the Pi version
// (pedal.json: {"default": "<address>", "devices": [{mac, name, added, type}]}).
#include <NimBLEDevice.h>
#include <mutex>
#include "common.h"

struct Dev { String mac, name; uint8_t type; };   // type: BLE_ADDR_PUBLIC / BLE_ADDR_RANDOM

static std::recursive_mutex mtx;       // device list + state
static std::mutex radio;               // one scan / connect at a time
static std::vector<Dev> devices;
static String startupMac, targetMac;
static volatile bool connected = false;
static String state = "starting";
static TaskHandle_t task = nullptr;

static NimBLEClient* client = nullptr;
static NimBLERemoteCharacteristic* midiChr = nullptr;

static uint8_t cc[128];
static int program = -1;
static std::mutex midiMtx;

static const NimBLEUUID SVC(MIDI_SERVICE_UUID), CHR(MIDI_CHAR_UUID);

// ------------------------------------------------------------- storage
static void save() {
  JsonDocument doc;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    doc["default"] = startupMac.length() ? startupMac.c_str() : nullptr;
    JsonArray a = doc["devices"].to<JsonArray>();
    for (auto& d : devices) {
      JsonObject o = a.add<JsonObject>();
      o["mac"] = d.mac; o["name"] = d.name; o["type"] = d.type;
    }
  }
  writeJson("/pedal.json", doc);
}

static void load() {
  JsonDocument doc;
  if (!readJson("/pedal.json", doc)) return;
  for (JsonVariant d : doc["devices"].as<JsonArray>()) {
    String mac = d["mac"] | "";
    mac.toUpperCase();
    if (mac.length() == 17) devices.push_back({mac, d["name"] | mac.c_str(), (uint8_t)(d["type"] | 0)});
  }
  String def = doc["default"] | "";
  def.toUpperCase();
  for (auto& d : devices) if (d.mac == def) startupMac = def;
  if (!startupMac.length() && devices.size()) startupMac = devices[0].mac;
}

static Dev* find(const String& mac) {
  for (auto& d : devices) if (d.mac == mac) return &d;
  return nullptr;
}

static String nameOf(const String& mac) {
  Dev* d = find(mac);
  return d ? d->name : String(PEDAL_NAME);
}

static void wake() { if (task) xTaskNotifyGive(task); }
static void setState(const char* v) { std::lock_guard<std::recursive_mutex> lock(mtx); state = v; }
static bool stateIs(const char* v) { std::lock_guard<std::recursive_mutex> lock(mtx); return state == v; }
static void waitMs(uint32_t ms) { ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms)); }

// ---------------------------------------------------------------- MIDI
static void resetMidiState() {
  std::lock_guard<std::mutex> lock(midiMtx);
  memset(cc, 0, sizeof cc);
  for (auto& e : EFFECTS) cc[e.cc] = e.on;   // assume all on until the pedal tells us
  program = -1;
}

// BLE-MIDI packet: header, then [timestamp] [status] data… (running status allowed)
static void onNotify(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  if (n < 3 || !(d[0] & 0x80)) return;
  uint8_t status = 0;
  size_t i = 1;
  std::lock_guard<std::mutex> lock(midiMtx);
  while (i < n) {
    if (d[i] & 0x80) {                      // timestamp byte
      if (++i >= n) break;
      if (d[i] & 0x80) {
        status = d[i++];
        if (status == 0xF0) { while (i < n && d[i] != 0xF7) i++; i++; status = 0; continue; }   // skip SysEx
      }
    }
    uint8_t type = status & 0xF0;
    size_t need = (type == 0xC0 || type == 0xD0) ? 1 : (status ? 2 : 0);
    if (!need || i + need > n) break;
    if (type == 0xB0) cc[d[i] & 0x7F] = d[i + 1] & 0x7F;
    else if (type == 0xC0) program = d[i] & 0x7F;
    i += need;
  }
}

static bool send3(uint8_t a, uint8_t b, uint8_t c, size_t len) {
  if (!connected || !midiChr) { logf("pedal not connected - command dropped"); return false; }
  uint8_t pkt[5] = {0x80, 0x80, a, b, c};
  return midiChr->writeValue(pkt, len, false);
}

bool midiConnected() { return connected && midiChr; }

void midiCC(uint8_t ccNum, uint8_t value) {
  if (send3(0xB0 | MIDI_CHANNEL, ccNum & 0x7F, value & 0x7F, 5)) {
    std::lock_guard<std::mutex> lock(midiMtx);
    cc[ccNum & 0x7F] = value & 0x7F;
  }
}

void midiProgram(int p) {
  p = constrain(p, 0, 127);
  if (send3(0xC0 | MIDI_CHANNEL, p, 0, 4)) {
    std::lock_guard<std::mutex> lock(midiMtx);
    program = p;
  }
}

int midiProgramNow() { std::lock_guard<std::mutex> lock(midiMtx); return program; }

bool midiEffectOn(int i) {
  std::lock_guard<std::mutex> lock(midiMtx);
  return cc[EFFECTS[i].cc] & 64;
}

void midiSetEffect(int i, bool on) {
  uint8_t cur;
  { std::lock_guard<std::mutex> lock(midiMtx); cur = cc[EFFECTS[i].cc]; }
  midiCC(EFFECTS[i].cc, on ? (cur | 64) : (cur & ~64));   // keep the effect-type bits
}

// ----------------------------------------------------------- bluetooth
class ClientCB : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient*) override {
    connected = false;
    midiChr = nullptr;
    wake();
  }
};
static ClientCB clientCB;

static bool connectTo(const String& mac, uint8_t type) {
  std::lock_guard<std::mutex> lock(radio);
  if (!client) {
    client = NimBLEDevice::createClient();
    client->setClientCallbacks(&clientCB, false);
    client->setConnectTimeout(6);
  }
  if (client->isConnected()) client->disconnect();
  if (!client->connect(NimBLEAddress(std::string(mac.c_str()), type))) {
    // address type unknown or wrong: try the other one once
    if (!client->connect(NimBLEAddress(std::string(mac.c_str()), type ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM)))
      return false;
  }
  NimBLERemoteService* svc = client->getService(SVC);
  NimBLERemoteCharacteristic* chr = svc ? svc->getCharacteristic(CHR) : nullptr;
  if (!chr) { logf("%s has no Bluetooth MIDI service", mac.c_str()); client->disconnect(); return false; }
  if (chr->canNotify()) chr->subscribe(true, onNotify);
  midiChr = chr;
  connected = true;
  return true;
}

struct Seen { String mac, name; int rssi; bool midi; uint8_t type; };

static std::vector<Seen> scanNow(uint32_t secs) {
  std::lock_guard<std::mutex> lock(radio);
  NimBLEScan* s = NimBLEDevice::getScan();
  s->setActiveScan(true);
  s->setInterval(100);
  s->setWindow(60);
  NimBLEScanResults r = s->start(secs, false);
  std::vector<Seen> out;
  for (int i = 0; i < r.getCount(); i++) {
    NimBLEAdvertisedDevice d = r.getDevice(i);
    String mac = d.getAddress().toString().c_str();
    mac.toUpperCase();
    String name = d.haveName() ? String(d.getName().c_str()) : mac;
    out.push_back({mac, name, d.getRSSI(), d.isAdvertisingService(SVC), d.getAddress().getType()});
  }
  s->clearResults();
  return out;
}

static void pedalTask(void*) {
  uint32_t misses = 0;
  String lastTarget = "-";
  for (;;) {
    String mac;
    bool haveDevices;
    { std::lock_guard<std::recursive_mutex> lock(mtx); mac = targetMac; haveDevices = !devices.empty(); }
    if (mac != lastTarget) { resetMidiState(); lastTarget = mac; }
    if (mac.length() && connected) {
      if (!stateIs("connected")) logf("connected to %s (%s)", nameOf(mac).c_str(), mac.c_str());
      setState("connected"); misses = 0;
      waitMs(1000);
      continue;
    }
    if (stateIs("connected")) logf("%s disconnected", nameOf(mac).c_str());
    if (!mac.length() && haveDevices) { setState("no device selected"); waitMs(3000); continue; }
    setState(mac.length() ? "connecting" : "searching");
    if (mac.length()) {
      uint8_t type;
      { std::lock_guard<std::recursive_mutex> lock(mtx); Dev* d = find(mac); type = d ? d->type : BLE_ADDR_PUBLIC; }
      if (connectTo(mac, type)) continue;
    } else {
      std::vector<Seen> seen = scanNow(5);
      std::sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) { return a.rssi > b.rssi; });
      for (auto& c : seen) {
        if (!c.midi || c.name != PEDAL_NAME) continue;
        logf("found %s %s - connecting", PEDAL_NAME, c.mac.c_str());
        if (connectTo(c.mac, c.type)) {
          {
            std::lock_guard<std::recursive_mutex> lock(mtx);
            if (!targetMac.length() && devices.empty()) {
              devices.push_back({c.mac, c.name, c.type});
              startupMac = targetMac = c.mac;
            }
          }
          save();
          logf("%s %s saved as startup device", PEDAL_NAME, c.mac.c_str());
          break;
        }
      }
      if (connected) continue;
    }
    // off or out of range: back off so the shared radio stays free for Wi-Fi
    misses++;
    waitMs(min<uint32_t>(2000 + 3000 * misses, 20000));
  }
}

// ------------------------------------------------------------ public API
void pedalBegin() {
  resetMidiState();
  NimBLEDevice::init("STOMPBOX-CTRL");
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  load();
  targetMac = startupMac;
  xTaskCreate(pedalTask, "pedal", 6144, nullptr, 3, &task);
}

void pedalStatus(JsonObject o) {
  std::lock_guard<std::recursive_mutex> lock(mtx);
  o["name"] = nameOf(targetMac);
  o["mac"] = targetMac.length() ? targetMac.c_str() : nullptr;
  o["state"] = state;
  o["connected"] = (bool)connected;
  o["default"] = startupMac.length() ? startupMac.c_str() : nullptr;
  JsonArray a = o["devices"].to<JsonArray>();
  for (auto& d : devices) {
    JsonObject x = a.add<JsonObject>();
    x["mac"] = d.mac; x["name"] = d.name; x["added"] = "";
    x["startup"] = d.mac == startupMac; x["active"] = d.mac == targetMac;
  }
}

bool pedalScan(JsonArray out, String& err) {
  std::vector<Seen> seen = scanNow(5);
  std::lock_guard<std::recursive_mutex> lock(mtx);
  std::vector<String> listed;
  auto add = [&](const String& mac, const String& name, int rssi, bool hasRssi, bool midi) {
    JsonObject x = out.add<JsonObject>();
    x["mac"] = mac; x["name"] = name; x["midi"] = midi;
    if (hasRssi) x["rssi"] = rssi; else x["rssi"] = nullptr;
    x["saved"] = find(mac) != nullptr; x["startup"] = mac == startupMac; x["active"] = mac == targetMac;
    x["connected"] = mac == targetMac && connected;
    listed.push_back(mac);
  };
  std::sort(seen.begin(), seen.end(), [](const Seen& a, const Seen& b) {
    return a.midi != b.midi ? a.midi : a.rssi > b.rssi; });
  for (auto& s : seen) add(s.mac, s.name, s.rssi, true, s.midi);
  // saved devices not advertising right now (e.g. the connected pedal)
  for (auto& d : devices)
    if (std::find(listed.begin(), listed.end(), d.mac) == listed.end()) add(d.mac, d.name, 0, false, true);
  return true;
}

bool pedalConnectNow(const String& macIn, bool startup, const String& name, String& err) {
  String mac = macIn;
  mac.toUpperCase();
  if (mac.length() != 17) { err = "invalid device address"; return false; }
  String old;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!find(mac)) devices.push_back({mac, name.length() ? name : mac, BLE_ADDR_PUBLIC});
    if (startup || !startupMac.length()) startupMac = mac;
    old = targetMac;
    targetMac = mac;
  }
  save();
  if (old.length() && old != mac) {
    connected = false;
    if (client && client->isConnected()) client->disconnect();
  }
  setState("connecting");
  logf("switching to %s (%s)%s", nameOf(mac).c_str(), mac.c_str(), mac == startupMac ? " - startup device" : "");
  wake();
  return true;
}

bool pedalSetStartup(const String& macIn, String& err) {
  String mac = macIn;
  mac.toUpperCase();
  {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!find(mac)) { err = "that device is not saved"; return false; }
    startupMac = mac;
    if (!targetMac.length()) targetMac = mac;
  }
  save();
  logf("startup device: %s (%s)", nameOf(mac).c_str(), mac.c_str());
  wake();
  return true;
}

void pedalForget(const String& macIn) {
  String mac = macIn;
  mac.toUpperCase();
  bool wasTarget;
  {
    std::lock_guard<std::recursive_mutex> lock(mtx);
    if (!mac.length()) mac = targetMac;
    if (!mac.length()) return;
    devices.erase(std::remove_if(devices.begin(), devices.end(), [&](const Dev& d) { return d.mac == mac; }), devices.end());
    if (startupMac == mac) startupMac = devices.size() ? devices[0].mac : "";
    wasTarget = targetMac == mac;
    if (wasTarget) targetMac = startupMac;
  }
  save();
  if (wasTarget) {
    connected = false;
    if (client && client->isConnected()) client->disconnect();
    setState(targetMac.length() ? "connecting" : "searching");
  }
  logf("forgot %s%s", mac.c_str(), devices.empty() ? " - searching for a " PEDAL_NAME : "");
  wake();
}
