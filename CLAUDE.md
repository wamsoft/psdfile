# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

吉里吉里Z (KirikiriZ) 用の PSD 読み込みプラグイン `psdfile.dll`。元は Boost.Spirit/Phoenix と `<tp_stub.h>` 依存だったが、2026-06-12 に大規模書き換え完了：**「Boost と吉里吉里依存を psdparse から切り離して pure C++17 化、pybind11 で Python から使えるようにし、PSD 書き出し機能も追加、吉里吉里プラグインを新コアに乗せ替え」**。

ユーザー向け TJS2 API は依然として `manual.tjs` が正本。`NCB_REGISTER_CLASS(PSD)` の登録内容と齟齬が出ないように維持する。

## Active rewrite plan (TaskList で進行管理中)

- ✅ Phase 1a: Boost 除去 (Spirit/Phoenix/filesystem/iterator_range/mapped_file/predef-endian) → 手書きパーサ
- ✅ Phase 1b: tp_stub 除去 (`tjs_string` → `std::u16string` = `psd::u16str`)
- ✅ Phase 2a: pybind11 バインディング (`python/psdparse_module.cpp`)
- ✅ Phase 2b: pytest セットアップ (`tests/`、20 テスト)
- ✅ Phase 3: mmap バッキング復活 + StreamReader 追加 (`load`/`loadFromStream`/`load_streamed` の 3 経路、equivalence pytest あり)
- ✅ Phase 4: PSD 書き出し (`PSDFile::save(path)` + Python `save()`)。load → save でバイト完全一致のラウンドトリップ。`psdwrite.h/cpp` の `WriterBase`/`FileWriter`/`writePSD()`。
- ✅ Phase 5.1: ライブラリ I/F から `wchar_t` 除去 (UTF-8 char のみ)。Win32 では内部だけ wide 変換。
- ✅ Phase 5.2: 吉里吉里プラグイン再構築。`iTJSBinaryStream` を `StreamReader::Source` でラップ → lazy I/O。`LOAD_MEMORY` 経路と stream cache 撤去。
- ✅ Phase 5.3: ドキュメント改訂

### 将来計画 (実装は保留, memory [[project-psdfile-save-modify-roadmap]] 参照)

- Phase 4b: per-channel save に切替 → レイヤ削除/複製対応
- Phase 4c: LayerExtraData::rawBytes を捨てて field 再シリアライズ (名前/blend mode 変更)
- Phase 4d: RLE encoder + 新規 PSDFile 作成 API

## アーキテクチャ (現状)

```
psdparse/                       ← pure C++17 ライブラリ (吉里吉里非依存)
  psdbase.h                       endian/型/共通アトム、PSD_LITTLE_ENDIAN
  psddata.h, psddesc.h            Data 構造 (Header, LayerInfo, Descriptor, …)
                                  LayerExtraData::rawBytes と Data::globalLayerMaskInfoRaw /
                                  layerAndMaskTrailing は ラウンドトリップ save 用に parse 時に保持。
  psdparse.h                      MemoryReader / StreamReader (IteratorBase 派生) と parsePSD 宣言
  psdparse.cpp                    手書き parser (SubBlock RAII + cloneRange 経由で sub-range bounded)
  psdwrite.h/cpp                  WriterBase / FileWriter / writePSD() (patch-back サイズ書き戻し方式)
  psdfile.h/cpp                   PSDFile (load/loadFromMemory/loadFromReader/loadFromStream/save)
  psdimage.cpp                    レイヤ/合成画像の decode (RLE, zip, ColorMode 切替)
  psddesc.cpp, psdlayer.cpp, psdresource.cpp, bmp.cpp
python/                         ← pybind11 モジュール (mmap or stream で PSDFile を Python に晒す)
  psdparse_module.cpp
  CMakeLists.txt
tests/                          ← pytest (sample PSD 2 件で 27 テスト)
  conftest.py, test_header.py, test_layers.py, test_images.py, test_save.py
tools/                          ← 開発者向けスクリプト
  psd_export.py                   PSD → layers.json + merged.png + per-layer PNGs (Pillow 利用)
psdclass.h, psdclass.cpp, psdclass_loadstream.cpp, main.cpp
                                ← 吉里吉里プラグイン (NCB 登録、`psd://` ストレージ)
                                  psdclass_loadstream.cpp の TJSBinaryStreamSource が
                                  iTJSBinaryStream を StreamReader::Source として晒し、
                                  PSD::loadStream が StreamReader 経由で loadFromReader を
                                  呼ぶ (lazy I/O)。LOAD_MEMORY 経路は撤去。
