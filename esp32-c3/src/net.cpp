// Wi-Fi (own hotspot + joining networks), captive portal, settings page and
// JSON API. The API matches the Raspberry Pi version, so the same page works.
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <LittleFS.h>
#include <mutex>
#include "common.h"
#include "web_index.h"   // generated from ../software/web/index.html by scripts/embed_web.py

static WebServer server(80);
static DNSServer dns;
static const IPAddress AP_IP(192, 168, 4, 1), AP_MASK(255, 255, 255, 0);
static String apSsid = HOTSPOT_SSID, apPass = HOTSPOT_PASSWORD;

// saved networks (/wifi.json): [{"ssid","password"}], most recent first
struct Net { String ssid, pass; };
static std::vector<Net> nets;
static std::mutex netMtx;
static String opState = "idle", opMsg = "", opAt = "";
static volatile bool opBusy = false;

static String uptime() {
  uint32_t t = millis() / 1000;
  char b[12];
  snprintf(b, sizeof b, "%02lu:%02lu:%02lu", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60), (unsigned long)(t % 60));
  return b;
}

static void setOp(const char* st, const String& msg) {
  std::lock_guard<std::mutex> lock(netMtx);
  opState = st; opMsg = msg; opAt = uptime();
  logf("wifi: %s", msg.c_str());
}

// ------------------------------------------------------------ storage
static void loadNets() {
  JsonDocument doc;
  if (readJson("/wifi.json", doc))
    for (JsonVariant n : doc["networks"].as<JsonArray>()) nets.push_back({n["ssid"] | "", n["password"] | ""});
  JsonDocument hs;
  if (readJson("/hotspot.json", hs)) {
    apSsid = hs["ssid"] | HOTSPOT_SSID;
    apPass = hs["password"] | HOTSPOT_PASSWORD;
  }
}

static void saveNets() {
  JsonDocument doc;
  JsonArray a = doc["networks"].to<JsonArray>();
  { std::lock_guard<std::mutex> lock(netMtx);
    for (auto& n : nets) { JsonObject o = a.add<JsonObject>(); o["ssid"] = n.ssid; o["password"] = n.pass; } }
  writeJson("/wifi.json", doc);
}

static void rememberNet(const String& ssid, const String& pass) {
  { std::lock_guard<std::mutex> lock(netMtx);
    nets.erase(std::remove_if(nets.begin(), nets.end(), [&](const Net& n) { return n.ssid == ssid; }), nets.end());
    nets.insert(nets.begin(), {ssid, pass}); }
  saveNets();
}

// ------------------------------------------------------------- Wi-Fi
static bool joinAndWait(const String& ssid, const String& pass, uint32_t ms) {
  WiFi.disconnect(false, false);
  delay(100);
  WiFi.begin(ssid.c_str(), pass.length() ? pass.c_str() : nullptr);
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(250);
  }
  return false;
}

// at start: join the strongest saved network that is in range
static void autoJoinTask(void*) {
  std::vector<Net> copy;
  { std::lock_guard<std::mutex> lock(netMtx); copy = nets; }
  if (copy.size()) {
    int n = WiFi.scanNetworks(false, false);
    String best; int bestRssi = -1000; String bestPass;
    for (int i = 0; i < n; i++)
      for (auto& s : copy)
        if (WiFi.SSID(i) == s.ssid && WiFi.RSSI(i) > bestRssi) { best = s.ssid; bestRssi = WiFi.RSSI(i); bestPass = s.pass; }
    WiFi.scanDelete();
    if (best.length()) {
      logf("joining %s", best.c_str());
      if (joinAndWait(best, bestPass, 20000)) logf("on %s - page at http://%s", best.c_str(), WiFi.localIP().toString().c_str());
      else logf("could not join %s", best.c_str());
    }
  }
  vTaskDelete(nullptr);
}

struct JoinReq { String ssid, pass; bool saved; };

