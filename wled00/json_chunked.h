#pragma once
// Requires wled.h (for AsyncWebServerRequest, CONTENT_TYPE_JSON, FPSTR) to be included first.

#include <functional>
#include <memory>
#include <type_traits>

class AsyncWebServerRequest;


// ── writeJSONString ───────────────────────────────────────────────────────────
// Writes a JSON-escaped quoted string into dest[0..maxLen-1].
// Returns bytes written, or 0 if the buffer was too small.

inline size_t writeJSONString(uint8_t* dest, size_t maxLen, const char* src) {
  size_t pos = 0;

  auto emit = [&](char c) -> bool {
    if (pos >= maxLen) return false;
    dest[pos++] = static_cast<uint8_t>(c);
    return true;
  };

  if (!emit('"')) return 0;
  for (const char* p = src; *p; ++p) {
    char esc = 0;
    switch (*p) {
      case '"':  esc = '"';  break;
      case '\\': esc = '\\'; break;
      case '\b': esc = 'b';  break;
      case '\f': esc = 'f';  break;
      case '\n': esc = 'n';  break;
      case '\r': esc = 'r';  break;
      case '\t': esc = 't';  break;
    }
    if (esc) {
      if (!emit('\\') || !emit(esc)) return 0;
    } else {
      if (!emit(*p)) return 0;
    }
  }
  if (!emit('"')) return 0;
  return pos;
}


// ── WriteResult ───────────────────────────────────────────────────────────────
// Return type for all writer callables: (uint8_t*, size_t) -> WriteResult.
//
//   {false, 0}  no room; retry with a fresh buffer
//   {false, n}  wrote n bytes, more to come; stop filling current buffer
//   {true,  n}  wrote n final bytes; writer is done (caller may advance)
//   {true,  0}  writer was already done (or item was empty/skipped)
//
// The "not done" signal tells the outer layer to stop filling the current
// buffer immediately — the sub-writer couldn't complete in the space left.

struct WriteResult {
  bool   done;
  size_t count;
};


// ── is_item_factory ───────────────────────────────────────────────────────────
// Detects whether Callback is a 1-arg factory (Iterator) -> writer,
// or a 3-arg direct writer (Iterator, uint8_t*, size_t) -> size_t.

template<typename Callback, typename Iterator, typename = void>
struct is_item_factory : std::false_type {};

template<typename Callback, typename Iterator>
struct is_item_factory<Callback, Iterator,
  decltype(void(std::declval<Callback>()(std::declval<Iterator>())))>
  : std::true_type {};


// ── JSONListWriter ────────────────────────────────────────────────────────────
// Stateful writer for the direct-writer path.
// Callback: (Iterator, uint8_t*, size_t) -> size_t
//   Return n > 0: wrote n bytes of this item (item is complete).
//   Return 0:     item didn't fit; will be retried next call.

template<typename Iterator, typename Callback>
struct JSONListWriter {
  Iterator current, begin_val, end_val;
  Callback cb;
  bool done;

  JSONListWriter(Iterator begin, Iterator end, Callback cb_)
    : current(begin), begin_val(begin), end_val(end), cb(cb_), done(false) {}

  WriteResult operator()(uint8_t* dest, size_t maxLen) {
    if (done) return {true, 0};
    size_t pos = 0;

    while (current != end_val) {
      if (pos + 2 > maxLen) break;                                    // need room for separator + ≥1 byte
      size_t n = cb(current, dest + pos + 1, maxLen - pos - 1);
      if (n == 0) break;                                              // didn't fit; retry next call
      dest[pos] = (current == begin_val) ? '[' : ',';
      pos += 1 + n;
      ++current;
    }

    if (current == end_val) {
      const size_t need = (current == begin_val) ? 2 : 1;            // empty array needs "[]", else "]"
      if (pos + need <= maxLen) {
        if (current == begin_val) dest[pos++] = '[';
        dest[pos++] = ']';
        done = true;
      }
    }

    return {done, pos};
  }
};


