#ifndef __PSDCLASS_H__
#define __PSDCLASS_H__

#include <tp_stub.h>
#include "psdfile.h"

class PSDStorage;
class PSDIterator;

class PSD : public psd::PSDFile
{
	friend class PSDStorage;
	friend class PSDIterator;
	
public:
	/**
	 * コンストラクタ
	 */
	PSD(iTJSDispatch2 *objthis);

	/**
	 * デストラクタ
	 */
	~PSD();

	/**
	 * 内包データの消去
	 */
	virtual void clearData();
	
	/**
	 * インスタンス生成ファクトリ
	 */
	static tjs_error factory(PSD **result, tjs_int numparams, tTJSVariant **params, iTJSDispatch2 *objthis);

	/**
	 * 生成時の自己オブジェクトを取得
	 */
	tTJSVariant getSelf();
	
	/**
	 * PSD画像のロード
	 * @param filename ファイル名
	 * @return ロードに成功したら true
	 */
	bool load(ttstr filename);

	/**
	 * octet に入った PSD データをロードする (psdparse の loadFromMemory)。
	 * バイト列は psdparse 側の内部バッファへコピーされるので、呼び出し後に
	 * octet を破棄しても問題ない。ファイル名を持たないので psd:// ストレージ
	 * への登録は行わない。
	 * @param data PSD ファイル全体を格納した octet
	 * @return ロードに成功したら true
	 */
	bool loadOctet(tTJSVariant data);

	/**
	 * 保持しているデータを明示的に破棄する。
	 * 吉里吉里側のオブジェクト寿命は GC 依存なので、大きな PSD を掴んだままに
	 * したくないときに呼ぶ。以後 isLoaded 相当の参照系は "no data" 例外になる。
	 */
	void clear() { clearData(); }

	static void clearStorageCache();
	
#define INTGETTER(tag) int get_ ## tag(){ return isLoaded ? header.tag : -1; }

	INTGETTER(width);
	INTGETTER(height);
	INTGETTER(channels);
	INTGETTER(depth);
  int get_color_mode()  { return isLoaded ? header.mode : -1; }
  int get_layer_count() { return isLoaded ? (int)layerList.size() : -1; }

  // 解像度 (image resource 1005)。既定 72dpi。未ロードは 0。
  double get_hresolution() { return isLoaded ? header.hres : 0.0; }
  double get_vresolution() { return isLoaded ? header.vres : 0.0; }

  // 合成済み画像にアルファチャンネルが含まれるか。未ロードは false。
  bool get_merged_alpha() { return isLoaded ? mergedAlpha : false; }

public:
	/**
	 * レイヤ種別の取得
	 * @param no レイヤ番号
	 * @return レイヤ種別
	 */
	int getLayerType(int no);

	/**
	 * レイヤ名称の取得
	 * @param no レイヤ番号
	 * @return レイヤ種別
	 */
	ttstr getLayerName(int no);

	/**
	 * レイヤ情報の取得
	 * @param no レイヤ番号
	 * @return レイヤ情報が格納された辞書
	 */
	tTJSVariant getLayerInfo(int no);

	/**
	 * レイヤデータの読み出し(内部処理)
	 * @param layer 読み出し先レイヤ
	 * @param no レイヤ番号
     * @param imageMode イメージモード
	 */
  void _getLayerData(tTJSVariant layer, int no, psd::ImageMode imageMode);

	/**
	 * レイヤデータの読み出し
	 * @param layer 読み出し先レイヤ
	 * @param no レイヤ番号
	 */
	void getLayerData(tTJSVariant layer, int no);

	/**
	 * レイヤデータの読み出し(生イメージ)
	 * @param layer 読み出し先レイヤ
	 * @param no レイヤ番号
	 */
	void getLayerDataRaw(tTJSVariant layer, int no);

	/**
	 * レイヤデータの読み出し(マスクのみ)
	 * @param layer 読み出し先レイヤ
	 * @param no レイヤ番号
	 */
	void getLayerDataMask(tTJSVariant layer, int no);

