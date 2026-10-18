#include "WifiManager.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_mac.h>

#define NVS_NS       "jc3248wifi"
#define NVS_KEY      "cfg1"
#define RECONNECT_MS 15000UL

#ifndef TCP_PORT
  #define TCP_PORT 5000
#endif

static uint32_t ipOf(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  return (uint32_t)a | ((uint32_t)b << 8) | ((uint32_t)c << 16) | ((uint32_t)d << 24);
}

void WifiManager::load(const char* node_name) {
  Preferences p;
  p.begin(NVS_NS, true);
  bool ok = p.getBytesLength(NVS_KEY) == sizeof(_s) && p.getBytes(NVS_KEY, &_s, sizeof(_s)) == sizeof(_s);
  p.end();
  if (ok) return;

  // defaults
  memset(&_s, 0, sizeof(_s));
#ifdef JC3248_WIFI_SSID
  _s.enabled = true;
  strncpy(_s.ssid, JC3248_WIFI_SSID, sizeof(_s.ssid) - 1);
  #ifdef JC3248_WIFI_PWD
  strncpy(_s.pass, JC3248_WIFI_PWD, sizeof(_s.pass) - 1);
  #endif
#else
  _s.enabled = false;            // nothing configured yet
#endif
  _s.mode = JCWIFI_NETWORK;
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  snprintf(_s.ap_ssid, sizeof(_s.ap_ssid), "MeshCore-%02X%02X", mac[4], mac[5]);
  strcpy(_s.ap_pass, "meshcore");
  _s.dhcp = true;
  _s.ip = ipOf(192, 168, 1, 50);
  _s.gateway = ipOf(192, 168, 1, 1);
  _s.mask = ipOf(255, 255, 255, 0);
  _s.port = TCP_PORT;
  (void)node_name;
}

void WifiManager::save() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.putBytes(NVS_KEY, &_s, sizeof(_s));
  p.end();
}

void WifiManager::begin(MultiSerialInterface* ifm, const char* node_name) {
  _ifm = ifm;
  load(node_name);
  // registered even while off, so the UI can show and switch it; the TCP
  // server itself is only started together with Wi-Fi (start()), because
  // lwIP is not initialised before the first WiFi.mode() call
  if (_ifm) _added = _ifm->addInterface(InterfaceType::WiFi, &_iface);
  start();
}

void WifiManager::stop() {
  _iface.disable();
  if (WiFi.getMode() == WIFI_OFF) return;   // never started
  WiFi.disconnect(true);
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
}

void WifiManager::start() {
  if (!_s.enabled) { stop(); return; }

  if (_s.mode == JCWIFI_AP) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(_s.ap_ssid, _s.ap_pass[0] ? _s.ap_pass : NULL);
  } else {
    WiFi.mode(WIFI_STA);
    if (_s.dhcp) WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
    else WiFi.config(IPAddress(_s.ip), IPAddress(_s.gateway), IPAddress(_s.mask), IPAddress(_s.gateway));
    WiFi.setAutoReconnect(true);
    if (_s.ssid[0]) WiFi.begin(_s.ssid, _s.pass);
    _last_reconnect = millis();
  }
  if (_server_started) {
    _iface.restartServer(_s.port);
  } else {
    _iface.begin(_s.port);
    _server_started = true;
  }
  _iface.enable();
  WIFI_DEBUG_PRINTLN("started: mode=%s ssid=%s port=%u ip=%s", _s.mode == JCWIFI_AP ? "AP" : "STA",
                     currentSsid(), _s.port,
                     (_s.mode == JCWIFI_AP ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str());
}

void WifiManager::apply(const WifiSettings& s) {
  _s = s;
  save();
  if (WiFi.getMode() != WIFI_OFF) {
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
  }
  start();
}

void WifiManager::setEnabled(bool on) {
  if (_s.enabled == on) return;
  _s.enabled = on;
  save();
  start();
}

void WifiManager::loop() {
  if (!_s.enabled || _s.mode != JCWIFI_NETWORK || !_s.ssid[0]) return;
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - _last_reconnect >= RECONNECT_MS) {
    _last_reconnect = millis();
    WiFi.reconnect();
  }
}

bool WifiManager::isUp() const {
  if (!_s.enabled) return false;
  if (_s.mode == JCWIFI_AP) return (WiFi.getMode() & WIFI_AP) != 0;
  return WiFi.status() == WL_CONNECTED;
}

uint32_t WifiManager::ip() const {
  if (!isUp()) return 0;
  return _s.mode == JCWIFI_AP ? (uint32_t)WiFi.softAPIP() : (uint32_t)WiFi.localIP();
}

int WifiManager::stations() const {
  if (!_s.enabled || _s.mode != JCWIFI_AP) return 0;
  return WiFi.softAPgetStationNum();
}

bool WifiManager::clientConnected() const {
  return _s.enabled && _iface.isConnected();
}

bool WifiManager::parseIP(const char* s, uint32_t& ip) {
  int part[4], n = 0;
  const char* p = s;
  while (n < 4) {
    if (*p < '0' || *p > '9') return false;
    int v = 0, digits = 0;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; if (++digits > 3) return false; }
    if (v > 255) return false;
    part[n++] = v;
    if (n < 4) { if (*p != '.') return false; p++; }
  }
  if (*p) return false;
  ip = ipOf(part[0], part[1], part[2], part[3]);
  return true;
}

void WifiManager::formatIP(uint32_t ip, char* d, size_t n) {
  snprintf(d, n, "%u.%u.%u.%u", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
           (unsigned)((ip >> 16) & 0xFF), (unsigned)(ip >> 24));
}
