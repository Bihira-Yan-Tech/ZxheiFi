/*
 * telegram.h - Telegram Bot API sales/event notifications
 * ZXHEIFI - NodeMCU ESP8266 Firmware
 *
 * Real gap found during the 2026-09-04 original-JuanFi audit: config.h
 * had TELEGRAM_BOT_TOKEN/TELEGRAM_CHAT_ID as placeholders, but nothing
 * ever actually sent anything - the previous comparison doc incorrectly
 * claimed this was "preserved" from the original. The original sends
 * FROM the router via a RouterOS on-login script (`/tool fetch` to the
 * Bot API); this project's architecture keeps everything NodeMCU-side,
 * so this is a NodeMCU HTTPS client instead.
 *
 * Messages are queued (queueMessage()) and sent one at a time from the
 * main loop (loop()), never inline in a request handler - the TLS
 * handshake to api.telegram.org can take 1-3+ seconds, which would
 * otherwise delay the customer's own login response.
 */
#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <Arduino.h>
#include "platform.h"
#include "config.h"

class TelegramNotifier {
public:
  bool enabled = false;
  String botToken;
  String chatId;

  // Drops the oldest queued message if the ring buffer is full, rather
  // than blocking or growing unbounded - a missed notification during a
  // busy stretch isn't worth risking memory pressure over on an
  // 80KB-RAM device.
  void queueMessage(const String& text) {
    if (!enabled || !botToken.length() || !chatId.length()) return;
    String trimmed = text.length() > TELEGRAM_MAX_MSG_LEN ? text.substring(0, TELEGRAM_MAX_MSG_LEN) : text;
    _queue[_head] = trimmed;
    _head = (_head + 1) % TELEGRAM_QUEUE_SIZE;
    if (_count < TELEGRAM_QUEUE_SIZE) {
      _count++;
    } else {
      _tail = (_tail + 1) % TELEGRAM_QUEUE_SIZE; // buffer was full - oldest just got overwritten
    }
  }

  // Sends at most one queued message per call - call this every main
  // loop iteration. Each send blocks for the TLS handshake duration,
  // which is exactly why this must never be called from inside a
  // request handler.
  void loop() {
    if (_count == 0 || !WiFi.isConnected()) return;

    String message = _queue[_tail];
    _tail = (_tail + 1) % TELEGRAM_QUEUE_SIZE;
    _count--;

    std::unique_ptr<ZxSecureClient> client(new ZxSecureClient());
    // No certificate pinning - a common, accepted tradeoff for
    // hobbyist/small-business ESP8266 projects talking to a fixed,
    // well-known API host, given the RAM/complexity cost of managing a
    // real cert store on this platform.
    client->setInsecure();

    HTTPClient https;
    String url = "https://api.telegram.org/bot" + botToken + "/sendMessage";
    if (!https.begin(*client, url)) return;
    https.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String body = "chat_id=" + chatId + "&text=" + urlEncode(message);
    https.POST(body);
    https.end();
  }

private:
  String _queue[TELEGRAM_QUEUE_SIZE];
  uint8_t _head = 0, _tail = 0, _count = 0;

  static String urlEncode(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
      char c = in[i];
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
        out += c;
      } else if (c == ' ') {
        out += '+';
      } else {
        char buf[4];
        snprintf(buf, sizeof(buf), "%%%02X", (unsigned char)c);
        out += buf;
      }
    }
    return out;
  }
};

#endif // TELEGRAM_H