	/**
	 * スライスデータの読み出し
	 * @return スライス情報辞書 %[ top, left, bottom, right, slices:[ %[ id, group_id, left, top, bottom, right ], ... ] ]
	 *         スライス情報がない場合は void を返す
	 */
	tTJSVariant getSlices();

	/**
	 * ガイドデータの読み出し
	 * @return ガイド情報辞書 %[ vertical:[ x1, x2, ... ], horizontal:[ y1, y2, ... ] ]
	 *         ガイド情報がない場合は void を返す
	 */
	tTJSVariant getGuides();

	/**
	 * 合成結果の取得。取得領域は画像全体サイズ内におさまってる必要があります
   * 注意：PSDファイル自体に合成済み画像が存在しない場合は取得に失敗します
   *
	 * @param layer 格納先レイヤ(width,heightサイズに調整される)
	 * @return 取得に成功したら true
	 */
  bool getBlend(tTJSVariant layer);

	/**
	 * レイヤーカンプ
	 */
	tTJSVariant getLayerComp();

	// ------------------------------------------------------------
	// 参照系メタデータ (psdclass_meta.cpp)
	//
	// psdparse がパース済みで保持しているが getLayerInfo では返していない
	// 情報を取り出す口。psdparse の Python バインディングにある
	// layer.mask / blending_ranges / sheet_color / info_keys / effects /
	// fill / descriptor() / 文書レベルのリソース群に対応する。
	// ------------------------------------------------------------

	/**
	 * レイヤマスクの詳細
	 * @param no レイヤ番号
	 * @return %[ top, left, bottom, right, width, height, default_color, flags,
	 *            relative, disabled, inverted, from_render, has_parameters,
	 *            user_density, user_feather, vector_density, vector_feather,
	 *            real:%[ flags, background, top, left, bottom, right ] ]
	 *         マスクを持たない場合は void
	 */
	tTJSVariant getLayerMask(int no);

	/**
	 * レイヤのブレンド範囲
	 * @param no レイヤ番号
	 * @return %[ gray:%[ source, dest ], channels:[ %[ source, dest ], ... ] ]
	 *         ブレンド範囲ブロックがない場合は void
	 *         source/dest は 32bit の生値 (上下 16bit に黒/白の範囲が入る)
	 */
	tTJSVariant getLayerBlendingRanges(int no);

	/**
	 * レイヤパネルの色ラベル ('lclr')
	 * @param no レイヤ番号
	 * @return %[ index, name ] (0/"none" 〜 11/"fuschia")。lclr が無ければ void
	 */
	tTJSVariant getLayerSheetColor(int no);

	/**
	 * レイヤが持つ追加レイヤ情報ブロックの 4CC キー一覧
	 * @param no レイヤ番号
	 * @return キー文字列 (4 文字) の配列
	 */
	tTJSVariant getLayerInfoKeys(int no);

	/**
	 * 追加レイヤ情報ブロックを Photoshop ディスクリプタとして辞書化する
	 * @param no レイヤ番号
	 * @param key 4 文字のキー ("lfx2" 等)
	 * @param skip ディスクリプタ本体前のバージョン接頭バイト数 (-1 で既知キーは自動)
	 * @return ネストした辞書 / 配列。キーが無い・解析不能なら void
	 */
	tTJSVariant getLayerDescriptor(int no, ttstr key, int skip);

	/**
	 * 追加レイヤ情報ブロックの生バイト
	 * @param no レイヤ番号
	 * @param key 4 文字のキー
	 * @return octet。キーが無ければ void
	 */
	tTJSVariant getLayerDescriptorBytes(int no, ttstr key);

	/**
	 * レイヤ効果 ('lfx2') のディスクリプタ辞書。効果が無ければ void
	 * @param no レイヤ番号
	 */
	tTJSVariant getLayerEffects(int no);

	/**
	 * 塗りつぶしレイヤの内容 ('SoCo'/'GdFl'/'PtFl')
	 * @param no レイヤ番号
	 * @return %[ type:"solid"|"gradient"|"pattern", data:%[ ... ] ]。無ければ void
	 */
	tTJSVariant getLayerFill(int no);

