#include "psdclass.h"

#ifndef LOAD_MEMORY

#include "psdparse/psdparse.h"

// Phase 1a 中の暫定: ストリームを一括メモリに吸い上げてから新パーサに渡す。
// Phase 3 で eager Blob 化が入ると、この一括読み込みは自然な動作になる。
bool
PSD::loadStream(const ttstr &filename)
{
	clearData();

	isLoaded = false;
	pStream = TVPCreateStream(filename, TJS_BS_READ);
	if (pStream) {
		mStreamSize = (tTVInteger)pStream->GetSize();
		if (mStreamSize > 0 && (tjs_uint64)mStreamSize < 0xFFFFFFFFull) {
			std::vector<unsigned char> buf((size_t)mStreamSize);
			pStream->Seek(0, TJS_BS_SEEK_SET);
			pStream->Read(buf.data(), (tjs_uint)mStreamSize);
			isLoaded = loadFromMemory(buf.data(), buf.size());
			if (!isLoaded) {
				clearData();
			}
		}
	}
	return isLoaded;
}

#endif
