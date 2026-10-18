#pragma once

// Logic of the interfaces page (page 3): switches for Bluetooth / Wi-Fi / USB
// (kept in NVS "jc3248if"), the Bluetooth PIN (node prefs, used from the next
// boot) and the Wi-Fi settings (WifiManager, applied at once).

#include <Arduino.h>
#include <helpers/MultiSerialInterface.h>
#include "UIInterfaces.h"
#include "TextInput.h"
#include "WifiManager.h"

class InterfacesController {
public:
  InterfacesController() : _ifm(NULL), _wifi(NULL), _ble_on(true), _usb_on(true), _boot_ble_pin(0),
                           _edit_field(0), _dirty(true), _toast_until(0), _restart(false), _visible(false) {
    memset(&_view, 0, sizeof(_view));
    memset(&_w, 0, sizeof(_w));
  }

  void begin(MultiSerialInterface* ifm, WifiManager* wifi);
  void loop();

  void setVisible(bool v);
  void tap(int x, int y);
  bool swipe(int dx, int dy);      // false = switch pages

  const UIIfView* view();
  bool dirty() const { return _dirty; }
  void clearDirty() { _dirty = false; }
  bool restartRequested() const { return _restart; }

  bool bleOn() const { return _ble_on; }
  bool usbOn() const { return _usb_on; }

private:
  enum Field : uint8_t {
    F_PIN_MODE, F_PIN, F_PIN_ACTIVE,
    F_MODE, F_SSID, F_PASS, F_AP_SSID, F_AP_PASS, F_AP_IP, F_DHCP, F_IP, F_GW, F_MASK, F_PORT
  };

  MultiSerialInterface* _ifm;
  WifiManager* _wifi;
  UIIfView _view;
  TextInput _input;
  WifiSettings _w;                 // Wi-Fi settings being edited
  bool _ble_on, _usb_on;
  uint32_t _boot_ble_pin;          // node prefs PIN at boot (0 = random)
  uint8_t _edit_field;
  bool _dirty;
  unsigned long _toast_until;
  bool _restart;
  bool _visible;

  void saveSwitches();
  void toggle(int link);
  void openDetail(int link);
  void buildMain();
  void buildDetail();
  void editField(uint8_t field);
  void fieldEntered();
  void choose(uint8_t field, int option);
  void button();
  bool wifiChanged() const;
  void showToast(const char* t);
};
