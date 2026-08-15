#include <ncbind.hpp>
#include "psdclass.h"
#include "psdengine.h"  // 編集系: RunStyleEdit / TextRunSpec / TextParagraphSpec
#include <vector>
#include <string>

#define BMPEXT TJS_W(".bmp")

// psdparse の Unicode 文字列 (std::u16string = UTF-16 host-order) を ttstr に
// 変換。Windows では tjs_char == char16_t (どちらも 16bit UTF-16 code unit)
// なので bit-level reinterpret で問題ない。length 指定で embedded NUL も保存。
static inline ttstr u16ToTjs(const psd::u16str &s) {
	static_assert(sizeof(tjs_char) == sizeof(char16_t),
	              "tjs_char must be 16-bit UTF-16 code unit");
	return ttstr(reinterpret_cast<const tjs_char *>(s.data()),
	             (tjs_int)s.length());
}

// ttstr (UTF-16 host-order) を psd::u16str に変換。tjs_char == char16_t 前提。
static inline psd::u16str tjsToU16(const ttstr &s) {
	return psd::u16str(reinterpret_cast<const char16_t *>(s.c_str()),
	                   (size_t)s.length());
}

// ttstr (UTF-16) を UTF-8 (std::string) に変換。psdparse の setLayerName /
// addLayer は UTF-8 名を受け取り内部で luni に再変換するため。
static std::string tjsToUtf8(const ttstr &s) {
	std::string out;
	const tjs_char *p = s.c_str();
	tjs_int n = s.length();
	for (tjs_int i = 0; i < n; i++) {
		uint32_t cp = (uint16_t)p[i];
		if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < n) {
			uint32_t lo = (uint16_t)p[i + 1];
			if (lo >= 0xdc00 && lo <= 0xdfff) {
				cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
				i++;
			}
		}
		if (cp < 0x80) {
			out.push_back((char)cp);
		} else if (cp < 0x800) {
			out.push_back((char)(0xc0 | (cp >> 6)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		} else if (cp < 0x10000) {
			out.push_back((char)(0xe0 | (cp >> 12)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		} else {
			out.push_back((char)(0xf0 | (cp >> 18)));
			out.push_back((char)(0x80 | ((cp >> 12) & 0x3f)));
			out.push_back((char)(0x80 | ((cp >> 6) & 0x3f)));
			out.push_back((char)(0x80 | (cp & 0x3f)));
		}
	}
	return out;
}

// psd::BlendMode enum を PSD の 4CC blendModeKey へ変換 (addLayer 用)。
// blendKeyToMode の逆写像。未知は 'norm'。
static int blendModeToKey(int mode) {
	switch ((psd::BlendMode)mode) {
	case psd::BLEND_MODE_NORMAL:        return 'norm';
	case psd::BLEND_MODE_DISSOLVE:      return 'diss';
	case psd::BLEND_MODE_DARKEN:        return 'dark';
	case psd::BLEND_MODE_MULTIPLY:      return 'mul ';
	case psd::BLEND_MODE_COLOR_BURN:    return 'idiv';
	case psd::BLEND_MODE_LINEAR_BURN:   return 'lbrn';
	case psd::BLEND_MODE_DARKER_COLOR:  return 'dkCl';
	case psd::BLEND_MODE_LIGHTEN:       return 'lite';
	case psd::BLEND_MODE_SCREEN:        return 'scrn';
	case psd::BLEND_MODE_COLOR_DODGE:   return 'div ';
	case psd::BLEND_MODE_LINEAR_DODGE:  return 'lddg';
	case psd::BLEND_MODE_LIGHTER_COLOR: return 'ltCl';
	case psd::BLEND_MODE_OVERLAY:       return 'over';
	case psd::BLEND_MODE_SOFT_LIGHT:    return 'sLit';
	case psd::BLEND_MODE_HARD_LIGHT:    return 'hLit';
	case psd::BLEND_MODE_VIVID_LIGHT:   return 'vLit';
	case psd::BLEND_MODE_LINEAR_LIGHT:  return 'lLit';
	case psd::BLEND_MODE_PIN_LIGHT:     return 'pLit';
	case psd::BLEND_MODE_HARD_MIX:      return 'hMix';
	case psd::BLEND_MODE_DIFFERENCE:    return 'diff';
	case psd::BLEND_MODE_EXCLUSION:     return 'smud';
	case psd::BLEND_MODE_SUBTRACT:      return 'fsub';
	case psd::BLEND_MODE_DIVIDE:        return 'fdiv';
	case psd::BLEND_MODE_HUE:           return 'hue ';
	case psd::BLEND_MODE_SATURATION:    return 'sat ';
	case psd::BLEND_MODE_COLOR:         return 'colr';
	case psd::BLEND_MODE_LUMINOSITY:    return 'lum ';
	case psd::BLEND_MODE_PASS_THROUGH:  return 'pass';
	default:                            return 'norm';
	}
}

// 吉里吉里 Layer から BGRA を tight-pack (imageWidth*imageHeight*4) で取り出す。
// 戻り値は呼び出し側で delete[]。取得失敗で 0。
static uint8_t *readLayerBGRA(tTJSVariant &layer, int &width, int &height) {
	if (!layer.AsObjectNoAddRef()->IsInstanceOf(0, 0, 0, TJS_W("Layer"), NULL)) {
		TVPThrowExceptionMessage(TJS_W("not layer"));
	}
	ncbPropAccessor obj(layer);
	width  = (int)obj.GetValue(TJS_W("imageWidth"),  ncbTypedefs::Tag<tjs_int>());
	height = (int)obj.GetValue(TJS_W("imageHeight"), ncbTypedefs::Tag<tjs_int>());
	if (width <= 0 || height <= 0) return 0;
	const uint8_t *src = (const uint8_t *)(tjs_intptr_t)
		obj.GetValue(TJS_W("mainImageBuffer"), ncbTypedefs::Tag<tjs_intptr_t>());
	tjs_int pitch = (tjs_int)obj.GetValue(TJS_W("mainImageBufferPitch"),
	                                      ncbTypedefs::Tag<tjs_int>());
	if (!src) return 0;
	uint8_t *buf = new uint8_t[(size_t)width * height * 4];
	for (int y = 0; y < height; y++)
		memcpy(buf + (size_t)y * width * 4,
		       src + (tjs_intptr_t)y * pitch, (size_t)width * 4);
	return buf;
}

// ncb.typeconv: cast: enum->int
NCB_TYPECONV_CAST_INTEGER(psd::LayerType);
NCB_TYPECONV_CAST_INTEGER(psd::BlendMode);

static int convBlendMode(psd::BlendMode mode)
{
	switch (mode) {
	case psd::BLEND_MODE_NORMAL:			// 'norm' = normal
		return ltPsNormal;
	case psd::BLEND_MODE_DARKEN:			// 'dark' = darken
		return ltPsDarken;
	case psd::BLEND_MODE_MULTIPLY:		// 'mul ' = multiply
		return ltPsMultiplicative;
	case psd::BLEND_MODE_COLOR_BURN:		// 'idiv' = color burn
		return ltPsColorBurn;
	case psd::BLEND_MODE_LINEAR_BURN:		// 'lbrn' = linear burn
		return ltPsSubtractive;
	case psd::BLEND_MODE_LIGHTEN:			// 'lite' = lighten
		return ltPsLighten;
	case psd::BLEND_MODE_SCREEN:			// 'scrn' = screen
		return ltPsScreen;
	case psd::BLEND_MODE_COLOR_DODGE:		// 'div ' = color dodge
		return ltPsColorDodge;
	case psd::BLEND_MODE_LINEAR_DODGE:	// 'lddg' = linear dodge
		return ltPsAdditive;
	case psd::BLEND_MODE_OVERLAY:			// 'over' = overlay
		return ltPsOverlay;
	case psd::BLEND_MODE_SOFT_LIGHT:		// 'sLit' = soft light
		return ltPsSoftLight;
	case psd::BLEND_MODE_HARD_LIGHT:		// 'hLit' = hard light
		return ltPsHardLight;
	case psd::BLEND_MODE_DIFFERENCE:		// 'diff' = difference
		return ltPsDifference;
	case psd::BLEND_MODE_EXCLUSION:		// 'smud' = exclusion
		return ltPsExclusion;
	case psd::BLEND_MODE_DISSOLVE:		// 'diss' = dissolve
	case psd::BLEND_MODE_VIVID_LIGHT:		// 'vLit' = vivid light
	case psd::BLEND_MODE_LINEAR_LIGHT:	// 'lLit' = linear light
	case psd::BLEND_MODE_PIN_LIGHT:		// 'pLit' = pin light
	case psd::BLEND_MODE_HARD_MIX:		// 'hMix' = hard mix
  case psd::BLEND_MODE_DARKER_COLOR:
  case psd::BLEND_MODE_LIGHTER_COLOR:
  case psd::BLEND_MODE_SUBTRACT:
  case psd::BLEND_MODE_DIVIDE:
		// not supported;
		break;
	}
	return ltPsNormal;
}

/**
 * C文字列処理用
 */
class NarrowString {
private:
	tjs_nchar *_data;
public:
	NarrowString(const ttstr &str) : _data(NULL) {
		tjs_int len = str.GetNarrowStrLen();
		if (len > 0) {
			_data = new tjs_nchar[len+1];
			str.ToNarrowStr(_data, len+1);
		}
	}
	~NarrowString() {
		delete[] _data;
	}

	const tjs_nchar *data() {
		return _data;
	}

	operator const char *() const
	{
		return (const char *)_data;
	}
};

/**
 * コンストラクタ
 */
PSD::PSD(iTJSDispatch2 *objthis)
	: objthis(objthis)
	, storageStarted(false)
{
};

/**
 * デストラクタ
 */
PSD::~PSD() {
	clearData(); // ここで呼ばないと delete 時には親のほうでは仮想関数がよばれない
};

/**
 * インスタンス生成ファクトリ
 */
tjs_error
PSD::factory(PSD **result, tjs_int numparams, tTJSVariant **params, iTJSDispatch2 *objthis)
{
	*result = new PSD(objthis);
	return S_OK;
}

/**
 * 生成時の自己オブジェクトを取得
 */
tTJSVariant
PSD::getSelf()
{
	return tTJSVariant(objthis, objthis);
}

/**
 * PSD画像のロード
 * @param filename ファイル名
 * @return ロードに成功したら true
 */
bool
PSD::load(ttstr filename)
{
	ttstr file = TVPGetPlacedPath(filename);
	if (!file.length()) return false;
	// 常に iTJSBinaryStream 経由でロード (ローカル/アーカイブ問わず)。
	// loadStream は StreamReader::Source ラッパ経由で psdparse の lazy
	// iterator に流すので、ファイル全体をメモリに読まない。
	loadStream(file);
	if (isLoaded) addToStorage(filename);
	return isLoaded;
}

void
PSD::clearData()
{
	removeFromStorage();
	layerIdIdxMap.clear();
	pathMap.clear();
	storageStarted = false;
	// PSDFile::clearData が channelImageData / imageData 等の iterator を
	// 破棄 → shared_ptr<Source> の refcount が落ちて Source が消える →
	// iTJSBinaryStream も自動解放される。
	psd::PSDFile::clearData();
}
	
/**
 * レイヤ番号が適切かどうか判定
 * @param no レイヤ番号
 */
void
PSD::checkLayerNo(int no)
{
	if (!isLoaded) {
		TVPThrowExceptionMessage(TJS_W("no data"));
	}
	if (no < 0 || no >= get_layer_count()) {
		TVPThrowExceptionMessage(TJS_W("not such layer"));
	}
}

/**
 * 名前の取得
 * @param layレイヤ情報
 */
ttstr
PSD::layname(psd::LayerInfo &lay)
{
	ttstr ret;
	if (!lay.layerNameUnicode.empty()) {
		ret = u16ToTjs(lay.layerNameUnicode);
	} else {
		ret = ttstr(lay.layerName.c_str());
	}
	return ret;
}

/**
 * レイヤ種別の取得
 * @param no レイヤ番号
 * @return レイヤ種別
 */
int
PSD::getLayerType(int no)
{
	checkLayerNo(no);
	return (int)layerList[no].layerType;
}

/**
 * レイヤ名称の取得
 * @param no レイヤ番号
 * @return レイヤ種別
 */
ttstr
PSD::getLayerName(int no)
{
	checkLayerNo(no);
	return layname(layerList[no]);
}

/**
 * レイヤ情報の取得
 * @param no レイヤ番号
 * @return レイヤ情報が格納された辞書
 */
tTJSVariant
PSD::getLayerInfo(int no)
{
	checkLayerNo(no);
	psd::LayerInfo &lay = layerList[no];
	tTJSVariant result;	
	ncbDictionaryAccessor dict;
	if (dict.IsValid()) {
#define SETPROP(dict, obj, prop) dict.SetValue(TJS_W(#prop), obj.prop)
		SETPROP(dict, lay, top);
		SETPROP(dict, lay, left);
		SETPROP(dict, lay, bottom);
		SETPROP(dict, lay, right);
		SETPROP(dict, lay, width);
		SETPROP(dict, lay, height);
		SETPROP(dict, lay, opacity);
		SETPROP(dict, lay, fill_opacity);
		bool mask = false;
		for (std::vector<psd::ChannelInfo>::iterator i = lay.channels.begin();
				 i != lay.channels.end();
				 i++) {
			if (i->isMaskChannel()) {
				mask = true;
				break;
			}
		}
		dict.SetValue(TJS_W("mask"), mask);
		// マスクパラメータ (density/feather)。 マスクを持ちパラメータブロックが
		// 実在するときのみ mask_params 辞書を設定する。
		{
			psd::LayerMask &lm = lay.extraData.layerMask;
			if (lm.present && lm.hasParameters) {
				ncbDictionaryAccessor mp;
				if (mp.IsValid()) {
					if (lm.userMaskDensity >= 0)
						mp.SetValue(TJS_W("user_density"), lm.userMaskDensity);
					if (lm.hasUserFeather)
						mp.SetValue(TJS_W("user_feather"), lm.userMaskFeather);
					if (lm.vectorMaskDensity >= 0)
						mp.SetValue(TJS_W("vector_density"), lm.vectorMaskDensity);
					if (lm.hasVectorFeather)
						mp.SetValue(TJS_W("vector_feather"), lm.vectorMaskFeather);
					dict.SetValue(TJS_W("mask_params"), mp.GetDispatch());
				}
			}
		}
		dict.SetValue(TJS_W("type"),       convBlendMode(lay.blendMode));
		dict.SetValue(TJS_W("layer_type"), lay.layerType);
		dict.SetValue(TJS_W("blend_mode"), lay.blendMode);
		dict.SetValue(TJS_W("visible"),    lay.isVisible());
		dict.SetValue(TJS_W("name"),       layname(lay));

		// additional information
		SETPROP(dict, lay, clipping);
		dict.SetValue(TJS_W("layer_id"), lay.layerId);
		dict.SetValue(TJS_W("obsolete"), lay.isObsolete());
		dict.SetValue(TJS_W("transparency_protected"), lay.isTransparencyProtected());
		dict.SetValue(TJS_W("pixel_data_irrelevant"),  lay.isPixelDataIrrelevant());

		// レイヤーカンプ
		if (lay.layerComps.size() > 0) {
			ncbDictionaryAccessor compDict;
			if (compDict.IsValid()) {
				for (std::map<int, psd::LayerCompInfo>::iterator it = lay.layerComps.begin();
						 it != lay.layerComps.end(); it++)	{
					ncbDictionaryAccessor tmp;
					if (tmp.IsValid()) {
						psd::LayerCompInfo &comp = it->second;
						tmp.SetValue(TJS_W("id"),         comp.id);
						tmp.SetValue(TJS_W("offset_x"),   comp.offsetX);
						tmp.SetValue(TJS_W("offset_y"),   comp.offsetY);
						tmp.SetValue(TJS_W("enable"),     comp.isEnabled);
						compDict.SetValue((tjs_int32)comp.id, tmp.GetDispatch());
					}
				}
				dict.SetValue(TJS_W("layer_comp"), compDict.GetDispatch());
			}
		}

		// SETPROP(dict, lay, adjustment_valid); // 調整レイヤーかどうか？レイヤタイプで判別可能
		// SETPROP(dict, lay, fill_opacity);
		// SETPROP(dict, lay, layer_name_id);
		// SETPROP(dict, lay, layer_version);
		// SETPROP(dict, lay, blend_clipped);
		// SETPROP(dict, lay, blend_interior);
		// SETPROP(dict, lay, knockout);
		// SETPROP(dict, lay, transparency); // lspf(protection)のもの
		// SETPROP(dict, lay, composite);
		// SETPROP(dict, lay, position_respectively);
		// SETPROP(dict, lay, sheet_color);
		// SETPROP(dict, lay, reference_point_x); // 塗りつぶしレイヤ（パターン）のオフセット
		// SETPROP(dict, lay, reference_point_y); // 塗りつぶしレイヤ（パターン）のオフセット
		// SETPROP(dict, lay, transparency_shapes_layer);
		// SETPROP(dict, lay, layer_mask_hides_effects);
		// SETPROP(dict, lay, vector_mask_hides_effects);
		// SETPROP(dict, lay, divider_type);
		// SETPROP(dict, lay, divider_blend_mode);

		// group layer はスクリプト側では layer_id 参照で引くようにする
		if (lay.parent != NULL)
			dict.SetValue(TJS_W("group_layer_id"), lay.parent->layerId);

		// テキストレイヤ情報 ('TySh' 由来)。layer_type==TEXT のとき present。
		// 非テキストレイヤでは "text" キー自体を設定しない (void 扱い)。
		if (lay.textData.present) {
			const psd::TextLayerData &td = lay.textData;
			ncbDictionaryAccessor tdict;
			if (tdict.IsValid()) {
				tdict.SetValue(TJS_W("text"),          u16ToTjs(td.text));
				tdict.SetValue(TJS_W("orientation"),   ttstr(td.orientation.c_str()));
				tdict.SetValue(TJS_W("justification"), td.justification);
				// アフィン変換 [xx, xy, yx, yy, tx, ty]
				ncbArrayAccessor tmat;
				if (tmat.IsValid()) {
					for (int i = 0; i < 6; i++)
						tmat.SetValue((tjs_int32)i, td.transform[i]);
					tdict.SetValue(TJS_W("transform"), tmat.GetDispatch());
				}
				// ラン単位スタイル
				ncbArrayAccessor truns;
				if (truns.IsValid()) {
					for (int i = 0; i < (int)td.runs.size(); i++) {
						const psd::TextStyleRun &run = td.runs[i];
						ncbDictionaryAccessor rdict;
						if (rdict.IsValid()) {
							rdict.SetValue(TJS_W("length"),       run.length);
							rdict.SetValue(TJS_W("font"),         u16ToTjs(run.font));
							rdict.SetValue(TJS_W("size_px"),      run.fontSize);
							// FillColor 未指定なら color は設定しない (void)
							if (run.hasColor) {
								ncbArrayAccessor rcol;
								if (rcol.IsValid()) {
									for (int c = 0; c < 4; c++)
										rcol.SetValue((tjs_int32)c, run.color[c]);
									rdict.SetValue(TJS_W("color"), rcol.GetDispatch());
								}
							}
							rdict.SetValue(TJS_W("tracking"),     run.tracking);
							rdict.SetValue(TJS_W("kerning"),      run.kerning);
							rdict.SetValue(TJS_W("auto_kerning"), run.autoKerning);
							// 疑似ボールド/イタリック/下線 (psdparse v0.9.0 で追加)
							rdict.SetValue(TJS_W("bold"),         run.bold);
							rdict.SetValue(TJS_W("italic"),       run.italic);
							rdict.SetValue(TJS_W("underline"),    run.underline);
							truns.SetValue((tjs_int32)i, rdict.GetDispatch());
						}
					}
					tdict.SetValue(TJS_W("runs"), truns.GetDispatch());
				}
				// 段落別行揃え (box text で段落ごとに揃えが変わるケース用)。
				if (!td.paragraphs.empty()) {
					ncbArrayAccessor tpars;
					if (tpars.IsValid()) {
						for (int i = 0; i < (int)td.paragraphs.size(); i++) {
							const psd::TextParagraph &par = td.paragraphs[i];
							ncbDictionaryAccessor pdict;
							if (pdict.IsValid()) {
								pdict.SetValue(TJS_W("length"),        par.length);
								pdict.SetValue(TJS_W("justification"), par.justification);
								tpars.SetValue((tjs_int32)i, pdict.GetDispatch());
							}
						}
						tdict.SetValue(TJS_W("paragraphs"), tpars.GetDispatch());
					}
				}
				dict.SetValue(TJS_W("text"), tdict.GetDispatch());
			}
		}

		result = dict;
	}

	return result;
}

/**
 * レイヤデータの読み出し(内部処理)
 * @param layer 読み出し先レイヤ
 * @param no レイヤ番号
 * @param imageMode イメージモード
 */
void
PSD::_getLayerData(tTJSVariant layer, int no, psd::ImageMode imageMode)
{
	if (!layer.AsObjectNoAddRef()->IsInstanceOf(0, 0, 0, TJS_W("Layer"), NULL)) {
		TVPThrowExceptionMessage(TJS_W("not layer"));
	}
	checkLayerNo(no);

	psd::LayerInfo &lay = layerList[no];
	psd::LayerMask &mask  = lay.extraData.layerMask;

	if (lay.layerType != psd::LAYER_TYPE_NORMAL
			&& ! (lay.layerType == psd::LAYER_TYPE_FOLDER
						&& imageMode == psd::IMAGE_MODE_MASK)) {
		TVPThrowExceptionMessage(TJS_W("invalid layer type"));
	}

	int left, top, width, height, opacity, fill_opacity, type;

	bool dummyMask = false;
	if (imageMode == psd::IMAGE_MODE_MASK) {
		left = mask.left;
		top = mask.top;
		width = mask.width;
		height = mask.height;
		opacity = 255;
                fill_opacity = 255;
		type = ltPsNormal;
		if (width == 0 || height == 0) {
			left = top = 0;
			width = height = 1;
			dummyMask = true;
		}
	} else {
		left = lay.left;
		top = lay.top;
		width = lay.width;
		height = lay.height;
		opacity = lay.opacity;
                fill_opacity = lay.fill_opacity;
		type = convBlendMode(lay.blendMode);
	}
	if (width <= 0 || height <= 0) {
		// サイズ０のレイヤはロードできない
		return;
	}

	ncbPropAccessor obj(layer);
	obj.SetValue(TJS_W("left"), left);
	obj.SetValue(TJS_W("top"), top);
	obj.SetValue(TJS_W("opacity"), opacity);
	obj.SetValue(TJS_W("fill_opacity"), fill_opacity);
	obj.SetValue(TJS_W("width"),  width);
	obj.SetValue(TJS_W("height"), height);
	obj.SetValue(TJS_W("type"),   type);
	obj.SetValue(TJS_W("visible"), lay.isVisible());
	obj.SetValue(TJS_W("imageLeft"),  0);
	obj.SetValue(TJS_W("imageTop"),   0);
	obj.SetValue(TJS_W("imageWidth"),  width);
	obj.SetValue(TJS_W("imageHeight"), height);
	obj.SetValue(TJS_W("name"), layname(lay));

	if (imageMode == psd::IMAGE_MODE_MASK)
		obj.SetValue(TJS_W("defaultMaskColor"), mask.defaultColor);

	// 画像データのコピー
	unsigned char *buffer = (unsigned char*)obj.GetValue(TJS_W("mainImageBufferForWrite"), ncbTypedefs::Tag<tjs_intptr_t>());
	int pitch = obj.GetValue(TJS_W("mainImageBufferPitch"), ncbTypedefs::Tag<tjs_int>());
	if (dummyMask) {
		buffer[0] = buffer[1] = buffer[2] = mask.defaultColor;
		buffer[3] = 255;
	} else {
		getLayerImage(lay, buffer, psd::BGRA_LE, pitch, imageMode);
	}
}


/**
 * レイヤデータの読み出し
 * @param layer 読み出し先レイヤ
 * @param no レイヤ番号
 */
void
PSD::getLayerData(tTJSVariant layer, int no)
{
	_getLayerData(layer, no, psd::IMAGE_MODE_MASKEDIMAGE);
}

/**
 * レイヤデータの読み出し(生イメージ)
 * @param layer 読み出し先レイヤ
 * @param no レイヤ番号
 */
void
PSD::getLayerDataRaw(tTJSVariant layer, int no)
{
	_getLayerData(layer, no, psd::IMAGE_MODE_IMAGE);
}

/**
 * レイヤデータの読み出し(マスクのみ)
 * @param layer 読み出し先レイヤ
 * @param no レイヤ番号
 */
void
PSD::getLayerDataMask(tTJSVariant layer, int no)
{
	_getLayerData(layer, no, psd::IMAGE_MODE_MASK);
}

/**
 * スライスデータの読み出し
 * @return スライス情報辞書 %[ top, left, bottom, right, slices:[ %[ id, group_id, left, top, bottom, right ], ... ] ]
 *         スライス情報がない場合は void を返す
 */
tTJSVariant
PSD::getSlices()
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	tTJSVariant result;
	ncbDictionaryAccessor dict;
	ncbArrayAccessor arr;
	if (slice.isEnabled) {
		if (dict.IsValid()) {
			psd::SliceResource &sr = slice;
			dict.SetValue(TJS_W("top"),    sr.boundingTop);
			dict.SetValue(TJS_W("left"),   sr.boundingLeft);
			dict.SetValue(TJS_W("bottom"), sr.boundingBottom);
			dict.SetValue(TJS_W("right"),  sr.boundingRight);
			dict.SetValue(TJS_W("name"),   u16ToTjs(sr.groupName));
			if (arr.IsValid()) {
				for (int i = 0; i < (int)sr.slices.size(); i++) {
					ncbDictionaryAccessor tmp;
					if (tmp.IsValid()) {
						psd::SliceItem &item = sr.slices[i];
						tmp.SetValue(TJS_W("id"),      	item.id);
						tmp.SetValue(TJS_W("group_id"), item.groupId);
						tmp.SetValue(TJS_W("origin"),   item.origin);
						tmp.SetValue(TJS_W("type"),     item.type);
						tmp.SetValue(TJS_W("left"),     item.left);
						tmp.SetValue(TJS_W("top"),      item.top);
						tmp.SetValue(TJS_W("right"),    item.right);
						tmp.SetValue(TJS_W("bottom"),   item.bottom);
						tmp.SetValue(TJS_W("color"),    ((item.colorA<<24) | (item.colorR<<16) | (item.colorG<<8) | item.colorB));
						tmp.SetValue(TJS_W("cell_text_is_html"),    item.isCellTextHtml);
						tmp.SetValue(TJS_W("horizontal_alignment"), item.horizontalAlign);
						tmp.SetValue(TJS_W("vertical_alignment"),   item.verticalAlign);
						tmp.SetValue(TJS_W("associated_layer_id"),	item.associatedLayerId);
						tmp.SetValue(TJS_W("name"),      u16ToTjs(item.name));
						tmp.SetValue(TJS_W("url"),       u16ToTjs(item.url));
						tmp.SetValue(TJS_W("target"),    u16ToTjs(item.target));
						tmp.SetValue(TJS_W("message"),   u16ToTjs(item.message));
						tmp.SetValue(TJS_W("alt_tag"),   u16ToTjs(item.altTag));
						tmp.SetValue(TJS_W("cell_text"), u16ToTjs(item.cellText));
						arr.SetValue((tjs_int32)i, tmp.GetDispatch());
					}
				}
				dict.SetValue(TJS_W("slices"), arr.GetDispatch());
			}
			result = dict;
		}
	}
	return result;
}

/**
 * ガイドデータの読み出し
 * @return ガイド情報辞書 %[ vertical:[ x1, x2, ... ], horizontal:[ y1, y2, ... ] ]
 *         ガイド情報がない場合は void を返す
 */
tTJSVariant
PSD::getGuides()
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	tTJSVariant result;
	ncbDictionaryAccessor dict;
	ncbArrayAccessor vert, horz;
	if (gridGuide.isEnabled) {
		psd::GridGuideResource gg = gridGuide;
		if (dict.IsValid() && vert.IsValid() && horz.IsValid()) {
			dict.SetValue(TJS_W("horz_grid"),  gg.horizontalGrid);
			dict.SetValue(TJS_W("vert_grid"),  gg.verticalGrid);
			dict.SetValue(TJS_W("vertical"),   vert.GetDispatch());
			dict.SetValue(TJS_W("horizontal"), horz.GetDispatch());
			for (int i = 0, v = 0, h = 0; i < (int)gg.guides.size(); i++) {
				if (gg.guides[i].direction == 0) {
					vert.SetValue(v++, gg.guides[i].location);
				} else {
					horz.SetValue(h++, gg.guides[i].location);
				}
			}
			result = dict;
		}
	}
	return result;
}

/**
 * 合成結果の取得。取得領域は画像全体サイズ内におさまってる必要があります
 * 注意：PSDファイル自体に合成済み画像が存在しない場合は取得に失敗します
 *
 * @param layer 格納先レイヤ(width,heightサイズに調整される)
 * @return 取得に成功したら true
 */
bool
PSD::getBlend(tTJSVariant layer) {
	if (!layer.AsObjectNoAddRef()->IsInstanceOf(0, 0, 0, TJS_W("Layer"), NULL)) {
		TVPThrowExceptionMessage(TJS_W("not layer"));
	}

	// 合成結果を生成
	if (imageData) {

		// 格納先を調整
		ncbPropAccessor obj(layer);
		obj.SetValue(TJS_W("width"),  get_width());
		obj.SetValue(TJS_W("height"), get_height());
		obj.SetValue(TJS_W("imageLeft"),  0);
		obj.SetValue(TJS_W("imageTop"),   0);
		obj.SetValue(TJS_W("imageWidth"),  get_width());
		obj.SetValue(TJS_W("imageHeight"), get_height());

		// 画像データのコピー
		unsigned char *buffer = (unsigned char*)obj.GetValue(TJS_W("mainImageBufferForWrite"), ncbTypedefs::Tag<tjs_intptr_t>());
		int pitch = obj.GetValue(TJS_W("mainImageBufferPitch"), ncbTypedefs::Tag<tjs_int>());
		getMergedImage(buffer, psd::BGRA_LE, pitch);

		return true;
	}

	return false;
}

/**
 * レイヤーカンプ
 */
tTJSVariant
PSD::getLayerComp()
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	tTJSVariant result;
	ncbDictionaryAccessor dict;
	ncbArrayAccessor arr;
	int compNum = layerComps.size();
	if (compNum > 0) {
		if (dict.IsValid()) {
			dict.SetValue(TJS_W("last_applied_id"), lastAppliedCompId);
			if (arr.IsValid()) {
				for (int i = 0; i < compNum; i++) {
					ncbDictionaryAccessor tmp;
					if (tmp.IsValid()) {
						psd::LayerComp &comp = layerComps[i];
						tmp.SetValue(TJS_W("id"),      	        comp.id);
						tmp.SetValue(TJS_W("record_visibility"), comp.isRecordVisibility);
						tmp.SetValue(TJS_W("record_position"),   comp.isRecordPosition);
						tmp.SetValue(TJS_W("record_appearance"), comp.isRecordAppearance);
						tmp.SetValue(TJS_W("name"),             u16ToTjs(comp.name));
						tmp.SetValue(TJS_W("comment"),          u16ToTjs(comp.comment));
						arr.SetValue((tjs_int32)i,        tmp.GetDispatch());
					}
				}
				dict.SetValue(TJS_W("comps"), arr.GetDispatch());
			}
			result = dict;
		}
	}
	return result;
}

