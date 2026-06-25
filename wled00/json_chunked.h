#pragma once
/*
 * json_chunked.h — Buffer-free chunked JSON streaming for ESPAsyncWebServer.
 *
 * Overview
 * --------
 * Provides stateful writer objects and helper functions for streaming
 * arbitrarily large JSON arrays and objects over HTTP in chunks, without
 * allocating a full serialization buffer.  All state is held in the writer
 * objects themselves; sendChunked callbacks capture them by value.
 *
 * Core types (namespace json_chunked)
 * ------------------------------------
 *  WriteResult  — return value for every writer call.
 *    {false, 0}   no room; retry with the same (or larger) buffer
 *    {false, n}   wrote n bytes; sub-writer not finished — stop filling the
 *                 current buffer and flush what has been collected so far
 *    {true,  n}   wrote n final bytes; writer is done, caller may advance
 *    {true,  0}   writer already finished, or item is empty/skipped.
 *                 In list context (factory form) a {true,0} on the first call
 *                 silently omits the item — no separator is emitted.
 *
 *  Element  — unified type for all serialization slots: list items, object
 *            keys, and object values.  Implicitly constructs from:
 *              const char*                → JSON-quoted string
 *              String / __FlashStringHelper*  → JSON-quoted string (copied)
 *              any integral type (not bool) → unquoted decimal integer
 *              bool                       → true / false literal
 *              JsonVariant                → ChunkPrint re-serialization
 *              any (uint8_t*,size_t)->WriteResult callable
 *                                         → wrapped directly (JSONListWriter,
 *                                           JSONObjectWriter, lambdas, ...)
 *            For cases not covered (PROGMEM raw bytes, integer object keys),
 *            use the explicit factory functions below.
 *
 *  KeyValuePair  — {Element key; Element value}; one JSON object entry.
 *
 * Explicit factory functions
 * --------------------------
 *  makeProgmemRawWriter(s)  PROGMEM byte sequence streamed verbatim
 *                           (already-serialized JSON; distinct from const char*
 *                           which goes through JSON string escaping)
 *
 * Stateful composers
 * ------------------
 *  writeJSONList(begin, end, cb)
 *    cb is either:
 *      (Iterator, uint8_t*, size_t) -> size_t   direct: cb writes one item
 *      (Iterator) -> Element                      factory: one writer per item
 *    In the factory form, returning an Element whose first call returns {true,0}
 *    silently skips that entry — useful for filtering sparse ranges without
 *    disturbing separators.
 *
 *  writeJSONObject(begin, end, makeItem)
 *    makeItem: (Iterator) -> KeyValuePair
 *
 *  writeJSONObject({kvpair, kvpair, …})
 *    Initializer-list form; items are evaluated at the call site (eager Element
 *    construction, lazy byte-writing).  Returns Element so the result can be
 *    used as a nested value or passed to respondJSONChunked.
 *
 * Sending responses
 * -----------------
 *  respondJSONChunked(request, Element)
 *    Core responder: pipes any Element into request->sendChunked.  Use this
 *    directly when you already have an Element (e.g. from writeJSONObject or
 *    writeJSONList) or when composing writers by hand.
 *
 *  respondJSONList / respondJSONObject
 *    Convenience wrappers: construct the writer and call respondJSONChunked.
 *    respondJSONObject also has an initializer-list overload.
 *
 * Lifetime
 * --------
 * The sendChunked lambda captures the outer writer by value.  Any per-item
 * state (shared_ptr to heap data, captured references to stable globals, raw
 * PROGMEM pointers) is held inside Element closures; only one item writer is
 * live at a time.  No global JSON buffer lock is needed for these endpoints.
 * 
 * 
 * This code was developed by Claude Sonnet <noreply@anthropic.com>, guided by
 * Will Miles <will@willmiles.net>.
 */

#include <functional>
#include <initializer_list>
#include <type_traits>
#include <vector>
#include <ESPAsyncWebServer.h>
#include "src/dependencies/json/ArduinoJson-v6.h"
#include "src/dependencies/json/AsyncJson-v6.h"   // ChunkPrint


