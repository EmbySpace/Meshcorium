#!/usr/bin/env python3
"""
Prints the Meshcorium chat history as text (or CSV).

  python read_chats.py chats.bin               # from the TF card: /meshcore/chats.bin
  python read_chats.py ui_chats.bin            # the last 25 kept in the node's flash
  python read_chats.py chats.bin --csv > chats.csv
  python read_chats.py chats.bin --chat 0      # only channel slot 0
  python read_chats.py chats.bin --tz 3        # local time = UTC + 3 h (default)

File format (see ChatStore.h): fixed 256-byte little-endian records
  u32 seq, u32 time (unix), u8 kind (1 = direct, 2 = channel), u8 status,
  u8 id[6], char sender[32], char text[208]
ui_chats.bin starts with an 8-byte header ("MCHT", u16 version, u16 count).
Channel names are not stored, only the channel slot; channel texts already
contain "<sender>: <message>".
"""
import argparse
import csv
import struct
import sys
from datetime import datetime, timedelta, timezone

RECORD = struct.Struct("<IIBB6s32s208s")
assert RECORD.size == 256
STATUS = {0: "", 1: "отправлено", 2: "доставлено", 3: "не доставлено"}


def cstr(b):
    return b.split(b"\0", 1)[0].decode("utf-8", "replace")


def records(path):
    with open(path, "rb") as f:
        data = f.read()
    off = 8 if data[:4] == b"MCHT" else 0
    for i in range(off, len(data) - RECORD.size + 1, RECORD.size):
        seq, t, kind, status, cid, sender, text = RECORD.unpack_from(data, i)
        if seq:
            yield seq, t, kind, status, cid, cstr(sender), cstr(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--csv", action="store_true", help="CSV instead of text")
    ap.add_argument("--chat", help="only this chat: channel slot number or start of a contact key (hex)")
    ap.add_argument("--tz", type=float, default=3.0, help="hours added to UTC (default 3)")
    a = ap.parse_args()
    tz = timezone(timedelta(hours=a.tz))

    sys.stdout.reconfigure(encoding="utf-8")       # Cyrillic also in the Windows console
    out = csv.writer(sys.stdout) if a.csv else None
    if out:
        sys.stdout.write("﻿")                  # BOM: Excel then reads the CSV as UTF-8
        out.writerow(["seq", "time", "chat", "direction", "sender", "text", "status"])

    for seq, t, kind, status, cid, sender, text in records(a.file):
        chat = f"канал {cid[0]}" if kind == 2 else f"личка {cid.hex()}"
        if a.chat is not None:
            if kind == 2 and a.chat != str(cid[0]):
                continue
            if kind == 1 and not cid.hex().startswith(a.chat.lower()):
                continue
        when = datetime.fromtimestamp(t, tz).strftime("%Y-%m-%d %H:%M") if t > 1700000000 else "время не задано"
        outgoing = status != 0
        if kind == 2 and not outgoing:
            who, msg = (text.split(": ", 1) + [""])[:2] if ": " in text else ("", text)
        else:
            who, msg = sender, text
        if out:
            out.writerow([seq, when, chat, "исх" if outgoing else "вх", who, msg, STATUS.get(status, "")])
        else:
            mark = "->" if outgoing else "<-"
            st = f"  ({STATUS[status]})" if outgoing and STATUS.get(status) else ""
            print(f"{when}  [{chat}] {mark} {who}: {msg}{st}")


if __name__ == "__main__":
    main()
