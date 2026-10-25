#include "web.h"
#include "web_assets.h"
#include "config.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ArduinoJson.h>

using namespace mc;

static WebServer server(80);
static DNSServer dns;
static Settings* S;
static RelayNode* N;
static BleLink* B;
static LoraRadio* L;
static WeatherStation* W;
static uint32_t rebootAt = 0;

static void sendJson(JsonDocument& doc, int code = 200) {
    String out;
    serializeJson(doc, out);
    server.sendHeader("Cache-Control", "no-store");
    server.send(code, "application/json; charset=utf-8", out);
}
static void fail(const String& why) {
    JsonDocument d;
    d["ok"] = false;
    d["error"] = why;
    sendJson(d);
}
static void okJson() { JsonDocument d; d["ok"] = true; sendJson(d); }
static bool admin() {
    if (server.authenticate("admin", S->adminPass.c_str())) return true;
    server.requestAuthentication(BASIC_AUTH, "Relay setup");
    return false;
}

static void apiState() {
    uint32_t since = server.arg("since").toInt();
    JsonDocument d;
    d["id"] = idLabel(N->id());
    d["name"] = S->name;
    d["ble"] = B->links();
    d["lora"] = L->ok();
    d["loraRx"] = N->stats().rxLora;
    d["loraTx"] = N->stats().txLora;
    auto stations = N->stations();
    JsonArray cs = d["contacts"].to<JsonArray>();
    for (auto& c : N->contacts()) {
        JsonObject o = cs.add<JsonObject>();
        o["id"] = idHex(c.id);
        o["name"] = c.name.empty() ? idLabel(c.id) : c.name;
        if (c.hasWx) { o["t"] = roundf(c.wx.tempC * 10.0f) / 10.0f; o["p"] = roundf(c.wx.pressHpa * 10.0f) / 10.0f; }   // weather next to the name
    }
    JsonArray ms = d["msgs"].to<JsonArray>();
    size_t count = 0;
    for (auto& m : N->log()) {
        // status changes of recent messages must reach the page too, so the last few are always included
        if (m.seq <= since && !(m.dir == ChatMsg::OUT && N->log().back().seq - m.seq < 6)) continue;
        if (++count > 40) break;
        JsonObject o = ms.add<JsonObject>();
        o["seq"] = m.seq;
        o["ts"] = (double)m.ts;
        o["name"] = m.fromName.empty() ? N->nameOf(m.from) : m.fromName;
        o["toName"] = N->nameOf(m.to);
        o["text"] = m.text;
        o["dir"] = (int)m.dir;
        o["status"] = (int)m.status;
        o["pub"] = m.pub;
        o["via"] = (int)m.via;
    }
    JsonObject wx = d["wx"].to<JsonObject>();
    wx["on"] = S->wxOn;
    wx["sensor"] = W->sensorOk();
    wx["every"] = S->wxMin;
    wx["next"] = W->nextInSec(millis());
    JsonArray ws = wx["stations"].to<JsonArray>();
    uint32_t nowMs = millis();
    for (auto& st : stations) {
        JsonObject o = ws.add<JsonObject>();
        o["id"] = idHex(st.id);
        o["name"] = st.name.empty() ? idLabel(st.id) : st.name;
        o["t"] = roundf(st.r.tempC * 10.0f) / 10.0f;
        o["p"] = roundf(st.r.pressHpa * 10.0f) / 10.0f;
        o["age"] = (nowMs - st.heardAt) / 1000;
        o["local"] = st.local;
        o["via"] = (int)st.via;
    }
    JsonArray as = d["alerts"].to<JsonArray>();
    for (auto& a : N->alerts()) {
        JsonObject o = as.add<JsonObject>();
        o["name"] = a.name.empty() ? idLabel(a.id) : a.name;
        o["active"] = a.active;
        if (a.hasLoc) { char b[40]; snprintf(b, sizeof b, "%.5f, %.5f", a.lat, a.lon); o["loc"] = String(b); }
    }
    sendJson(d);
}

static void apiSend() {
    String kind = server.arg("kind"), text = server.arg("text"), nick = server.arg("name");
    nick.trim();
    std::string err;
    bool ok;
    if (kind == "public") ok = N->sendPublic(nick.length() ? nick.c_str() : S->name.c_str(), text.c_str(), &err);
    else {
        uint32_t to = 0;
        if (!parseId(server.arg("to").c_str(), to)) return fail("কাকে পাঠাবেন বেছে নিন / choose a person");
        ok = N->sendPrivate(to, text.c_str(), nick.c_str(), &err);
    }
    if (ok) okJson(); else fail(err.c_str());
}