	/**
	 * カラーテーブル (インデックスカラー文書)
	 * @return %[ valid_count, transparency_index, colors:[ 0xAARRGGBB, ... ] ]
	 *         カラーテーブルが無ければ void
	 */
	tTJSVariant getColorTable();

	/**
	 * global layer mask info (マスク表示用のオーバーレイ色)
	 * @return %[ overlay_color_space, color:[c1,c2,c3,c4], opacity, kind ]
	 *         ブロックが空/不在なら void
	 */
	tTJSVariant getGlobalLayerMask();

	/**
	 * 文書が持つイメージリソースの ID 一覧
	 * @return ID (整数) の配列
	 */
	tTJSVariant getImageResourceIds();

	/**
	 * イメージリソースの生バイト
	 * @param id リソース ID (1039=ICC, 1058=EXIF, 1060=XMP, 1036/1033=サムネイル)
	 * @return octet。該当リソースが無ければ void
	 */
	tTJSVariant getImageResource(int id);

	/** ICC プロファイル (リソース 1039) の生バイト。無ければ void */
	tTJSVariant getICCProfile() { return getImageResource(1039); }
	/** EXIF (リソース 1058) の生バイト。無ければ void */
	tTJSVariant getEXIF()       { return getImageResource(1058); }

	/**
	 * XMP パケット (リソース 1060)。UTF-8 XML を文字列にして返す
	 * @return 文字列。無ければ void
	 */
	tTJSVariant getXMP();

	/**
	 * 埋め込みサムネイル (リソース 1036=RGB / 旧 1033=BGR)
	 * @return %[ format:"jpeg"|"raw", width, height, bits, resource_id,
	 *            data:<octet> ]。無ければ void
	 */
	tTJSVariant getThumbnail();

	/**
	 * LayerIDが未設定のレイヤに対してID番号を自動割り付け(base_id+1からlayer_no順に)
	 * @param base_id 割り付けID最小番号-1(※既存の全てのレイヤIDがこれより大きかったらその値が利用される)
	 * @return IDを設定したレイヤの枚数
	 */
	int assignAutoIds(int base_id = 0);

	// ------------------------------------------------------------
	// 編集系 API (psdparse v0.7+ の編集機能を吉里吉里バインドに公開)
	//
	// いずれも in-memory の layerList / imageData を操作するだけの軽量操作で、
	// 実バイトは save() 時に再構築される。画素編集系は 8bit RGB 文書のみ対応。
	// 編集後は psd:// ストレージのレイヤ検索キャッシュを破棄する。
	// 注意: 合成画像 (getBlend の元) は編集後は古いままになる (Photoshop が
	// 開いて再合成するまで反映されない)。
	// ------------------------------------------------------------

	/**
	 * 現在の内容を PSD ファイルとして書き出す
	 * @param filename 保存先ファイル名 (ローカルパス)
	 * @return 成功したら true
	 */
	bool save(ttstr filename);

	/**
	 * この PSD を空の 8bit 文書 (白の合成画像) として初期化する。
	 * 以後 addLayer(...) でレイヤを足して save() できる。
	 * @param mode カラーモード (color_mode_* 定数。既定は color_mode_rgb)。
	 *             psdparse 側が新規作成に対応しているのは 8bit RGB のみで、
	 *             それ以外を渡すと false が返る。
	 * @return 成功したら true
	 */
	bool createBlank(int width, int height, int mode);

	/** レイヤを 1 枚削除。範囲外で false。 */
	bool deleteLayer(int index);
	/** レイヤを from から to へ移動する (to は削除後リストでの挿入位置)。 */
	bool moveLayer(int from, int to);
	/** レイヤが占める塊 (フォルダなら区切り+中身) を %[ start:, count: ] で返す。
	 *  範囲外なら void。 */
	tTJSVariant groupSpan(int index);
	/** 同じ階層の隣の兄弟と入れ替える (フォルダは塊ごと)。up=true で表示上ひとつ上。
	 *  移動後の自分のインデックス。端で動かせない場合は -1。 */
	int  moveLayerSibling(int index, bool up);
	/** [from, from+count) を to へ移動する (to は取り除く前のインデックス)。 */
	bool moveLayerRange(int from, int count, int to);
	/** レイヤを複製し、複製の新インデックスを返す。失敗 -1。 */
	int  duplicateLayer(int index);
	/** 別 PSD からレイヤをコピー挿入する。destIndex<0 で末尾。新インデックス、失敗 -1。 */
	int  copyLayerFrom(tTJSVariant src, int srcIndex, int destIndex);

