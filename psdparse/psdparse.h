#ifndef __psdparse_h__
#define __psdparse_h__

#include "stdafx.h"
#include "psddata.h"

#include <cstring>
#include <algorithm>

namespace psd {

// MemoryReader: IteratorBase over a contiguous byte buffer.
//
// Carries a [start_, end_) sub-range within an underlying shared buffer so
// init()/size()/rest()/eoi() observe the sub-range -- matching how the
// original Boost.Spirit IteratorData<Iterator> behaved over qi::raw[advance(N)]
// captures.  cloneOffset(N) carves out a sub-reader starting at pos_+N and
// extending to the parent's end_.
//
// PSD is big-endian on disk; convToNative=true swaps to host endianness.
class MemoryReader : public IteratorBase {
public:
  MemoryReader(const uint8_t *base, int length)
    : base_(base), start_(0), end_(length), pos_(0) {}

  IteratorBase *clone() override {
    MemoryReader *r = new MemoryReader(base_, end_);
    r->start_ = start_;
    r->pos_   = pos_;
    return r;
  }
  IteratorBase *cloneOffset(int offset) override {
    int newStart = pos_ + offset;
    if (newStart < start_) newStart = start_;
    if (newStart > end_)   newStart = end_;
    MemoryReader *r = new MemoryReader(base_, end_);
    r->start_ = newStart;
    r->pos_   = newStart;
    return r;
  }
  IteratorBase *cloneRange(int offset, int length) override {
    int newStart = pos_ + offset;
    if (newStart < 0)    newStart = 0;
    if (newStart > end_) newStart = end_;
    if (length < 0) length = 0;
    int newEnd = newStart + length;
    if (newEnd > end_) newEnd = end_;
    if (newEnd < newStart) newEnd = newStart;
    MemoryReader *r = new MemoryReader(base_, newEnd);
    r->start_ = newStart;
    r->pos_   = newStart;
    return r;
  }
  void init() override { pos_ = start_; }
  bool eoi() override { return pos_ >= end_; }
  int size() override { return end_ - start_; }
  int rest() override { return end_ - pos_; }
  void advance(int n) override {
    pos_ += n;
    if (pos_ > end_)   pos_ = end_;
    if (pos_ < start_) pos_ = start_;
  }
  int getCh() override {
    if (pos_ >= end_) return -1;
    return base_[pos_++];
  }
  int getData(void *buffer, int n) override {
    int avail = end_ - pos_;
    if (n > avail) n = avail;
    if (n > 0) std::memcpy(buffer, base_ + pos_, (size_t)n);
    pos_ += n;
    return n;
  }
  int16_t getInt16(bool convToNative=true) override {
    if (end_ - pos_ < 2) return -1;
    uint8_t b0 = base_[pos_++];
    uint8_t b1 = base_[pos_++];
    return assemble16_(b0, b1, convToNative);
  }
  int32_t getInt32(bool convToNative=true) override {
    if (end_ - pos_ < 4) return -1;
    uint8_t b[4];
    b[0] = base_[pos_++]; b[1] = base_[pos_++];
    b[2] = base_[pos_++]; b[3] = base_[pos_++];
    return assemble32_(b, convToNative);
  }
  int64_t getInt64(bool convToNative=true) override {
    if (end_ - pos_ < 8) return -1;
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = base_[pos_++];
    return assemble64_(b, convToNative);
  }
  void getUnicodeString(u16str &str, bool convToNative=true) override {
    int len = getInt32(true);
    str.clear();
    if (len < 0) return;
    str.reserve((size_t)len);
    for (int i = 0; i < len; i++) {
      str.push_back((char16_t)getInt16(convToNative));
    }
  }

private:
  static int16_t assemble16_(uint8_t hi, uint8_t lo, bool convToNative) {
    // PSD is BE: hi is the high byte on disk.
#ifdef PSD_LITTLE_ENDIAN
    bool swap = convToNative;
#else
    bool swap = !convToNative;
#endif
    union { uint8_t u8[2]; int16_t i16; } u;
    if (swap) { u.u8[0] = lo; u.u8[1] = hi; }
    else      { u.u8[0] = hi; u.u8[1] = lo; }
    return u.i16;
  }
  static int32_t assemble32_(const uint8_t *b, bool convToNative) {
#ifdef PSD_LITTLE_ENDIAN
    bool swap = convToNative;
#else
    bool swap = !convToNative;
#endif
    union { uint8_t u8[4]; int32_t i32; } u;
    if (swap) { u.u8[0]=b[3]; u.u8[1]=b[2]; u.u8[2]=b[1]; u.u8[3]=b[0]; }
    else      { u.u8[0]=b[0]; u.u8[1]=b[1]; u.u8[2]=b[2]; u.u8[3]=b[3]; }
    return u.i32;
  }
  static int64_t assemble64_(const uint8_t *b, bool convToNative) {
#ifdef PSD_LITTLE_ENDIAN
    bool swap = convToNative;
#else
    bool swap = !convToNative;
#endif
    union { uint8_t u8[8]; int64_t i64; } u;
    if (swap) { for (int i = 0; i < 8; i++) u.u8[i] = b[7-i]; }
    else      { for (int i = 0; i < 8; i++) u.u8[i] = b[i]; }
    return u.i64;
  }

