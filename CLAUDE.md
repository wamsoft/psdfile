# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

吉里吉里Z (KirikiriZ) 用の PSD 読み込みプラグイン `psdfile.dll`。元は Boost.Spirit/Phoenix と `<tp_stub.h>` 直結の単一リポジトリだったが、2026-06-12 に PSD パース/書き出しコア部分を [wamsoft/psdparse](https://github.com/wamsoft/psdparse) に分離。psdfile は **psdparse を submodule で取り込んで吉里吉里バインドを行う薄いラッパー** の構成になった。

ユーザー向け TJS2 API は `manual.tjs` が正本。`NCB_REGISTER_CLASS(PSD)` の登録内容と齟齬が出ないように維持する。

## 構造

```
psdfile/                         ← このリポジトリ
├── CMakeLists.txt               psdfile.dll の build 設定
├── CMakePresets.json            x64-windows (kirikiri Debug/Release)、x86-windows
├── Makefile                     make PRESET=x64-windows BUILD_TYPE=Debug build
├── vcpkg.json                   zlib のみ (psdparse から継承)
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

依存: vcpkg で zlib のみ。Boost は完全に削除済み。

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

psdparse C++ ライブラリの設計詳細 (IteratorBase / MemoryReader / StreamReader / WriterBase / round-trip save の仕組み) は [external/psdparse/docs/ARCHITECTURE.md](external/psdparse/docs/ARCHITECTURE.md) を参照。

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
