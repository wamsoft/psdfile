#ifndef __PSDCLASS_CONV_H__
#define __PSDCLASS_CONV_H__

// psdparse の文字列型 (u16str / UTF-8 std::string) と 吉里吉里 の ttstr を
// 相互変換する小物。psdclass.cpp と psdclass_meta.cpp の両方から使う。
//
// tjs_char == char16_t (どちらも 16bit UTF-16 code unit) 前提。psdfile.dll は
// Windows ターゲットのみなので問題ない。

#include <tp_stub.h>
#include "psdbase.h"
#include <string>

// psdparse の Unicode 文字列 (std::u16string = UTF-16 host-order) を ttstr に
// 変換。bit-level reinterpret で問題ない。length 指定で embedded NUL も保存。
static inline ttstr u16ToTjs(const psd::u16str &s) {
	static_assert(sizeof(tjs_char) == sizeof(char16_t),
	              "tjs_char must be 16-bit UTF-16 code unit");
	return ttstr(reinterpret_cast<const tjs_char *>(s.data()),
	             (tjs_int)s.length());
}

// ttstr (UTF-16 host-order) を psd::u16str に変換。
static inline psd::u16str tjsToU16(const ttstr &s) {
	return psd::u16str(reinterpret_cast<const char16_t *>(s.c_str()),
	                   (size_t)s.length());
}

// ttstr (UTF-16) を UTF-8 (std::string) に変換。psdparse の setLayerName /
// addLayer は UTF-8 名を受け取り内部で luni に再変換するため。
static inline std::string tjsToUtf8(const ttstr &s) {
	std::string out;
	const tjs_char *p = s.c_str();
	tjs_int n = s.length();
	for (tjs_int i = 0; i < n; i++) {
		uint32_t cp = (uint16_t)p[i];
		if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < n) {
			uint32_t lo = (uint16_t)p[i + 1];
			if (lo >= 0xdc00 && lo <= 0xdfff) {
				cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
				i++;
			}
		}
		if (cp < 0x80) {
			out.push_back((char)cp);
		} else if (cp < 0x800) {
			out.push_back((char)(0xc0 | (cp >> 6)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		} else if (cp < 0x10000) {
			out.push_back((char)(0xe0 | (cp >> 12)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		} else {
			out.push_back((char)(0xf0 | (cp >> 18)));
			out.push_back((char)(0x80 | ((cp >> 12) & 0x3f)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		}
	}
	return out;
}

// 4CC (int) → 4 文字の ttstr。追加レイヤ情報キー / blendModeKey の表示用。
// 末尾スペースを含むキー ('mul ' 等) もそのまま 4 文字で返す。
static inline ttstr fourccToTjs(int key) {
	tjs_char s[5] = {
		(tjs_char)(tjs_uint8)((key >> 24) & 0xff),
		(tjs_char)(tjs_uint8)((key >> 16) & 0xff),
		(tjs_char)(tjs_uint8)((key >>  8) & 0xff),
		(tjs_char)(tjs_uint8)( key        & 0xff),
		0
	};
	return ttstr(s, 4);
}

// 4 文字の ttstr → 4CC (int)。4 文字でないときは例外。
static inline int tjsToFourcc(const ttstr &s) {
	if (s.length() != 4)
		TVPThrowExceptionMessage(TJS_W("key must be a 4-character string"));
	const tjs_char *p = s.c_str();
	return ((int)(tjs_uint8)p[0] << 24) | ((int)(tjs_uint8)p[1] << 16)
	     | ((int)(tjs_uint8)p[2] <<  8) |  (int)(tjs_uint8)p[3];
}

#endif
