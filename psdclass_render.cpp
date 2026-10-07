// ============================================================================
// 描画系: レイヤからの文書合成 / レイヤ 1 枚の効果込み描画 / シェイプとパス
//
// psdparse の PSDFile::compositeImage / renderLayer / shapeMask と、パスの
// ラスタライズ (psdfx C API: psdfx_fill_path / psdfx_stroke_path /
// psdfx_flatten_subpath) を吉里吉里へ出す層。Python バインディングの
// composite() / render_layer() / layer.vector_mask / PSDFile.paths /
// layer.shape / shape_mask() / flatten_path() / rasterize_path() / stroke_path()
// に対応する。
// ============================================================================

#include <ncbind.hpp>
#include "psdclass.h"
#include "psdclass_conv.h"
#include "psdfx.h"
#include <cstring>
#include <vector>
#include <string>

namespace {

// 配列は自分自身をコンテキストにした variant で返す (コンテキストが null だと
// TJS 側で添字アクセスできない)
tTJSVariant arrayVariant(ncbArrayAccessor &a)
{
	iTJSDispatch2 *d = a.GetDispatch();
	return tTJSVariant(d, d);
}

void checkLayerObject(tTJSVariant &layer)
{
	if (layer.Type() != tvtObject || !layer.AsObjectNoAddRef() ||
	    !layer.AsObjectNoAddRef()->IsInstanceOf(0, 0, 0, TJS_W("Layer"), NULL))
		TVPThrowExceptionMessage(TJS_W("not layer"));
}

psd::LayerInfo &layerAt(PSD *self, int no)
{
	if (!self->isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	if (no < 0 || no >= (int)self->layerList.size())
		TVPThrowExceptionMessage(TJS_W("not such layer"));
	return self->layerList[(size_t)no];
}

// レイヤの大きさを w x h にして、メイン画像のバッファと pitch を返す
unsigned char *prepareLayer(tTJSVariant &layer, int left, int top, int w, int h, int &pitch)
{
	ncbPropAccessor obj(layer);
	obj.SetValue(TJS_W("left"), left);
	obj.SetValue(TJS_W("top"), top);
	obj.SetValue(TJS_W("width"), w);
	obj.SetValue(TJS_W("height"), h);
	obj.SetValue(TJS_W("imageLeft"), 0);
	obj.SetValue(TJS_W("imageTop"), 0);
	obj.SetValue(TJS_W("imageWidth"), w);
	obj.SetValue(TJS_W("imageHeight"), h);
	obj.SetValue(TJS_W("type"), (tjs_int)ltAlpha);   // ストレートアルファ
	unsigned char *buffer = (unsigned char*)obj.GetValue(TJS_W("mainImageBufferForWrite"), ncbTypedefs::Tag<tjs_intptr_t>());
	pitch = obj.GetValue(TJS_W("mainImageBufferPitch"), ncbTypedefs::Tag<tjs_int>());
	return buffer;
}

// 詰めた BGRA をレイヤへ
void copyBGRA(tTJSVariant &layer, const std::vector<uint8_t> &bgra, int left, int top, int w, int h)
{
	int pitch = 0;
	unsigned char *buf = prepareLayer(layer, left, top, w, h, pitch);
	for (int y = 0; y < h; y++)
		memcpy(buf + (size_t)y * pitch, &bgra[(size_t)y * w * 4], (size_t)w * 4);
}

// 被覆率 (1 byte/px) をレイヤへ。getLayerDataMask と同じく B=G=R=値, A=255。
void copyGray(tTJSVariant &layer, const std::vector<uint8_t> &m, int left, int top, int w, int h)
{
	int pitch = 0;
	unsigned char *buf = prepareLayer(layer, left, top, w, h, pitch);
	for (int y = 0; y < h; y++) {
		unsigned char *d = buf + (size_t)y * pitch;
		const uint8_t *s = &m[(size_t)y * w];
		for (int x = 0; x < w; x++, d += 4) { d[0] = d[1] = d[2] = s[x]; d[3] = 255; }
	}
}

tTJSVariant statsToTjs(const psd::CompositeStats &st)
{
	tTJSVariant result;
	ncbDictionaryAccessor d;
	if (d.IsValid()) {
		d.SetValue(TJS_W("skipped_adjustments"),   st.skippedAdjustments);
		d.SetValue(TJS_W("unsupported_clip_base"), st.unsupportedClipBase);
		d.SetValue(TJS_W("unsupported_effects"),   st.unsupportedEffects);
		result = d;
	}
	return result;
}

tTJSVariant xyToTjs(double x, double y)
{
	tTJSVariant result;
	ncbArrayAccessor a;
	if (a.IsValid()) {
		a.SetValue(0, (tjs_real)x);
		a.SetValue(1, (tjs_real)y);
		result = arrayVariant(a);
	}
	return result;
}

template <size_t N>
tTJSVariant realsToTjs(const double (&v)[N])
{
	tTJSVariant result;
	ncbArrayAccessor a;
	if (a.IsValid()) {
		for (size_t i = 0; i < N; i++) a.SetValue((tjs_int32)i, (tjs_real)v[i]);
		result = arrayVariant(a);
	}
	return result;
}

// パス (座標は文書に対する割合) を文書ピクセルの辞書へ。Python の
// layer.vector_mask['path'] と同じ形。
tTJSVariant pathToTjs(const psd::PathData &pd, double w, double h)
{
	tTJSVariant result;
	ncbDictionaryAccessor d;
	if (!d.IsValid()) return result;
	ncbArrayAccessor subs;
	tjs_int32 si = 0;
	for (const auto &sp : pd.subpaths) {
		ncbArrayAccessor knots;
		tjs_int32 ki = 0;
		for (const auto &k : sp.knots) {
			ncbDictionaryAccessor kd;
			kd.SetValue(TJS_W("anchor"),    xyToTjs(k.anchor.x * w, k.anchor.y * h));
			kd.SetValue(TJS_W("preceding"), xyToTjs(k.preceding.x * w, k.preceding.y * h));
			kd.SetValue(TJS_W("leaving"),   xyToTjs(k.leaving.x * w, k.leaving.y * h));
			kd.SetValue(TJS_W("linked"),    k.linked ? 1 : 0);
			knots.SetValue(ki++, kd.GetDispatch());
		}
		ncbDictionaryAccessor sd;
		sd.SetValue(TJS_W("closed"),    sp.closed ? 1 : 0);
		sd.SetValue(TJS_W("operation"), sp.operation);
		sd.SetValue(TJS_W("index"),     sp.index);
		sd.SetValue(TJS_W("knots"),     arrayVariant(knots));
		subs.SetValue(si++, sd.GetDispatch());
	}
	d.SetValue(TJS_W("subpaths"), arrayVariant(subs));
	if (pd.initialFill >= 0) d.SetValue(TJS_W("initial_fill"), pd.initialFill);
	if (pd.hasClipboard) {
		ncbDictionaryAccessor c;
		c.SetValue(TJS_W("top"),        pd.clipboardTop);
		c.SetValue(TJS_W("left"),       pd.clipboardLeft);
		c.SetValue(TJS_W("bottom"),     pd.clipboardBottom);
		c.SetValue(TJS_W("right"),      pd.clipboardRight);
		c.SetValue(TJS_W("resolution"), pd.clipboardResolution);
		d.SetValue(TJS_W("clipboard"), c.GetDispatch());
	}
	result = d;
	return result;
}

// --- TJS のパス → psdfx -------------------------------------------------------

bool hasMember(tTJSVariant &obj, const tjs_char *name)
{
	tTJSVariant v;
	iTJSDispatch2 *d = obj.AsObjectNoAddRef();
	return d && TJS_SUCCEEDED(d->PropGet(0, name, NULL, &v, d)) && v.Type() != tvtVoid;
}

tTJSVariant member(tTJSVariant &obj, const tjs_char *name)
{
	tTJSVariant v;
	iTJSDispatch2 *d = obj.AsObjectNoAddRef();
	if (d) d->PropGet(0, name, NULL, &v, d);
	return v;
}

tjs_int arrayCount(tTJSVariant &arr)
{
	return (tjs_int)member(arr, TJS_W("count"));
}

tTJSVariant arrayAt(tTJSVariant &arr, tjs_int i)
{
	tTJSVariant v;
	iTJSDispatch2 *d = arr.AsObjectNoAddRef();
	if (d) d->PropGetByNum(0, i, &v, d);
	return v;
}

bool isArray(tTJSVariant &v)
{
	if (v.Type() != tvtObject || !v.AsObjectNoAddRef()) return false;
	return v.AsObjectNoAddRef()->IsInstanceOf(0, 0, 0, TJS_W("Array"), NULL) == TJS_S_TRUE;
}

void readXY(tTJSVariant v, double &x, double &y)
{
	if (!isArray(v) || arrayCount(v) < 2) TVPThrowExceptionMessage(TJS_W("point must be [x, y]"));
	x = (tjs_real)arrayAt(v, 0);
	y = (tjs_real)arrayAt(v, 1);
}

struct TjsPath {
	std::vector<std::vector<psdfx_knot>> knots;
	std::vector<psdfx_subpath> subs;
	int initialFill = 0;
};

// %[ subpaths:[...], initial_fill ] か、サブパスの配列を受ける。
// knot は %[ anchor:[x,y], preceding:[x,y], leaving:[x,y] ] か [x, y] (直線の頂点)。
void readPath(tTJSVariant path, TjsPath &out)
{
	if (path.Type() != tvtObject) TVPThrowExceptionMessage(TJS_W("path must be a dictionary or an array"));
	tTJSVariant subs = path;
	if (!isArray(path)) {
		if (!hasMember(path, TJS_W("subpaths"))) TVPThrowExceptionMessage(TJS_W("path needs subpaths"));
		subs = member(path, TJS_W("subpaths"));
		if (hasMember(path, TJS_W("initial_fill")))
			out.initialFill = (tjs_int)member(path, TJS_W("initial_fill")) == 1 ? 1 : 0;
	}
	std::vector<int> closed, ops;
	const tjs_int n = arrayCount(subs);
	for (tjs_int i = 0; i < n; i++) {
		tTJSVariant sp = arrayAt(subs, i);
		tTJSVariant ks = member(sp, TJS_W("knots"));
		std::vector<psdfx_knot> knots;
		const tjs_int kn = isArray(ks) ? arrayCount(ks) : 0;
		for (tjs_int j = 0; j < kn; j++) {
			tTJSVariant kv = arrayAt(ks, j);
			psdfx_knot k = {};
			if (isArray(kv)) {
				readXY(kv, k.x, k.y);
				k.in_x = k.out_x = k.x; k.in_y = k.out_y = k.y;
			} else {
				readXY(member(kv, TJS_W("anchor")), k.x, k.y);
				k.in_x = k.out_x = k.x; k.in_y = k.out_y = k.y;
				if (hasMember(kv, TJS_W("preceding"))) readXY(member(kv, TJS_W("preceding")), k.in_x, k.in_y);
				if (hasMember(kv, TJS_W("leaving")))   readXY(member(kv, TJS_W("leaving")), k.out_x, k.out_y);
			}
			knots.push_back(k);
		}
		out.knots.push_back(knots);
		closed.push_back(hasMember(sp, TJS_W("closed")) ? ((tjs_int)member(sp, TJS_W("closed")) ? 1 : 0) : 1);
		ops.push_back(hasMember(sp, TJS_W("operation")) ? (tjs_int)member(sp, TJS_W("operation")) : -1);
	}
	for (size_t i = 0; i < out.knots.size(); i++)
		out.subs.push_back({ out.knots[i].data(), (int)out.knots[i].size(), closed[i], ops[i] });
}

int pickName(tTJSVariant &opt, const tjs_char *key, const tjs_char *const *names, int count, int def)
{
	if (opt.Type() != tvtObject || !hasMember(opt, key)) return def;
	ttstr v = member(opt, key);
	for (int i = 0; i < count; i++) if (v == names[i]) return i;
	TVPThrowExceptionMessage((ttstr(TJS_W("unknown ")) + key + TJS_W(": ") + v).c_str());
	return def;
}

double optReal(tTJSVariant &opt, const tjs_char *key, double def)
{
	if (opt.Type() != tvtObject || !hasMember(opt, key)) return def;
	return (tjs_real)member(opt, key);
}

void checkRaster(int w, int h)
{
	if (w <= 0 || h <= 0) TVPThrowExceptionMessage(TJS_W("width and height must be positive"));
	if ((long long)w * h > (1LL << 28)) TVPThrowExceptionMessage(TJS_W("raster too large"));
}

} // namespace

// ============================================================================
// 合成 / 描画
// ============================================================================

tTJSVariant
PSD::getComposite(tTJSVariant layer, bool effects, tTJSVariant background)
{
	checkLayerObject(layer);
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	psd::CompositeOptions opt;
	opt.effects = effects;
	if (background.Type() != tvtVoid) {
		const tjs_int c = (tjs_int)background;   // 0xRRGGBB
		opt.background = true;
		opt.backgroundColor[0] = (uint8_t)((c >> 16) & 0xff);
		opt.backgroundColor[1] = (uint8_t)((c >> 8) & 0xff);
		opt.backgroundColor[2] = (uint8_t)(c & 0xff);
	}
	std::vector<uint8_t> out;
	psd::CompositeStats st;
	if (!compositeImage(out, opt, &st))
		TVPThrowExceptionMessage(TJS_W("cannot composite this document"));
	copyBGRA(layer, out, 0, 0, header.width, header.height);
	return statsToTjs(st);
}

bool
PSD::renderLayer(tTJSVariant layer, int no, bool effects)
{
	checkLayerObject(layer);
	psd::LayerInfo &lay = layerAt(this, no);
	psd::CompositeOptions opt;
	opt.effects = effects;
	std::vector<uint8_t> out;
	int left = 0, top = 0, w = 0, h = 0;
	if (!psd::PSDFile::renderLayer(no, out, left, top, w, h, opt)) return false;
	copyBGRA(layer, out, left, top, w, h);
	ncbPropAccessor obj(layer);
	obj.SetValue(TJS_W("name"), layname(lay));
	return true;
}

// ============================================================================
// パスとシェイプ
// ============================================================================

tTJSVariant
PSD::getVectorMask(int no)
{
	psd::LayerInfo &lay = layerAt(this, no);
	const psd::VectorMask &vm = lay.vectorMask;
	tTJSVariant result;
	if (!vm.present) return result;
	ncbDictionaryAccessor d;
	if (!d.IsValid()) return result;
	d.SetValue(TJS_W("key"),        fourccToTjs(vm.key));
	d.SetValue(TJS_W("inverted"),   vm.inverted() ? 1 : 0);
	d.SetValue(TJS_W("not_linked"), vm.notLinked() ? 1 : 0);
	d.SetValue(TJS_W("disabled"),   vm.disabled() ? 1 : 0);
	d.SetValue(TJS_W("path"),       pathToTjs(vm.path, header.width, header.height));
	result = d;
	return result;
}

tTJSVariant
PSD::getPaths()
{
	if (!isLoaded) TVPThrowExceptionMessage(TJS_W("no data"));
	tTJSVariant result;
	ncbArrayAccessor arr;
	if (!arr.IsValid()) return result;
	tjs_int32 i = 0;
	for (const auto &p : savedPaths) {
		ncbDictionaryAccessor d;
		d.SetValue(TJS_W("id"),   p.id);
		d.SetValue(TJS_W("kind"), ttstr(p.id == 1025 ? TJS_W("work") : TJS_W("saved")));
		d.SetValue(TJS_W("name"), tTJSVariant((const tjs_uint8 *)p.name.data(), (tjs_uint)p.name.size()));
		if (p.hasNameUnicode) d.SetValue(TJS_W("unicode_name"), u16ToTjs(p.nameUnicode));
		d.SetValue(TJS_W("path"), pathToTjs(p.path, header.width, header.height));
		arr.SetValue(i++, d.GetDispatch());
	}
	result = arrayVariant(arr);
	return result;
}

static const tjs_char *fillKindName(int k)
{
	switch (k) {
	case 'SoCo': return TJS_W("solid");
	case 'GdFl': return TJS_W("gradient");
	case 'PtFl': return TJS_W("pattern");
	default:     return 0;
	}
}

tTJSVariant
PSD::getLayerShape(int no)
{
	psd::LayerInfo &lay = layerAt(this, no);
	psd::ShapeInfo si;
	tTJSVariant result;
	if (!psd::decodeShape(lay, si)) return result;
	ncbDictionaryAccessor d;
	if (!d.IsValid()) return result;
	d.SetValue(TJS_W("fill_enabled"),   (si.hasStroke ? si.stroke.fillEnabled : true) ? 1 : 0);
	d.SetValue(TJS_W("stroke_enabled"), (si.hasStroke && si.stroke.strokeEnabled) ? 1 : 0);
	if (si.hasFill) {
		ncbDictionaryAccessor f;
		if (const tjs_char *k = fillKindName(si.fillKind)) f.SetValue(TJS_W("kind"), ttstr(k));
		if (si.fill) f.SetValue(TJS_W("descriptor"), psdDescriptorToTjs(si.fill.get()));
		d.SetValue(TJS_W("fill"), f.GetDispatch());
	}
	if (si.hasStroke) {
		const psd::ShapeStroke &s = si.stroke;
		const double dpi = header.hres > 0 ? header.hres : 72.0;
		const double w = s.width * (s.widthInPoints ? dpi / 72.0 : 1.0);
		static const tjs_char *kAlign[] = { TJS_W("outside"), TJS_W("inside"), TJS_W("center") };
		static const tjs_char *kCap[]   = { TJS_W("butt"), TJS_W("round"), TJS_W("square") };
		static const tjs_char *kJoin[]  = { TJS_W("miter"), TJS_W("round"), TJS_W("bevel") };
		auto clamp3 = [](int v) { return v < 0 ? 0 : v > 2 ? 2 : v; };
		ncbDictionaryAccessor st;
		st.SetValue(TJS_W("width"),       w);
		st.SetValue(TJS_W("alignment"),   ttstr(kAlign[clamp3(s.alignment)]));
		st.SetValue(TJS_W("cap"),         ttstr(kCap[clamp3(s.cap)]));
		st.SetValue(TJS_W("join"),        ttstr(kJoin[clamp3(s.join)]));
		st.SetValue(TJS_W("miter_limit"), s.miterLimit);
		ncbArrayAccessor dashes;
		for (size_t i = 0; i < s.dashes.size(); i++) dashes.SetValue((tjs_int32)i, (tjs_real)(s.dashes[i] * w));
		st.SetValue(TJS_W("dashes"),      arrayVariant(dashes));
		st.SetValue(TJS_W("dash_offset"), s.dashOffset * w);
		st.SetValue(TJS_W("opacity"),     s.opacity);
		st.SetValue(TJS_W("blend_mode"),  ttstr(s.blendMode.c_str()));
		if (const tjs_char *k = fillKindName(s.contentKind)) st.SetValue(TJS_W("content_kind"), ttstr(k));
		if (s.content) st.SetValue(TJS_W("content"), psdDescriptorToTjs(s.content.get()));
		d.SetValue(TJS_W("stroke"), st.GetDispatch());
	}
	ncbArrayAccessor origins;
	tjs_int32 oi = 0;
	for (const auto &o : si.origins) {
		ncbDictionaryAccessor od;
		const tjs_char *name = o.type == 1 ? TJS_W("rectangle") : o.type == 2 ? TJS_W("rounded_rectangle")
		                     : o.type == 4 ? TJS_W("line") : o.type == 5 ? TJS_W("ellipse") : 0;
		if (name) od.SetValue(TJS_W("type"), ttstr(name));
		od.SetValue(TJS_W("type_id"), o.type);
		od.SetValue(TJS_W("index"),   o.index);
		if (o.hasBox)   od.SetValue(TJS_W("box"),   realsToTjs(o.box));
		if (o.hasRadii) od.SetValue(TJS_W("radii"), realsToTjs(o.radii));
		if (o.hasLine) {
			od.SetValue(TJS_W("line"),        realsToTjs(o.line));
			od.SetValue(TJS_W("line_weight"), o.lineWeight);
		}
		od.SetValue(TJS_W("invalidated"), o.invalidated ? 1 : 0);
		origins.SetValue(oi++, od.GetDispatch());
	}
	d.SetValue(TJS_W("origins"), arrayVariant(origins));
	if (lay.vectorMask.present)
		d.SetValue(TJS_W("path"), pathToTjs(lay.vectorMask.path, header.width, header.height));
	result = d;
	return result;
}

tTJSVariant
PSD::getShapeMask(tTJSVariant layer, int no, ttstr part)
{
	checkLayerObject(layer);
	layerAt(this, no);
	psd::ShapePart p;
	if (part == TJS_W("fill")) p = psd::SHAPE_PART_FILL;
	else if (part == TJS_W("stroke")) p = psd::SHAPE_PART_STROKE;
	else if (part == TJS_W("both") || part.IsEmpty()) p = psd::SHAPE_PART_BOTH;
	else TVPThrowExceptionMessage(TJS_W("part must be fill, stroke or both"));
	std::vector<uint8_t> m;
	int left = 0, top = 0, w = 0, h = 0;
	tTJSVariant result;
	if (!shapeMask(no, p, m, left, top, w, h)) return result;
	copyGray(layer, m, left, top, w, h);
	ncbDictionaryAccessor d;
	if (d.IsValid()) {
		d.SetValue(TJS_W("left"), left);
		d.SetValue(TJS_W("top"), top);
		d.SetValue(TJS_W("width"), w);
		d.SetValue(TJS_W("height"), h);
		result = d;
	}
	return result;
}

// ============================================================================
// 任意のパス (static)
// ============================================================================

tTJSVariant
PSD::flattenPath(tTJSVariant path, double tolerance)
{
	TjsPath p;
	readPath(path, p);
	tTJSVariant result;
	ncbArrayAccessor arr;
	if (!arr.IsValid()) return result;
	for (size_t i = 0; i < p.subs.size(); i++) {
		const psdfx_subpath &s = p.subs[i];
		const int n = psdfx_flatten_subpath(&s, tolerance, NULL, 0);
		std::vector<double> xy((size_t)n * 2);
		psdfx_flatten_subpath(&s, tolerance, xy.data(), n);
		ncbArrayAccessor pts;
		for (int j = 0; j < n; j++) pts.SetValue((tjs_int32)j, xyToTjs(xy[(size_t)j * 2], xy[(size_t)j * 2 + 1]));
		ncbDictionaryAccessor d;
		d.SetValue(TJS_W("closed"), s.closed ? 1 : 0);
		d.SetValue(TJS_W("operation"), s.operation);
		d.SetValue(TJS_W("points"), arrayVariant(pts));
		arr.SetValue((tjs_int32)i, d.GetDispatch());
	}
	result = arrayVariant(arr);
	return result;
}

void
PSD::rasterizePath(tTJSVariant layer, tTJSVariant path, int width, int height, double left, double top)
{
	checkLayerObject(layer);
	checkRaster(width, height);
	TjsPath p;
	readPath(path, p);
	std::vector<uint8_t> m((size_t)width * height);
	psdfx_fill_path(p.subs.data(), (int)p.subs.size(), p.initialFill, m.data(), width, height,
	                width, left, top);
	copyGray(layer, m, 0, 0, width, height);
}

void
PSD::strokePath(tTJSVariant layer, tTJSVariant path, int width, int height, tTJSVariant style)
{
	checkLayerObject(layer);
	checkRaster(width, height);
	TjsPath p;
	readPath(path, p);
	static const tjs_char *kAlign[] = { TJS_W("outside"), TJS_W("inside"), TJS_W("center") };
	static const tjs_char *kCap[]   = { TJS_W("butt"), TJS_W("round"), TJS_W("square") };
	static const tjs_char *kJoin[]  = { TJS_W("miter"), TJS_W("round"), TJS_W("bevel") };
	psdfx_stroke_style st = {};
	st.width = optReal(style, TJS_W("line_width"), 1.0);
	const int a = pickName(style, TJS_W("alignment"), kAlign, 3, 2);
	st.alignment = a == 0 ? PSDFX_STROKE_OUTSIDE : a == 1 ? PSDFX_STROKE_INSIDE : PSDFX_STROKE_CENTER;
	st.cap = pickName(style, TJS_W("cap"), kCap, 3, 0);
	st.join = pickName(style, TJS_W("join"), kJoin, 3, 0);
	st.miter_limit = optReal(style, TJS_W("miter_limit"), 4.0);
	std::vector<double> dashes;
	if (style.Type() == tvtObject && hasMember(style, TJS_W("dashes"))) {
		tTJSVariant ds = member(style, TJS_W("dashes"));
		const tjs_int n = isArray(ds) ? arrayCount(ds) : 0;
		for (tjs_int i = 0; i < n; i++) dashes.push_back((tjs_real)arrayAt(ds, i));
	}
	st.dashes = dashes.empty() ? NULL : dashes.data();
	st.dash_count = (int)dashes.size();
	st.dash_offset = optReal(style, TJS_W("dash_offset"), 0.0);
	const double left = optReal(style, TJS_W("left"), 0.0), top = optReal(style, TJS_W("top"), 0.0);
	std::vector<uint8_t> m((size_t)width * height);
	psdfx_stroke_path(p.subs.data(), (int)p.subs.size(), p.initialFill, &st, m.data(), width,
	                  height, width, left, top);
	copyGray(layer, m, 0, 0, width, height);
}