// レイヤ名を返す
ttstr
PSD::path_layname(psd::LayerInfo &lay)
{
	ttstr ret = layname(lay);
	// 正規化
	ttstr from = "/";
	ttstr to   = "_";
	ret.Replace(from, to, true);
	ret.ToLowerCase();
	return ret;
}

// レイヤのパス名を返す
ttstr
PSD::pathname(psd::LayerInfo &lay)
{
	ttstr name = "";
	psd::LayerInfo *p = lay.parent;
	while (p) {
		name = path_layname(*p) + "/" + name;
		p = p->parent;
	}
	return ttstr("root/") + name;
}

// ストレージ処理用データの初期化
void
PSD::startStorage()
{
	if (!storageStarted) {
		storageStarted = true;
		// レイヤ検索用の情報を生成
		int count = (int)layerList.size();
		for (int i=count-1;i>=0;i--) {
			psd::LayerInfo &lay = layerList[i];
			if (lay.layerType == psd::LAYER_TYPE_NORMAL) {
				pathMap[pathname(lay)][path_layname(lay)] = i;
				layerIdIdxMap[lay.layerId] = i;
			}
		}
	}
}

bool
checkAllNum(const tjs_char *p)
{
	while (*p != '\0') {
		if (!(*p >= '0' && *p <= '9')) {
			return false;
		}
		p++;
	}
	return true;
}
	