static void apiTime() {
    double t = server.arg("t").toDouble();
    if (t > 1.6e12) N->setWallClock((uint64_t)t);
    okJson();
}

static void apiSettingsGet() {
    if (!admin()) return;
    JsonDocument d;
    JsonObject s = d["s"].to<JsonObject>();
    s["name"] = S->name; s["apSsid"] = S->apSsid; s["apPass"] = S->apPass; s["wifiCh"] = S->wifiCh;
    s["loraOn"] = S->loraOn; s["freq"] = S->freq; s["sf"] = S->sf; s["bw"] = S->bw; s["cr"] = S->cr;
    s["power"] = S->power; s["sync"] = S->sync; s["duty"] = S->duty; s["beaconMin"] = S->beaconMin; s["loraTtl"] = S->loraTtl; s["lcdAddr"] = S->lcdAddr;
    s["wxOn"] = S->wxOn; s["wxMin"] = S->wxMin; s["wxAddr"] = S->wxAddr;
    s["alertsOn"] = S->alertsOn; s["alertSound"] = S->alertSound; s["ledPin"] = S->ledPin; s["buzPin"] = S->buzPin;
    JsonArray info = d["info"].to<JsonArray>();
    info.add(String("Node ID: ") + idLabel(N->id()).c_str());
    info.add(String("Firmware ") + FIRMWARE_VERSION + " · uptime " + (millis() / 60000) + " min · free heap " + ESP.getFreeHeap() / 1024 + " KB");
    info.add(String("BLE phones linked: ") + B->links() + " · LoRa radio: " + (L->ok() ? "OK" : "ERROR (check wiring)"));
    info.add(String("LoRa packets rx/tx: ") + N->stats().rxLora + "/" + N->stats().txLora + " · queue " + (unsigned)N->loraQueueLen() + " · routes " + (unsigned)N->routeCount());
    info.add(String("Known people: ") + (unsigned)N->contacts().size() + " · dropped packets: " + N->stats().dropped + " (" + N->stats().lastDrop.c_str() + ")");
    {
        String w = String("Weather: ") + (S->wxOn ? "on" : "off") + " · BMP280 " + (W->sensorOk() ? (String("OK at 0x") + String(W->sensorAddr(), HEX)) : String("not found")) + " · every " + S->wxMin + " min · sent " + W->sentCount();
        if (W->hasReading()) w += String(" · last ") + String(W->last().tempC, 1) + " C, " + String(W->last().pressHpa, 1) + " hPa";
        info.add(w);
    }
    info.add(String("Last LoRa signal: RSSI ") + (int)L->lastRssi() + " dBm, SNR " + (int)L->lastSnr() + " dB");
    sendJson(d);
}

static bool inList(float v, std::initializer_list<float> l) { for (float x : l) if (fabsf(x - v) < 0.01f) return true; return false; }

