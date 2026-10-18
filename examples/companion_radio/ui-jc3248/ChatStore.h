#pragma once

// Chat history for the JC3248W535 screen.
//
// The companion firmware itself keeps no message history (it only queues
// messages until the phone app fetches them), so the screen keeps its own:
//  - RAM cache (PSRAM) with the newest CHAT_CACHE_MAX messages, for display
//  - internal flash (SPIFFS): the newest CHAT_FLASH_KEEP messages, survives reboots
//  - TF card: every message, appended to /meshcore/chats.bin
// To spare the flash, new messages are written in batches of CHAT_FLUSH_BATCH
// (or after CHAT_FLUSH_MAX_MS if fewer arrive). A power loss can therefore
// lose the last few unsaved messages.

#include <stdint.h>
#include <stddef.h>

namespace fs { class FS; }

#define CHAT_CACHE_MAX      600
#define CHAT_FLASH_KEEP     25
#define CHAT_FLUSH_BATCH    5
#define CHAT_FLUSH_MAX_MS   (30UL * 60 * 1000)
#define CHAT_ID_LEN         6

enum ChatKind : uint8_t { CHAT_NONE = 0, CHAT_DM = 1, CHAT_CHANNEL = 2 };

enum ChatMsgStatus : uint8_t {
  MSG_IN = 0,          // received
  MSG_OUT_SENT,        // sent from this screen (channel: final state)
  MSG_OUT_DELIVERED,   // direct message, ACK received
  MSG_OUT_FAILED,      // direct message, no ACK in time / send failed
};

// which chat a message belongs to:
//  DM      -> first 6 bytes of the contact's public key
//  channel -> channel slot index + first 5 bytes of its secret
struct ChatId {
  uint8_t kind;
  uint8_t id[CHAT_ID_LEN];
  bool operator==(const ChatId& o) const;
};

// one message, 256 bytes in RAM, on flash and on the card
struct ChatRecord {
  uint32_t seq;            // monotonic, 0 = empty
  uint32_t time;           // unix time (our clock if set, else the sender's)
  uint8_t kind;
  uint8_t status;          // ChatMsgStatus
  uint8_t id[CHAT_ID_LEN];
  char sender[32];
  char text[208];

  ChatId chat() const;
};

class ChatStore {
public:
  ChatStore() : _cache(NULL), _start(0), _count(0), _next_seq(1), _pending(0), _first_pending(0),
                _flash(NULL), _sd(NULL), _sd_max_seq(0), _dirty_flash(false) { }

  bool begin(fs::FS* flash);
  // (re)attach the card after it was mounted; NULL = card gone
  void attachSd(fs::FS* sd);
  bool hasSd() const { return _sd != NULL; }

  // appends a message; returns it (valid until the cache wraps around)
  ChatRecord* add(const ChatId& chat, const char* sender, const char* text, uint32_t time, uint8_t status);
  // a record changed (e.g. delivery status): saved with the next batch
  void touched() { _dirty_flash = true; }

  void loop(unsigned long now_ms);
  void flush();

  int count() const { return _count; }
  ChatRecord& at(int i) { return _cache[(_start + i) % CHAT_CACHE_MAX]; }   // 0 = oldest
  ChatRecord* bySeq(uint32_t seq);
  uint32_t lastSeq() const { return _next_seq - 1; }

private:
  ChatRecord* _cache;
  int _start, _count;
  uint32_t _next_seq;
  int _pending;
  unsigned long _first_pending;
  fs::FS* _flash;
  fs::FS* _sd;
  uint32_t _sd_max_seq;
  bool _dirty_flash;

  void push(const ChatRecord& r);
  void saveFlash();
  void appendSd();
};
