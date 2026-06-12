#include "stdafx.h"

#include "psdwrite.h"

#include <cstring>

namespace psd {

// ===========================================================================
// FileWriter
// ===========================================================================

FileWriter::FileWriter(const char *path) : fp_(nullptr) {
#ifdef _WIN32
  // fopen は ANSI 限定なので UTF-8 → UTF-16 → _wfopen で開く。
  std::wstring w = utf8ToWide(path);
  if (w.empty()) return;
  fp_ = _wfopen(w.c_str(), L"wb");
#else
  fp_ = std::fopen(path, "wb");
#endif
}

FileWriter::~FileWriter() { close(); }

void FileWriter::close() {
  if (fp_) { std::fclose(fp_); fp_ = nullptr; }
}

size_t FileWriter::putData(const void *data, size_t n) {
  if (!fp_ || n == 0) return 0;
  return std::fwrite(data, 1, n, fp_);
}

int64_t FileWriter::tell() const {
  if (!fp_) return -1;
#ifdef _WIN32
  return _ftelli64(fp_);
#else
  return (int64_t)ftello(fp_);
#endif
}

bool FileWriter::seek(int64_t pos) {
  if (!fp_) return false;
#ifdef _WIN32
  return _fseeki64(fp_, pos, SEEK_SET) == 0;
#else
  return fseeko(fp_, (off_t)pos, SEEK_SET) == 0;
#endif
}

// ===========================================================================
// writePSD
// ===========================================================================
//
// PSD ファイルは 5 セクションで構成される。各セクションには長さフィールドが
// 先頭に付いていることが多いので、書き込み時は「placeholder 0 を書く →
// 中身を書く → 戻ってサイズを埋める」の patch-back パターンを多用する。
//
//   1. File Header (26 bytes 固定)
//   2. Color Mode Data (4 byte length + content)
//   3. Image Resources (4 byte length + 各リソースは 8BIM + id + Pascal name +
//      4 byte data size + data + 偶数 padding の繰り返し)
//   4. Layer and Mask Information (4 byte length + Layer Info subsection +
//      Global Layer Mask Info subsection + 任意の追加 info)
//   5. Image Data (compression word + interleaved planes)
//
// Round-trip 戦略:
//   * 構造的に再現可能なフィールド (header, layer record, blend key, …) は
//     parsed フィールドから再シリアライズ
//   * 詳細パースが煩雑な block (layer mask / blending range / additional
//     layer info の生バイト, global layer mask info の生バイト, channel image
//     data の RLE/raw bytes, merged image bytes) は parse 時に保持した
//     IteratorBase からそのまま転送
//
// "patch-back" ヘルパ: ラムダ的に使いたいので明示マクロは使わず、ローカル
// で position を保存して後で書き戻す方式。

namespace {

inline void writeHeader(WriterBase &w, const Header &h) {
  w.putData("8BPS", 4);
  w.putUint16BE((uint16_t)h.version);
  w.putZero(6);  // reserved
  w.putUint16BE((uint16_t)h.channels);
  w.putUint32BE((uint32_t)h.height);
  w.putUint32BE((uint32_t)h.width);
  w.putUint16BE((uint16_t)h.depth);
  w.putUint16BE((uint16_t)h.mode);
}

inline void writeColorModeData(WriterBase &w, const Data &data) {
  if (data.colorModeIterator && data.colorModeSize > 0) {
    w.putUint32BE((uint32_t)data.colorModeSize);
    w.copyAllFrom(data.colorModeIterator);
  } else {
    w.putUint32BE(0);
  }
}

inline void writeImageResources(WriterBase &w, const Data &data) {
  int64_t sizePos = w.tell();
  w.putUint32BE(0); // placeholder
  int64_t bodyStart = w.tell();
  for (const auto &res : data.imageResourceList) {
    w.putData("8BIM", 4);
    w.putUint16BE(res.id);
    int nameLen = (int)res.name.size();
    if (nameLen > 255) nameLen = 255;
    w.putCh(nameLen);
    if (nameLen > 0) w.putData(res.name.data(), (size_t)nameLen);
    // pascal: length byte + chars, total padded to even
    int total = 1 + nameLen;
    if (total & 1) w.putZero(1);
    w.putUint32BE((uint32_t)res.size);
    if (res.data && res.size > 0) w.copyAllFrom(res.data);
    // data section padded to even
    if (res.size & 1) w.putZero(1);
  }
  int64_t bodyEnd = w.tell();
  // patch back
  w.seek(sizePos);
  w.putUint32BE((uint32_t)(bodyEnd - bodyStart));
  w.seek(bodyEnd);
}

inline void writeLayerRecord(WriterBase &w, const LayerInfo &lay) {
  w.putInt32BE(lay.top);
  w.putInt32BE(lay.left);
  w.putInt32BE(lay.bottom);
  w.putInt32BE(lay.right);
  w.putUint16BE((uint16_t)lay.channels.size());
  for (const auto &ch : lay.channels) {
    w.putInt16BE((int16_t)ch.id);
    w.putUint32BE((uint32_t)ch.length);
  }
  w.putData("8BIM", 4);
  // blendModeKey は parse 時に getInt32(true) で読んだ値 (host int)。書く時も
  // 同じ BE で出すと元のディスク表現に戻る。
  w.putUint32BE((uint32_t)lay.blendModeKey);
  w.putCh(lay.opacity);
  w.putCh(lay.clipping);
  w.putCh(lay.flag);
  w.putZero(1); // filler

  // extra data: size + content
  int64_t extraSizePos = w.tell();
  w.putUint32BE(0); // placeholder
  int64_t extraStart = w.tell();
  if (lay.extraData.rawBytes) {
    w.copyAllFrom(lay.extraData.rawBytes);
  }
  int64_t extraEnd = w.tell();
  w.seek(extraSizePos);
  w.putUint32BE((uint32_t)(extraEnd - extraStart));
  w.seek(extraEnd);
}

inline void writeLayerInfo(WriterBase &w, const Data &data) {
  int64_t sizePos = w.tell();
  w.putUint32BE(0); // placeholder
  int64_t bodyStart = w.tell();
  int16_t count = (int16_t)data.layerList.size();
  if (data.mergedAlpha) count = (int16_t)(-count);
  w.putInt16BE(count);
  for (const auto &lay : data.layerList) writeLayerRecord(w, lay);
  // channel image data (全 layer の全 channel の compression word + bytes
  // が連結された 1 ブロック)
  if (data.channelImageData) w.copyAllFrom(data.channelImageData);
  int64_t bodyEnd = w.tell();
  // PSD 仕様: layer info の長さは 2 の倍数 padding が必要。
  if ((bodyEnd - bodyStart) & 1) { w.putZero(1); bodyEnd++; }
  w.seek(sizePos);
  w.putUint32BE((uint32_t)(bodyEnd - bodyStart));
  w.seek(bodyEnd);
}

inline void writeGlobalLayerMaskInfo(WriterBase &w, const Data &data) {
  if (data.globalLayerMaskInfoRaw) {
    int64_t sizePos = w.tell();
    w.putUint32BE(0);
    int64_t start = w.tell();
    w.copyAllFrom(data.globalLayerMaskInfoRaw);
    int64_t end = w.tell();
    w.seek(sizePos);
    w.putUint32BE((uint32_t)(end - start));
    w.seek(end);
  } else {
    w.putUint32BE(0); // 空ブロック
  }
}

inline void writeLayerAndMask(WriterBase &w, const Data &data) {
  int64_t sizePos = w.tell();
  w.putUint32BE(0);
  int64_t bodyStart = w.tell();
  writeLayerInfo(w, data);
  writeGlobalLayerMaskInfo(w, data);
  // global layer mask info より後ろにあった secondary layer info (Lr16/Lr32 等)
  if (data.layerAndMaskTrailing) w.copyAllFrom(data.layerAndMaskTrailing);
  int64_t bodyEnd = w.tell();
  w.seek(sizePos);
  w.putUint32BE((uint32_t)(bodyEnd - bodyStart));
  w.seek(bodyEnd);
}

inline void writeImageData(WriterBase &w, const Data &data) {
  if (data.imageData) w.copyAllFrom(data.imageData);
}

} // anonymous namespace

bool writePSD(WriterBase &w, const Data &data) {
  if (!w.ok()) return false;
  writeHeader(w, data.header);
  writeColorModeData(w, data);
  writeImageResources(w, data);
  writeLayerAndMask(w, data);
  writeImageData(w, data);
  return w.ok();
}

} // namespace psd
