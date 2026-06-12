#include "psdclass.h"
#include "psdparse/psdparse.h"

#include <memory>

namespace {

// iTJSBinaryStream を psd::StreamReader::Source として晒すラッパ。
//
// Source は shared_ptr で StreamReader 内部および parse 結果として PSDFile
// に保持される iterator クローン群に共有される。すなわち PSD インスタンス
// が isLoaded == true のあいだは、どこかしらの shared_ptr が src を生かして
// くれるので、stream は確実に生存する。
//
// 所有権を取り、デストラクタで iTJSBinaryStream::Destruct() を呼ぶ。
class TJSBinaryStreamSource : public psd::StreamReader::Source {
public:
	TJSBinaryStreamSource(iTJSBinaryStream *s, size_t totalSize)
	  : s_(s), size_(totalSize) {}
	~TJSBinaryStreamSource() override {
		if (s_) s_->Destruct();
	}
	size_t size() const override { return size_; }
	size_t read(uint8_t *out, size_t offset, size_t len) override {
		if (!s_ || offset >= size_ || len == 0) return 0;
		s_->Seek((tjs_int64)offset, TJS_BS_SEEK_SET);
		return (size_t)s_->Read(out, (tjs_uint)len);
	}
private:
	iTJSBinaryStream *s_;
	size_t size_;
};

} // namespace

bool
PSD::loadStream(const ttstr &filename)
{
	clearData();
	isLoaded = false;

	iTJSBinaryStream *stream = TVPCreateStream(filename, TJS_BS_READ);
	if (!stream) return false;

	tjs_uint64 sz = stream->GetSize();
	// 4GB 級は今は弾く (内部 int 幅の問題)。psd 仕様上も実用的な上限。
	if (sz == 0 || sz >= 0x7FFFFFFFull) {
		stream->Destruct();
		return false;
	}

	// Source が stream の所有権を取る。これ以降 stream の解放は Source に
	// 任せる (PSDFile 側 iterator が全部消えたら src も消えて stream も死ぬ)。
	auto src = std::make_shared<TJSBinaryStreamSource>(stream, (size_t)sz);
	psd::StreamReader reader(src);
	isLoaded = loadFromReader(reader);
	if (!isLoaded) clearData();
	// ローカル reader はここで破棄されるが、parser がクローンしたイテレータ
	// 群が PSDFile 内部に残っており、それらが shared_ptr<Source> を保持する
	// ので src と stream は生存し続ける。
	return isLoaded;
}