namespace json_chunked {

// ── quoteJsonString ───────────────────────────────────────────────────────────
// Writes a JSON-escaped quoted string into dest[0..maxLen-1].
// Returns bytes written, or 0 if the buffer was too small.

inline size_t quoteJsonString(uint8_t* dest, size_t maxLen, const char* src) {
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
// Return type from element writing functions, indicating how many bytes were written and whether the writer is done.
struct WriteResult {
  bool   done;
  size_t count;
};


// ── is_element_fn ─────────────────────────────────────────────────────────────
// C++11-compatible substitute for std::is_invocable_r (which arrived in C++17).
// Evaluates to true_type when F can be called as F(uint8_t*, size_t) and
// returns WriteResult; false_type otherwise.  The void default and the
// enable_if<is_same<decltype(call expression), WriteResult>> specialisation
// together form a standard SFINAE pattern: if the call expression is ill-formed
// (F not callable with those args) the specialisation is discarded and the
// primary (false_type) wins instead of producing a hard error.

template<typename F, typename = void>
struct is_element_fn : std::false_type {};

template<typename F>
struct is_element_fn<F, typename std::enable_if<
    std::is_same<
      decltype(std::declval<F>()(std::declval<uint8_t*>(), std::declval<size_t>())),
      WriteResult
    >::value
  >::type> : std::true_type {};


// ── Element ────────────────────────────────────────────────────────────────────
// Unified serialization slot type.  See file header for implicit conversions.
struct Element {
  std::function<WriteResult(uint8_t*, size_t)> fn;

  Element() = default;
  // Explicitly default the copy/move constructors so they don't get caught by the template constructor below.
  Element(const Element&) = default;
  Element(Element&&) = default;
  Element& operator=(const Element&) = default;
  Element& operator=(Element&&) = default;  

  // From any callable with the Element signature (JSONListWriter, lambdas, …)
  template<typename F,
    typename = typename std::enable_if<
      is_element_fn<typename std::decay<F>::type>::value
      && !std::is_same<typename std::decay<F>::type, Element>::value  // don't capture copies
    >::type>
  Element(F&& f) : fn(std::forward<F>(f)) {}

  // const char* → JSON-quoted string (captures pointer; safe for literals)
  Element(const char* s) {
    bool written = false;
    fn = [s, written](uint8_t* buf, size_t len) mutable -> WriteResult {
      size_t n = 0;
      if (!written) {
        n = quoteJsonString(buf, len, s);
        written = (n>0);
      }
      return {written, n};
    };
  }

  // String → JSON-quoted string (copies into closure)
  Element(String s) {
    bool written = false;
    fn = [s, written](uint8_t* buf, size_t len) mutable -> WriteResult {
      if (written) return {true, 0};
      size_t n = quoteJsonString(buf, len, s.c_str());
      if (n) { written = true; return {true, n}; }
      return {false, 0};
    };
  }

  // __FlashStringHelper* → copies to String, then JSON-quoted
  Element(const __FlashStringHelper* fs) : Element(String(fs)) {}

  // Any integer type (uint8_t, uint16_t, uint32_t, int, …) → unquoted decimal.
  // enable_if<is_integral> admits any integer width; the is_same<bool> exclusion
  // stops true/false from silently becoming 1/0 — bool has its own constructor.
  template<typename T,
    typename = typename std::enable_if<
      std::is_integral<T>::value && !std::is_same<T, bool>::value
    >::type>
  Element(T v) : Element(static_cast<int32_t>(v)) {}

  // bool → true / false JSON literal
  Element(bool v) {
    const char* s = v ? "true" : "false";
    size_t      n = v ? 4 : 5;
    bool written = false;
    fn = [s, n, written](uint8_t* buf, size_t len) mutable -> WriteResult {
      if (written) return {true, 0};
      if (n > len)  return {false, 0};
      memcpy(buf, s, n);
      written = true;
      return {true, n};
    };
  }

  // int32_t → unquoted decimal integer (use makeIntKeyWriter for quoted keys)
  Element(int32_t v) {
    String s(v);
    bool written = false;
    fn = [s, written](uint8_t* buf, size_t len) mutable -> WriteResult {
      if (written) return {true, 0};
      size_t n = s.length();
      if (n > len) return {false, 0};
      memcpy(buf, s.c_str(), n);
      written = true;
      return {true, n};
    };
  }

  // JsonVariant → ChunkPrint re-serialization (variant must outlive the writer)
  Element(JsonVariant v) {
    size_t total = measureJson(v);
    size_t sent  = 0;
    fn = [v, total, sent](uint8_t* buf, size_t maxLen) mutable -> WriteResult {
      if (sent >= total) return {true, 0};
      size_t n = total - sent < maxLen ? total - sent : maxLen;
      ChunkPrint cp(buf, sent, n);
      serializeJson(v, cp);
      sent += n;
      return {sent >= total, n};
    };
  }

  WriteResult operator()(uint8_t* buf, size_t len) const { return fn(buf, len); }
  explicit operator bool() const { return bool(fn); }
};


// ── Explicit factory functions ────────────────────────────────────────────────

// makeProgmemRawWriter: streams raw PROGMEM bytes verbatim (pre-serialized JSON)
inline Element makeProgmemRawWriter(const char* src) {  // src points into PROGMEM
  size_t total = strlen_P(src);
  size_t sent  = 0;
  return Element([src, total, sent](uint8_t* buf, size_t maxLen) mutable -> WriteResult {
    if (sent >= total) return {true, 0};
    size_t n = total - sent < maxLen ? total - sent : maxLen;
    memcpy_P(buf, src + sent, n);
    sent += n;
    return {sent >= total, n};
  });
}


// ── KeyValuePair ──────────────────────────────────────────────────────────────
// One JSON object entry.  Aggregate-initialise directly from native types:
//   { "key",   42 }       — string key, integer value
//   { "state", stateVar } — string key, JsonVariant value
//   { F("effects"), writeJSONList(...) } — flash key, sub-list writer

struct KeyValuePair {
  Element key;
  Element value;
};


// ── is_item_factory ───────────────────────────────────────────────────────────
// Detects whether Callback is a 1-arg factory (Iterator) -> Element,
// or a 3-arg direct writer (Iterator, uint8_t*, size_t) -> size_t.

template<typename Callback, typename Iterator, typename = void>
struct is_item_factory : std::false_type {};

template<typename Callback, typename Iterator>
struct is_item_factory<Callback, Iterator,
  decltype(void(std::declval<Callback>()(std::declval<Iterator>())))>
  : std::true_type {};


// ── JSONListWriter ────────────────────────────────────────────────────────────
// Stateful writer for the direct-callback path.
// Callback: (Iterator, uint8_t*, size_t) -> size_t
//   Return n > 0: wrote n bytes (item complete).
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
      if (pos + 2 > maxLen) break;
      size_t n = cb(current, dest + pos + 1, maxLen - pos - 1);
      if (n == 0) break;
      dest[pos] = (current == begin_val) ? '[' : ',';
      pos += 1 + n;
      ++current;
    }