  const uint8_t *base_;
  int start_;
  int end_;
  int pos_;
};

// StreamReader: IteratorBase for arbitrary seekable byte streams.
//
// Holds a shared "byte source" (std::istream or any subclass of
// StreamReader::Source) and a small per-reader cache.  Sub-readers created
// via cloneOffset / cloneRange share the same source but track their own
// [start, end) and pos with private caches.  Use this for stream-only
// providers (kirikiri iTJSBinaryStream, network-backed seekable streams,
// std::ifstream when mmap is unavailable, ...).
//
// Lifetime contract: every sub-reader holds a shared_ptr to the source, so
// the source stays alive as long as any reader points into it.
class StreamReader : public IteratorBase {
public:
  // Abstract byte source. Subclass to wrap iTJSBinaryStream etc.
  // size() returns total source size; read() copies up to len bytes at offset.
  class Source {
  public:
    virtual ~Source() = default;
    virtual size_t size() const = 0;
    virtual size_t read(uint8_t *out, size_t offset, size_t len) = 0;
  };

  explicit StreamReader(std::shared_ptr<Source> src)
    : src_(std::move(src)),
      start_(0),
      end_((int)(src_ ? src_->size() : 0)),
      pos_(0),
      cachePos_(-1),
      cacheLen_(0) {}