// ── JSONListFactoryWriter ─────────────────────────────────────────────────────
// Stateful writer for the factory path.
// MakeItem: (Iterator) -> Writer, where Writer: (uint8_t*, size_t) -> WriteResult
//
// Empty items ({true, 0} on first call) are silently skipped.
// Multi-call items are supported: the separator is committed only when
// the first bytes of the item arrive, so empty items leave no separator.

template<typename Iterator, typename MakeItem>
struct JSONListFactoryWriter {
  typedef std::function<WriteResult(uint8_t*, size_t)> ItemWriter;

  Iterator current, end_val;
  MakeItem makeItem;
  ItemWriter item;          // empty std::function == no item in progress
  bool first, done, itemActive;  // itemActive: separator already written for current item

  JSONListFactoryWriter(Iterator begin, Iterator end, MakeItem makeItem_)
    : current(begin), end_val(end), makeItem(makeItem_),
      first(true), done(false), itemActive(false) {}

  WriteResult operator()(uint8_t* dest, size_t maxLen) {
    if (done) return {true, 0};
    size_t pos = 0;

    while (current != end_val) {
      if (!item) {
        item       = makeItem(current);
        itemActive = false;
      }

      // Reserve a slot for the separator until the item produces its first byte.
      size_t sepReserve = itemActive ? 0 : 1;
      if (pos + sepReserve + 1 > maxLen) break;

      auto [idone, n] = item(dest + pos + sepReserve, maxLen - pos - sepReserve);

      if (n > 0 && !itemActive) {
        dest[pos] = first ? '[' : ',';
        first      = false;
        pos       += 1;
        itemActive = true;
      }
      pos += n;

      if (idone) {
        item       = ItemWriter();
        ++current;
        itemActive = false;
      } else {
        break;   // not done: either no room ({false,0}) or partial write ({false,n})
      }
    }

    if (current == end_val) {
      const size_t need = first ? 2 : 1;
      if (pos + need <= maxLen) {
        if (first) dest[pos++] = '[';
        dest[pos++] = ']';
        done = true;
      }
    }

    return {done, pos};
  }
};


// ── writeJSONList ─────────────────────────────────────────────────────────────
// Returns a stateful writer for [begin, end) using the given callback.
// Dispatches to JSONListWriter or JSONListFactoryWriter based on callback arity.

template<typename Iterator, typename Callback>
typename std::enable_if<
  !is_item_factory<Callback, Iterator>::value,
  JSONListWriter<Iterator, Callback>
>::type
writeJSONList(Iterator begin, Iterator end, Callback cb) {
  return JSONListWriter<Iterator, Callback>(begin, end, cb);
}

template<typename Iterator, typename Callback>
typename std::enable_if<
  is_item_factory<Callback, Iterator>::value,
  JSONListFactoryWriter<Iterator, Callback>
>::type
writeJSONList(Iterator begin, Iterator end, Callback cb) {
  return JSONListFactoryWriter<Iterator, Callback>(begin, end, cb);
}


// ── respondJSONList ───────────────────────────────────────────────────────────
// Sends [begin, end) as a chunked JSON array HTTP response.

template<typename Iterator, typename Callback>
void respondJSONList(AsyncWebServerRequest* request, Iterator begin, Iterator end, Callback cb) {
  auto writer = writeJSONList(begin, end, cb);
  request->sendChunked(FPSTR(CONTENT_TYPE_JSON),
    [writer](uint8_t* data, size_t len, size_t) mutable -> size_t {
      auto [done, n] = writer(data, len);
      return n;
    });
}


// ── KeyValuePair ──────────────────────────────────────────────────────────────
// Holds one JSON object entry: a key writer and a value writer.
// Both use the (uint8_t*, size_t) -> WriteResult contract.

struct KeyValuePair {
  typedef std::function<WriteResult(uint8_t*, size_t)> Writer;
  Writer key;
  Writer value;
};

// makeStringKey: JSON-quoted string key writer.
// The const char* overload captures the pointer — safe for string literals
// (program lifetime) but NOT for local buffers.  Use the String overload when
// the string may not outlive the returned writer.
inline KeyValuePair::Writer makeStringKey(const char* s) {
  bool written = false;
  return KeyValuePair::Writer(
    [s, written](uint8_t* buf, size_t len) mutable -> WriteResult {
      if (written) return {true, 0};
      size_t n = writeJSONString(buf, len, s);
      if (n) { written = true; return {true, n}; }
      return {false, 0};
    });
}