/*
 * 指定した名前のレイヤの存在チェック
 * @param name パスを含むレイヤ名
 * @param layerIdxRet レイヤインデックス番号を返す
 */
bool
PSD::CheckExistentStorage(const ttstr &filename, int *layerIdxRet)
{
	startStorage();

	// ルート部を取得
	const tjs_char *p = filename.c_str();

	// id指定の場合
	if (TJS_strncmp(p, TJS_W("id/"), 3) == 0) {

		p += 3;

		// 拡張子を除去して判定
		const tjs_char *q;
		if (!(q = TJS_strrchr(p, '/')) && ((q = TJS_strchr(p, '.')) && (TJS_strcmp(q, BMPEXT) == 0))) {
			ttstr name = ttstr(p, q-p);
			q = name.c_str();
			if (checkAllNum(q)) { // 文字混入禁止
				int id = TJS_atoi(q);
				LayerIdIdxMap::const_iterator n = layerIdIdxMap.find(id);
				if (n != layerIdIdxMap.end()) {
					if (layerIdxRet) *layerIdxRet = n->second;
					return true;
				}
			}
		}

	} else {

		// パスを分離
		ttstr pname, fname;
		// 最後の/を探す
		const tjs_char *q;
		if ((q = TJS_strrchr(p, '/'))) {
			pname = ttstr(p, q-p+1);
			fname = ttstr(q+1);
		} else {
			return false;
		}

		// 拡張子分離
		ttstr basename;
		p = fname.c_str();
		// 最初の . を探す
		if ((q = TJS_strchr(p, '.')) && (TJS_strcmp(q, BMPEXT) == 0)) {
			basename = ttstr(p, q-p);
		} else {
			return false;
		}

		// 名前を探す
		PathMap::const_iterator n = pathMap.find(pname);
		if (n != pathMap.end()) {
			const NameIdxMap &names = n->second;
			NameIdxMap::const_iterator m = names.find(basename);
			if (m != names.end()) {
				if (layerIdxRet) *layerIdxRet = m->second;
				return true;
			}
		}
	}

	return false;
}

