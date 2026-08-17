// ============================================================================
// 参照系メタデータ + ディスクリプタ橋渡し
//
// psdparse がパース済みで保持しているのに getLayerInfo では返していなかった
// 情報を吉里吉里へ出す層。psdparse の Python バインディングにある
//   layer.mask / blending_ranges / sheet_color / info_keys / effects / fill /
//   descriptor() / descriptor_bytes()
//   PSDFile.color_table / global_layer_mask / image_resource* / xmp / thumbnail
// と同じ内容を TJS の辞書 / 配列 / octet で返す。
//
// あわせて、Photoshop ディスクリプタの「部分辞書を重ねて葉の値だけ差し替える」
// 編集 (setLayerEffects / setLayerDescriptor) もここに置く。解析済みの型付き
// Descriptor に対して値だけ上書きし、psdwrite の直列化器で書き戻すので、
// 変更しなかった部分はバイト一致のまま保たれる。
// ============================================================================

#include <ncbind.hpp>
#include "psdclass.h"
#include "psdclass_conv.h"
#include "psddesc.h"
#include "psdwrite.h"
#include <vector>
#include <string>

namespace {

// ----------------------------------------------------------------------------
// 小物
// ----------------------------------------------------------------------------

// IteratorBase の指すブロック先頭から size バイトを読み出す。
// clone() + init() で元の iterator の読み位置を壊さない (遅延参照のまま)。
static std::string readBlockBytes(psd::IteratorBase *data, int size)
{
	std::string buf((size_t)(size > 0 ? size : 0), '\0');
	if (size > 0 && data) {
		psd::IteratorBase *rd = data->clone();
		rd->init();
		rd->getData(&buf[0], size);
		delete rd;
	}
	return buf;
}

// std::string を octet の tTJSVariant にする。
static tTJSVariant bytesToOctet(const std::string &s)
{
	return tTJSVariant((const tjs_uint8 *)s.data(), (tjs_uint)s.size());
}

// レイヤ番号のチェック付きで LayerInfo を引く。
static psd::LayerInfo &layerAt(PSD *self, int no)
{
	if (!self->isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	if (no < 0 || no >= (int)self->layerList.size())
		TVPThrowExceptionMessage(TJS_W("not such layer"));
	return self->layerList[(size_t)no];
}

// 追加レイヤ情報ブロックを 4CC で探す。無ければ 0。
static const psd::AdditionalLayerInfo *findAdditional(const psd::LayerInfo &lay, int key)
{
	for (std::vector<psd::AdditionalLayerInfo>::const_iterator it =
	       lay.extraData.additionalLayers.begin();
	     it != lay.extraData.additionalLayers.end(); ++it) {
		if (it->key == key && it->data) return &(*it);
	}
	return 0;
}

// 既知キーのディスクリプタ本体前にあるバージョン接頭バイト数。
// psdparse の Python バインディング (layerDescriptor) と同じ表。
static int defaultDescriptorSkip(int key)
{
	switch (key) {
	case 'lfx2':                             return 8;  // objVer + descVer
	case 'SoCo': case 'GdFl': case 'PtFl':   return 4;  // descVer
	case 'SoLd': case 'SoLE':                return 12; // 'soLD' + ver + descVer
	case 'vstk': case 'CgEd':                return 4;  // descVer
	case 'vscg':                             return 8;  // key + ver
	case 'vogk':                             return 8;  // ver + dataVer
	default:                                 return 0;
	}
}

// ----------------------------------------------------------------------------
// Descriptor → TJS (読み出し)
// ----------------------------------------------------------------------------

static const tjs_char *descUnitName(psd::DescriptorUnit u)
{
	switch (u) {
	case psd::UNIT_POINTS:      return TJS_W("points");
	case psd::UNIT_MILLIMETERS: return TJS_W("millimeters");
	case psd::UNIT_ANGLE:       return TJS_W("angle");
	case psd::UNIT_DENSITY:     return TJS_W("density");
	case psd::UNIT_DISTANCE:    return TJS_W("distance");
	case psd::UNIT_NONE:        return TJS_W("none");
	case psd::UNIT_PERCENT:     return TJS_W("percent");
	case psd::UNIT_PIXELS:      return TJS_W("pixels");
	default:                    return TJS_W("unknown");
	}
}

static tTJSVariant descToTjs(psd::Descriptor *d);

// ディスクリプタアイテム 1 個を TJS の値へ。type フィールドではなく
// dynamic_cast で振り分ける (DescriptorReference と DescriptorRawData が同じ
// 'tdta' タグを共有しているため)。
static tTJSVariant descItemToTjs(psd::DescriptorItem *it)
{
	tTJSVariant result;  // 既定は void
	if (!it) return result;
	if (psd::DescriptorInteger *x = dynamic_cast<psd::DescriptorInteger *>(it))
		return tTJSVariant((tjs_int)x->val);
	if (psd::DescriptorDouble *x = dynamic_cast<psd::DescriptorDouble *>(it))
		return tTJSVariant((tjs_real)x->val);
	if (psd::DescriptorBoolean *x = dynamic_cast<psd::DescriptorBoolean *>(it))
		return tTJSVariant((tjs_int)(x->val ? 1 : 0));
	if (psd::DescriptorString *x = dynamic_cast<psd::DescriptorString *>(it))
		return tTJSVariant(u16ToTjs(x->val));
	if (psd::DescriptorUnitFloat *x = dynamic_cast<psd::DescriptorUnitFloat *>(it)) {
		ncbDictionaryAccessor u;
		if (u.IsValid()) {
			u.SetValue(TJS_W("value"), (tjs_real)x->val);
			u.SetValue(TJS_W("unit"),  ttstr(descUnitName(x->unit)));
			result = u;
		}
		return result;
	}
	if (psd::DescriptorEnumerated *x = dynamic_cast<psd::DescriptorEnumerated *>(it)) {
		ncbDictionaryAccessor e;
		if (e.IsValid()) {
			e.SetValue(TJS_W("type"),  ttstr(x->typeId.c_str()));
			e.SetValue(TJS_W("value"), ttstr(x->enumId.c_str()));
			result = e;
		}
		return result;
	}
	if (psd::DescriptorList *x = dynamic_cast<psd::DescriptorList *>(it)) {
		ncbArrayAccessor arr;
		if (arr.IsValid()) {
			for (int i = 0; i < (int)x->items.size(); i++)
				arr.SetValue((tjs_int32)i, descItemToTjs(x->items[i]));
			result = arr;
		}
		return result;
	}
	if (psd::Descriptor *x = dynamic_cast<psd::Descriptor *>(it))
		return descToTjs(x);
	if (psd::DescriptorRawData *x = dynamic_cast<psd::DescriptorRawData *>(it))
		return bytesToOctet(x->bytes);
	if (psd::DescriptorClass *x = dynamic_cast<psd::DescriptorClass *>(it))
		return tTJSVariant(ttstr(x->classId.c_str()));
	if (psd::DescriptorAlias *x = dynamic_cast<psd::DescriptorAlias *>(it))
		return tTJSVariant(ttstr(x->alias.c_str()));
	// DescriptorReference と未知の型は void。
	return result;
}

static tTJSVariant descToTjs(psd::Descriptor *d)
{
	tTJSVariant result;
	ncbDictionaryAccessor dict;
	if (dict.IsValid()) {
		// キーは生の 4CC (末尾スペースを含むことがある) か可変長名。
		for (psd::Descriptor::ItemMap::const_iterator it = d->itemMap.begin();
		     it != d->itemMap.end(); ++it) {
			dict.SetValue(ttstr(it->first.c_str()).c_str(), descItemToTjs(it->second));
		}
		result = dict;
	}
	return result;
}

// 追加レイヤ情報 key のバイト列を (skip バイト読み飛ばしてから) ディスクリプタ
// として解析して辞書化する。キーが無い / 解析できないときは void。
static tTJSVariant keyDescriptorToTjs(const psd::LayerInfo &lay, int key, int skip)
{
	tTJSVariant result;
	const psd::AdditionalLayerInfo *a = findAdditional(lay, key);
	if (!a) return result;
	psd::IteratorBase *rd = a->data->clone();
	rd->init();                        // このキーのデータ先頭へ巻き戻す
	if (skip > 0) rd->advance(skip);
	psd::Descriptor desc;
	desc.load(rd);                     // 途中で失敗しても読めた分は有効
	delete rd;
	if (desc.itemMap.empty()) return result;
	return descToTjs(&desc);
}

// ----------------------------------------------------------------------------
// TJS → Descriptor (部分マージ)
//
// 辞書を列挙するのではなく「ディスクリプタ側のキーを引きにいく」方向で実装する。
// TJS の Dictionary 列挙は EnumMembers + コールバックオブジェクトが必要で面倒な
// うえ、意味論的にも psdparse 側と同じ「既存キーだけ編集する」になる。
// ----------------------------------------------------------------------------

static void mergeVariantIntoItem(psd::DescriptorItem *item, tTJSVariant val);

static void mergeDictIntoDescriptor(psd::Descriptor *d, tTJSVariant changes)
{
	if (changes.Type() != tvtObject) return;
	ncbPropAccessor acc(changes);
	for (psd::Descriptor::ItemMap::iterator it = d->itemMap.begin();
	     it != d->itemMap.end(); ++it) {
		tTJSVariant v;
		// 指定が無いキーは触らない (存在しないキーは無視)。
		if (!acc.checkVariant(ttstr(it->first.c_str()).c_str(), v)) continue;
		if (v.Type() == tvtVoid) continue;
		mergeVariantIntoItem(it->second, v);
	}
}

static void mergeVariantIntoItem(psd::DescriptorItem *item, tTJSVariant val)
{
	if (psd::DescriptorInteger *x = dynamic_cast<psd::DescriptorInteger *>(item)) {
		x->val = (int32_t)(tjs_int)val;
	} else if (psd::DescriptorDouble *x = dynamic_cast<psd::DescriptorDouble *>(item)) {
		x->val = (double)(tjs_real)val;
	} else if (psd::DescriptorBoolean *x = dynamic_cast<psd::DescriptorBoolean *>(item)) {
		x->val = ((tjs_int)val) != 0;
	} else if (psd::DescriptorString *x = dynamic_cast<psd::DescriptorString *>(item)) {
		x->val = tjsToU16(ttstr(val));
	} else if (psd::DescriptorUnitFloat *x = dynamic_cast<psd::DescriptorUnitFloat *>(item)) {
		// %[ value: ... ] でも生の数値でも受ける (unit は変えない)。
		if (val.Type() == tvtObject) {
			ncbPropAccessor u(val);
			if (u.HasValue(TJS_W("value"))) x->val = (double)u.getRealValue(TJS_W("value"));
		} else {
			x->val = (double)(tjs_real)val;
		}
	} else if (psd::DescriptorEnumerated *x = dynamic_cast<psd::DescriptorEnumerated *>(item)) {
		// %[ type:, value: ] でも、値だけの文字列でも受ける。
		if (val.Type() == tvtObject) {
			ncbPropAccessor e(val);
			if (e.HasValue(TJS_W("type")))
				x->typeId = tjsToUtf8(e.getStrValue(TJS_W("type")));
			if (e.HasValue(TJS_W("value")))
				x->enumId = tjsToUtf8(e.getStrValue(TJS_W("value")));
		} else if (val.Type() == tvtString) {
			x->enumId = tjsToUtf8(ttstr(val));
		}
	} else if (psd::DescriptorList *x = dynamic_cast<psd::DescriptorList *>(item)) {
		if (val.Type() != tvtObject) return;
		ncbPropAccessor l(val);
		int n = (int)l.GetArrayCount();
		if (n > (int)x->items.size()) n = (int)x->items.size();
		for (int i = 0; i < n; i++) {
			tTJSVariant e;
			if (!l.checkVariant((tjs_int)i, e)) continue;
			if (e.Type() == tvtVoid) continue;
			mergeVariantIntoItem(x->items[(size_t)i], e);
		}
	} else if (psd::Descriptor *x = dynamic_cast<psd::Descriptor *>(item)) {
		mergeDictIntoDescriptor(x, val);
	}
	// RawData / Reference / Class / Alias はマージ対象外 (無視)。
}

} // namespace

// ============================================================================
// レイヤ単位の参照系
// ============================================================================

tTJSVariant
PSD::getLayerMask(int no)
{
	psd::LayerInfo &lay = layerAt(this, no);
	const psd::LayerMask &m = lay.extraData.layerMask;
	tTJSVariant result;
	if (!m.present) return result;   // マスク無しは void
	ncbDictionaryAccessor dict;
	if (!dict.IsValid()) return result;
	dict.SetValue(TJS_W("top"),    m.top);
	dict.SetValue(TJS_W("left"),   m.left);
	dict.SetValue(TJS_W("bottom"), m.bottom);
	dict.SetValue(TJS_W("right"),  m.right);
	dict.SetValue(TJS_W("width"),  m.width);
	dict.SetValue(TJS_W("height"), m.height);
	dict.SetValue(TJS_W("default_color"), m.defaultColor);
	dict.SetValue(TJS_W("flags"),         m.flags);           // 生のフラグバイト
	dict.SetValue(TJS_W("relative"),       (m.flags & 1) != 0); // 位置がレイヤ相対
	dict.SetValue(TJS_W("disabled"),       (m.flags & 2) != 0); // マスク無効
	dict.SetValue(TJS_W("inverted"),       (m.flags & 4) != 0); // 反転 (廃止仕様)
	dict.SetValue(TJS_W("from_render"),    (m.flags & 8) != 0); // 他データのレンダ由来
	dict.SetValue(TJS_W("has_parameters"), (m.flags & 16) != 0); // density/feather あり
	// パラメータ (density 0..255 / feather px)。不在のキーは設定しない (void)。
	if (m.userMaskDensity >= 0)  dict.SetValue(TJS_W("user_density"),   m.userMaskDensity);
	if (m.hasUserFeather)        dict.SetValue(TJS_W("user_feather"),   m.userMaskFeather);
	if (m.vectorMaskDensity >= 0) dict.SetValue(TJS_W("vector_density"), m.vectorMaskDensity);
	if (m.hasVectorFeather)      dict.SetValue(TJS_W("vector_feather"), m.vectorMaskFeather);
	// real/user mask (ブロックサイズ >= 36 のとき)。
	if (m.hasReal) {
		ncbDictionaryAccessor rd;
		if (rd.IsValid()) {
			rd.SetValue(TJS_W("flags"),      m.realFlags);
			rd.SetValue(TJS_W("background"), m.realUserMaskBackground);
			rd.SetValue(TJS_W("top"),        m.enclosingTop);
			rd.SetValue(TJS_W("left"),       m.enclosingLeft);
			rd.SetValue(TJS_W("bottom"),     m.enclosingBottom);
			rd.SetValue(TJS_W("right"),      m.enclosingRight);
			dict.SetValue(TJS_W("real"), rd.GetDispatch());
		}
	}
	result = dict;
	return result;
}

tTJSVariant
PSD::getLayerBlendingRanges(int no)
{
	psd::LayerInfo &lay = layerAt(this, no);
	const psd::LayerBlendingRange &b = lay.extraData.layerBlendingRange;
	tTJSVariant result;
	if (!b.present) return result;
	ncbDictionaryAccessor dict;
	if (!dict.IsValid()) return result;
	// source/dest は 32bit の生値 (上下 16bit に黒/白の範囲が詰まっている)。
	ncbDictionaryAccessor gray;
	if (gray.IsValid()) {
		gray.SetValue(TJS_W("source"), b.grayBlendSource);
		gray.SetValue(TJS_W("dest"),   b.grayBlendDest);
		dict.SetValue(TJS_W("gray"), gray.GetDispatch());
	}
	ncbArrayAccessor arr;
	if (arr.IsValid()) {
		for (int i = 0; i < (int)b.channels.size(); i++) {
			ncbDictionaryAccessor c;
			if (c.IsValid()) {
				c.SetValue(TJS_W("source"), b.channels[i].source);
				c.SetValue(TJS_W("dest"),   b.channels[i].dest);
				arr.SetValue((tjs_int32)i, c.GetDispatch());
			}
		}
		dict.SetValue(TJS_W("channels"), arr.GetDispatch());
	}
	result = dict;
	return result;
}

tTJSVariant
PSD::getLayerSheetColor(int no)
{
	// 'lclr' は 2 バイトのインデックス + 6 バイトのパディング。
	// 名前は Photoshop のレイヤパネルの並び。
	static const tjs_char *kNames[] = {
		TJS_W("none"), TJS_W("red"), TJS_W("orange"), TJS_W("yellow"),
		TJS_W("green"), TJS_W("blue"), TJS_W("violet"), TJS_W("gray"),
		TJS_W("seafoam"), TJS_W("indigo"), TJS_W("magenta"), TJS_W("fuschia"),
	};
	psd::LayerInfo &lay = layerAt(this, no);
	tTJSVariant result;
	const psd::AdditionalLayerInfo *a = findAdditional(lay, 'lclr');
	if (!a) return result;   // lclr ブロック自体が無い
	psd::IteratorBase *rd = a->data->clone();
	rd->init();
	int idx = (uint16_t)rd->getInt16(true);
	delete rd;
	ncbDictionaryAccessor dict;
	if (dict.IsValid()) {
		dict.SetValue(TJS_W("index"), idx);
		dict.SetValue(TJS_W("name"),
		              ttstr(idx >= 0 && idx < (int)(sizeof(kNames) / sizeof(kNames[0]))
		                    ? kNames[idx] : TJS_W("unknown")));
		result = dict;
	}
	return result;
}

tTJSVariant
PSD::getLayerInfoKeys(int no)
{
	psd::LayerInfo &lay = layerAt(this, no);
	tTJSVariant result;
	ncbArrayAccessor arr;
	if (arr.IsValid()) {
		int i = 0;
		for (std::vector<psd::AdditionalLayerInfo>::const_iterator it =
		       lay.extraData.additionalLayers.begin();
		     it != lay.extraData.additionalLayers.end(); ++it, i++) {
			arr.SetValue((tjs_int32)i, fourccToTjs(it->key));
		}
		result = arr;
	}
	return result;
}

tTJSVariant
PSD::getLayerDescriptor(int no, ttstr key, int skip)
{
	psd::LayerInfo &lay = layerAt(this, no);
	int k = tjsToFourcc(key);
	if (skip < 0) skip = defaultDescriptorSkip(k);
	return keyDescriptorToTjs(lay, k, skip);
}

tTJSVariant
PSD::getLayerDescriptorBytes(int no, ttstr key)
{
	psd::LayerInfo &lay = layerAt(this, no);
	tTJSVariant result;
	const psd::AdditionalLayerInfo *a = findAdditional(lay, tjsToFourcc(key));
	if (!a) return result;
	return bytesToOctet(readBlockBytes(a->data, a->size));
}

tTJSVariant
PSD::getLayerEffects(int no)
{
	// 'lfx2' = objVer(4) + descVer(4) のあとにディスクリプタ本体。
	psd::LayerInfo &lay = layerAt(this, no);
	return keyDescriptorToTjs(lay, 'lfx2', 8);
}

tTJSVariant
PSD::getLayerFill(int no)
{
	// 塗りつぶしレイヤの内容: 'SoCo'(単色) / 'GdFl'(グラデ) / 'PtFl'(パターン)。
	// いずれも version(4) のあとにディスクリプタ本体。
	static const struct { int key; const tjs_char *type; } tbl[] = {
		{ 'SoCo', TJS_W("solid")    },
		{ 'GdFl', TJS_W("gradient") },
		{ 'PtFl', TJS_W("pattern")  },
	};
	psd::LayerInfo &lay = layerAt(this, no);
	tTJSVariant result;
	for (int i = 0; i < (int)(sizeof(tbl) / sizeof(tbl[0])); i++) {
		tTJSVariant d = keyDescriptorToTjs(lay, tbl[i].key, 4);
		if (d.Type() == tvtVoid) continue;
		ncbDictionaryAccessor dict;
		if (dict.IsValid()) {
			dict.SetValue(TJS_W("type"), ttstr(tbl[i].type));
			dict.SetValue(TJS_W("data"), d);
			result = dict;
		}
		return result;
	}
	return result;
}

// ============================================================================
// 文書単位の参照系
// ============================================================================

tTJSVariant
PSD::getColorTable()
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	tTJSVariant result;
	if (colorTable.colors.empty()) return result;
	ncbDictionaryAccessor dict;
	if (!dict.IsValid()) return result;
	dict.SetValue(TJS_W("valid_count"),        (tjs_int)colorTable.validCount);
	dict.SetValue(TJS_W("transparency_index"), (tjs_int)colorTable.transparencyIndex);
	ncbArrayAccessor arr;
	if (arr.IsValid()) {
		// 吉里吉里の色表記に合わせて 0xAARRGGBB へパックして返す
		// (透明色インデックスのエントリは a=0 になっている)。
		for (int i = 0; i < (int)colorTable.colors.size(); i++) {
			const psd::ColorRgba &c = colorTable.colors[i];
			tjs_uint32 argb = ((tjs_uint32)c.a << 24) | ((tjs_uint32)c.r << 16)
			                | ((tjs_uint32)c.g <<  8) |  (tjs_uint32)c.b;
			arr.SetValue((tjs_int32)i, tTJSVariant((tjs_int64)argb));
		}
		dict.SetValue(TJS_W("colors"), arr.GetDispatch());
	}
	result = dict;
	return result;
}

