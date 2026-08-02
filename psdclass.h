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
	 * この PSD を空の 8bit RGB 文書 (白の合成画像) として初期化する。
	 * 以後 addLayer(...) でレイヤを足して save() できる。
	 * @return 成功したら true
	 */
	bool createBlank(int width, int height);

	/** レイヤを 1 枚削除。範囲外で false。 */
	bool deleteLayer(int index);
	/** レイヤを from から to へ移動する (to は削除後リストでの挿入位置)。 */
	bool moveLayer(int from, int to);
	/** レイヤを複製し、複製の新インデックスを返す。失敗 -1。 */
	int  duplicateLayer(int index);
	/** 別 PSD からレイヤをコピー挿入する。destIndex<0 で末尾。新インデックス、失敗 -1。 */
	int  copyLayerFrom(tTJSVariant src, int srcIndex, int destIndex);

	/** レイヤを改名 (pascal 名 + luni)。失敗で false。 */
	bool setLayerName(int index, ttstr name);
	/** 塗り不透明度 (0..255) の編集。失敗で false。 */
	bool setFillOpacity(int index, int opacity);
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
	 *  %[ size_px, color:[r,g,b,a], tracking, kerning, bold, italic, underline ]
	 *  (指定したキーだけ上書き)。 */
	void setLayerRunStyle(int index, int runIndex, tTJSVariant style);

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