/*
 * 指定したパスにあるファイル名一覧の取得
 * @param pathname パス名
 * @param lister リスト取得用インターフェース
 */
void
PSD::GetListAt(const ttstr &pathname, iTVPStorageLister *lister)
{
	startStorage();

	// ID一覧から名前を生成
	if (pathname == "id/") {
		LayerIdIdxMap::const_iterator it = layerIdIdxMap.begin();
		while (it != layerIdIdxMap.end()) {
			ttstr name = ttstr(it->first);
			lister->Add(name + BMPEXT);
			it++;
		}
		return;
	}

	// パス登録情報から名前を生成
	PathMap::const_iterator n = pathMap.find(pathname);
	if (n != pathMap.end()) {
		const NameIdxMap &names = n->second;
		NameIdxMap::const_iterator it = names.begin();
		while (it != names.end()) {
			ttstr name = it->first;
			lister->Add(name + BMPEXT);
			it++;
		}
	}
}

/*
 * メモリ上のBMPデータを返す iTJSBinaryStream 実装
 */
namespace {
class PSDMemoryStream : public iTJSBinaryStream {
	unsigned char *buf;
	tjs_uint64 size;
	tjs_uint64 pos;
public:
	PSDMemoryStream(unsigned char *buf, tjs_uint64 size) : buf(buf), size(size), pos(0) {}
	virtual ~PSDMemoryStream() { delete[] buf; }