static void joinTask(void* arg) {
  JoinReq* r = (JoinReq*)arg;
  delay(1500);   // let the HTTP reply reach the phone first
  String prevSsid = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "";
  String prevPass;
  { std::lock_guard<std::mutex> lock(netMtx); for (auto& n : nets) if (n.ssid == prevSsid) prevPass = n.pass; }
  setOp("working", "connecting to " + r->ssid + "... the hotspot may restart, rejoin " + apSsid + " in ~30 s");
  if (joinAndWait(r->ssid, r->pass, 25000)) {
    rememberNet(r->ssid, r->pass);
    setOp("ok", "connected to " + r->ssid + " - settings page at http://" + WiFi.localIP().toString());
  } else {
    String reason = WiFi.status() == WL_CONNECT_FAILED ? "wrong password?" : "no answer from the network";
    if (prevSsid.length() && prevSsid != r->ssid) joinAndWait(prevSsid, prevPass, 20000);
    setOp("error", "could not connect to " + r->ssid + ": " + reason + (prevSsid.length() ? " - back on " + prevSsid : ""));
  }
  opBusy = false;
  delete r;
  vTaskDelete(nullptr);
}

// ------------------------------------------------------------- HTTP
static void sendJson(const JsonDocument& doc, int code = 200) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}

static void sendError(const String& msg) {
  JsonDocument d;
  d["ok"] = false; d["error"] = msg;
  sendJson(d, 400);
}

static void sendOk() { JsonDocument d; d["ok"] = true; sendJson(d); }

static bool body(JsonDocument& doc) {
  if (deserializeJson(doc, server.arg("plain"))) { sendError("invalid JSON"); return false; }
  return true;
}

static bool foreignHost() {
  String h = server.hostHeader();
  int c = h.indexOf(':');
  if (c >= 0) h = h.substring(0, c);
  h.toLowerCase();
  if (!h.length()) return false;
  bool ip = true;
  for (char ch : h) if (!isdigit(ch) && ch != '.') ip = false;
  return !ip && h != MDNS_NAME && h != String(MDNS_NAME) + ".local" && h != "stompbox";
}

static void redirectToPage() {
  server.sendHeader("Location", "http://192.168.4.1/");
  server.sendHeader("Cache-Control", "no-store");
  server.send(302, "text/html", "<a href=\"http://192.168.4.1/\">STOMPBOX settings</a>");
}

static void handleIndex() {
  if (foreignHost()) return redirectToPage();
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html", (const char*)INDEX_GZ, INDEX_GZ_LEN);
}

static void handleStatus() {
  JsonDocument d;
  d["version"] = STOMPBOX_VERSION;
  d["platform"] = STOMPBOX_PLATFORM;
  d["pedal_connected"] = midiConnected();
  if (midiConnected()) d["midi_port"] = "BLE"; else d["midi_port"] = nullptr;
  pedalStatus(d["pedal"].to<JsonObject>());
  int p = midiProgramNow();
  if (p >= 0) d["program"] = p; else d["program"] = nullptr;
  JsonObject fx = d["effects"].to<JsonObject>();
  for (int i = 0; i < 5; i++) fx[EFFECTS[i].key] = midiEffectOn(i);
  enginePressed(d["pressed"].to<JsonArray>());
  enginePinsLow(d["pins_low"].to<JsonArray>());
  d["gpio"] = "esp32";
  d["profile"] = String(engineSettings().active);
  logToJson(d["log"].to<JsonArray>(), 15);
  sendJson(d);
}

static void addPins(JsonObject d) {
  static const int allowed[] = ALLOWED_PINS;
  JsonArray a = d["pins_available"].to<JsonArray>();
  for (int g : allowed) { JsonObject o = a.add<JsonObject>(); o["gpio"] = g; o["label"] = "GPIO" + String(g); }
}

static void handleConfigGet() {
  JsonDocument d;
  settingsToJson(engineSettings(), d.to<JsonObject>());
  addPins(d.as<JsonObject>());
  sendJson(d);
}

static void handleConfigPost() {
  JsonDocument in;
  if (!body(in)) return;
  Settings s;
  String err;
  if (!parseSettings(in.as<JsonVariantConst>(), s, err)) return sendError(err);
  JsonDocument saved;
  settingsToJson(s, saved.to<JsonObject>());
  writeJson("/config.json", saved);
  engineApply(s);
  logf("settings saved");
  JsonDocument out;
  out["ok"] = true;
  settingsToJson(s, out["config"].to<JsonObject>());
  addPins(out["config"].as<JsonObject>());
  sendJson(out);
}

