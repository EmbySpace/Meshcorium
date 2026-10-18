#pragma once

// Wi-Fi of the JC3248W535 companion, configured on the device (settings
// page 3) instead of at build time: either joins a network (DHCP or static
// IP) or opens its own access point, and serves the MeshCore companion
// protocol on a TCP port. Settings live in NVS ("jc3248wifi").
//
// Optional build-time defaults (used until something is saved on the device):
//   -D JC3248_WIFI_SSID='"MyNetwork"'  -D JC3248_WIFI_PWD='"secret"'

#include <Arduino.h>
#include <helpers/MultiSerialInterface.h>
#include <helpers/esp32/SerialWifiInterface.h>

enum WifiMode : uint8_t { JCWIFI_NETWORK = 0, JCWIFI_AP = 1 };

struct WifiSettings {
  bool enabled;
  uint8_t mode;             // WifiMode
  char ssid[33];            // network to join
  char pass[65];
  char ap_ssid[33];         // own access point
  char ap_pass[65];         // empty = open, else >= 8 characters
  bool dhcp;
  uint32_t ip, gateway, mask;   // static IP (network byte order as IPAddress)
  uint16_t port;
};

class WifiManager {
public:
  WifiManager() : _ifm(NULL), _added(false), _server_started(false), _last_reconnect(0) { memset(&_s, 0, sizeof(_s)); }

  void begin(MultiSerialInterface* ifm, const char* node_name);
  void loop();

  const WifiSettings& settings() const { return _s; }
  // saves and restarts Wi-Fi with the new settings
  void apply(const WifiSettings& s);
  void setEnabled(bool on);

  // status
  bool isUp() const;                 // joined the network / access point running
  uint32_t ip() const;
  bool clientConnected() const;      // an app is connected over TCP
  int stations() const;              // devices joined to our access point
  const char* currentSsid() const { return _s.mode == JCWIFI_AP ? _s.ap_ssid : _s.ssid; }

  static bool parseIP(const char* s, uint32_t& ip);
  static void formatIP(uint32_t ip, char* dest, size_t size);

private:
  MultiSerialInterface* _ifm;
  SerialWifiInterface _iface;
  WifiSettings _s;
  bool _added;
  bool _server_started;     // the TCP server needs the network stack, i.e. Wi-Fi started once
  unsigned long _last_reconnect;

  void load(const char* node_name);
  void save();
  void start();
  void stop();
};
