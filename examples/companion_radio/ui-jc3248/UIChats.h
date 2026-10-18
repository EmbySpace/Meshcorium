#pragma once

// Chats page of the JC3248W535 UI: what is on screen (UIChatsView, filled by
// ChatsController) plus rendering and touch hit-testing. Pure drawing code,
// no firmware dependencies (rendered on a PC by tools/preview.cpp too).
//
//   list ──tap row──> chat ──tap input bar──> keyboard ──send──> chat
//    │                  └──QR button──> QR of the contact / channel
//    ├──"Новый чат"──> new: [Личка] [Приватный канал] [Публичный #канал] [Мой QR]
//    │                        └─> contact picker / keyboard for the name
//    └──QR button──> QR of this node
// Back: the chevron in the header or a swipe to the right.

#include "UIGfx.h"

enum UIChatScreen : uint8_t { CHATS_LIST, CHATS_VIEW, CHATS_NEW, CHATS_CONTACTS, CHATS_KEYBOARD, CHATS_QR };

enum UIChatRowKind : uint8_t { ROW_DM, ROW_CHANNEL_PUBLIC, ROW_CHANNEL_PRIVATE,
                               ROW_CONTACT_CHAT, ROW_CONTACT_REPEATER, ROW_CONTACT_ROOM, ROW_CONTACT_SENSOR };

struct UIChatRow {
  uint8_t kind;             // UIChatRowKind
  char title[32];
  char preview[72];         // last message / contact type
  char time[12];            // "14:05", "17.10", "вчера", ""
  uint16_t unread;
};

enum UIBubbleStatus : uint8_t { BUBBLE_IN, BUBBLE_SENT, BUBBLE_DELIVERED, BUBBLE_FAILED, BUBBLE_SENDING };

struct UIBubble {
  uint8_t status;           // UIBubbleStatus, BUBBLE_IN = incoming
  const char* sender;       // shown above incoming channel messages, may be NULL
  const char* text;
  char time[8];
};

enum UIKbLayout : uint8_t { KB_RU, KB_EN, KB_SYM };

// on-screen keyboard with its input field (shared by chats and settings)
struct UIKeyboardView {
  char title[48];
  const char* text;
  const char* prefix;       // fixed text before the input ("#"), may be NULL
  int bytes, max_bytes;
  uint8_t layout;           // UIKbLayout
  bool shift;
  const char* action;       // label of the confirm key
};

#define UI_QR_MAX 81        // modules per side (QR version 16)

struct UIChatsView {
  UIChatScreen screen;

  // list / contact picker
  const UIChatRow* rows;
  int row_count;
  int row_scroll;           // first visible row

  // chat
  char chat_title[32];
  uint8_t chat_kind;        // UIChatRowKind of the open chat
  const UIBubble* bubbles;  // oldest first
  int bubble_count;
  int bubble_scroll;        // bubbles hidden at the bottom (0 = newest visible)

  UIKeyboardView kb;

  // QR
  const uint8_t* qr;        // qr_size * qr_size modules, 1 = dark
  int qr_size;
  char qr_title[40];
  char qr_lines[3][48];

  // short message over the bottom of the screen ("" = none)
  char toast[48];
};

enum UIChatHitType : uint8_t {
  HIT_NONE, HIT_BACK, HIT_ROW, HIT_NEW_CHAT, HIT_MY_QR, HIT_CHAT_QR, HIT_COMPOSE,
  HIT_NEW_DM, HIT_NEW_PRIVATE, HIT_NEW_PUBLIC, HIT_KEY, HIT_CLOSE
};

enum UIKeyType : uint8_t { KEY_CHAR, KEY_SHIFT, KEY_BACKSPACE, KEY_SPACE, KEY_LANG, KEY_SYM, KEY_ACTION };

struct UIKey {
  UIKeyType type;
  const char* text;         // KEY_CHAR: UTF-8 character (lower case)
};

struct UIChatHit {
  UIChatHitType type;
  int index;                // HIT_ROW: row index
  UIKey key;                // HIT_KEY
};

namespace UIChats {
  // draws the area below the status bar
  void render(UIGfx& g, const UIChatsView& v);
  UIChatHit hitTest(const UIChatsView& v, int x, int y);
  int visibleRows(const UIChatsView& v);      // for scrolling
  // keyboard screen (input field + keys), below the status bar
  void renderKeyboard(UIGfx& g, const UIKeyboardView& k);
  // HIT_BACK (field title), HIT_KEY or HIT_NONE
  UIChatHit hitTestKeyboard(const UIKeyboardView& k, int x, int y);
  // upper-cases one UTF-8 letter (Latin + Cyrillic) into dest
  void upper(const char* ch, char* dest, size_t size);
}