static void handleWifiState() {
  JsonDocument d;
  JsonObject c = d["current"].to<JsonObject>();
  bool on = WiFi.status() == WL_CONNECTED;
  c["connected"] = on;
  if (on) {
    c["ssid"] = WiFi.SSID(); c["ip"] = WiFi.localIP().toString(); c["channel"] = WiFi.channel(); c["connection"] = WiFi.SSID();
  } else { c["ssid"] = nullptr; c["ip"] = nullptr; c["channel"] = nullptr; c["connection"] = nullptr; }
  JsonObject h = d["hotspot"].to<JsonObject>();
  h["ssid"] = apSsid; h["ip"] = "192.168.4.1"; h["channel"] = WiFi.channel(); h["running"] = true;
  JsonArray s = d["saved"].to<JsonArray>();
  JsonObject op = d["op"].to<JsonObject>();
  { std::lock_guard<std::mutex> lock(netMtx);
    for (auto& n : nets) { JsonObject o = s.add<JsonObject>(); o["name"] = n.ssid; o["ssid"] = n.ssid; }
    op["state"] = opState; op["message"] = opMsg; op["at"] = opAt; }
  sendJson(d);
}

static void handleWifiScan() {
  int n = WiFi.scanNetworks(false, false);
  JsonDocument d;
  d["ok"] = true;
  JsonArray a = d["networks"].to<JsonArray>();
  String cur = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "";
  std::vector<int> idx;
  for (int i = 0; i < n; i++) {
    if (!WiFi.SSID(i).length()) continue;
    bool dup = false;
    for (int j : idx) if (WiFi.SSID(j) == WiFi.SSID(i)) dup = true;   // strongest first already
    if (!dup) idx.push_back(i);
  }
  for (int i : idx) {
    JsonObject o = a.add<JsonObject>();
    o["ssid"] = WiFi.SSID(i);
    o["signal"] = constrain(2 * (WiFi.RSSI(i) + 100), 0, 100);
    o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    o["channel"] = WiFi.channel(i);
    o["in_use"] = WiFi.SSID(i) == cur;
  }
  WiFi.scanDelete();
  sendJson(d);
}

static void startJoin(const String& ssid, const String& pass) {
  if (opBusy) return sendError("a Wi-Fi change is already in progress");
  opBusy = true;
  setOp("working", "starting...");
  xTaskCreate(joinTask, "join", 6144, new JoinReq{ssid, pass, false}, 2, nullptr);
  sendOk();
}

static void handleWifiConnect() {
  JsonDocument in;
  if (!body(in)) return;
  String ssid = in["ssid"] | "", pass = in["password"] | "";
  ssid.trim();
  if (ssid.length() < 1 || ssid.length() > 32) return sendError("network name must be 1-32 characters");
  if (pass.length() && (pass.length() < 8 || pass.length() > 63)) return sendError("Wi-Fi passwords are 8-63 characters");
  startJoin(ssid, pass);
}

static void handleWifiUp() {
  JsonDocument in;
  if (!body(in)) return;
  String name = in["name"] | "", pass;
  bool found = false;
  { std::lock_guard<std::mutex> lock(netMtx); for (auto& n : nets) if (n.ssid == name) { pass = n.pass; found = true; } }
  if (!found) return sendError("that network is not saved");
  startJoin(name, pass);
}

static void handleWifiForget() {
  JsonDocument in;
  if (!body(in)) return;
  String name = in["name"] | "";
  { std::lock_guard<std::mutex> lock(netMtx);
    nets.erase(std::remove_if(nets.begin(), nets.end(), [&](const Net& n) { return n.ssid == name; }), nets.end()); }
  saveNets();
  if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == name) WiFi.disconnect(false, false);
  setOp("ok", "forgot " + name);
  sendOk();
}

static void handleAction() {
  JsonDocument in;
  if (!body(in)) return;
  Action a;
  String err;
  if (!parseAction(in.as<JsonVariantConst>(), a, true, err)) return sendError(err);
  runAction(a);
  sendOk();
}

static void handleCC() {
  JsonDocument in;
  if (!body(in)) return;
  int c = in["cc"] | -1, v = in["value"] | -1;
  if (c < 0 || c > 127 || v < 0 || v > 127) return sendError("cc and value must be 0-127");
  midiCC(c, v);
  JsonDocument d;
  d["ok"] = true; d["connected"] = midiConnected();
  sendJson(d);
}

static void handleBtScan() {
  JsonDocument d;
  String err;
  d["ok"] = true;
  if (!pedalScan(d["devices"].to<JsonArray>(), err)) return sendError(err);
  sendJson(d);
}

static void handleBt(int what) {
  JsonDocument in;
  if (!body(in)) return;
  String mac = in["mac"] | "", err;
  bool ok = true;
  if (what == 0) ok = pedalConnectNow(mac, in["startup"] | false, in["name"] | "", err);
  else if (what == 1) ok = pedalSetStartup(mac, err);
  else pedalForget(mac);
  if (!ok) return sendError(err);
  sendOk();
}

