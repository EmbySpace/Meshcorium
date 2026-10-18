#pragma once

// Logic of the chats page: navigation between the chat screens, sending
// messages, creating channels, unread counters, delivery confirmations and
// QR codes. Drawing is done by UIChats from the UIChatsView kept here.

#include <Arduino.h>
#include <helpers/ContactInfo.h>
#include "ChatStore.h"
#include "UIChats.h"
#include "TextInput.h"

namespace fs { class FS; }

class ChatsController {
public:
  ChatsController();

  void begin(fs::FS* flash);
  void attachSd(fs::FS* sd);
  void loop();

  // incoming traffic (from UITask's AbstractUITask callbacks)
  void onContactMessage(const ContactInfo& from, uint32_t sender_timestamp, const char* text);
  void onChannelMessage(int channel_idx, uint32_t timestamp, const char* text);
  void onAck(uint32_t ack);

  // touch, only while the chats page is shown
  void setVisible(bool visible);
  void tap(int x, int y);
  // returns false when the swipe should switch pages instead
  bool swipe(int dx, int dy);

  const UIChatsView* view();
  bool dirty() const { return _dirty; }
  void clearDirty() { _dirty = false; }
  int unreadTotal() const { return _unread_total; }
  bool onChatList() const { return _view.screen == CHATS_LIST; }
  void flush() { _store.flush(); }     // before a reboot

private:
  enum KbPurpose : uint8_t { KB_SEND, KB_NEW_PRIVATE, KB_NEW_PUBLIC };

  struct Entry {            // one chat in the list
    ChatId id;
    uint32_t last_read;     // seq of the newest message the user has seen
  };
  struct PendingAck {
    uint32_t ack, seq;
    unsigned long deadline;
  };

  #define CHATS_MAX_ROWS     400     // chats or contacts in a list
  #define CHATS_MAX_BUBBLES  60
  #define CHATS_MAX_ENTRIES  96
  #define CHATS_MAX_ACKS     8

  ChatStore _store;
  UIChatsView _view;
  bool _dirty, _list_dirty;
  int _unread_total;

  UIChatRow* _rows;                       // chat list (PSRAM)
  ChatId* _row_ids;
  int _list_count;
  UIChatRow* _contact_rows;               // contact picker (PSRAM)
  ChatId* _contact_ids;                   // DM chat ids = key prefixes
  uint8_t* _contact_type;
  int _contact_count;
  UIBubble _bubbles[CHATS_MAX_BUBBLES];
  char _bubble_sender[CHATS_MAX_BUBBLES][32];

  Entry _entries[CHATS_MAX_ENTRIES];
  int _entry_count;
  PendingAck _acks[CHATS_MAX_ACKS];

  ChatId _open;                           // chat shown in CHATS_VIEW
  KbPurpose _kb_purpose;
  TextInput _input;
  ChatId _draft_for;                      // chat the unsent draft in _input belongs to
  UIChatScreen _kb_return, _qr_return;
  uint32_t _boot_seq;                     // messages up to this one count as read
  bool _visible;
  unsigned long _toast_until;

  uint8_t* _qr;                           // UI_QR_MAX * UI_QR_MAX modules

  Entry* entry(const ChatId& id, bool create);
  void rebuildList();
  void rebuildContacts();
  void rebuildChat();
  void openChat(const ChatId& id);
  void markRead(const ChatId& id);
  void recountUnread();
  void showScreen(UIChatScreen s);
  void showToast(const char* text);
  void openKeyboard(KbPurpose p);
  void keyPressed(const UIKey& k);
  void confirmKeyboard();
  void sendMessage(const char* text);
  void createChannel(bool is_public, const char* name);
  bool showQR(const char* url, const char* title, const char* l1, const char* l2, const char* l3);
  void showMyQR();
  void showChatQR();
  void showContactQR(const uint8_t* pub_key, const char* name, uint8_t type);

  void chatTitle(const ChatId& id, char* dest, size_t size, uint8_t* row_kind);
  int maxTextBytes() const;
  uint32_t nowTime(uint32_t fallback) const;
  void formatTime(uint32_t t, char* dest, size_t size) const;
};
