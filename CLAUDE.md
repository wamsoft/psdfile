# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

吉里吉里Z (KirikiriZ) 用の PSD 読み込み/編集/書き出しプラグイン `psdfile.dll`。元は Boost.Spirit/Phoenix と `<tp_stub.h>` 直結の単一リポジトリだったが、2026-06-12 に PSD パース/書き出しコア部分を [wamsoft/psdparse](https://github.com/wamsoft/psdparse) に分離。psdfile は **psdparse を submodule で取り込んで吉里吉里バインドを行う薄いラッパー** の構成になった。読み込みに加え、psdparse v0.7+ の編集機能 (レイヤ構造/画素/マスク/テキスト編集・新規作成・`save`) も公開している (「編集系 API」節)。

ユーザー向け TJS2 API は `manual.tjs` が正本。`NCB_REGISTER_CLASS(PSD)` の登録内容と齟齬が出ないように維持する。

## 構造

```
psdfile/                         ← このリポジトリ
├── CMakeLists.txt               psdfile.dll の build 設定
├── CMakePresets.json            x64-windows (kirikiri Debug/Release)、x86-windows
├── Makefile                     make PRESET=x64-windows BUILD_TYPE=Debug build
├── vcpkg.json                   zlib のみ (吉里吉里 build 用に vcpkg で供給)
├── manual.tjs                   ★ TJS2 API 正本
├── psdclass.h                   PSD クラスヘッダ
├── psdclass.cpp                 PSD クラス本体 (getLayerData / getBlend / ストレージ等)
├── psdclass_loadstream.cpp      iTJSBinaryStream → StreamReader::Source ラッパ
├── main.cpp                     PSDStorage (psd:// ストレージ) + NCB 登録
└── external/
    └── psdparse/                ★ submodule (wamsoft/psdparse)
```

psdparse 側の C++ ライブラリ、Python バインディング、pytest、tools (`psd_export.py`)、Python API ドキュメント、将来計画 (Phase 4b/c/d) は全部 [submodule 先](https://github.com/wamsoft/psdparse) を見ること。

## Clone 手順

```bash
git clone --recursive https://github.com/wamsoft/psdfile.git
# or:
git clone https://github.com/wamsoft/psdfile.git
cd psdfile && git submodule update --init --recursive
```

## ビルド

要件: MSVC、CMake 3.16+、[vcpkg](https://vcpkg.io/) (`VCPKG_ROOT` 必須)、隣接の `tp_stub` / `ncbind` (`D:\test\tp_stub`、`D:\test\ncbind` を既定で見る。`TPSTUB_DIR` で上書き可)。

```powershell
$env:VCPKG_ROOT = 'd:\vcpkg'

# Debug ビルド
make PRESET=x64-windows BUILD_TYPE=Debug build

# Release ビルド
make PRESET=x64-windows BUILD_TYPE=Release build
```

成果物:
- `build/x64-windows/Debug/psdfile.dll`
- `build/x64-windows/external/psdparse/psdparse/Debug/psdparse_cli.exe` (psdparse 側 CLI、smoke test 用)

依存: zlib のみ。Boost は完全に削除済み。

zlib の供給: psdparse 側の `psdparse/CMakeLists.txt` は `find_package(ZLIB QUIET)` →
見つからなければ `FetchContent` で zlib 1.3.1 をソース取得するフォールバックを持つ
(psdparse 単体では vcpkg 不要になった)。psdfile の吉里吉里 build では `vcpkg.json` の
`zlib` を vcpkg static triplet で供給しており、`find_package` がそれを拾うので FetchContent は
発動しない。VCPKG_ROOT 未設定などで vcpkg zlib が無い場合のみソース fetch にフォールバックする。

**重要なハマりポイント:**
- clang LSP は tp_stub.h / ncbind.hpp / submodule 先のヘッダを解決できず大量の偽 error を出す。CMake/MSVC ビルドは通るので無視する。
- submodule を update してないと configure 時に `external/psdparse/CMakeLists.txt` が無いと言って fail する。`git submodule update --init --recursive` で修復。

## アーキテクチャ (kirikiri 側のみ)

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

`u16str → ttstr` 変換は `psdclass.cpp` の `u16ToTjs(const psd::u16str&)` (length 指定の `ttstr(const tjs_char*, tjs_int)` で構成)。`tjs_char == char16_t` 前提なので Windows 専用前提だが psdfile.dll は Windows ターゲットのみなので問題なし。

### テキストレイヤ ('TySh')

psdparse で追加された `LayerInfo::textData` (`psd::TextLayerData`) を
`getLayerInfo()` が辞書キー `text` として転送する (テキストレイヤ = `layer_type_text`(=5)
のときのみ。それ以外はキー自体を設定しない)。中身は `text` / `orientation` /
`justification` / `transform[6]` / `runs[]`(ラン単位の font・size_px・color[RGBA]・
tracking・kerning・auto_kerning・bold・italic・underline) / `paragraphs[]`(段落別の
length・justification)。
psdparse 側で 'TySh' の追加レイヤ情報 → Adobe *EngineData* ミニ言語を `psdengine.cpp`
が解析して埋める。`layer_type_text` 定数も `NCB_REGISTER_CLASS(PSD)` / `manual.tjs` に
追加済み。未対応 (psdparse 側 ROADMAP): 非RGB FillColor・warp text・段落テキスト境界等。

### 編集系 API (psdparse v0.7+)

psdparse v0.8.0 で編集機能一式が入ったのを受け吉里吉里バインドに公開し、v0.10.0
(submodule ref `f521d2f`) でリッチテキスト / テキスト配置 / フォルダ対応レイヤ移動を
追加公開した。実装は `psdclass.cpp` に PSD メンバとして薄く被せている:

- **参照追加**: `hresolution`/`vresolution` プロパティ (`header.hres/vres`)、
  `getLayerInfo()` に `mask_params`(density/feather) と text の `paragraphs` /
  ラン単位の `bold`/`italic`/`underline`。
- **構造編集**: `deleteLayer` / `moveLayer` / `duplicateLayer` / `copyLayerFrom` /
  `setLayerName` / `setFillOpacity` / `setMask*` — 大半は `psd::PSDFile::*` への
  pass-through + 構造変化時に `invalidateStorageCache()` (psd:// キャッシュ破棄)。
- **フォルダ対応の移動**: `groupSpan`(→ `%[start,count]`) / `moveLayerSibling`(→ 新
  index、端で -1) / `moveLayerRange`。`moveLayer` はフォルダを塊で動かさないので、
  グループごと動かすときはこちら。
- **画素編集**: `setLayerPixels` / `setLayerMaskPixels` / `addLayer` / `setMergedImage`
  — 吉里吉里 `Layer` の `mainImageBuffer`(BGRA) を `readLayerBGRA()` で tight-pack して
  psdparse に渡す。8bit RGB 文書のみ (psdparse 側制約)。
- **新規作成 / 保存**: `createBlank(w,h)` / `save(filename)`。save は `TVPGetLocalName`
  でローカルパス化 → `NarrowString` (fopen ベースの `FileWriter`)。
- **テキスト編集**: `setLayerText` / `setLayerRunStyle` / `setLayerRichText` /
  `setLayerJustification` / `getLayerFonts`、配置系に `get,setLayerTextTransform` /
  `moveTextLayer` / `get,setLayerTextBounds`。v0.9.0 でテキスト編集が psdparse の
  C++ ライブラリ側 (`psd::PSDFile::setLayerText` 等) へ上がったので、v0.8 時代に
  `psdclass.cpp` が持っていた python glue の移植 (`editTextLayerImpl`) は削除済み。
  現在は「TJS 引数 → `RunStyleEdit`/`TextRunSpec`/`TextParagraphSpec` 変換 +
  `errorOut` の例外化」だけを行う (`readRunStyleEdit` / `throwTextError`)。

補助変換: `tjsToU16`(ttstr→u16str)、`tjsToUtf8`(ttstr→UTF-8。luni 名 / フォント名用)、
`blendModeToKey`(psd::BlendMode→4CC。`addLayer` 用)。省略引数を持つ
`addLayer` / `setLayerRichText` / `setLayerJustification` / `moveLayerSibling` は
ncbind の `RawCallback` で公開している (NCB_METHOD は既定値を扱えないため)。

編集後の合成画像 (getBlend の元) は古いままになる (psdparse 側仕様 — Photoshop が
開いて再合成するまで反映されない)。テキストのラスタも同様で、psdparse は字形を
再描画しないため `moveTextLayer` / `setLayerTextBounds` 等の結果は Photoshop で
開き直したときに反映される。

psdparse C++ ライブラリの設計詳細 (IteratorBase / MemoryReader / VectorReader /
StreamReader / WriterBase / MemoryWriter / round-trip save / EngineData パース・
再直列化・編集の仕組み) は [external/psdparse/docs/ARCHITECTURE.md](external/psdparse/docs/ARCHITECTURE.md) と [docs/PYTHON_API.md](external/psdparse/docs/PYTHON_API.md) を参照。

## 行儀よく避ける改変

- `manual.tjs` を更新せずに `NCB_REGISTER_CLASS(PSD)` の登録だけ変更しない (manual.tjs が user-facing 正本)。
- `PSD::~PSD()` の `clearData()` 明示呼び出しは virtual dispatch のため必要。`= default` にしない。
- psdparse 側の改修が必要なときは external/psdparse で submodule を編集 → psdparse 側で commit/push → psdfile 側で submodule ref を bump → psdfile commit、の順。psdfile で submodule のファイルを直接編集して放置しない。
- 吉里吉里依存 (tp_stub, ncbind, tjs_char, ttstr) を psdparse 側に滲ませない。submodule の独立性が崩れる。

## TJS2 API 変更時の手順

1. `psdclass.cpp` で実装変更
2. `psdclass.cpp` 末尾の `NCB_REGISTER_CLASS(PSD)` で公開
3. `manual.tjs` に同じシグネチャを追記
4. ビルド (`make PRESET=x64-windows BUILD_TYPE=Debug build`) で smoke 確認