	virtual tjs_uint64 TJS_INTF_METHOD Seek(tjs_int64 offset, tjs_int whence) {
		tjs_int64 newpos;
		switch (whence) {
		case TJS_BS_SEEK_SET: newpos = offset; break;
		case TJS_BS_SEEK_CUR: newpos = (tjs_int64)pos + offset; break;
		case TJS_BS_SEEK_END: newpos = (tjs_int64)size + offset; break;
		default: return pos;
		}
		if (newpos < 0) newpos = 0;
		if ((tjs_uint64)newpos > size) newpos = (tjs_int64)size;
		pos = (tjs_uint64)newpos;
		return pos;
	}
	virtual tjs_uint TJS_INTF_METHOD Read(void *buffer, tjs_uint read_size) {
		tjs_uint64 avail = size - pos;
		if ((tjs_uint64)read_size > avail) read_size = (tjs_uint)avail;
		if (read_size) memcpy(buffer, buf + pos, read_size);
		pos += read_size;
		return read_size;
	}
	virtual tjs_uint TJS_INTF_METHOD Write(const void *, tjs_uint) { return 0; }
	virtual void TJS_INTF_METHOD SetEndOfStorage() {}
	virtual tjs_uint64 TJS_INTF_METHOD GetSize() { return size; }
};
}

#if !defined(_WIN32)
typedef struct tagBITMAPFILEHEADER
{
	tjs_uint16	bfType;
	tjs_uint32	bfSize;
	tjs_uint16	bfReserved1;
	tjs_uint16	bfReserved2;
	tjs_uint32	bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;
#endif

#ifndef BI_RGB
	#define BI_RGB			0
	#define BI_RLE8			1
	#define BI_RLE4			2
	#define BI_BITFIELDS	3
#endif



/*
 * 指定した名前のレイヤの画像ファイルをストリームで返す
 * @param name パスを含むレイヤ名
 * @return ファイルストリーム
 */
iTJSBinaryStream *
PSD::openLayerImage(const ttstr &name)
{
	static int n=0;

	int layerIdx;
	if (CheckExistentStorage(name, &layerIdx)) {
		if (layerIdx < (int)layerList.size()) {
			psd::LayerInfo &lay = layerList[layerIdx];

			if (lay.layerType != psd::LAYER_TYPE_NORMAL || lay.width <= 0 || lay.height <= 0) {
				return 0;
			}
			int width  = lay.width;
			int height = lay.height;
			int pitch  = width*4;

			int hsize = sizeof(BITMAPFILEHEADER);
			int isize = hsize + sizeof(BITMAPINFOHEADER);
			int size  = isize  + pitch * height;

			// メモリ上にBMP画像を作成してストリームとして返す
			unsigned char *p = new unsigned char[size];
			if (p) {
				BITMAPFILEHEADER bfh;
				bfh.bfType      = 'B' + ('M' << 8);
				bfh.bfSize      = size;
				bfh.bfReserved1 = 0;
				bfh.bfReserved2 = 0;
				bfh.bfOffBits   = isize;
				memcpy(p,        &bfh, sizeof bfh);

				BITMAPINFOHEADER bih;
				bih.biSize = sizeof(bih);
				bih.biWidth = width;
				bih.biHeight = height;
				bih.biPlanes = 1;
				bih.biBitCount = 32;
				bih.biCompression = BI_RGB;
				bih.biSizeImage = 0;
				bih.biXPelsPerMeter = 0;
				bih.biYPelsPerMeter = 0;
				bih.biClrUsed = 0;
				bih.biClrImportant = 0;
				memcpy(p + hsize, &bih, sizeof bih);
				getLayerImage(lay, p + isize + pitch * (height - 1), psd::BGRA_LE, -pitch, psd::IMAGE_MODE_MASKEDIMAGE);

				return new PSDMemoryStream(p, (tjs_uint64)size);
			}
		}
	}
	return 0;
}

/**
 * LayerIDが未設定のレイヤに対してID番号を自動割り付け(base_id+1からlayer_no順に)
 * @param base_id 割り付けID最小番号-1(※既存のいずれかのレイヤIDがこれより大きかったらその値が利用される)
 * @return IDを設定したレイヤの枚数
 */
int PSD::assignAutoIds(int base_id)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	size_t count = layerList.size();
	int min_id = -1;
	typedef std::vector<psd::LayerInfo> LayerVec;
	for (LayerVec::const_iterator it = layerList.begin(); it != layerList.end(); ++it) {
		if (min_id < it->layerId) min_id = it->layerId;
	}
	if (base_id < min_id) base_id = min_id;

	int assigned = 0;
	for (LayerVec::iterator it = layerList.begin(); it != layerList.end(); ++it) {
		if (it->layerId == -1) {
			it->layerId = ++base_id;
			++assigned;
		}
	}
	return assigned;
}
static tjs_error AssignAutoIds(tTJSVariant *r, tjs_int numparams, tTJSVariant **params, PSD *instance) {
	if (!instance) return TJS_E_NATIVECLASSCRASH;
	int base_id = numparams > 0 ? (tjs_int)*params[0] : 0;
	int cnt = instance->assignAutoIds(base_id);
	if (r) *r = (tjs_int)cnt;
	return TJS_S_OK;
}

