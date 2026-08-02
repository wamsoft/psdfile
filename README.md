# psdfile

吉里吉里Z (KirikiriZ) 用の PSD (Photoshop) ファイル読み込みプラグイン `psdfile.dll`。
PSD ファイルの構造解析・レイヤ単位の画像取得・合成画像 (merged image) 取得を行い、
さらに `psd://` 仮想ストレージとして個別レイヤを画像ファイルのように扱えるようにする。

PSD パース/書き出しのコア部分は別リポジトリ [wamsoft/psdparse](https://github.com/wamsoft/psdparse) に分離されており、本リポジトリは submodule として取り込んで吉里吉里バインドだけを行う。

## clone

submodule を含むので必ず `--recursive` で clone するか、後から `git submodule update --init` する:

```sh
git clone --recursive https://github.com/wamsoft/psdfile.git
# or
git clone https://github.com/wamsoft/psdfile.git
cd psdfile
git submodule update --init --recursive
```

## ビルド

要件:
- Windows + MSVC (Visual Studio 2022 で動作確認)
- CMake 3.16 以上
- [vcpkg](https://vcpkg.io/) (環境変数 `VCPKG_ROOT` を設定)
- [tp_stub](https://github.com/krkrz/krkrz) と [ncbind](https://github.com/krkrz/ncbind) を隣接ディレクトリに配置
  (規定では `..\tp_stub` / `..\ncbind` を見る。CMake オプション `TPSTUB_DIR` で上書き可)

```powershell
$env:VCPKG_ROOT = 'd:\vcpkg'

# Makefile 経由 (推奨)
make PRESET=x64-windows BUILD_TYPE=Debug build
make PRESET=x64-windows BUILD_TYPE=Release build

# 直接 cmake で叩く場合
cmake --preset x64-windows
cmake --build --preset x64-windows --config Release
```

成果物: `build/x64-windows/Release/psdfile.dll`

依存パッケージは `zlib` のみ。吉里吉里 build では vcpkg manifest (`vcpkg.json`) の
static triplet で供給する。なお psdparse 側は zlib が見つからなければ CMake の
`FetchContent` でソース取得するフォールバックを持つため、psdparse 単体では vcpkg を必要としない
(psdfile 側は上記どおり vcpkg 経由で供給し、その zlib が使われる)。

## 使い方

### TJS2 API

詳細は [`manual.tjs`](manual.tjs) を参照 (これが API 正本)。

```javascript
var psd = new PSD();
psd.load("foo.psd");

// 基本プロパティ
psd.width, psd.height, psd.channels, psd.color_mode, psd.layer_count;
psd.hresolution, psd.vresolution;   // 解像度 (dpi, 既定 72)

// レイヤ情報
var info = psd.getLayerInfo(0);
// info.name, info.top/left/bottom/right, info.blend_mode, info.visible, ...

// テキストレイヤ (info.layer_type == PSD.layer_type_text) は info.text を持つ
if (info.text !== void) {
    info.text.text;           // 本文 (改行は \r)
    info.text.orientation;    // "horizontal" / "vertical"
    info.text.justification;  // 0=左 1=右 2=中央
    info.text.transform;      // [ xx, xy, yx, yy, tx, ty ]
    // info.text.runs[i] = %[ length, font, size_px, color:[r,g,b,a], tracking, kerning, auto_kerning ]
}

// レイヤ画像をレイヤオブジェクトに展開
var lay = new Layer(window, null);
psd.getLayerData(lay, 0);        // マスク繰り込み済み
psd.getLayerDataRaw(lay, 0);     // マスク無視
psd.getLayerDataMask(lay, 0);    // マスクのみ

// 合成済み画像
psd.getBlend(lay);
```

### psd:// 仮想ストレージ

ロード済み PSD はそのまま吉里吉里のストレージとしても見える。レイヤ画像を
通常のファイルアクセスと同じ書き方で読み込める:

```
psd://<PSDファイル名>/root/<フォルダ名>/.../<レイヤ名>.bmp
psd://<PSDファイル名>/id/<レイヤID>.bmp
```

- PSD ファイル名はベース名のみで小文字に正規化される
- フォルダ名・レイヤ名も小文字化され、含まれる `/` は `_` に置換される
- 名前が重複する場合は後にあるものが優先

```javascript
var lay = new Layer(win, null);
lay.loadImages("psd://foo.psd/root/face/eye.bmp");
```

### 編集・書き出し API

psdparse v0.7+ の編集機能を吉里吉里バインドに公開している。いずれも in-memory の
内容を編集し、`save()` で PSD ファイルとして書き出す。画素/レイヤ追加系は **8bit RGB
文書のみ**対応。編集後の合成画像 (`getBlend` の元) は古いままになる (Photoshop で開いて
再合成するまで反映されない)。

```javascript
var psd = new PSD();
psd.load("foo.psd");

// レイヤ構造の編集
psd.deleteLayer(3);
psd.moveLayer(0, 2);
var dup = psd.duplicateLayer(1);
psd.setLayerName(0, "背景");
psd.setFillOpacity(1, 128);

// 画素の差し替え / レイヤ追加 (Layer オブジェクトの BGRA を取り込む)
psd.setLayerPixels(2, srcLayer);
var idx = psd.addLayer("新規", 10, 20, srcLayer, PSD.blend_mode_multiply, 255);
psd.setMergedImage(mergedLayer);      // プレビュー(合成画像)の差し替え

// マスク
psd.setLayerMaskPixels(2, maskLayer, 0, 0);
psd.setMaskDensity(2, 200);
psd.setMaskFeather(2, 1.5);

// テキストレイヤの編集
psd.setLayerText(4, "差し替えたテキスト");
psd.setLayerRunStyle(4, 0, %[ size_px:48, bold:true, color:[1,0,0,1] ]);

// 別 PSD からレイヤをコピー (カラーモード/深度が一致していること)
var other = new PSD(); other.load("bar.psd");
psd.copyLayerFrom(other, 0, -1);

psd.save("out.psd");

// 完全新規作成
var np = new PSD();
np.createBlank(640, 480);
np.addLayer("layer1", 0, 0, someLayer);
np.save("new.psd");
```

詳細な引数は [`manual.tjs`](manual.tjs) の「編集系 API」節を参照。

## サポートする PSD 形式

| 形式 | 状態 |
|---|---|
| RGB 8/16/32bpp | ○ (16/32bpp は ICC プロファイル非対応、Photoshop と色が異なる場合あり) |
| CMYK 8/16/32bpp | ○ (16/32bpp は同上) |
| Grayscale | ○ |
| Bitmap (2値) | △ レイヤが存在しないので `getBlend()` で取得 |
| Indexed | △ 同上 |
| Lab | × 未対応 |
| Multichannel | × 未対応 |
| DuoTone | × 未対応 |

レイヤマスクは画像読み込み時にアルファチャネルへ繰り込まれる。クリッピングマスクは未対応。

テキストレイヤは本文・向き (縦/横)・行揃え・配置変換・段落別行揃え (`paragraphs`)・
ラン単位のフォント/サイズ/色/字送りを `getLayerInfo().text` から取得できる
(psdparse の 'TySh' + EngineData 解析)。本文・ラン単位スタイルの編集も
`setLayerText()` / `setLayerRunStyle()` で可能。非RGB 塗り色・ワープテキスト・
段落テキスト境界等は未対応 (psdparse 側 ROADMAP 参照)。

レイヤ構造・画素・マスク・テキストの編集と PSD 書き出し (`save`) にも対応する
(「編集・書き出し API」節参照。画素/レイヤ追加系は 8bit RGB 文書のみ)。

## アーキテクチャ

PSD コア:

```
external/psdparse/                ← submodule (https://github.com/wamsoft/psdparse, MIT)
  psdparse/   — C++17 ライブラリ
  python/     — pybind11 バインディング (psdfile からは使わない)
  tests/      — pytest (psdfile からは実行しない)
  docs/       — 仕様・ロードマップ
```

吉里吉里ラッパ:

```
psdclass.h, psdclass.cpp          PSD クラス本体 (Layer 取得・ストレージ I/F)
psdclass_loadstream.cpp           iTJSBinaryStream を psd::StreamReader::Source で
                                  ラップして lazy I/O のまま psdparse に流す
main.cpp                          PSDStorage (psd:// ストレージ) + NCB 登録
```

PSD ファイルは parse 段階では構造メタデータだけ読み込まれ、レイヤピクセルは要求された時点で
ストリーム越しに展開される (大型 PSD でもメモリを食わない設計)。

## 開発時の流れ

- psdparse 側 (パーサ/エンコーダ等) を直したい場合: [wamsoft/psdparse](https://github.com/wamsoft/psdparse) で commit/push してから本リポジトリで `git submodule update --remote external/psdparse` → コミット。
- 吉里吉里バインドだけ変更したい場合: 本リポジトリの `psdclass*.cpp` / `main.cpp` を直接編集 + `manual.tjs` を同期更新。

## ライセンス

- 本プラグイン (`psdfile.dll` 部分): 吉里吉里本体に準拠
- 同梱の zlib 部分: zlib license
- submodule の psdparse: MIT License (`external/psdparse/LICENSE`)

## Authors

- わたなべごう
- ゆーき
