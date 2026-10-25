#include "settings.h"
#include <Preferences.h>

static const char* NS = "mcrelay";

void settingsLoad(Settings& s) {
    Preferences p;
    p.begin(NS, true);
    s.name = p.getString("name", s.name);
    s.apSsid = p.getString("ssid", s.apSsid);
    s.apPass = p.getString("apass", s.apPass);
    s.adminPass = p.getString("admin", s.adminPass);
    s.freq = p.getFloat("freq", s.freq);
    s.sf = p.getInt("sf", s.sf);
    s.bw = p.getFloat("bw", s.bw);
    s.cr = p.getInt("cr", s.cr);
    s.power = p.getInt("pwr", s.power);
    s.sync = p.getInt("sync", s.sync);
    s.duty = p.getInt("duty", s.duty);
    s.loraOn = p.getBool("lora", s.loraOn);
    s.beaconMin = p.getInt("beacon", s.beaconMin);
    s.loraTtl = p.getInt("lttl", s.loraTtl);
    s.alertsOn = p.getBool("alert", s.alertsOn);
    s.alertSound = p.getBool("asound", s.alertSound);
    s.ledPin = p.getInt("ledpin", s.ledPin);
    s.buzPin = p.getInt("buzpin", s.buzPin);
    s.wxOn = p.getBool("wxon", s.wxOn);
    s.wxMin = p.getInt("wxmin", s.wxMin);
    s.wxAddr = p.getInt("wxaddr", s.wxAddr);
    s.lcdAddr = p.getInt("lcd", s.lcdAddr);
    s.wifiCh = p.getInt("wch", s.wifiCh);
    p.end();
}

void settingsSave(const Settings& s) {
    Preferences p;
    p.begin(NS, false);
    p.putString("name", s.name);
    p.putString("ssid", s.apSsid);
    p.putString("apass", s.apPass);
    p.putString("admin", s.adminPass);
    p.putFloat("freq", s.freq);
    p.putInt("sf", s.sf);
    p.putFloat("bw", s.bw);
    p.putInt("cr", s.cr);
    p.putInt("pwr", s.power);
    p.putInt("sync", s.sync);
    p.putInt("duty", s.duty);
    p.putBool("lora", s.loraOn);
    p.putInt("beacon", s.beaconMin);
    p.putInt("lttl", s.loraTtl);
    p.putBool("alert", s.alertsOn);
    p.putBool("asound", s.alertSound);
    p.putInt("ledpin", s.ledPin);
    p.putInt("buzpin", s.buzPin);
    p.putBool("wxon", s.wxOn);
    p.putInt("wxmin", s.wxMin);
    p.putInt("wxaddr", s.wxAddr);
    p.putInt("lcd", s.lcdAddr);
    p.putInt("wch", s.wifiCh);
    p.end();
}

bool identityLoad(mc::crypto::Identity& id) {
    Preferences p;
    p.begin(NS, false);
    uint8_t priv[32];
    if (p.getBytesLength("idpriv") == 32) {
        p.getBytes("idpriv", priv, 32);
        p.end();
        return mc::crypto::fromPrivate(id, priv);
    }
    bool ok = mc::crypto::generate(id, 0xF0);
    if (ok) p.putBytes("idpriv", id.priv, 32);
    p.end();
    return ok;
}

void identityReset() { Preferences p; p.begin(NS, false); p.remove("idpriv"); p.end(); }
void factoryReset() { Preferences p; p.begin(NS, false); p.clear(); p.end(); }