// addLayer(name, left, top, layer, blendMode=blend_mode_normal, opacity=255,
//          destIndex=-1)。末尾 3 引数を optional にするため RawCallback で公開。
static tjs_error AddLayer(tTJSVariant *r, tjs_int numparams, tTJSVariant **params, PSD *instance) {
	if (!instance) return TJS_E_NATIVECLASSCRASH;
	if (numparams < 4) return TJS_E_BADPARAMCOUNT;
	ttstr name       = *params[0];
	int   left       = (tjs_int)*params[1];
	int   top        = (tjs_int)*params[2];
	tTJSVariant layer = *params[3];
	int   blendMode  = numparams > 4 ? (tjs_int)*params[4] : (int)psd::BLEND_MODE_NORMAL;
	int   opacity    = numparams > 5 ? (tjs_int)*params[5] : 255;
	int   destIndex  = numparams > 6 ? (tjs_int)*params[6] : -1;
	int idx = instance->addLayer(name, left, top, layer, blendMode, opacity, destIndex);
	if (r) *r = (tjs_int)idx;
	return TJS_S_OK;
}

// ============================================================================
// 編集系 API 実装
// ============================================================================

// レイヤ構造を変える編集の後で psd:// ストレージのレイヤ検索キャッシュを破棄する。
// (indices/名前が変わるため。 次回アクセス時に startStorage() で再構築される)
void
PSD::invalidateStorageCache()
{
	layerIdIdxMap.clear();
	pathMap.clear();
	storageStarted = false;
}

bool
PSD::save(ttstr filename)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	// ローカルパスへ変換 (file:// 等の storage 名にも対応)。fopen ベースなので
	// OS ロケールの narrow 名で渡す。TVPGetLocalName は in-place で書き換える。
	ttstr local = filename;
	TVPGetLocalName(local);
	NarrowString path(local);
	return psd::PSDFile::save((const char *)path);
}

bool
PSD::createBlank(int width, int height)
{
	removeFromStorage();
	invalidateStorageCache();
	return psd::PSDFile::createBlank(width, height);
}

bool
PSD::deleteLayer(int index)
{
	bool r = psd::PSDFile::deleteLayer(index);
	if (r) invalidateStorageCache();
	return r;
}

bool
PSD::moveLayer(int from, int to)
{
	bool r = psd::PSDFile::moveLayer(from, to);
	if (r) invalidateStorageCache();
	return r;
}

tTJSVariant
PSD::groupSpan(int index)
{
	tTJSVariant result;
	int start = index, count = 1;
	if (!psd::PSDFile::groupSpan(index, start, count)) return result;  // 範囲外は void
	ncbDictionaryAccessor dict;
	if (dict.IsValid()) {
		dict.SetValue(TJS_W("start"), start);
		dict.SetValue(TJS_W("count"), count);
		result = dict;
	}
	return result;
}

int
PSD::moveLayerSibling(int index, bool up)
{
	int newIndex = index;
	if (!psd::PSDFile::moveLayerSibling(index, up, &newIndex)) return -1;
	invalidateStorageCache();
	return newIndex;
}

bool
PSD::moveLayerRange(int from, int count, int to)
{
	bool r = psd::PSDFile::moveLayerRange(from, count, to);
	if (r) invalidateStorageCache();
	return r;
}

int
PSD::duplicateLayer(int index)
{
	int r = psd::PSDFile::duplicateLayer(index);
	if (r >= 0) invalidateStorageCache();
	return r;
}

int
PSD::copyLayerFrom(tTJSVariant src, int srcIndex, int destIndex)
{
	iTJSDispatch2 *obj = src.AsObjectNoAddRef();
	PSD *psrc = ncbInstanceAdaptor<PSD>::GetNativeInstance(obj);
	if (!psrc) TVPThrowExceptionMessage(TJS_W("not a PSD instance"));
	int r = psd::PSDFile::copyLayerFrom(*psrc, srcIndex, destIndex);
	if (r >= 0) invalidateStorageCache();
	return r;
}

bool
PSD::setLayerName(int index, ttstr name)
{
	std::string u8 = tjsToUtf8(name);
	bool r = psd::PSDFile::setLayerName(index, u8.c_str());
	if (r) invalidateStorageCache();  // path 名が変わる
	return r;
}

bool PSD::setFillOpacity(int index, int opacity)   { return psd::PSDFile::setFillOpacity(index, opacity); }
bool PSD::setMaskDisabled(int index, bool disabled){ return psd::PSDFile::setMaskDisabled(index, disabled); }
bool PSD::setMaskDensity(int index, int density)   { return psd::PSDFile::setMaskDensity(index, density); }
bool PSD::setMaskFeather(int index, double feather){ return psd::PSDFile::setMaskFeather(index, feather); }
bool PSD::setMaskDefaultColor(int index, int color){ return psd::PSDFile::setMaskDefaultColor(index, color); }

bool
PSD::setLayerPixels(int index, tTJSVariant layer)
{
	int w, h;
	uint8_t *bgra = readLayerBGRA(layer, w, h);
	if (!bgra) return false;
	bool r = psd::PSDFile::setLayerPixels(index, bgra, w, h);
	delete[] bgra;
	if (r) invalidateStorageCache();
	return r;
}

bool
PSD::setLayerMaskPixels(int index, tTJSVariant layer, int top, int left)
{
	int w, h;
	uint8_t *bgra = readLayerBGRA(layer, w, h);
	if (!bgra) return false;
	// BGRA から B 成分 (getLayerDataMask が b=g=r=mask で返すのと対応) を抜いて
	// グレースケール化。
	std::vector<uint8_t> gray((size_t)w * h);
	for (size_t i = 0; i < gray.size(); i++) gray[i] = bgra[i * 4];
	delete[] bgra;
	return psd::PSDFile::setLayerMaskPixels(index, gray.data(), top, left, w, h);
}

int
PSD::addLayer(ttstr name, int left, int top, tTJSVariant layer,
              int blendMode, int opacity, int destIndex)
{
	int w, h;
	uint8_t *bgra = readLayerBGRA(layer, w, h);
	if (!bgra) return -1;
	std::string u8 = tjsToUtf8(name);
	int r = psd::PSDFile::addLayer(u8.c_str(), left, top, bgra, w, h,
	                               blendModeToKey(blendMode), opacity, destIndex);
	delete[] bgra;
	if (r >= 0) invalidateStorageCache();
	return r;
}

bool
PSD::setMergedImage(tTJSVariant layer)
{
	int w, h;
	uint8_t *bgra = readLayerBGRA(layer, w, h);
	if (!bgra) return false;
	bool r = psd::PSDFile::setMergedImage(bgra, w, h);
	delete[] bgra;
	return r;
}

// --- テキストレイヤ編集 -----------------------------------------------------
// psdparse v0.9.0 でテキスト編集一式が C++ ライブラリ側 (psd::PSDFile::setLayerText
// 等) へ上がったので、吉里吉里バインドは引数変換とエラーの例外化だけを行う薄い層に
// なっている (v0.8 までは python/psdparse_module.cpp の glue を移植して持っていた)。

