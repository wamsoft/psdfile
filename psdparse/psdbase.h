#ifndef __psdbase_h__
#define __psdbase_h__

#include <string>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

#ifdef _DEBUG
#define dprint(...) printf(__VA_ARGS__);
#else
#define dprint(...) ((void)0)
#endif

#ifndef _MSC_VER
  #include <stdint.h>
#endif

// little-endian detection (replacement for BOOST_LITTLE_ENDIAN)
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
  #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    #define PSD_LITTLE_ENDIAN 1
  #endif
#elif defined(_WIN32) || defined(_M_IX86) || defined(_M_X64) || \
      defined(_M_ARM) || defined(_M_ARM64) || \
      defined(__i386__) || defined(__x86_64__) || \
      defined(__arm__) || defined(__aarch64__)
  #define PSD_LITTLE_ENDIAN 1
#endif

namespace psd {
#ifdef _MSC_VER
#ifndef _STDINT
  typedef __int8 int8_t;
  typedef unsigned __int8 uint8_t;
  typedef __int16 int16_t;
  typedef unsigned __int16 uint16_t;
  typedef __int32 int32_t;
  typedef unsigned __int32 uint32_t;
  typedef __int64 int64_t;
  typedef unsigned __int64 uint64_t;
#endif
#endif

  typedef float  float32_t;
  typedef double float64_t;

  // PSD on-disk Unicode strings are UTF-16BE. Internally we store them as
  // std::u16string (host-byte-order UTF-16 code units).
  typedef std::u16string u16str;

  // ビット情報を維持した型変換用のunion
  union pun32 {
    uint32_t  i;
    float32_t f;
  };
  union pun64 {
    uint64_t  i;
    float64_t f;
  };

  // イメージ取得時にカラー配置を指定するためのフォーマット構造体
  struct ColorFormat {
    ColorFormat(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    : rShift(r), gShift(g), bShift(b), aShift(a) {}

    uint8_t rShift;
    uint8_t gShift;
    uint8_t bShift;
    uint8_t aShift;
  };

  // デフォルトのWindows向けBGRA(LittleEndian)フォーマット
  static const ColorFormat BGRA_LE(16, 8, 0, 24);

  // イテレータ参照用基底クラス
	class IteratorBase {
	public:
		IteratorBase() {};
		virtual ~IteratorBase() {};
		virtual IteratorBase *clone() = 0;
    // cloneOffset(offset): sub-reader starting at current pos + offset, with
    //   the SAME end as the parent (inherits the parent's upper bound).
    virtual IteratorBase *cloneOffset(int offset) = 0;
    // cloneRange(offset, length): sub-reader bounded to exactly `length`
    //   bytes starting at current pos + offset. Use this when carving out a
    //   size-prefixed sub-block whose contents must not bleed past `length`.
    virtual IteratorBase *cloneRange(int offset, int length) = 0;
		virtual void init() = 0;
		virtual int getCh() = 0;
		virtual int16_t getInt16(bool convToNative=true) = 0;
		virtual int32_t getInt32(bool convToNative=true) = 0;
		virtual int64_t getInt64(bool convToNative=true) = 0;
		virtual int getData(void *buffer, int size) = 0;
		virtual bool eoi() = 0;
    virtual void getUnicodeString(u16str &str, bool convToNative=true) = 0;
    virtual int size() = 0;
    virtual int rest() = 0;
    virtual void advance(int size) = 0;
	};

  // swap utilities
  inline uint16_t byteSwap16(uint16_t x) {
    return (((x & 0xff00) >> 8) | ((x & 0x00ff) << 8));
  }

  inline uint32_t byteSwap32(uint32_t x) {
    return ((((x) & 0xff000000) >> 24) |
            (((x) & 0x00ff0000) >>  8) |
            (((x) & 0x0000ff00) <<  8) |
            (((x) & 0x000000ff) << 24));
  }

#ifdef _WIN32
  // UTF-8 → UTF-16 厳密変換。public I/F (load/save/FileWriter 等) は UTF-8
  // のみと約束しているので、不正バイトは MB_ERR_INVALID_CHARS で弾く。
  // 戻り値が空文字列なら変換失敗。
  inline std::wstring utf8ToWide(const char *u8) {
    if (!u8) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8, -1, nullptr, 0);
    if (wlen <= 0) return {};
    std::wstring w((size_t)wlen, L'\0');
    int got = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, u8, -1, &w[0], wlen);
    if (got <= 0) return {};
    if (!w.empty() && w.back() == L'\0') w.pop_back();
    return w;
  }
#endif

  inline uint64_t byteSwap64(uint64_t x) {
    return ((x >> 56) |
            ((x >> 40) & 0xff00) |
            ((x >> 24) & 0xff0000) |
            ((x >>  8) & 0xff000000) |
            ((x <<  8) & (0xffull << 32)) |
            ((x << 24) & (0xffull << 40)) |
            ((x << 40) & (0xffull << 48)) |
            (x << 56));
  }
}

#endif // __psdbase_h__