  IteratorBase *clone() override {
    auto r = new StreamReader(src_);
    r->start_ = start_;
    r->end_   = end_;
    r->pos_   = pos_;
    return r;
  }
  IteratorBase *cloneOffset(int offset) override {
    int newStart = pos_ + offset;
    if (newStart < 0)    newStart = 0;
    if (newStart > end_) newStart = end_;
    auto r = new StreamReader(src_);
    r->start_ = newStart;
    r->end_   = end_;
    r->pos_   = newStart;
    return r;
  }
  IteratorBase *cloneRange(int offset, int length) override {
    int newStart = pos_ + offset;
    if (newStart < 0) newStart = 0;
    int srcSize = (int)src_->size();
    if (newStart > srcSize) newStart = srcSize;
    if (length < 0) length = 0;
    int newEnd = newStart + length;
    if (newEnd > srcSize) newEnd = srcSize;
    if (newEnd < newStart) newEnd = newStart;
    auto r = new StreamReader(src_);
    r->start_ = newStart;
    r->end_   = newEnd;
    r->pos_   = newStart;
    return r;
  }
  void init() override { pos_ = start_; }
  bool eoi() override  { return pos_ >= end_; }
  int size() override  { return end_ - start_; }
  int rest() override  { return end_ - pos_; }
  void advance(int n) override {
    pos_ += n;
    if (pos_ > end_)   pos_ = end_;
    if (pos_ < start_) pos_ = start_;
  }
  int getCh() override {
    if (pos_ >= end_) return -1;
    uint8_t b;
    if (readAt_(pos_, &b, 1) != 1) return -1;
    pos_++;
    return b;
  }
  int getData(void *buffer, int n) override {
    int avail = end_ - pos_;
    if (n > avail) n = avail;
    if (n <= 0) return 0;
    size_t got = readAt_(pos_, (uint8_t *)buffer, (size_t)n);
    pos_ += (int)got;
    return (int)got;
  }
  int16_t getInt16(bool convToNative=true) override {
    uint8_t b[2];
    if (getData(b, 2) != 2) return -1;
    return assembleInt16_(b, convToNative);
  }
  int32_t getInt32(bool convToNative=true) override {
    uint8_t b[4];
    if (getData(b, 4) != 4) return -1;
    return assembleInt32_(b, convToNative);
  }
  int64_t getInt64(bool convToNative=true) override {
    uint8_t b[8];
    if (getData(b, 8) != 8) return -1;
    return assembleInt64_(b, convToNative);
  }
  void getUnicodeString(u16str &str, bool convToNative=true) override {
    int len = getInt32(true);
    str.clear();
    if (len < 0) return;
    str.reserve((size_t)len);
    for (int i = 0; i < len; i++) {
      str.push_back((char16_t)getInt16(convToNative));
    }
  }

private:
  // 4 KB cache fed by source->read(); satisfies most short sequential reads
  // without re-entering the source.
  size_t readAt_(int pos, uint8_t *out, size_t len) {
    if (!src_) return 0;
    size_t copied = 0;
    while (copied < len) {
      if (cachePos_ < 0 || pos < cachePos_ ||
          pos >= cachePos_ + cacheLen_) {
        cachePos_ = pos;
        cacheLen_ = (int)src_->read(cache_, (size_t)pos, sizeof(cache_));
        if (cacheLen_ <= 0) break;
      }
      int offsetInCache = pos - cachePos_;
      int avail         = cacheLen_ - offsetInCache;
      int want          = (int)(len - copied);
      int n             = (avail < want) ? avail : want;
      std::memcpy(out + copied, cache_ + offsetInCache, (size_t)n);
      copied += (size_t)n;
      pos    += n;
    }
    return copied;
  }
  static int16_t assembleInt16_(const uint8_t *b, bool convToNative) {
#ifdef PSD_LITTLE_ENDIAN
    bool swap = convToNative;
#else
    bool swap = !convToNative;
#endif
    union { uint8_t u8[2]; int16_t i16; } u;
    if (swap) { u.u8[0]=b[1]; u.u8[1]=b[0]; }
    else      { u.u8[0]=b[0]; u.u8[1]=b[1]; }
    return u.i16;
  }
  static int32_t assembleInt32_(const uint8_t *b, bool convToNative) {
#ifdef PSD_LITTLE_ENDIAN
    bool swap = convToNative;
#else
    bool swap = !convToNative;
#endif
    union { uint8_t u8[4]; int32_t i32; } u;
    if (swap) { u.u8[0]=b[3]; u.u8[1]=b[2]; u.u8[2]=b[1]; u.u8[3]=b[0]; }
    else      { u.u8[0]=b[0]; u.u8[1]=b[1]; u.u8[2]=b[2]; u.u8[3]=b[3]; }
    return u.i32;
  }
  static int64_t assembleInt64_(const uint8_t *b, bool convToNative) {
#ifdef PSD_LITTLE_ENDIAN
    bool swap = convToNative;
#else
    bool swap = !convToNative;
#endif
    union { uint8_t u8[8]; int64_t i64; } u;
    if (swap) { for (int i = 0; i < 8; i++) u.u8[i] = b[7-i]; }
    else      { for (int i = 0; i < 8; i++) u.u8[i] = b[i]; }
    return u.i64;
  }

  std::shared_ptr<Source> src_;
  int start_;
  int end_;
  int pos_;
  uint8_t cache_[4096];
  int cachePos_;  // absolute pos covered by cache[0..cacheLen_); -1 = invalid
  int cacheLen_;
};

// Parser entry point. Returns true if the structural parse succeeded
// (header signature matched and the four top-level blocks were read).
bool parsePSD(IteratorBase &reader, Data &data);

}  // namespace psd

#endif