	/** レイヤを改名 (pascal 名 + luni)。失敗で false。 */
	bool setLayerName(int index, ttstr name);
	/** 塗り不透明度 (0..255) の編集。失敗で false。 */
	bool setFillOpacity(int index, int opacity);

	// --- レイヤレコード項目の編集 -------------------------------------------
	// いずれも layerList のフィールドを書き換えるだけ。レイヤレコードは save()
	// 時に必ずフィールドから再直列化されるので追加のフラグ操作は要らない。
	// 範囲外の index で false。

	/** 不透明度 (0..255) の編集。 */
	bool setLayerOpacity(int index, int opacity);
	/** クリッピング (0=base / 1=non-base) の編集。 */
	bool setLayerClipping(int index, int clipping);
	/** 可視フラグの編集 (flag bit1 の反転)。 */
	bool setLayerVisible(int index, bool visible);
	/** 合成モードの編集。mode は blend_mode_* 定数、または 4 文字のキー
	 *  ("mul " 等、3 文字キーの末尾スペースに注意)。 */
	bool setLayerBlendMode(int index, tTJSVariant mode);

	// --- ディスクリプタ編集 (psdclass_meta.cpp) ------------------------------
	// 解析済みの型付きディスクリプタに部分辞書を重ねて、葉の値だけ差し替える。
	// 構造 / classID / 型 / キー順は保たれるので、変更しなければバイト一致。
	// 失敗時は例外。

	/** レイヤ効果 ('lfx2') の値を編集する。changes は getLayerEffects と同じ形の
	 *  部分辞書 (存在する葉だけ上書き。未知キーは無視)。 */
	void setLayerEffects(int index, tTJSVariant changes);
	/** 任意の追加レイヤ情報キーに対する setLayerEffects の一般版。
	 *  skip はディスクリプタ本体前のバージョン接頭バイト数 (-1 で既知キーは自動)。 */
	void setLayerDescriptor(int index, ttstr key, tTJSVariant changes, int skip);
	/** マスク無効フラグの編集 (マスクを持つレイヤのみ)。 */
	bool setMaskDisabled(int index, bool disabled);
	/** ユーザーマスク濃度 (0..255) の編集。 */
	bool setMaskDensity(int index, int density);
	/** ユーザーマスクぼかし (px) の編集。 */
	bool setMaskFeather(int index, double feather);
	/** マスク既定色 (0..255) の編集。 */
	bool setMaskDefaultColor(int index, int color);

	/** 既存レイヤの画素を Layer の内容 (BGRA) で差し替える。 */
	bool setLayerPixels(int index, tTJSVariant layer);
	/** レイヤのマスク画素を Layer の内容 (グレー=B成分) で差し替える。矩形も設定。 */
	bool setLayerMaskPixels(int index, tTJSVariant layer, int top, int left);
	/** 新規画像レイヤを (left,top) に追加。blendMode は blend_mode_*。新インデックス、失敗 -1。 */
	int  addLayer(ttstr name, int left, int top, tTJSVariant layer,
	              int blendMode, int opacity, int destIndex);
	/** 合成済み画像 (プレビュー) を Layer の内容で差し替える。canvas サイズ一致必須。 */
	bool setMergedImage(tTJSVariant layer);

