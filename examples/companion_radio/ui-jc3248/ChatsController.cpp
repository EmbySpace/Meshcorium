#include "ChatsController.h"
#include "../MyMesh.h"
#include "target.h"
#include <helpers/AdvertTimeSync.h>
#include <Utils.h>
#include <FS.h>
#include <qrcode.h>
#include <esp_random.h>
#include <esp_heap_caps.h>
#include <time.h>

#ifndef UI_TZ_OFFSET_MIN
  #define UI_TZ_OFFSET_MIN 180
#endif

#define TOAST_MS        2500
#define ACK_MIN_WAIT_MS 15000UL
#define CHANNEL_NAME_MAX 31

// ----------------------------------------------------------------- utils ----

// upper-cases a whole UTF-8 string (Latin + Cyrillic)
static void upperStr(const char* src, char* dest, size_t size) {
  size_t o = 0;
  while (*src && o + 4 < size) {
    uint32_t cp;
    int n = UIGfx::decodeUTF8(src, cp);
    char one[5] = { 0 }, up[5];
    memcpy(one, src, n < 4 ? n : 4);
    UIChats::upper(one, up, sizeof(up));
    size_t k = strlen(up);
    memcpy(dest + o, up, k);
    o += k;
    src += n;
  }
  dest[o] = 0;
}

static void urlEncode(const char* s, char* dest, size_t size) {
  static const char* hex = "0123456789ABCDEF";
  size_t o = 0;
  for (; *s && o + 4 < size; s++) {
    uint8_t c = (uint8_t)*s;
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      dest[o++] = c;
    } else {
      dest[o++] = '%'; dest[o++] = hex[c >> 4]; dest[o++] = hex[c & 15];
    }
  }
  dest[o] = 0;
}

static void toHexLower(char* dest, const uint8_t* src, size_t len) {
  mesh::Utils::toHex(dest, src, len);
  for (char* p = dest; *p; p++) if (*p >= 'A' && *p <= 'F') *p += 32;
}

static ChatId dmId(const uint8_t* pub_key) {
  ChatId id;
  id.kind = CHAT_DM;
  memcpy(id.id, pub_key, CHAT_ID_LEN);
  return id;
}

static ChatId channelId(int idx, const uint8_t* secret) {
  ChatId id;
  id.kind = CHAT_CHANNEL;
  id.id[0] = (uint8_t)idx;
  memcpy(&id.id[1], secret, CHAT_ID_LEN - 1);
  return id;
}

// the chat's channel slot, if it still holds the same channel
static bool channelOf(const ChatId& id, ChannelDetails& ch) {
  if (id.kind != CHAT_CHANNEL) return false;
  if (!the_mesh.getChannel(id.id[0], ch) || ch.name[0] == 0) return false;
  return memcmp(ch.channel.secret, &id.id[1], CHAT_ID_LEN - 1) == 0;
}

static bool isPublicChannelName(const char* name) {
  return name[0] == '#' || strcasecmp(name, "Public") == 0;
}

// ------------------------------------------------------------------- QR ----

static uint8_t* s_qr_dest;
static int s_qr_size;

static void qrCopy(esp_qrcode_handle_t q) {
  int n = esp_qrcode_get_size(q);
  if (n > UI_QR_MAX) { s_qr_size = 0; return; }
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) s_qr_dest[y * n + x] = esp_qrcode_get_module(q, x, y) ? 1 : 0;
  s_qr_size = n;
}

// --------------------------------------------------------------------------

ChatsController::ChatsController()
    : _dirty(true), _list_dirty(true), _unread_total(0), _rows(NULL), _row_ids(NULL), _list_count(0),
      _contact_rows(NULL), _contact_ids(NULL), _contact_type(NULL), _contact_count(0),
      _entry_count(0), _kb_purpose(KB_SEND), _kb_return(CHATS_LIST), _qr_return(CHATS_LIST),
      _boot_seq(0), _visible(false), _toast_until(0), _qr(NULL) {
  memset(&_view, 0, sizeof(_view));
  memset(&_open, 0, sizeof(_open));
  memset(_acks, 0, sizeof(_acks));
  memset(&_draft_for, 0, sizeof(_draft_for));
}