// Overloads that copy the string into the closure (safe for local / flash strings)
inline KeyValuePair::Writer makeStringKey(String s) {
  bool written = false;
  return KeyValuePair::Writer(
    [s, written](uint8_t* buf, size_t len) mutable -> WriteResult {
      if (written) return {true, 0};
      size_t n = writeJSONString(buf, len, s.c_str());
      if (n) { written = true; return {true, n}; }
      return {false, 0};
    });
}
inline KeyValuePair::Writer makeStringKey(const __FlashStringHelper* fs) {
  return makeStringKey(String(fs));
}

// makeIntKeyWriter: JSON-quoted decimal integer key  (e.g. "42")
inline KeyValuePair::Writer makeIntKeyWriter(int32_t v) {
  return makeStringKey(String(v));
}

// makeIntWriter: unquoted decimal integer value  (e.g. 42)
inline KeyValuePair::Writer makeIntWriter(int32_t v) {
  String s(v);
  bool written = false;
  return KeyValuePair::Writer(
    [s, written](uint8_t* buf, size_t len) mutable -> WriteResult {
      if (written) return {true, 0};
      size_t n = s.length();
      if (n > len) return {false, 0};
      memcpy(buf, s.c_str(), n);
      written = true;
      return {true, n};
    });
}


// ── JSONObjectWriter ──────────────────────────────────────────────────────────
// Stateful writer that produces {"key":value,...} from an iterator range.
// MakeItem: (Iterator) -> KeyValuePair
//   Called once per item; returned key/value writers are drained to completion.

template<typename Iterator, typename MakeItem>
struct JSONObjectWriter {
  typedef typename KeyValuePair::Writer ItemWriter;
  enum class Phase : uint8_t { NeedItem, Sep, Key, Colon, Value };

  Iterator current, end_val;
  MakeItem makeItem;
  ItemWriter keyWriter, valueWriter;
  Phase phase;
  bool first, done;

  JSONObjectWriter(Iterator begin, Iterator end, MakeItem mi)
    : current(begin), end_val(end), makeItem(mi),
      phase(Phase::NeedItem), first(true), done(false) {}

  WriteResult operator()(uint8_t* dest, size_t maxLen) {
    if (done) return {true, 0};
    size_t pos = 0;

    while (pos < maxLen) {
      if (current == end_val) {
        const size_t need = first ? 2 : 1;       // "{}" vs "}"
        if (pos + need > maxLen) break;
        if (first) dest[pos++] = '{';
        dest[pos++] = '}';
        done = true;
        break;
      }

      switch (phase) {
        case Phase::NeedItem: {
          auto kv    = makeItem(current);
          keyWriter   = kv.key;
          valueWriter = kv.value;
          phase = Phase::Sep;
          continue;
        }
        case Phase::Sep:
          dest[pos++] = first ? '{' : ',';
          first = false;
          phase = Phase::Key;
          continue;
        case Phase::Key: {
          auto [kdone, n] = keyWriter(dest + pos, maxLen - pos);
          pos += n;
          if (kdone) { phase = Phase::Colon; continue; }
          goto exit_loop;   // key didn't fit; return what we have
        }
        case Phase::Colon:
          dest[pos++] = ':';
          phase = Phase::Value;
          continue;
        case Phase::Value: {
          auto [vdone, n] = valueWriter(dest + pos, maxLen - pos);
          pos += n;
          if (vdone) {
            ++current;
            phase = Phase::NeedItem;
            continue;
          }
          // Not done: either no room or partial write — stop filling.
          goto exit_loop;
        }
      }
    }
    exit_loop:

    if (current == end_val && !done) {
      const size_t need = first ? 2 : 1;
      if (pos + need <= maxLen) {
        if (first) dest[pos++] = '{';
        dest[pos++] = '}';
        done = true;
      }
    }

    return {done, pos};
  }
};