tTJSVariant
PSD::getGlobalLayerMask()
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	tTJSVariant result;
	const psd::GlobalLayerMaskInfo &g = globalLayerMaskInfo;
	if (!g.present) return result;
	ncbDictionaryAccessor dict;
	if (!dict.IsValid()) return result;
	dict.SetValue(TJS_W("overlay_color_space"), g.overlayColorSpace);
	ncbArrayAccessor col;
	if (col.IsValid()) {
		col.SetValue((tjs_int32)0, g.color1);
		col.SetValue((tjs_int32)1, g.color2);
		col.SetValue((tjs_int32)2, g.color3);
		col.SetValue((tjs_int32)3, g.color4);
		dict.SetValue(TJS_W("color"), col.GetDispatch());
	}
	dict.SetValue(TJS_W("opacity"), g.opacity);  // 0..100
	dict.SetValue(TJS_W("kind"),    g.kind);     // 0=反転 / 1=全マスク / 128=レイヤ毎
	result = dict;
	return result;
}

tTJSVariant
PSD::getImageResourceIds()
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	tTJSVariant result;
	ncbArrayAccessor arr;
	if (arr.IsValid()) {
		for (int i = 0; i < (int)imageResourceList.size(); i++)
			arr.SetValue((tjs_int32)i, (tjs_int)imageResourceList[i].id);
		result = arr;
	}
	return result;
}

