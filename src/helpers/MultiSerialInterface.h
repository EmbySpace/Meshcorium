#pragma once

#include "BaseSerialInterface.h"

#ifndef MAX_INTERFACES
  // ble, usb, wifi, ethernet
  #define MAX_INTERFACES 4
#endif

enum class InterfaceType : uint8_t {
  NONE,
  Bluetooth,
  USB,
  WiFi,
  Ethernet,
  HardwareSerial
};

class MultiSerialInterface : public BaseSerialInterface {
private:

  struct RegisteredInterface {
    InterfaceType type = InterfaceType::NONE;
    BaseSerialInterface* instance = nullptr;
  };

  bool _enabled = false;
  RegisteredInterface _interfaces[MAX_INTERFACES] = {};
  int _reply_target = -1;   // index of the interface the last command came from
  int _next_poll = 0;
  unsigned long _last_rx[MAX_INTERFACES] = {};   // millis() of the last frame per interface, 0 = never

public:
  bool addInterface(InterfaceType type, BaseSerialInterface* iface) {
    // make sure an interface was provided
    if(iface == nullptr){
      return false;
    }

    // put it in the first free slot
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(_interfaces[i].instance == nullptr){
        _interfaces[i].instance = iface;
        _interfaces[i].type = type;
        return true;
      }
    }

    // no free slots available
    return false;
  }

  bool removeInterface(BaseSerialInterface* iface) {
    // make sure an interface was provided
    if(iface == nullptr){
      return false;
    }

    // find and remove interface
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(_interfaces[i].instance == iface){
        _interfaces[i] = {};
        if(_reply_target == i) _reply_target = -1;
        return true;
      }
    }

    // interface not found
    return false;
  }

  void enableBluetooth() {
    for(auto iface : _interfaces){
      if(iface.instance && iface.type == InterfaceType::Bluetooth){
        iface.instance->enable();
      }
    }
  }

  void disableBluetooth() {
    for(auto iface : _interfaces){
      if(iface.instance && iface.type == InterfaceType::Bluetooth){
        iface.instance->disable();
      }
    }
  }

  bool isBluetoothEnabled() {
    for(auto iface : _interfaces){
      if(iface.instance && iface.type == InterfaceType::Bluetooth){
        return iface.instance->isEnabled();
      }
    }
    return false; 
  }

  // enable all interfaces
  void enable() override {
    _enabled = true;
    for(auto iface : _interfaces){
      if(iface.instance){
        iface.instance->enable();
      }
    }
  }

  // disable all interfaces
  void disable() override {
    _enabled = false;
    for(auto iface : _interfaces){
      if(iface.instance){
        iface.instance->disable();
      }
    }
  }

  bool isEnabled() const override { 
    return _enabled; 
  }

  bool isConnected() const override {
    // not connected when disabled
    if(!_enabled){
      return false;
    }

    // check if any interface is connected
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(isActive(i)) {
        return true;
      }
    }

    // nothing connected
    return false;
  }

  // per-interface state, e.g. for status icons on a display
  bool hasInterface(InterfaceType type) const {
    return find(type) >= 0;
  }

  bool isInterfaceEnabled(InterfaceType type) const {
    int i = find(type);
    return i >= 0 && _interfaces[i].instance->isEnabled();
  }

  void setInterfaceEnabled(InterfaceType type, bool enabled) {
    int i = find(type);
    if (i < 0) return;
    if (enabled) _interfaces[i].instance->enable();
    else _interfaces[i].instance->disable();
  }

  bool isInterfaceConnected(InterfaceType type) const {
    int i = find(type);
    return _enabled && i >= 0 && isActive(i);
  }

  // millis() when the interface last received a command frame (0 = never).
  // Serial-type links (USB) can't tell whether an app is attached, but an
  // app talking to the node sends frames.
  unsigned long lastFrameMillis(InterfaceType type) const {
    int i = find(type);
    return i >= 0 ? _last_rx[i] : 0;
  }

  // loop all interfaces
  void loop() override {
    for(auto iface : _interfaces){
      if(iface.instance){
        iface.instance->loop();
      }
    }
  }

  bool isWriteBusy() const override {
    // not busy when disabled
    if(!_enabled){
      return false;
    }

    // check if any connected interface is busy
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(isActive(i) && _interfaces[i].instance->isWriteBusy()){
        return true;
      }
    }

    // nothing busy
    return false;
  }

  // Frame routing with several clients connected at once:
  //  - replies to a command (codes < 0x80) go only to the client that sent the command,
  //    so one app never sees the answers meant for another one
  //  - push notifications (codes >= 0x80) and received messages go to every
  //    connected client, so all of them stay in sync
  size_t writeFrame(const uint8_t src[], size_t len) override {
    // don't write when disabled or nothing provided
    if(!_enabled || len == 0){
      return 0;
    }

    if(!isBroadcastFrame(src[0]) && _reply_target >= 0 && _interfaces[_reply_target].instance){
      if(!isActive(_reply_target)){
        return len;   // client that asked has gone, drop the reply
      }
      return _interfaces[_reply_target].instance->writeFrame(src, len);
    }

    // write frame to all connected interfaces
    bool anySuccessful = false;
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(isActive(i) && _interfaces[i].instance->writeFrame(src, len) == len){
        anySuccessful = true;
      }
    }

    return anySuccessful ? len : 0;
  }

  size_t checkRecvFrame(uint8_t dest[]) override {
    // don't read when disabled
    if(!_enabled){
      return 0;
    }

    // poll the interfaces round-robin so a busy client can't starve the others
    // (BLE / WiFi also flush their send queues from inside checkRecvFrame)
    for(int n = 0; n < MAX_INTERFACES; n++){
      int i = (_next_poll + n) % MAX_INTERFACES;
      if(_interfaces[i].instance && _interfaces[i].instance->isEnabled()){
        size_t frameSize = _interfaces[i].instance->checkRecvFrame(dest);
        if(frameSize > 0){
          _reply_target = i;              // answers to this command go back here
          _last_rx[i] = millis() | 1;     // | 1: never 0 once a frame arrived
          _next_poll = (i + 1) % MAX_INTERFACES;
          return frameSize;
        }
      }
    }

    // no frame received
    return 0;
  }

  int getReplyTarget() const override { return _reply_target; }
  void setReplyTarget(int target) override {
    _reply_target = (target >= 0 && target < MAX_INTERFACES) ? target : -1;
  }

private:
  int find(InterfaceType type) const {
    for(int i = 0; i < MAX_INTERFACES; i++){
      if(_interfaces[i].instance && _interfaces[i].type == type){
        return i;
      }
    }
    return -1;
  }

  bool isActive(int i) const {
    return _interfaces[i].instance && _interfaces[i].instance->isEnabled() && _interfaces[i].instance->isConnected();
  }

  // frames every connected client should get (see writeFrame)
  static bool isBroadcastFrame(uint8_t code) {
    return code >= 0x80          // PUSH_CODE_*
        || code == 7  || code == 8     // RESP_CODE_CONTACT_MSG_RECV / CHANNEL_MSG_RECV
        || code == 16 || code == 17    // ..._V3
        || code == 27;                 // RESP_CODE_CHANNEL_DATA_RECV
  }

};