CMakeLists.txt                  ← top: kirikiri プラグインと Python モジュールを option で出し分け
CMakePresets.json               ← x64-windows (kirikiri) / x64-windows-python (Python) を分離
                                  static CRT (kirikiri) と dynamic CRT (Python) は同居不能なので別 build dir
```

### IteratorBase の遅延参照は HARD 要件

`psd::IteratorBase` は parser/decode の唯一の I/O 抽象。**`std::vector<uint8_t>` への eager コピーは絶対 NG** (元 mmap 実装の存在理由はこれ)。Phase 3 で `fileBuffer_` vector を Win32 CreateFileMapping ベースに戻した。詳細は memory `feedback-lazy-iterator` 参照。

実装ヒエラルキー:

```
IteratorBase (psdbase.h, 純粋仮想 — parser はこれだけ見る)
├── MemoryReader   (psdparse.h)  ... mmap/バッファ向け
└── StreamReader   (psdparse.h)  ... 汎用ストリーム
    └── Source (純粋仮想)
        ├── IStreamSource         (psdfile.cpp 無名 ns)         ... std::istream
        └── TJSBinaryStreamSource (psdclass_loadstream.cpp 無名 ns) ... iTJSBinaryStream
```

`clone()` / `cloneOffset(int)` / `cloneRange(int, int)` で sub-reader を切り出す。`cloneRange` は **size-prefixed block を厳密にバウンディング** するために必須 (Phase 3 で導入。これがないと不正な dataSize で無限ループに陥る → 18.7 GB 食う事故あり、2026-06-12)。

### parser 安全条件

`parseImageResources` / `parseLayerExtraData` の while ループには **前進保証** (`posAfter <= posBefore` で break) を必ず入れる。これがないと garbage 入力で無限ループする。

## ビルド & 検証

詳細は memory `build-msvc-preset` を見る。要点:

```powershell
$env:VCPKG_ROOT = 'd:\vcpkg'   # msys2 bash 経由起動時は継承済みなので不要

# Makefile 経由 (推奨。デフォルトは PRESET=x64-windows / BUILD_TYPE=Release)
make PRESET=x64-windows-python prebuild   # cmake configure
make PRESET=x64-windows-python build       # cmake --build

# 直接 cmake で叩く場合:
cmake --preset x64-windows                 # kirikiri プラグイン
cmake --build --preset x64-windows --config Debug
cmake --preset x64-windows-python          # Python モジュール
cmake --build --preset x64-windows-python --config Release