tTJSVariant
PSD::getImageResource(int id)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	tTJSVariant result;
	for (int i = 0; i < (int)imageResourceList.size(); i++) {
		const psd::ImageResourceInfo &res = imageResourceList[i];
		if ((int)res.id != id) continue;
		return bytesToOctet(readBlockBytes(res.data, res.size));
	}
	return result;
}

tTJSVariant
PSD::getXMP()
{
	// XMP パケット (リソース 1060) は UTF-8 の XML。
	// 不正な UTF-8 が入っている可能性を考えるなら getImageResource(1060) で
	// 生バイトを取ること。
	tTJSVariant oct = getImageResource(1060);
	tTJSVariant result;
	if (oct.Type() != tvtOctet) return result;
	tTJSVariantOctet *o = oct.AsOctetNoAddRef();
	if (!o) return result;
	std::string s((const char *)o->GetData(), (size_t)o->GetLength());
	return tTJSVariant(u16ToTjs(psd::utf8ToU16(s)));
}

tTJSVariant
PSD::getThumbnail()
{
	// 埋め込みサムネイル: リソース 1036 (RGB) / 旧 1033 (BGR)。
	// ヘッダ 28 バイト + ペイロード。format==1 なら JFIF JPEG。
	tTJSVariant result;
	int resId = 1036;
	tTJSVariant oct = getImageResource(1036);
	if (oct.Type() != tvtOctet) {
		resId = 1033;
		oct = getImageResource(1033);
	}
	if (oct.Type() != tvtOctet) return result;
	tTJSVariantOctet *o = oct.AsOctetNoAddRef();
	if (!o) return result;
	const tjs_uint8 *p = o->GetData();
	tjs_uint len = o->GetLength();
	if (!p || len < 28) return result;
	tjs_uint32 fmt = ((tjs_uint32)p[0] << 24) | ((tjs_uint32)p[1] << 16)
	               | ((tjs_uint32)p[2] <<  8) |  (tjs_uint32)p[3];
	tjs_uint32 w   = ((tjs_uint32)p[4] << 24) | ((tjs_uint32)p[5] << 16)
	               | ((tjs_uint32)p[6] <<  8) |  (tjs_uint32)p[7];
	tjs_uint32 h   = ((tjs_uint32)p[8] << 24) | ((tjs_uint32)p[9] << 16)
	               | ((tjs_uint32)p[10] << 8) |  (tjs_uint32)p[11];
	tjs_uint32 bits = ((tjs_uint32)p[24] << 8) | (tjs_uint32)p[25];
	ncbDictionaryAccessor dict;
	if (dict.IsValid()) {
		dict.SetValue(TJS_W("format"), ttstr(fmt == 1 ? TJS_W("jpeg") : TJS_W("raw")));
		dict.SetValue(TJS_W("width"),  (tjs_int)w);
		dict.SetValue(TJS_W("height"), (tjs_int)h);
		dict.SetValue(TJS_W("bits"),   (tjs_int)bits);
		dict.SetValue(TJS_W("resource_id"), resId);  // 1036=RGB順 / 1033=BGR順
		dict.SetValue(TJS_W("data"), tTJSVariant(p + 28, len - 28));
		result = dict;
	}
	return result;
}