static const char UPDATE_PAGE[] PROGMEM = R"HTML(<!doctype html><meta name="viewport" content="width=device-width,initial-scale=1">
<title>STOMPBOX firmware update</title><body style="font-family:system-ui;background:#121315;color:#eceae4;padding:24px;max-width:520px;margin:auto">
<h2>Firmware update</h2><p>Choose <code>stompbox-esp32c3-app.bin</code> from the repository's <code>esp32-c3/release</code> folder.
Settings are kept. The controller restarts when done.</p>
<form method="POST" action="/update" enctype="multipart/form-data"><input type="file" name="fw" accept=".bin" required>
<p><button style="padding:10px 18px">Upload and install</button></p></form><p><a style="color:#ffb020" href="/">Back to settings</a></p>)HTML";

static void setupRoutes() {
  server.on("/", HTTP_GET, handleIndex);
  server.on("/index.html", HTTP_GET, handleIndex);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/config", HTTP_GET, handleConfigGet);
  server.on("/api/config", HTTP_POST, handleConfigPost);
  server.on("/api/default", HTTP_GET, [] {
    JsonDocument d;
    settingsToJson(defaultSettings(), d.to<JsonObject>());
    addPins(d.as<JsonObject>());
    sendJson(d);
  });
  server.on("/api/sim", HTTP_POST, [] {
    JsonDocument in;
    if (!body(in)) return;
    engineSimulate(in["button"] | -1, in["down"] | false);
    sendOk();
  });
  server.on("/api/action", HTTP_POST, handleAction);
  server.on("/api/cc", HTTP_POST, handleCC);
  server.on("/api/wifi", HTTP_GET, handleWifiState);
  server.on("/api/wifi/scan", HTTP_POST, handleWifiScan);
  server.on("/api/wifi/connect", HTTP_POST, handleWifiConnect);
  server.on("/api/wifi/up", HTTP_POST, handleWifiUp);
  server.on("/api/wifi/forget", HTTP_POST, handleWifiForget);
  server.on("/api/bt/scan", HTTP_POST, handleBtScan);
  server.on("/api/bt/connect", HTTP_POST, [] { handleBt(0); });
  server.on("/api/bt/startup", HTTP_POST, [] { handleBt(1); });
  server.on("/api/bt/forget", HTTP_POST, [] { handleBt(2); });
  server.on("/api/pedal/forget", HTTP_POST, [] { pedalForget(""); sendOk(); });
  server.on("/update", HTTP_GET, [] { server.send_P(200, "text/html", UPDATE_PAGE); });
  server.on("/update", HTTP_POST,
    [] {
      bool ok = !Update.hasError();
      server.send(200, "text/html", ok ? "Update installed - restarting... <a href=\"/\">back</a>"
                                       : "Update failed. <a href=\"/update\">Try again</a>");
      if (ok) { delay(800); ESP.restart(); }
    },
    [] {
      HTTPUpload& up = server.upload();
      if (up.status == UPLOAD_FILE_START) { logf("firmware update: %s", up.filename.c_str()); Update.begin(UPDATE_SIZE_UNKNOWN); }
      else if (up.status == UPLOAD_FILE_WRITE) Update.write(up.buf, up.currentSize);
      else if (up.status == UPLOAD_FILE_END) { if (Update.end(true)) logf("firmware update ok (%u bytes)", up.totalSize); else logf("firmware update failed"); }
    });
  // captive portal: phones' connectivity checks and any unknown address -> settings page
  server.onNotFound(redirectToPage);
}

void netBegin() {
  loadNets();
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setAutoReconnect(true);
  WiFi.softAPConfig(AP_IP, AP_IP, AP_MASK);
  WiFi.softAP(apSsid.c_str(), apPass.length() ? apPass.c_str() : nullptr, 6, 0, HOTSPOT_MAX_CLIENTS);
  logf("hotspot %s at 192.168.4.1", apSsid.c_str());
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", AP_IP);
  if (MDNS.begin(MDNS_NAME)) MDNS.addService("http", "tcp", 80);
  setupRoutes();
  server.begin();
  logf("settings page on port 80");
  xTaskCreate(autoJoinTask, "autojoin", 6144, nullptr, 1, nullptr);
}

void netLoop() {
  dns.processNextRequest();
  server.handleClient();
}