# テスト (build dir を conftest.py が sys.path に挿入)
C:\Users\go\.venv\Scripts\python.exe -m pytest -v
```

成果物:
- `build/x64-windows/Debug/psdfile.dll`
- `build/x64-windows/psdparse/Debug/psdparse_cli.exe`
- `build/x64-windows-python/python/Release/psdparse.cp312-win_amd64.pyd`

依存: vcpkg で zlib のみ (Phase 1a 後)。Boost は完全に削除済み。

**重要なハマりポイント:**
- clang LSP は tp_stub.h / ncbind.hpp / pybind11.h を解決できず大量の偽 error を出す。CMake/MSVC ビルドは通るので無視する。
- Python は msys2 のを引きやすい (CMake が `find_program(python)` で msys2 側を見つける)。`Python3_EXECUTABLE` を明示するか、PATH を整える。`python/CMakeLists.txt` で `PYBIND11_FINDPYTHON ON` 経由で modern FindPython を使うようにしてある。
- バックグラウンドの Python ループは禁止。タイムアウト付きフォアグラウンドのみ。

## 行儀よく避ける改変

- `IteratorBase` の遅延参照を vector に Blob 化する変更は要件違反 (Phase 3 でこれをやって戻した経緯あり)。
- Spirit 文法を「復活」させる変更も避ける (Boost 全部依存に戻る)。
- `psdclass*.cpp` の `/Od` 強制は元々 Boost 起因のバグ回避だったので Phase 1a (Boost 除去) 完了に伴って外した (2026-06-12)。再度 `/Od` を入れる必要はない。
- `manual.tjs` を更新せずに NCB 登録だけ変更しない。manual.tjs が user-facing 正本。
- `PSD::~PSD()` の `clearData()` 明示呼び出しは virtual dispatch のため必要。`= default` にしない。

## ライブラリ I/F 規約 (Phase 5.1 後)

- すべての public パス引数は **UTF-8 char\*** (例: `psd::PSDFile::load(const char*)`, `psd::FileWriter(const char*)`)
- Win32 では `psd::utf8ToWide` (psdbase.h, inline) で内部だけ UTF-16 に変換してから OS API へ
- `wchar_t` overload は廃止。kirikiri 側 (`tjs_char`/`ttstr`) や Python 側 (`str`) で呼び出し前に UTF-8 への変換責任を負う
- pybind11 は Python `str` → `std::string` を自動で UTF-8 エンコードするので、Python から呼ぶ場合は意識不要
- 吉里吉里側はファイル名を `ttstr` で受け、`TVPCreateStream(filename, TJS_BS_READ)` で開いてから `iTJSBinaryStream` を `TJSBinaryStreamSource` でラップする経路に統一 (パス文字列を C++ コアに直接渡すケースは無くなった)

## Phase 4 ラウンドトリップ save の仕組み

`load(p) -> save(q)` で q が p とバイト完全一致するように、構造復元コストの高い
領域は parse 時に **生バイトの IteratorBase を別途保持** している:

- `LayerExtraData::rawBytes` — layer record の extra data ブロック全域 (layer mask /
  blending range / Pascal 名 / additional layer info 全部入り)
- `Data::globalLayerMaskInfoRaw` — global layer mask info ブロックの本体
- `Data::layerAndMaskTrailing` — layer-and-mask info の global mask より後ろの
  追加 info (Lr16/Lr32 等、未解釈)
- `ImageResourceInfo::data`, `AdditionalLayerInfo::data` — 元から保持
- `Data::colorModeIterator`, `Data::channelImageData`, `Data::imageData` — 元から保持

これらが揃っているので、`writePSD()` は構造フィールド (Header / 各 layer record /
チャネル数等) を再シリアライズしつつ、内部ブロックは全部 `copyAllFrom(iterator)` で
ドカっと転送するだけ。「parse して書き戻すと壊れる」事故を防ぐため、追加 info の
スキップは絶対 NG (この trailing capture が無いと UI PSD で 19KB 失う)。

**現状の save の制約**: 上記の通り、load 元の iterator/raw bytes に強く依存するので
**load → save の bit-identical round-trip 専用**。レイヤ追加/削除/中身書き換えは
未対応。改変対応 (per-channel save / field 再シリアライズ / RLE encoder 追加) の
段階計画は memory `project-psdfile-save-modify-roadmap` 参照。

## 吉里吉里プラグイン側の構造 (Phase 5.2 後)

```
PSD : public psd::PSDFile        (psdclass.h)
├── load(ttstr filename)         filename → TVPGetPlacedPath → loadStream
├── loadStream(ttstr file)        TVPCreateStream → TJSBinaryStreamSource (shared_ptr)
│                                  → psd::StreamReader → loadFromReader
├── clearData()                   ← virtual。PSDFile::clearData() を呼ぶと iterator
│                                  群が消えて shared_ptr<Source> refcount が落ち、
│                                  Source dtor が iTJSBinaryStream::Destruct() を呼ぶ
└── 既存の getLayerData / getBlend / openLayerImage / etc.

psdclass_loadstream.cpp 無名 ns
└── TJSBinaryStreamSource : public psd::StreamReader::Source
    ├── ctor で iTJSBinaryStream* の所有権を取る
    ├── dtor で Destruct() を呼ぶ
    └── read(offset, len) は Seek + Read
```

u16str → ttstr 変換は `psdclass.cpp` の `u16ToTjs(const psd::u16str&)` (length 指定の
`ttstr(const tjs_char*, tjs_int)` で構成)。`tjs_char == char16_t` 前提なので Windows
専用前提だが psdfile.dll は Windows ターゲットのみなので問題なし。