// ============================================================================
// ディスクリプタ編集
// ============================================================================

void
PSD::setLayerDescriptor(int index, ttstr key, tTJSVariant changes, int skip)
{
	psd::LayerInfo &lay = layerAt(this, index);
	int k = tjsToFourcc(key);
	if (skip < 0) skip = defaultDescriptorSkip(k);

	const psd::AdditionalLayerInfo *a = findAdditional(lay, k);
	if (!a) TVPThrowExceptionMessage(TJS_W("layer has no descriptor block for that key"));

	psd::IteratorBase *rd = a->data->clone();
	rd->init();
	std::vector<uint8_t> prefix((size_t)(skip > 0 ? skip : 0));
	// objVer/descVer 等の接頭バイトはそのまま持ち越す。
	if (skip > 0) rd->getData(prefix.data(), skip);
	psd::Descriptor desc;
	desc.load(rd);
	delete rd;

	mergeDictIntoDescriptor(&desc, changes);

	std::vector<uint8_t> buf;
	psd::MemoryWriter w(buf);
	if (!prefix.empty()) w.putData(prefix.data(), prefix.size());
	psd::writeDescriptorBody(w, &desc);
	while (buf.size() & 3u) buf.push_back(0);   // ディスクリプタは 4 バイト境界へ
	if (!setAdditionalInfoBytes(index, k, buf.data(), (int)buf.size()))
		TVPThrowExceptionMessage(TJS_W("failed to write descriptor block"));
}

void
PSD::setLayerEffects(int index, tTJSVariant changes)
{
	setLayerDescriptor(index, ttstr(TJS_W("lfx2")), changes, 8);
}
