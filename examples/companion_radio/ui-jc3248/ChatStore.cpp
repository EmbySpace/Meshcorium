#include "ChatStore.h"
#include <Arduino.h>
#include <FS.h>
#include <esp_heap_caps.h>
#include <string.h>

static_assert(sizeof(ChatRecord) == 256, "ChatRecord is a fixed 256-byte file record");

#define FLASH_FILE   "/ui_chats.bin"
#define SD_DIR       "/meshcore"
#define SD_FILE      "/meshcore/chats.bin"
#define FILE_MAGIC   0x5448434DUL     // "MCHT"
#define FILE_VERSION 1

struct FlashHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t count;
};

bool ChatId::operator==(const ChatId& o) const {
  return kind == o.kind && memcmp(id, o.id, CHAT_ID_LEN) == 0;
}

ChatId ChatRecord::chat() const {
  ChatId c;
  c.kind = kind;
  memcpy(c.id, id, CHAT_ID_LEN);
  return c;
}

static void copyStr(char* dest, size_t size, const char* src) {
  if (!src) src = "";
  strncpy(dest, src, size - 1);
  dest[size - 1] = 0;
  // don't leave a cut UTF-8 sequence at the end
  size_t n = strlen(dest);
  if (n > 0 && ((uint8_t)dest[n - 1] & 0x80)) {
    size_t k = n;
    while (k > 0 && ((uint8_t)dest[k - 1] & 0xC0) == 0x80) k--;   // continuation bytes
    if (k > 0) {
      uint8_t lead = (uint8_t)dest[k - 1];
      size_t need = (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
      if (n - (k - 1) < need) dest[k - 1] = 0;
    }
  }
}

bool ChatStore::begin(fs::FS* flash) {
  _flash = flash;
  _cache = (ChatRecord*)heap_caps_calloc(CHAT_CACHE_MAX, sizeof(ChatRecord), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!_cache) return false;

  if (_flash && _flash->exists(FLASH_FILE)) {
    fs::File f = _flash->open(FLASH_FILE, "r");
    FlashHeader h;
    if (f && f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) && h.magic == FILE_MAGIC && h.version == FILE_VERSION) {
      ChatRecord r;
      for (int i = 0; i < h.count && f.read((uint8_t*)&r, sizeof(r)) == sizeof(r); i++) {
        if (r.seq) push(r);
      }
    }
    if (f) f.close();
  }
  return true;
}

void ChatStore::push(const ChatRecord& r) {
  if (_count < CHAT_CACHE_MAX) {
    _cache[(_start + _count) % CHAT_CACHE_MAX] = r;
    _count++;
  } else {                                  // full: overwrite the oldest
    _cache[_start] = r;
    _start = (_start + 1) % CHAT_CACHE_MAX;
  }
  if (r.seq >= _next_seq) _next_seq = r.seq + 1;
}

ChatRecord* ChatStore::bySeq(uint32_t seq) {
  for (int i = _count - 1; i >= 0; i--) {
    ChatRecord& r = at(i);
    if (r.seq == seq) return &r;
    if (r.seq < seq) break;
  }
  return NULL;
}

ChatRecord* ChatStore::add(const ChatId& chat, const char* sender, const char* text, uint32_t time, uint8_t status) {
  if (!_cache) return NULL;
  ChatRecord r;
  memset(&r, 0, sizeof(r));
  r.seq = _next_seq;
  r.time = time;
  r.kind = chat.kind;
  r.status = status;
  memcpy(r.id, chat.id, CHAT_ID_LEN);
  copyStr(r.sender, sizeof(r.sender), sender);
  copyStr(r.text, sizeof(r.text), text);
  push(r);

  if (_pending == 0) _first_pending = millis();
  _pending++;
  return &at(_count - 1);
}

void ChatStore::attachSd(fs::FS* sd) {
  _sd = sd;
  _sd_max_seq = 0;
  if (!_sd || !_cache) return;

  if (!_sd->exists(SD_DIR)) _sd->mkdir(SD_DIR);

  // load the tail of the card's log and merge it with what is in RAM
  // (flash recents + anything new): card records first, then RAM records
  // the card does not have yet
  ChatRecord* merged = (ChatRecord*)heap_caps_malloc(CHAT_CACHE_MAX * sizeof(ChatRecord), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  int n = 0;
  if (_sd->exists(SD_FILE)) {
    fs::File f = _sd->open(SD_FILE, "r");
    if (f) {
      size_t total = f.size() / sizeof(ChatRecord);
      size_t first = total > CHAT_CACHE_MAX ? total - CHAT_CACHE_MAX : 0;
      f.seek(first * sizeof(ChatRecord));
      ChatRecord r;
      while (f.read((uint8_t*)&r, sizeof(r)) == sizeof(r)) {
        if (r.seq > _sd_max_seq) _sd_max_seq = r.seq;
        if (merged && r.seq) merged[n++] = r;
      }
      f.close();
    }
  }
  if (!merged) return;

  for (int i = 0; i < _count && n < CHAT_CACHE_MAX; i++) {
    if (at(i).seq > _sd_max_seq) merged[n++] = at(i);
  }
  _start = 0;
  _count = 0;
  for (int i = 0; i < n; i++) push(merged[i]);
  heap_caps_free(merged);

  appendSd();   // catch up with messages kept in flash only
}

void ChatStore::appendSd() {
  if (!_sd) return;
  int first = _count;
  while (first > 0 && at(first - 1).seq > _sd_max_seq) first--;
  if (first == _count) return;

  fs::File f = _sd->open(SD_FILE, "a");
  if (!f) { _sd = NULL; return; }   // card removed / broken: carry on with flash only
  for (int i = first; i < _count; i++) {
    if (f.write((const uint8_t*)&at(i), sizeof(ChatRecord)) != sizeof(ChatRecord)) break;
    _sd_max_seq = at(i).seq;
  }
  f.close();
}

void ChatStore::saveFlash() {
  if (!_flash) return;
  fs::File f = _flash->open(FLASH_FILE, "w");
  if (!f) return;
  int n = _count < CHAT_FLASH_KEEP ? _count : CHAT_FLASH_KEEP;
  FlashHeader h = { FILE_MAGIC, FILE_VERSION, (uint16_t)n };
  f.write((const uint8_t*)&h, sizeof(h));
  for (int i = _count - n; i < _count; i++) f.write((const uint8_t*)&at(i), sizeof(ChatRecord));
  f.close();
}

void ChatStore::flush() {
  appendSd();
  saveFlash();
  _pending = 0;
  _dirty_flash = false;
}

void ChatStore::loop(unsigned long now_ms) {
  if (_pending >= CHAT_FLUSH_BATCH || (_pending > 0 && now_ms - _first_pending >= CHAT_FLUSH_MAX_MS)) {
    flush();
  }
}