void ChatsController::begin(fs::FS* flash) {
  _store.begin(flash);
  _rows = (UIChatRow*)heap_caps_calloc(CHATS_MAX_ROWS, sizeof(UIChatRow), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  _row_ids = (ChatId*)heap_caps_calloc(CHATS_MAX_ROWS, sizeof(ChatId), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  _contact_rows = (UIChatRow*)heap_caps_calloc(CHATS_MAX_ROWS, sizeof(UIChatRow), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  _contact_ids = (ChatId*)heap_caps_calloc(CHATS_MAX_ROWS, sizeof(ChatId), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  _contact_type = (uint8_t*)heap_caps_calloc(CHATS_MAX_ROWS, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  _qr = (uint8_t*)heap_caps_calloc(UI_QR_MAX * UI_QR_MAX, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  _boot_seq = _store.lastSeq();
  _view.screen = CHATS_LIST;
  _view.rows = _rows;
  _view.bubbles = _bubbles;
  rebuildList();
}

void ChatsController::attachSd(fs::FS* sd) {
  bool had_all_read = _store.lastSeq() == _boot_seq;
  _store.attachSd(sd);
  if (had_all_read) _boot_seq = _store.lastSeq();   // history from the card is not "new"
  rebuildList();
  if (_view.screen == CHATS_VIEW) rebuildChat();
}

void ChatsController::setVisible(bool visible) {
  _visible = visible;
  if (visible && _view.screen == CHATS_VIEW) markRead(_open);
  _dirty = true;
}

// --------------------------------------------------------------- time ----

uint32_t ChatsController::nowTime(uint32_t fallback) const {
  uint32_t t = rtc_clock.getCurrentTime();
  if (AdvertTimeSync::isTimeValid(t)) return t;
  return AdvertTimeSync::isTimeValid(fallback) ? fallback : t;
}

void ChatsController::formatTime(uint32_t t, char* dest, size_t size) const {
  dest[0] = 0;
  if (!AdvertTimeSync::isTimeValid(t)) return;
  time_t lt = (time_t)t + UI_TZ_OFFSET_MIN * 60;
  struct tm m;
  gmtime_r(&lt, &m);
  uint32_t now = rtc_clock.getCurrentTime();
  if (AdvertTimeSync::isTimeValid(now)) {
    long day = (long)((t + UI_TZ_OFFSET_MIN * 60) / 86400), today = (long)((now + UI_TZ_OFFSET_MIN * 60) / 86400);
    if (day == today) { snprintf(dest, size, "%02d:%02d", m.tm_hour, m.tm_min); return; }
    if (day == today - 1) { strncpy(dest, "вчера", size); dest[size - 1] = 0; return; }
  }
  snprintf(dest, size, "%02d.%02d", m.tm_mday, m.tm_mon + 1);
}

// ------------------------------------------------------------- incoming ----

void ChatsController::onContactMessage(const ContactInfo& from, uint32_t sender_timestamp, const char* text) {
  ChatId id = dmId(from.id.pub_key);
  _store.add(id, from.name, text, nowTime(sender_timestamp), MSG_IN);
  if (_visible && (_view.screen == CHATS_VIEW || _view.screen == CHATS_KEYBOARD) && _open == id) markRead(id);
  rebuildList();
  if (_open == id) rebuildChat();
  _dirty = true;
}

void ChatsController::onChannelMessage(int channel_idx, uint32_t timestamp, const char* text) {
  ChannelDetails ch;
  if (!the_mesh.getChannel(channel_idx, ch) || ch.name[0] == 0) return;
  ChatId id = channelId(channel_idx, ch.channel.secret);
  _store.add(id, "", text, nowTime(timestamp), MSG_IN);   // text is "<sender>: <message>"
  if (_visible && (_view.screen == CHATS_VIEW || _view.screen == CHATS_KEYBOARD) && _open == id) markRead(id);
  rebuildList();
  if (_open == id) rebuildChat();
  _dirty = true;
}

void ChatsController::onAck(uint32_t ack) {
  for (int i = 0; i < CHATS_MAX_ACKS; i++) {
    if (_acks[i].ack == 0 || _acks[i].ack != ack) continue;
    ChatRecord* r = _store.bySeq(_acks[i].seq);
    if (r) { r->status = MSG_OUT_DELIVERED; _store.touched(); }
    _acks[i].ack = 0;
    if (_view.screen == CHATS_VIEW) rebuildChat();
    _dirty = true;
  }
}

void ChatsController::loop() {
  unsigned long now = millis();
  _store.loop(now);

  for (int i = 0; i < CHATS_MAX_ACKS; i++) {
    if (_acks[i].ack && (long)(now - _acks[i].deadline) >= 0) {
      ChatRecord* r = _store.bySeq(_acks[i].seq);
      if (r && r->status == MSG_OUT_SENT) { r->status = MSG_OUT_FAILED; _store.touched(); }
      _acks[i].ack = 0;
      if (_view.screen == CHATS_VIEW) rebuildChat();
      _dirty = true;
    }
  }
  if (_view.toast[0] && (long)(now - _toast_until) >= 0) {
    _view.toast[0] = 0;
    _dirty = true;
  }
}

// --------------------------------------------------------------- lists ----

ChatsController::Entry* ChatsController::entry(const ChatId& id, bool create) {
  for (int i = 0; i < _entry_count; i++) if (_entries[i].id == id) return &_entries[i];
  if (!create || _entry_count >= CHATS_MAX_ENTRIES) return NULL;
  _entries[_entry_count].id = id;
  _entries[_entry_count].last_read = _boot_seq;
  return &_entries[_entry_count++];
}

void ChatsController::markRead(const ChatId& id) {
  Entry* e = entry(id, true);
  if (e) e->last_read = _store.lastSeq();
  rebuildList();
}

void ChatsController::chatTitle(const ChatId& id, char* dest, size_t size, uint8_t* row_kind) {
  dest[0] = 0;
  if (id.kind == CHAT_CHANNEL) {
    ChannelDetails ch;
    if (channelOf(id, ch)) {
      strncpy(dest, ch.name, size - 1); dest[size - 1] = 0;
      if (row_kind) *row_kind = isPublicChannelName(ch.name) ? ROW_CHANNEL_PUBLIC : ROW_CHANNEL_PRIVATE;
    } else {
      strncpy(dest, "Удалённый канал", size - 1); dest[size - 1] = 0;
      if (row_kind) *row_kind = ROW_CHANNEL_PRIVATE;
    }
    return;
  }
  if (row_kind) *row_kind = ROW_DM;
  ContactInfo* c = the_mesh.lookupContactByPubKey(id.id, CHAT_ID_LEN);
  if (c) { strncpy(dest, c->name, size - 1); dest[size - 1] = 0; return; }
  // contact gone: name from the history
  for (int i = _store.count() - 1; i >= 0; i--) {
    ChatRecord& r = _store.at(i);
    if (r.chat() == id && r.status == MSG_IN && r.sender[0]) { strncpy(dest, r.sender, size - 1); dest[size - 1] = 0; return; }
  }
  strncpy(dest, "Контакт", size - 1); dest[size - 1] = 0;
}

void ChatsController::rebuildList() {
  if (!_rows) return;
  int n = 0, total_unread = 0;

  // chats with messages, newest activity first
  for (int i = _store.count() - 1; i >= 0; i--) {
    ChatRecord& r = _store.at(i);
    ChatId id = r.chat();
    ChannelDetails ch;
    if (id.kind == CHAT_CHANNEL && !channelOf(id, ch)) continue;   // channel was deleted / replaced

    int k = 0;
    while (k < n && !(_row_ids[k] == id)) k++;
    if (k == n) {
      if (n >= CHATS_MAX_ROWS) continue;
      UIChatRow& row = _rows[n];
      memset(&row, 0, sizeof(row));
      _row_ids[n] = id;
      chatTitle(id, row.title, sizeof(row.title), &row.kind);
      if (r.status != MSG_IN) snprintf(row.preview, sizeof(row.preview), "Вы: %s", r.text);
      else { strncpy(row.preview, r.text, sizeof(row.preview) - 1); }
      for (char* p = row.preview; *p; p++) if (*p == '\n') *p = ' ';
      formatTime(r.time, row.time, sizeof(row.time));
      n++;
    }
    Entry* e = entry(id, false);
    uint32_t last_read = e ? e->last_read : _boot_seq;
    if (r.status == MSG_IN && r.seq > last_read) { _rows[k].unread++; total_unread++; }
  }

  // channels without messages yet
  for (int idx = 0; idx < MAX_GROUP_CHANNELS && n < CHATS_MAX_ROWS; idx++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(idx, ch) || ch.name[0] == 0) continue;
    ChatId id = channelId(idx, ch.channel.secret);
    int k = 0;
    while (k < n && !(_row_ids[k] == id)) k++;
    if (k < n) continue;
    UIChatRow& row = _rows[n];
    memset(&row, 0, sizeof(row));
    _row_ids[n] = id;
    chatTitle(id, row.title, sizeof(row.title), &row.kind);
    strcpy(row.preview, "сообщений пока нет");
    n++;
  }

  _list_count = n;
  if (_view.screen == CHATS_LIST) {
    _view.rows = _rows;
    _view.row_count = n;
    int max_scroll = n - UIChats::visibleRows(_view);
    if (_view.row_scroll > max_scroll) _view.row_scroll = max_scroll > 0 ? max_scroll : 0;
  }
  _unread_total = total_unread;
  _list_dirty = false;
  _dirty = true;
}

void ChatsController::rebuildContacts() {
  if (!_contact_rows) return;
  int n = 0;
  static uint32_t lastmod[CHATS_MAX_ROWS];
  ContactsIterator it = the_mesh.startContactsIterator();
  ContactInfo c;
  while (it.hasNext(&the_mesh, c) && n < CHATS_MAX_ROWS) {
    if (c.type == ADV_TYPE_NONE || c.name[0] == 0) continue;
    // insertion sort, most recently heard first
    int k = n;
    while (k > 0 && lastmod[k - 1] < c.lastmod) {
      _contact_rows[k] = _contact_rows[k - 1]; _contact_ids[k] = _contact_ids[k - 1];
      _contact_type[k] = _contact_type[k - 1]; lastmod[k] = lastmod[k - 1];
      k--;
    }
    UIChatRow& row = _contact_rows[k];
    memset(&row, 0, sizeof(row));
    strncpy(row.title, c.name, sizeof(row.title) - 1);
    switch (c.type) {
      case ADV_TYPE_CHAT:     row.kind = ROW_CONTACT_CHAT;     strcpy(row.preview, "компаньон"); break;
      case ADV_TYPE_REPEATER: row.kind = ROW_CONTACT_REPEATER; strcpy(row.preview, "ретранслятор \xE2\x80\xA2 QR-код"); break;
      case ADV_TYPE_ROOM:     row.kind = ROW_CONTACT_ROOM;     strcpy(row.preview, "комната \xE2\x80\xA2 QR-код"); break;
      default:                row.kind = ROW_CONTACT_SENSOR;   strcpy(row.preview, "сенсор \xE2\x80\xA2 QR-код"); break;
    }
    _contact_ids[k] = dmId(c.id.pub_key);
    _contact_type[k] = c.type;
    lastmod[k] = c.lastmod;
    n++;
  }
  _contact_count = n;
}

void ChatsController::rebuildChat() {
  int found = 0;
  int idx[CHATS_MAX_BUBBLES];
  for (int i = _store.count() - 1; i >= 0 && found < CHATS_MAX_BUBBLES; i--) {
    if (_store.at(i).chat() == _open) idx[found++] = i;
  }
  for (int b = 0; b < found; b++) {
    ChatRecord& r = _store.at(idx[found - 1 - b]);       // oldest first
    UIBubble& u = _bubbles[b];
    u.sender = NULL;
    u.text = r.text;
    switch (r.status) {
      case MSG_IN:            u.status = BUBBLE_IN; break;
      case MSG_OUT_DELIVERED: u.status = BUBBLE_DELIVERED; break;
      case MSG_OUT_FAILED:    u.status = BUBBLE_FAILED; break;
      default:                u.status = BUBBLE_SENT; break;
    }
    if (r.status == MSG_IN && r.kind == CHAT_CHANNEL) {
      // channel text is "<sender>: <message>"
      const char* sep = strstr(r.text, ": ");
      if (sep && sep - r.text < 32) {
        size_t n = sep - r.text;
        memcpy(_bubble_sender[b], r.text, n);
        _bubble_sender[b][n] = 0;
        u.sender = _bubble_sender[b];
        u.text = sep + 2;
      }
    }
    formatTime(r.time, u.time, sizeof(u.time));
  }
  _view.bubble_count = found;
  if (_view.bubble_scroll > found - 1) _view.bubble_scroll = found > 0 ? found - 1 : 0;
  chatTitle(_open, _view.chat_title, sizeof(_view.chat_title), &_view.chat_kind);
  _dirty = true;
}

// ------------------------------------------------------------ navigation ----

void ChatsController::showScreen(UIChatScreen s) {
  UIChatScreen prev = _view.screen;
  _view.screen = s;
  if (s == CHATS_LIST) {
    if (prev != CHATS_LIST) _view.row_scroll = 0;
    rebuildList();
  } else if (s == CHATS_CONTACTS) {
    _view.rows = _contact_rows;
    _view.row_count = _contact_count;
    if (prev != CHATS_CONTACTS) _view.row_scroll = 0;
  }
  _dirty = true;
}

void ChatsController::showToast(const char* text) {
  strncpy(_view.toast, text, sizeof(_view.toast) - 1);
  _view.toast[sizeof(_view.toast) - 1] = 0;
  _toast_until = millis() + TOAST_MS;
  _dirty = true;
}

void ChatsController::openChat(const ChatId& id) {
  _open = id;
  _view.bubble_scroll = 0;
  _view.screen = CHATS_VIEW;
  markRead(id);
  rebuildChat();
}

const UIChatsView* ChatsController::view() {
  if (_list_dirty && _view.screen == CHATS_LIST) rebuildList();
  return &_view;
}

void ChatsController::tap(int x, int y) {
  UIChatHit h = UIChats::hitTest(_view, x, y);
  switch (_view.screen) {
    case CHATS_LIST:
      if (h.type == HIT_ROW) openChat(_row_ids[h.index]);
      else if (h.type == HIT_NEW_CHAT) showScreen(CHATS_NEW);
      else if (h.type == HIT_MY_QR) showMyQR();
      break;
    case CHATS_NEW:
      if (h.type == HIT_BACK) showScreen(CHATS_LIST);
      else if (h.type == HIT_NEW_DM) { rebuildContacts(); showScreen(CHATS_CONTACTS); }
      else if (h.type == HIT_NEW_PRIVATE) openKeyboard(KB_NEW_PRIVATE);
      else if (h.type == HIT_NEW_PUBLIC) openKeyboard(KB_NEW_PUBLIC);
      else if (h.type == HIT_MY_QR) showMyQR();
      break;
    case CHATS_CONTACTS:
      if (h.type == HIT_BACK) showScreen(CHATS_NEW);
      else if (h.type == HIT_ROW) {
        if (_contact_type[h.index] == ADV_TYPE_CHAT) {
          openChat(_contact_ids[h.index]);
        } else {
          ContactInfo* c = the_mesh.lookupContactByPubKey(_contact_ids[h.index].id, CHAT_ID_LEN);
          if (c) showContactQR(c->id.pub_key, c->name, c->type);
        }
      }
      break;
    case CHATS_VIEW:
      if (h.type == HIT_BACK) showScreen(CHATS_LIST);
      else if (h.type == HIT_CHAT_QR) showChatQR();
      else if (h.type == HIT_COMPOSE) openKeyboard(KB_SEND);
      break;
    case CHATS_KEYBOARD:
      if (h.type == HIT_BACK) showScreen(_kb_return);
      else if (h.type == HIT_KEY) keyPressed(h.key);
      break;
    case CHATS_QR:
      showScreen(_qr_return);
      break;
  }
}

bool ChatsController::swipe(int dx, int dy) {
  bool horizontal = abs(dx) > abs(dy);
  if (horizontal) {
    if (_view.screen == CHATS_LIST) return false;          // switch pages
    if (dx > 0) {                                          // swipe right = back
      switch (_view.screen) {
        case CHATS_VIEW: case CHATS_NEW: showScreen(CHATS_LIST); break;
        case CHATS_CONTACTS: showScreen(CHATS_NEW); break;
        case CHATS_KEYBOARD: showScreen(_kb_return); break;
        case CHATS_QR: showScreen(_qr_return); break;
        default: break;
      }
    }
    return true;
  }

  // vertical: scroll
  if (_view.screen == CHATS_LIST || _view.screen == CHATS_CONTACTS) {
    int vis = UIChats::visibleRows(_view);
    int step = vis > 1 ? vis - 1 : 1;
    int s = _view.row_scroll + (dy < 0 ? step : -step);
    int max_scroll = _view.row_count - vis;
    if (s > max_scroll) s = max_scroll;
    if (s < 0) s = 0;
    if (s != _view.row_scroll) { _view.row_scroll = s; _dirty = true; }
  } else if (_view.screen == CHATS_VIEW) {
    int s = _view.bubble_scroll + (dy > 0 ? 2 : -2);       // finger down = older messages
    if (s > _view.bubble_count - 1) s = _view.bubble_count - 1;
    if (s < 0) s = 0;
    if (s != _view.bubble_scroll) { _view.bubble_scroll = s; _dirty = true; }
  }
  return true;
}

// -------------------------------------------------------------- keyboard ----

int ChatsController::maxTextBytes() const {
  if (_kb_purpose == KB_NEW_PRIVATE) return CHANNEL_NAME_MAX;
  if (_kb_purpose == KB_NEW_PUBLIC) return CHANNEL_NAME_MAX - 1;   // + '#'
  if (_open.kind == CHAT_CHANNEL) {
    int n = MAX_TEXT_LEN - (int)strlen(the_mesh.getNodePrefs()->node_name) - 2;   // "<name>: "
    return n > 0 ? n : 0;
  }
  return MAX_TEXT_LEN;
}

void ChatsController::openKeyboard(KbPurpose p) {
  _kb_purpose = p;
  _kb_return = _view.screen;
  char title[48], up[40];
  switch (p) {
    case KB_SEND:
      upperStr(_view.chat_title, up, sizeof(up));
      snprintf(title, sizeof(title), "%s %s", _open.kind == CHAT_CHANNEL ? "В КАНАЛ" : "ДЛЯ", up);
      // a draft of this chat stays when the keyboard is left and opened again
      _input.open(TextInput::TEXT, title, "Отпр.", _draft_for == _open ? _input.text() : "", maxTextBytes());
      break;
    case KB_NEW_PRIVATE:
      _input.open(TextInput::TEXT, "НАЗВАНИЕ КАНАЛА", "Создать", "", maxTextBytes());
      break;
    case KB_NEW_PUBLIC:
      _input.open(TextInput::HASHTAG, "ПУБЛИЧНЫЙ КАНАЛ", "Создать", "", maxTextBytes(), "#");
      break;
  }
  _draft_for = _open;
  _view.kb = _input.view;
  showScreen(CHATS_KEYBOARD);
}

void ChatsController::keyPressed(const UIKey& k) {
  if (_input.key(k)) {
    confirmKeyboard();
    return;
  }
  _view.kb = _input.view;
  _dirty = true;
}

void ChatsController::confirmKeyboard() {
  char text[212];
  strncpy(text, _input.text(), sizeof(text) - 1);
  text[sizeof(text) - 1] = 0;
  if (!text[0]) return;
  _input.clear();
  _view.kb = _input.view;

  if (_kb_purpose == KB_SEND) {
    sendMessage(text);
  } else if (_kb_purpose == KB_NEW_PRIVATE) {
    createChannel(false, text);
  } else {
    char name[CHANNEL_NAME_MAX + 2];
    snprintf(name, sizeof(name), "#%s", text);
    createChannel(true, name);
  }
}

// --------------------------------------------------------------- actions ----

void ChatsController::sendMessage(const char* text) {
  const char* me = the_mesh.getNodePrefs()->node_name;
  uint32_t t = nowTime(0);
  if (_open.kind == CHAT_DM) {
    uint32_t ack = 0, est = 0;
    int res = the_mesh.uiSendDirect(_open.id, text, ack, est);
    ChatRecord* r = _store.add(_open, me, text, t, res == MSG_SEND_FAILED ? MSG_OUT_FAILED : MSG_OUT_SENT);
    if (res == MSG_SEND_FAILED) {
      showToast("Не отправлено: контакт не найден");
    } else if (ack && r) {
      for (int i = 0; i < CHATS_MAX_ACKS; i++) {
        if (_acks[i].ack) continue;
        unsigned long wait = (unsigned long)est * 2;
        if (wait < ACK_MIN_WAIT_MS) wait = ACK_MIN_WAIT_MS;
        _acks[i] = { ack, r->seq, millis() + wait };
        break;
      }
    }
  } else {
    ChannelDetails ch;
    bool ok = channelOf(_open, ch) && the_mesh.uiSendChannel(_open.id[0], text);
    _store.add(_open, me, text, t, ok ? MSG_OUT_SENT : MSG_OUT_FAILED);
    if (!ok) showToast("Не отправлено");
  }
  _view.bubble_scroll = 0;
  _view.screen = CHATS_VIEW;
  markRead(_open);
  rebuildChat();
}

void ChatsController::createChannel(bool is_public, const char* name) {
  uint8_t secret[16];
  if (is_public) {
    // same key as the MeshCore apps: SHA-256 of the lower-cased "#name", first 16 bytes
    char lower[CHANNEL_NAME_MAX + 2];
    size_t i = 0;
    for (; name[i] && i < sizeof(lower) - 1; i++) lower[i] = (name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i];
    lower[i] = 0;
    mesh::Utils::sha256(secret, sizeof(secret), (const uint8_t*)lower, strlen(lower));
  } else {
    esp_fill_random(secret, sizeof(secret));
  }

  // already there? (same key)
  for (int idx = 0; idx < MAX_GROUP_CHANNELS; idx++) {
    ChannelDetails ch;
    if (the_mesh.getChannel(idx, ch) && ch.name[0] && memcmp(ch.channel.secret, secret, 16) == 0) {
      openChat(channelId(idx, secret));
      showToast("Этот канал уже есть");
      return;
    }
  }

  int idx = the_mesh.uiAddChannel(name, secret);
  if (idx < 0) {
    showScreen(CHATS_NEW);
    showToast("Нет свободного места для канала");
    return;
  }
  openChat(channelId(idx, secret));
  showToast(is_public ? "Канал создан" : "Канал создан. QR - кнопка справа");
}

// ------------------------------------------------------------------- QR ----

bool ChatsController::showQR(const char* url, const char* title, const char* l1, const char* l2, const char* l3) {
  if (!_qr) return false;
  s_qr_dest = _qr;
  s_qr_size = 0;
  esp_qrcode_config_t cfg = { qrCopy, 16, ESP_QRCODE_ECC_MED };
  if (esp_qrcode_generate(&cfg, url) != ESP_OK || s_qr_size == 0) {
    showToast("Не удалось построить QR-код");
    return false;
  }
  _view.qr = _qr;
  _view.qr_size = s_qr_size;
  strncpy(_view.qr_title, title, sizeof(_view.qr_title) - 1);
  _view.qr_title[sizeof(_view.qr_title) - 1] = 0;
  const char* lines[3] = { l1, l2, l3 };
  for (int i = 0; i < 3; i++) {
    strncpy(_view.qr_lines[i], lines[i] ? lines[i] : "", sizeof(_view.qr_lines[i]) - 1);
    _view.qr_lines[i][sizeof(_view.qr_lines[i]) - 1] = 0;
  }
  _qr_return = _view.screen;
  showScreen(CHATS_QR);
  return true;
}

void ChatsController::showContactQR(const uint8_t* pub_key, const char* name, uint8_t type) {
  char enc[100], hex[PUB_KEY_SIZE * 2 + 1], url[240];
  urlEncode(name, enc, sizeof(enc));
  toHexLower(hex, pub_key, PUB_KEY_SIZE);
  snprintf(url, sizeof(url), "meshcore://contact/add?name=%s&public_key=%s&type=%d", enc, hex, type ? type : ADV_TYPE_CHAT);
  showQR(url, name, "Контакт для MeshCore.", "В приложении: Контакты,", "+ и сканировать QR");
}

void ChatsController::showMyQR() {
  NodePrefs* p = the_mesh.getNodePrefs();
  char enc[100], hex[PUB_KEY_SIZE * 2 + 1], url[240];
  urlEncode(p->node_name, enc, sizeof(enc));
  toHexLower(hex, the_mesh.getSelfPubKey(), PUB_KEY_SIZE);
  snprintf(url, sizeof(url), "meshcore://contact/add?name=%s&public_key=%s&type=%d", enc, hex, ADV_TYPE_CHAT);
  showQR(url, p->node_name, "Эта нода как контакт.", "В приложении: Контакты,", "+ и сканировать QR");
}

void ChatsController::showChatQR() {
  if (_open.kind == CHAT_DM) {
    ContactInfo* c = the_mesh.lookupContactByPubKey(_open.id, CHAT_ID_LEN);
    if (c) showContactQR(c->id.pub_key, c->name, c->type);
    else showToast("Контакт не найден");
    return;
  }
  ChannelDetails ch;
  if (!channelOf(_open, ch)) { showToast("Канал не найден"); return; }
  char enc[100], hex[33], url[200];
  urlEncode(ch.name, enc, sizeof(enc));
  toHexLower(hex, ch.channel.secret, 16);
  snprintf(url, sizeof(url), "meshcore://channel/add?name=%s&secret=%s", enc, hex);
  bool pub = isPublicChannelName(ch.name);
  showQR(url, ch.name, pub ? "Публичный канал." : "Приватный канал: QR",
         pub ? "В приложении: Каналы," : "даёт доступ к нему.", pub ? "+ и сканировать QR" : "Каналы, + , QR");
}
