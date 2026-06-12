#ifndef __psdfile_h__
#define __psdfile_h__

#include "psdbase.h"
#include "psddata.h"
#include <cstdint>
#include <istream>
#include <memory>
#include <vector>

namespace psd {
  // イメージ取得モード
  enum ImageMode {
    IMAGE_MODE_IMAGE,       // マスクをくりこまないイメージデータ
    IMAGE_MODE_MASK,        // マスク情報のみのイメージデータ(グレー)
    IMAGE_MODE_MASKEDIMAGE, // マスクをアルファに繰り込んだイメージデータ
  };

  // PSD ファイルクラス
  //
  //   load(path)            : ファイルを mmap で開く (全読み込みしない)。
  //                           IteratorBase 経由のレイヤ画像取得は OS のページ
  //                           キャッシュ越しに必要なバイトだけ読まれる。
  //   loadFromMemory(p, n)  : 呼び出し元のバイト列を内部 vector にコピー保持。
  //                           ファイルアクセスを介さずロードしたいケース用。
  //   loadFromReader(reader): 汎用エントリ。任意の IteratorBase 実装を受ける。
  //                           reader が指す storage は PSDFile のライフタイム
  //                           中、呼び出し元が維持する責任を負う。
  //                           (kirikiri プラグインは iTJSBinaryStream をラップ
  //                            した自前 StreamReader をここに流し込む)
  class PSDFile : public Data {
  public:
    PSDFile();
    ~PSDFile();

    bool isLoaded;

    // filename は UTF-8。Win32 では内部で UTF-16 に変換してから OS API を叩く。
    bool load(const char *filename);
    bool loadFromMemory(const uint8_t *data, size_t size);
    bool loadFromReader(IteratorBase &reader);
    // 任意の seekable な std::istream を全領域のサイズ付きで受ける。
    // istream の所有権はとらない -- 呼び出し元が PSDFile より長く維持する責任。
    bool loadFromStream(std::istream &stream);
    // 同上の所有権ありバージョン: stream を内部に取り込んで PSDFile が
    // 解放されるまで維持する (Python など、stream を別に管理しづらい状況用)。
    bool loadFromStream(std::unique_ptr<std::istream> stream);

    // PSDFile を空に戻す。mmap を unmap し、vector を解放する。
    void clearData() override;

    // 現在ロード済みの内容を PSD ファイルとして path (UTF-8) に書き出す。
    // 失敗 (open エラー / 未ロード) で false。
    bool save(const char *filename);

    // 画像データ取得インタフェース (バッファピッチが０の場合は full fill)
    bool getMergedImage(void *buf, const ColorFormat &format, int bufPitchByte);
    bool getLayerImage(const LayerInfo &layer, void *buf, const ColorFormat &format,
                       int bufPitchByte, ImageMode mode);
    bool getLayerImageById(int layerId, void *buf, const ColorFormat &format,
                           int bufPitchByte, ImageMode mode);

  private:
    // OS マップ領域 (path から load した場合)。pimpl で windows.h 等の漏出を防ぐ。
    struct Mapping;
    std::unique_ptr<Mapping> mapping_;
    // ユーザー渡しバイト列の保持 (loadFromMemory 用)。
    std::vector<uint8_t> ownedBuffer_;
    // 所有権版 loadFromStream で取り込んだ istream を維持する。
    std::unique_ptr<std::istream> ownedStream_;

    // clearData/state 初期化は呼ばず、与えられた stream をパースするだけ。
    // 両 loadFromStream overload からの共有実体。
    bool parseFromStream_(std::istream &stream);
  };

} // namespace psd

#endif