	/** テキストレイヤの本文を差し替える (スタイルは先頭ランに畳まれる)。 */
	void setLayerText(int index, ttstr text);
	/** テキストレイヤの runIndex 番目のランのスタイルを編集する。style は辞書
	 *  %[ font, size_px, color:[r,g,b,a], tracking, kerning, bold, italic, underline ]
	 *  (指定したキーだけ上書き)。 */
	void setLayerRunStyle(int index, int runIndex, tTJSVariant style);
	/** 本文とラン構成 / 段落構成をまとめて差し替える (書式付きテキスト編集)。
	 *  runs は %[ length, <setLayerRunStyle の style キー> ] の配列、
	 *  paragraphs は %[ length, justification ] の配列 (どちらも void 可)。 */
	void setLayerRichText(int index, ttstr text, tTJSVariant runs, tTJSVariant paragraphs);
	/** 段落の行揃えだけ変える。paraIndex < 0 で全段落。0=左 1=右 2=中央。 */
	void setLayerJustification(int index, int justification, int paraIndex);
	/** テキストレイヤの EngineData が持つフォント名一覧 (配列)。 */
	tTJSVariant getLayerFonts(int index);

	/** テキストレイヤのアフィン変換 [xx,xy,yx,yy,tx,ty] を配列で返す。 */
	tTJSVariant getLayerTextTransform(int index);
	/** テキストレイヤのアフィン変換を差し替える (要素 6 個の配列)。 */
	void setLayerTextTransform(int index, tTJSVariant matrix);
	/** テキストレイヤを平行移動する (変換の tx/ty とレイヤ/マスク矩形の両方)。 */
	void moveTextLayer(int index, double dx, double dy);
	/** 流し込み枠を %[ left:, top:, right:, bottom: ] で返す (変換のローカル座標)。 */
	tTJSVariant getLayerTextBounds(int index);
	/** 流し込み枠を差し替える (変換のローカル座標)。 */
	void setLayerTextBounds(int index, double left, double top, double right, double bottom);

protected:
	iTJSDispatch2 *objthis; ///< 自己オブジェクト情報の参照
	ttstr dname; ///< 登録用ベース名

	/**
	 * iTJSBinaryStream をストリームとしてロードする。
	 * Source ラッパ + StreamReader 経由で psdparse の lazy iterator に
	 * 流す。バッファ全コピーは行わない。
	 */
	bool loadStream(const ttstr &filename);

	/**
	 * レイヤ番号が適切かどうか判定
	 * @param no レイヤ番号
	 */
	void checkLayerNo(int no);

	/**
	 * 名前の取得
	 * @param layレイヤ情報
	 */
	static ttstr layname(psd::LayerInfo &lay);
	
	// ------------------------------------------------------------
	// ストレージレイヤ参照用インターフェース
	// ------------------------------------------------------------
	
protected:

	// ストレージ情報登録
	void addToStorage(const ttstr &filename);
	void removeFromStorage();

	// 編集後に psd:// レイヤ検索キャッシュを破棄 (次回アクセスで再構築)
	void invalidateStorageCache();

	bool storageStarted; //< ストレージ用の情報初期化済みフラグ

	// レイヤ名を返す
	static ttstr path_layname(psd::LayerInfo &lay);

	// レイヤのパス名を返す
	static ttstr pathname(psd::LayerInfo &lay);

	// ストレージ処理用データの初期化
	void startStorage();

	/*
	 * 指定した名前のレイヤの存在チェック
	 * @param name パスを含むレイヤ名
	 * @param layerIdxRet レイヤインデックス番号を返す
	 */
	bool CheckExistentStorage(const ttstr &filename, int *layerIdxRet=0);

	/*
	 * 指定したパスにあるファイル名一覧の取得
	 * @param pathname パス名
	 * @param lister リスト取得用インターフェース
	 */
	void GetListAt(const ttstr &pathname, iTVPStorageLister *lister);

	/*
	 * 指定した名前のレイヤの画像ファイルをストリームで返す
	 * @param name パスを含むレイヤ名
	 * @return ファイルストリーム
	 */
	iTJSBinaryStream *openLayerImage(const ttstr &name);
	
	// パス名記録用

	typedef std::map<int,int> LayerIdIdxMap; // layerId とレイヤ情報インデックスのマップ
	LayerIdIdxMap layerIdIdxMap;

	typedef std::map<ttstr,int> NameIdxMap;     //< レイヤ名とlayerId のマップ
	typedef std::map<ttstr,NameIdxMap> PathMap; //< パス別のレイヤ名一覧
	PathMap pathMap;
};

#endif