// psdparse 側の失敗理由 (ASCII) を吉里吉里例外にして投げる。
static void throwTextError(const std::string &err)
{
	ttstr msg = err.empty() ? ttstr(TJS_W("text layer edit failed"))
	                        : ttstr(err.c_str());
	TVPThrowExceptionMessage(msg.c_str());
}

// スタイル辞書 (font/size_px/color/tracking/kerning/bold/italic/underline) を
// RunStyleEdit へ読み取る。存在するキーだけ has* を立てて上書き対象にする。
static void readRunStyleEdit(tTJSVariant style, psd::RunStyleEdit &edit)
{
	if (style.Type() != tvtObject) return;
	ncbPropAccessor s(style);
	if (s.HasValue(TJS_W("font"))) {
		edit.hasFont = true;
		edit.font = tjsToUtf8(s.getStrValue(TJS_W("font")));
	}
	if (s.HasValue(TJS_W("size_px"))) {
		edit.hasSize = true;
		edit.size = (double)s.getRealValue(TJS_W("size_px"));
	}
	if (s.HasValue(TJS_W("tracking"))) {
		edit.hasTracking = true;
		edit.tracking = (int)s.getIntValue(TJS_W("tracking"));
	}
	if (s.HasValue(TJS_W("kerning"))) {
		edit.hasKerning = true;
		edit.kerning = (int)s.getIntValue(TJS_W("kerning"));
	}
	if (s.HasValue(TJS_W("bold"))) {
		edit.hasBold = true;
		edit.bold = s.getIntValue(TJS_W("bold")) != 0;
	}
	if (s.HasValue(TJS_W("italic"))) {
		edit.hasItalic = true;
		edit.italic = s.getIntValue(TJS_W("italic")) != 0;
	}
	if (s.HasValue(TJS_W("underline"))) {
		edit.hasUnderline = true;
		edit.underline = s.getIntValue(TJS_W("underline")) != 0;
	}
	if (s.HasValue(TJS_W("color"))) {
		tTJSVariant cv = s.GetValue(TJS_W("color"), ncbTypedefs::Tag<tTJSVariant>());
		if (cv.Type() == tvtObject) {
			ncbPropAccessor col(cv);
			// [r, g, b] の 3 要素指定も許す (a は 1.0 のまま)
			int n = (int)col.GetArrayCount();
			if (n > 4) n = 4;
			if (n > 0) {
				edit.hasColor = true;
				for (int c = 0; c < n; c++)
					edit.color[c] = (float)col.getRealValue((tjs_int)c);
			}
		}
	}
}

void
PSD::setLayerText(int index, ttstr text)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	std::string err;
	if (!psd::PSDFile::setLayerText(index, tjsToU16(text), &err)) throwTextError(err);
}

void
PSD::setLayerRunStyle(int index, int runIndex, tTJSVariant style)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	psd::RunStyleEdit edit;
	readRunStyleEdit(style, edit);
	std::string err;
	if (!psd::PSDFile::setLayerRunStyle(index, runIndex, edit, &err)) throwTextError(err);
}

void
PSD::setLayerRichText(int index, ttstr text, tTJSVariant runs, tTJSVariant paragraphs)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	// runs: %[ length:, <style keys> ] の配列。void / 空なら単一ランへ畳まれる。
	std::vector<psd::TextRunSpec> runSpecs;
	if (runs.Type() == tvtObject) {
		ncbPropAccessor ra(runs);
		int n = (int)ra.GetArrayCount();
		for (int i = 0; i < n; i++) {
			tTJSVariant rv = ra.GetValue((tjs_int)i, ncbTypedefs::Tag<tTJSVariant>());
			if (rv.Type() != tvtObject) continue;
			ncbPropAccessor r(rv);
			psd::TextRunSpec spec;
			spec.length = (int)r.getIntValue(TJS_W("length"));
			readRunStyleEdit(rv, spec.style);
			runSpecs.push_back(spec);
		}
	}
	// paragraphs: %[ length:, justification: ] の配列。
	std::vector<psd::TextParagraphSpec> parSpecs;
	if (paragraphs.Type() == tvtObject) {
		ncbPropAccessor pa(paragraphs);
		int n = (int)pa.GetArrayCount();
		for (int i = 0; i < n; i++) {
			tTJSVariant pv = pa.GetValue((tjs_int)i, ncbTypedefs::Tag<tTJSVariant>());
			if (pv.Type() != tvtObject) continue;
			ncbPropAccessor p(pv);
			psd::TextParagraphSpec spec;
			spec.length = (int)p.getIntValue(TJS_W("length"));
			if (p.HasValue(TJS_W("justification"))) {
				spec.hasJustification = true;
				spec.justification = (int)p.getIntValue(TJS_W("justification"));
			}
			parSpecs.push_back(spec);
		}
	}
	std::string err;
	if (!psd::PSDFile::setLayerRichText(index, tjsToU16(text), runSpecs, parSpecs, &err))
		throwTextError(err);
}

void
PSD::setLayerJustification(int index, int justification, int paraIndex)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	std::string err;
	if (!psd::PSDFile::setLayerJustification(index, paraIndex, justification, &err))
		throwTextError(err);
}

tTJSVariant
PSD::getLayerFonts(int index)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	std::vector<std::string> names;  // UTF-8
	std::string err;
	if (!psd::PSDFile::getLayerFonts(index, names, &err)) throwTextError(err);
	tTJSVariant result;
	ncbArrayAccessor arr;
	if (arr.IsValid()) {
		for (int i = 0; i < (int)names.size(); i++)
			arr.SetValue((tjs_int32)i, u16ToTjs(psd::utf8ToU16(names[i])));
		result = arr;
	}
	return result;
}

tTJSVariant
PSD::getLayerTextTransform(int index)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	double m[6];
	std::string err;
	if (!psd::PSDFile::getLayerTextTransform(index, m, &err)) throwTextError(err);
	tTJSVariant result;
	ncbArrayAccessor arr;
	if (arr.IsValid()) {
		for (int i = 0; i < 6; i++) arr.SetValue((tjs_int32)i, m[i]);
		result = arr;
	}
	return result;
}

void
PSD::setLayerTextTransform(int index, tTJSVariant matrix)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	if (matrix.Type() != tvtObject)
		TVPThrowExceptionMessage(TJS_W("transform must be an array of 6 numbers"));
	ncbPropAccessor ma(matrix);
	if ((int)ma.GetArrayCount() < 6)
		TVPThrowExceptionMessage(TJS_W("transform must be an array of 6 numbers"));
	double m[6];
	for (int i = 0; i < 6; i++) m[i] = (double)ma.getRealValue((tjs_int)i);
	std::string err;
	if (!psd::PSDFile::setLayerTextTransform(index, m, &err)) throwTextError(err);
}

void
PSD::moveTextLayer(int index, double dx, double dy)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	std::string err;
	if (!psd::PSDFile::moveTextLayer(index, dx, dy, &err)) throwTextError(err);
}

tTJSVariant
PSD::getLayerTextBounds(int index)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	double l, t, r, b;
	std::string err;
	if (!psd::PSDFile::getLayerTextBounds(index, l, t, r, b, &err)) throwTextError(err);
	tTJSVariant result;
	ncbDictionaryAccessor dict;
	if (dict.IsValid()) {
		dict.SetValue(TJS_W("left"),   l);
		dict.SetValue(TJS_W("top"),    t);
		dict.SetValue(TJS_W("right"),  r);
		dict.SetValue(TJS_W("bottom"), b);
		result = dict;
	}
	return result;
}

void
PSD::setLayerTextBounds(int index, double left, double top, double right, double bottom)
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	std::string err;
	if (!psd::PSDFile::setLayerTextBounds(index, left, top, right, bottom, &err))
		throwTextError(err);
}

// setLayerRichText(index, text, runs=void, paragraphs=void) /
// setLayerJustification(index, justification, paraIndex=-1) /
// moveLayerSibling(index, up=true) の省略引数用 RawCallback。
static tjs_error SetLayerRichText(tTJSVariant *r, tjs_int numparams, tTJSVariant **params, PSD *instance) {
	if (!instance) return TJS_E_NATIVECLASSCRASH;
	if (numparams < 2) return TJS_E_BADPARAMCOUNT;
	int   index = (tjs_int)*params[0];
	ttstr text  = *params[1];
	tTJSVariant runs       = numparams > 2 ? *params[2] : tTJSVariant();
	tTJSVariant paragraphs = numparams > 3 ? *params[3] : tTJSVariant();
	instance->setLayerRichText(index, text, runs, paragraphs);
	return TJS_S_OK;
}