// ── writeJSONObject ───────────────────────────────────────────────────────────
// Factory form: makeItem(Iterator) -> KeyValuePair

template<typename Iterator, typename MakeItem>
JSONObjectWriter<Iterator, MakeItem>
writeJSONObject(Iterator begin, Iterator end, MakeItem mi) {
  return JSONObjectWriter<Iterator, MakeItem>(begin, end, mi);
}

// Two-callback form: keyCb(Iterator, uint8_t*, size_t) -> size_t
//                    valCb(Iterator, uint8_t*, size_t) -> size_t
// Adapts into the factory form via std::function (one allocation per item).

template<typename Iterator, typename KeyCb, typename ValueCb>
JSONObjectWriter<Iterator, std::function<KeyValuePair(Iterator)>>
writeJSONObject(Iterator begin, Iterator end, KeyCb keyCb, ValueCb valCb) {
  typedef std::function<KeyValuePair(Iterator)> MakeItem;
  MakeItem mi = [keyCb, valCb](Iterator it) -> KeyValuePair {
    return KeyValuePair{
      [keyCb, it](uint8_t* buf, size_t len) -> WriteResult {
        size_t n = keyCb(it, buf, len);
        return {n > 0, n};
      },
      [valCb, it](uint8_t* buf, size_t len) -> WriteResult {
        size_t n = valCb(it, buf, len);
        return {n > 0, n};
      }
    };
  };
  return JSONObjectWriter<Iterator, MakeItem>(begin, end, mi);
}


// ── respondJSONObject ─────────────────────────────────────────────────────────
// Sends {"key":value,...} as a chunked JSON object HTTP response.
// Factory form (makeItem returns KeyValuePair):

template<typename Iterator, typename MakeItem>
void respondJSONObject(AsyncWebServerRequest* request, Iterator begin, Iterator end, MakeItem mi) {
  auto writer = writeJSONObject(begin, end, mi);
  request->sendChunked(FPSTR(CONTENT_TYPE_JSON),
    [writer](uint8_t* data, size_t len, size_t) mutable -> size_t {
      auto [done, n] = writer(data, len);
      return n;
    });
}

// Two-callback form:

template<typename Iterator, typename KeyCb, typename ValueCb>
void respondJSONObject(AsyncWebServerRequest* request, Iterator begin, Iterator end, KeyCb keyCb, ValueCb valCb) {
  auto writer = writeJSONObject(begin, end, keyCb, valCb);
  request->sendChunked(FPSTR(CONTENT_TYPE_JSON),
    [writer](uint8_t* data, size_t len, size_t) mutable -> size_t {
      auto [done, n] = writer(data, len);
      return n;
    });
}


// ── makeProgmemRawWriter ──────────────────────────────────────────────────────
// Streams the raw bytes of a PROGMEM string (already-serialized JSON) in chunks.

inline KeyValuePair::Writer makeProgmemRawWriter(const char* src) {  // src points into PROGMEM
  size_t total = strlen_P(src);
  size_t sent  = 0;
  return KeyValuePair::Writer(
    [src, total, sent](uint8_t* buf, size_t maxLen) mutable -> WriteResult {
      if (sent >= total) return {true, 0};
      size_t n = total - sent < maxLen ? total - sent : maxLen;
      memcpy_P(buf, src + sent, n);
      sent += n;
      return {sent >= total, n};
    });
}

// ── makeArduinoJsonWriter ─────────────────────────────────────────────────────
// Streams an already-populated ArduinoJSON variant via ChunkPrint re-serialization.
// The JsonVariant must remain valid (document must not be cleared/destroyed)
// for the lifetime of the returned writer.

inline KeyValuePair::Writer makeArduinoJsonWriter(JsonVariant v) {
  size_t total = measureJson(v);
  size_t sent  = 0;
  return KeyValuePair::Writer(
    [v, total, sent](uint8_t* buf, size_t maxLen) mutable -> WriteResult {
      if (sent >= total) return {true, 0};
      size_t n = total - sent < maxLen ? total - sent : maxLen;
      ChunkPrint cp(buf, sent, n);
      serializeJson(v, cp);
      sent += n;
      return {sent >= total, n};
    });
}