static void apiSettingsPost() {
    if (!admin()) return;
    Settings n = *S;
    auto num = [&](const char* k) { return server.arg(k).toFloat(); };
    String name = server.arg("name");
    name.trim();
    if (name.length() < 1 || name.length() > 24) return fail("নাম ১–২৪ অক্ষর / name must be 1-24 characters");
    n.name = name;
    String ssid = server.arg("apSsid");
    if (ssid.length() < 1 || ssid.length() > 31) return fail("Wi-Fi নাম ১–৩১ অক্ষর");
    n.apSsid = ssid;
    String pass = server.arg("apPass");
    if (pass.length() != 0 && pass.length() < 8) return fail("Wi-Fi পাসওয়ার্ড কমপক্ষে ৮ অক্ষর (বা ফাঁকা)");
    n.apPass = pass;
    n.wifiCh = constrain((int)num("wifiCh"), 1, 11);
    String ap = server.arg("adminPass");
    if (ap.length()) { if (ap.length() < 6) return fail("সেটআপ পাসওয়ার্ড কমপক্ষে ৬ অক্ষর"); n.adminPass = ap; }
    n.loraOn = server.arg("loraOn") == "1";
    float f = num("freq");
    if (f < 410 || f > 525) return fail("ফ্রিকোয়েন্সি 410–525 MHz");
    n.freq = f;
    int sf = (int)num("sf");
    if (sf < 7 || sf > 12) return fail("SF 7–12");
    n.sf = sf;
    float bw = num("bw");
    if (!inList(bw, {62.5f, 125.0f, 250.0f, 500.0f})) return fail("bandwidth 62.5/125/250/500");
    n.bw = bw;
    n.cr = constrain((int)num("cr"), 5, 8);
    n.power = constrain((int)num("power"), 2, 17);
    n.sync = strtol(server.arg("sync").c_str(), nullptr, 16) & 0xFF;
    n.duty = constrain((int)num("duty"), 1, 100);
    n.beaconMin = constrain((int)num("beaconMin"), 5, 120);
    n.loraTtl = constrain((int)num("loraTtl"), 2, 16);
    n.alertsOn = server.arg("alertsOn") == "1";
    n.alertSound = server.arg("alertSound") == "1";
    auto pinOk = [](int p) {
        if (p == -1) return true;
        static const int ok[] = {2, 15, 16, 17,
#if LCD_I2C
            4, 13, 25, 27, 32, 33,
#endif
        };
        for (int x : ok) if (x == p) return true;
        return false;
    };
    n.ledPin = server.arg("ledPin").length() ? (int)server.arg("ledPin").toInt() : -1;
    n.buzPin = server.arg("buzPin").length() ? (int)server.arg("buzPin").toInt() : -1;
    if (!pinOk(n.ledPin) || !pinOk(n.buzPin)) return fail("LED/buzzer pin: use 2, 15, 16 or 17 (empty = none)");
    if (n.ledPin >= 0 && n.ledPin == n.buzPin) return fail("LED and buzzer need different pins");
    n.lcdAddr = strtol(server.arg("lcdAddr").c_str(), nullptr, 16) & 0x7F;
    int wa = (int)strtol(server.arg("wxAddr").c_str(), nullptr, 16);
    if (wa != 0 && wa != 0x76 && wa != 0x77) return fail("BMP280 address: 0 (auto), 76 or 77");
    n.wxAddr = wa;
    settingsSave(n);
    rebootAt = millis() + 1500;
    okJson();
}

// Weather on/off and the interval apply at once, without a reboot.
static void apiWx() {
    if (!admin()) return;
    int mins = (int)server.arg("min").toInt();
    if (mins < 1 || mins > 1440) return fail("সময়ের ব্যবধান ১–১৪৪০ মিনিট / interval must be 1-1440 minutes");
    S->wxOn = server.arg("on") == "1";
    S->wxMin = mins;
    settingsSave(*S);
    W->configure(S->wxOn, S->wxMin);
    okJson();
}
static void apiWxNow() {
    if (!admin()) return;
    if (!S->wxOn) return fail("আবহাওয়া ব্রডকাস্ট বন্ধ আছে / weather broadcast is off");
    if (!W->sensorOk()) return fail("BMP280 সেন্সর পাওয়া যায়নি / BMP280 not found (check wiring)");
    W->sendSoon();
    okJson();
}

void WebUi::begin(Settings* s, RelayNode* node, BleLink* ble, LoraRadio* lora, WeatherStation* wx) {
    S = s; N = node; B = ble; L = lora; W = wx;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(S->apSsid.c_str(), S->apPass.length() >= 8 ? S->apPass.c_str() : nullptr, S->wifiCh, 0, 4);
    delay(100);
    dns.start(53, "*", WiFi.softAPIP());            // any name opens the chat page (captive portal)
    server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", CHAT_HTML); });
    server.on("/setup", HTTP_GET, [] { if (admin()) server.send_P(200, "text/html; charset=utf-8", SETUP_HTML); });
    server.on("/api/state", HTTP_GET, apiState);
    server.on("/api/send", HTTP_POST, apiSend);
    server.on("/api/time", HTTP_POST, apiTime);
    server.on("/api/settings", HTTP_GET, apiSettingsGet);
    server.on("/api/settings", HTTP_POST, apiSettingsPost);
    server.on("/api/wx", HTTP_POST, apiWx);
    server.on("/api/wx/send", HTTP_POST, apiWxNow);
    server.on("/api/reboot", HTTP_POST, [] { if (!admin()) return; okJson(); rebootAt = millis() + 800; });
    server.on("/api/newid", HTTP_POST, [] { if (!admin()) return; identityReset(); okJson(); rebootAt = millis() + 800; });
    server.on("/api/factory", HTTP_POST, [] { if (!admin()) return; factoryReset(); okJson(); rebootAt = millis() + 800; });
    server.onNotFound([] {
        server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
        server.send(302, "text/plain", "");
    });
    server.begin();
    Serial.printf("wifi: AP '%s' at %s\n", S->apSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

void WebUi::handle() {
    dns.processNextRequest();
    server.handleClient();
    if (rebootAt && (int32_t)(millis() - rebootAt) > 0) ESP.restart();
}

String WebUi::ip() const { return WiFi.softAPIP().toString(); }