    if (current == end_val) {
      const size_t need = (current == begin_val) ? 2 : 1;
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
// MakeItem: (Iterator) -> Element (or any type implicitly convertible to Element)
//
// Empty items ({true, 0} on first call) are silently skipped; no separator is
// emitted until an item actually produces bytes.

template<typename Iterator, typename MakeItem>
struct JSONListFactoryWriter {
  Iterator current, end_val;
  MakeItem makeItem;
  Element   item;
  bool     first, done, itemActive;

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

      // Reserve one byte for the separator until the item produces its first byte.
      const size_t sepReserve = itemActive ? 0 : 1;
      if (pos + sepReserve + 1 > maxLen) break;

      WriteResult ir = item(dest + pos + sepReserve, maxLen - pos - sepReserve);

      if (ir.count > 0 && !itemActive) {
        dest[pos] = first ? '[' : ',';
        first      = false;
        pos       += 1;
        itemActive = true;
      }
      pos += ir.count;

      if (ir.done) {
        item       = Element();
        ++current;
        itemActive = false;
      } else {
        break;
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



// ── JSONObjectWriter ──────────────────────────────────────────────────────────
// MakeItem: (Iterator) -> KeyValuePair

template<typename Iterator>
struct JSONObjectWriter {
  enum class Phase : uint8_t { NeedItem, Sep, Key, Colon, Value };
  typedef std::function<KeyValuePair(Iterator)> MakeItem;

  Iterator current, end_val;
  MakeItem makeItem;
  Element   keyWriter, valueWriter;
  Phase    phase;
  bool     first, done;

  JSONObjectWriter(Iterator begin, Iterator end, MakeItem mi)
    : current(begin), end_val(end), makeItem(std::move(mi)),
      phase(Phase::NeedItem), first(true), done(false) {}

  WriteResult operator()(uint8_t* dest, size_t maxLen) {
    if (done) return {true, 0};
    size_t pos = 0;

    while (pos < maxLen) {
      if (current == end_val) {
        const size_t need = first ? 2 : 1;
        if (pos + need > maxLen) break;
        if (first) dest[pos++] = '{';
        dest[pos++] = '}';
        done = true;
        break;
      }

      switch (phase) {
        case Phase::NeedItem: {
          auto kv    = makeItem(current);
          keyWriter   = std::move(kv.key);
          valueWriter = std::move(kv.value);
          phase = Phase::Sep;
          continue;
        }
        case Phase::Sep:
          dest[pos++] = first ? '{' : ',';
          first = false;
          phase = Phase::Key;
          continue;
        case Phase::Key: {
          WriteResult kr = keyWriter(dest + pos, maxLen - pos);
          pos += kr.count;
          if (kr.done) { phase = Phase::Colon; continue; }
          goto exit_loop;
        }
        case Phase::Colon:
          dest[pos++] = ':';
          phase = Phase::Value;
          continue;
        case Phase::Value: {
          WriteResult vr = valueWriter(dest + pos, maxLen - pos);
          pos += vr.count;
          if (vr.done) { ++current; phase = Phase::NeedItem; continue; }
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
JSONObjectWriter<Iterator>
writeJSONObject(Iterator begin, Iterator end, MakeItem mi) {
  return JSONObjectWriter<Iterator>(begin, end, std::move(mi));
}

// Two-callback form: keyCb(Iterator, uint8_t*, size_t) -> size_t
//                    valCb(Iterator, uint8_t*, size_t) -> size_t

template<typename Iterator, typename KeyCb, typename ValueCb>
JSONObjectWriter<Iterator>
writeJSONObject(Iterator begin, Iterator end, KeyCb keyCb, ValueCb valCb) {
  return JSONObjectWriter<Iterator>(begin, end,
    [keyCb, valCb](Iterator it) -> KeyValuePair {
      return {
        [keyCb, it](uint8_t* buf, size_t len) -> WriteResult {
          size_t n = keyCb(it, buf, len);
          return {n > 0, n};
        },
        [valCb, it](uint8_t* buf, size_t len) -> WriteResult {
          size_t n = valCb(it, buf, len);
          return {n > 0, n};
        }
      };
    });
}


// Initializer-list form: writeJSONObject({ {"key", val}, {"key2", val2} })
// Items (KeyValuePair values) are constructed at the call site and copied into a
// heap vector so the returned Element can outlive the current stack frame (needed
// because sendChunked fires its callback asynchronously after this function returns).
inline Element writeJSONObject(std::initializer_list<KeyValuePair> items) {
  typedef std::vector<KeyValuePair> KVVec;
#if __cplusplus >= 201402L  
  KVVec vec(items.begin(), items.end());
  return writeJSONObject(size_t{0}, vec.size(),
      [vec = std::move(vec)](size_t i) -> KeyValuePair { return std::move(vec[i]); });
#else
  std::shared_ptr<KVVec> vec = std::make_shared<KVVec>(items.begin(), items.end());
  return writeJSONObject(size_t{0}, vec->size(),
      [vec](size_t i) -> KeyValuePair { return std::move((*vec)[i]); });
#endif      
}

// ── respondJSONChunked ────────────────────────────────────────────────────────
// Core responder: pipes any Element into request->sendChunked.

void respondJSONChunked(AsyncWebServerRequest* request, Element writer) {    
  request->sendChunked(FPSTR(CONTENT_TYPE_JSON),
#if __cplusplus >= 201402L
    [writer = std::move(writer)]
#else
    [writer]
#endif    
    (uint8_t* data, size_t len, size_t) mutable -> size_t {
      WriteResult r = writer(data, len);
      return r.count;
    });
}

// ── respondJSONList ───────────────────────────────────────────────────────────

template<typename Iterator, typename Callback>
void respondJSONList(AsyncWebServerRequest* request, Iterator begin, Iterator end, Callback cb) {
  respondJSONChunked(request, writeJSONList(begin, end, cb));
}

// ── respondJSONObject ─────────────────────────────────────────────────────────

template<typename Iterator, typename MakeItem>
void respondJSONObject(AsyncWebServerRequest* request, Iterator begin, Iterator end, MakeItem mi) {
  respondJSONChunked(request, writeJSONObject(begin, end, mi));
}

template<typename Iterator, typename KeyCb, typename ValueCb>
void respondJSONObject(AsyncWebServerRequest* request, Iterator begin, Iterator end, KeyCb keyCb, ValueCb valCb) {
  respondJSONChunked(request, writeJSONObject(begin, end, keyCb, valCb));
}

inline void respondJSONObject(AsyncWebServerRequest* request, std::initializer_list<KeyValuePair> items) {
  respondJSONChunked(request, writeJSONObject(items));
}


} // namespace json_chunked