static tjs_error SetLayerJustification(tTJSVariant *r, tjs_int numparams, tTJSVariant **params, PSD *instance) {
	if (!instance) return TJS_E_NATIVECLASSCRASH;
	if (numparams < 2) return TJS_E_BADPARAMCOUNT;
	int index         = (tjs_int)*params[0];
	int justification = (tjs_int)*params[1];
	int paraIndex     = numparams > 2 ? (tjs_int)*params[2] : -1;
	instance->setLayerJustification(index, justification, paraIndex);
	return TJS_S_OK;
}

static tjs_error MoveLayerSibling(tTJSVariant *r, tjs_int numparams, tTJSVariant **params, PSD *instance) {
	if (!instance) return TJS_E_NATIVECLASSCRASH;
	if (numparams < 1) return TJS_E_BADPARAMCOUNT;
	int  index = (tjs_int)*params[0];
	bool up    = numparams > 1 ? ((tjs_int)*params[1] != 0) : true;
	int idx = instance->moveLayerSibling(index, up);
	if (r) *r = (tjs_int)idx;
	return TJS_S_OK;
}

NCB_REGISTER_CLASS(PSD) {

	Factory(&ClassT::factory);

	Variant("color_mode_bitmap",              (int)psd::COLOR_MODE_BITMAP);
  Variant("color_mode_grayscale",           (int)psd::COLOR_MODE_GRAYSCALE);
  Variant("color_mode_indexed",             (int)psd::COLOR_MODE_INDEXED);
  Variant("color_mode_rgb",                 (int)psd::COLOR_MODE_RGB);
  Variant("color_mode_cmyk",                (int)psd::COLOR_MODE_CMYK);
  Variant("color_mode_multichannel",        (int)psd::COLOR_MODE_MULTICHANNEL);
  Variant("color_mode_duotone",             (int)psd::COLOR_MODE_DUOTONE);
  Variant("color_mode_lab",                 (int)psd::COLOR_MODE_LAB);
  
  Variant("blend_mode_normal",              (int)psd::BLEND_MODE_NORMAL);
  Variant("blend_mode_dissolve",            (int)psd::BLEND_MODE_DISSOLVE);
  Variant("blend_mode_darken",              (int)psd::BLEND_MODE_DARKEN);
  Variant("blend_mode_multiply",            (int)psd::BLEND_MODE_MULTIPLY);
  Variant("blend_mode_color_burn",          (int)psd::BLEND_MODE_COLOR_BURN);
  Variant("blend_mode_linear_burn",         (int)psd::BLEND_MODE_LINEAR_BURN);
  Variant("blend_mode_lighten",             (int)psd::BLEND_MODE_LIGHTEN);
  Variant("blend_mode_screen",              (int)psd::BLEND_MODE_SCREEN);
  Variant("blend_mode_color_dodge",         (int)psd::BLEND_MODE_COLOR_DODGE);
  Variant("blend_mode_linear_dodge",        (int)psd::BLEND_MODE_LINEAR_DODGE);
  Variant("blend_mode_overlay",             (int)psd::BLEND_MODE_OVERLAY);
  Variant("blend_mode_soft_light",          (int)psd::BLEND_MODE_SOFT_LIGHT);
  Variant("blend_mode_hard_light",          (int)psd::BLEND_MODE_HARD_LIGHT);
  Variant("blend_mode_vivid_light",         (int)psd::BLEND_MODE_VIVID_LIGHT);
  Variant("blend_mode_linear_light",        (int)psd::BLEND_MODE_LINEAR_LIGHT);
  Variant("blend_mode_pin_light",           (int)psd::BLEND_MODE_PIN_LIGHT);
  Variant("blend_mode_hard_mix",            (int)psd::BLEND_MODE_HARD_MIX);
  Variant("blend_mode_difference",          (int)psd::BLEND_MODE_DIFFERENCE);
  Variant("blend_mode_exclusion",           (int)psd::BLEND_MODE_EXCLUSION);
  Variant("blend_mode_hue",                 (int)psd::BLEND_MODE_HUE);
  Variant("blend_mode_saturation",          (int)psd::BLEND_MODE_SATURATION);
  Variant("blend_mode_color",               (int)psd::BLEND_MODE_COLOR);
  Variant("blend_mode_luminosity",          (int)psd::BLEND_MODE_LUMINOSITY);
  Variant("blend_mode_pass_through",        (int)psd::BLEND_MODE_PASS_THROUGH);

  // NOTE libpsd 非互換モード
  Variant("blend_mode_darker_color",        (int)psd::BLEND_MODE_DARKER_COLOR);
  Variant("blend_mode_lighter_color",       (int)psd::BLEND_MODE_LIGHTER_COLOR);
  Variant("blend_mode_subtract",            (int)psd::BLEND_MODE_SUBTRACT);
  Variant("blend_mode_divide",              (int)psd::BLEND_MODE_DIVIDE);
  

  // NOTE この定数はlibpsd互換ではありません(folderまでは互換)
  Variant("layer_type_normal",              (int)psd::LAYER_TYPE_NORMAL);
  Variant("layer_type_hidden",              (int)psd::LAYER_TYPE_HIDDEN);
  Variant("layer_type_folder",              (int)psd::LAYER_TYPE_FOLDER);
  Variant("layer_type_adjust",              (int)psd::LAYER_TYPE_ADJUST);
  Variant("layer_type_fill",                (int)psd::LAYER_TYPE_FILL);
  Variant("layer_type_text",                (int)psd::LAYER_TYPE_TEXT);

	NCB_METHOD(load);

#define INTPROP(name) Property(TJS_W(# name), &Class::get_ ## name, (int)0)

	INTPROP(width);
	INTPROP(height);
	INTPROP(channels);
	INTPROP(depth);
	INTPROP(color_mode);
	INTPROP(layer_count);

	Property(TJS_W("hresolution"), &Class::get_hresolution, (int)0);
	Property(TJS_W("vresolution"), &Class::get_vresolution, (int)0);

	NCB_METHOD(getLayerType);
	NCB_METHOD(getLayerName);
	NCB_METHOD(getLayerInfo);
	NCB_METHOD(getLayerData);
	NCB_METHOD(getLayerDataRaw);
	NCB_METHOD(getLayerDataMask);

	NCB_METHOD(getSlices);
	NCB_METHOD(getGuides);
	NCB_METHOD(getBlend);
  NCB_METHOD(getLayerComp);

  NCB_METHOD(clearStorageCache);

	RawCallback("assignAutoIds", &AssignAutoIds, 0);

	// --- 編集系 API ---
	NCB_METHOD(save);
	NCB_METHOD(createBlank);
	NCB_METHOD(deleteLayer);
	NCB_METHOD(moveLayer);
	NCB_METHOD(groupSpan);
	RawCallback("moveLayerSibling", &MoveLayerSibling, 0);
	NCB_METHOD(moveLayerRange);
	NCB_METHOD(duplicateLayer);
	NCB_METHOD(copyLayerFrom);
	NCB_METHOD(setLayerName);
	NCB_METHOD(setFillOpacity);
	NCB_METHOD(setMaskDisabled);
	NCB_METHOD(setMaskDensity);
	NCB_METHOD(setMaskFeather);
	NCB_METHOD(setMaskDefaultColor);
	NCB_METHOD(setLayerPixels);
	NCB_METHOD(setLayerMaskPixels);
	RawCallback("addLayer", &AddLayer, 0);
	NCB_METHOD(setMergedImage);
	NCB_METHOD(setLayerText);
	NCB_METHOD(setLayerRunStyle);
	RawCallback("setLayerRichText", &SetLayerRichText, 0);
	RawCallback("setLayerJustification", &SetLayerJustification, 0);
	NCB_METHOD(getLayerFonts);
	NCB_METHOD(getLayerTextTransform);
	NCB_METHOD(setLayerTextTransform);
	NCB_METHOD(moveTextLayer);
	NCB_METHOD(getLayerTextBounds);
	NCB_METHOD(setLayerTextBounds);
};

