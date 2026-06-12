#ifndef __psdwrite_h__
#define __psdwrite_h__

#include "psdbase.h"
#include "psddata.h"

#include <cstdio>
#include <cstdint>

namespace psd {

// WriterBase: 出力側の I/O 抽象 (IteratorBase の対)。
//
// putData() / tell() / seek() の 3 つだけが純粋仮想。BE 整数や Pascal 文字列
// 等の高水準ヘルパは全てこれらの上に default 実装。
//
// seek/tell は length-prefixed セクション (image resources / layer & mask /
// extra data / ...) のサイズパッチ用に必須 (placeholder 書く → 中身書く →
// seek 戻ってサイズ書く → 末尾に seek 戻る)。
class WriterBase {
public:
  virtual ~WriterBase() = default;

  // 必須: バイト書き込み + 位置取得/設定 + 成否確認
  virtual size_t putData(const void *data, size_t n) = 0;
  virtual int64_t tell() const = 0;
  virtual bool seek(int64_t pos) = 0;
  virtual bool ok() const = 0;

  // 高水準ヘルパ
  void putCh(int b) {
    uint8_t v = (uint8_t)b;
    putData(&v, 1);
  }
  void putBytes(const uint8_t *p, size_t n) { putData(p, n); }
  void putZero(size_t n) {
    static const uint8_t z[64] = {0};
    while (n > sizeof(z)) { putData(z, sizeof(z)); n -= sizeof(z); }
    if (n > 0) putData(z, n);
  }
  void putUint16BE(uint16_t v) {
    uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v };
    putData(b, 2);
  }
  void putUint32BE(uint32_t v) {
    uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                     (uint8_t)(v >>  8), (uint8_t) v };
    putData(b, 4);
  }
  void putInt16BE(int16_t v)   { putUint16BE((uint16_t)v); }
  void putInt32BE(int32_t v)   { putUint32BE((uint32_t)v); }

  // Iterator から末尾までを丸ごとコピー。戻り値はコピーしたバイト数。
  size_t copyAllFrom(IteratorBase *it) {
    if (!it) return 0;
    it->init();
    size_t total = 0;
    uint8_t buf[8192];
    while (!it->eoi()) {
      int got = it->getData(buf, (int)sizeof(buf));
      if (got <= 0) break;
      putData(buf, (size_t)got);
      total += (size_t)got;
    }
    return total;
  }
};

// FILE * ベースの WriterBase 実装。fopen/fwrite/_fseeki64/_ftelli64 使用。
// path は UTF-8 (Win32 では内部で UTF-16 → _wfopen)。
class FileWriter : public WriterBase {
public:
  explicit FileWriter(const char *path);
  ~FileWriter() override;
  FileWriter(const FileWriter &) = delete;
  FileWriter &operator=(const FileWriter &) = delete;

  bool ok() const override { return fp_ != nullptr; }
  size_t putData(const void *data, size_t n) override;
  int64_t tell() const override;
  bool seek(int64_t pos) override;
  void close();

private:
  FILE *fp_;
};

// Data 全体を PSD ファイルフォーマットで w に書き出す。ラウンドトリップ
// (load → save → re-load で構造一致) を目標とする。w.ok() && writePSD()==true
// で成功。
bool writePSD(WriterBase &w, const Data &data);

} // namespace psd

#endif // __psdwrite_h__
