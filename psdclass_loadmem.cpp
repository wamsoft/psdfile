#include "psdclass.h"

#ifdef LOAD_MEMORY

#include "psdparse/psdparse.h"

void
PSD::clearMemory()
{
	if (mBuffer) {
		delete[] mBuffer;
		mBuffer = 0;
	}
}

bool
PSD::loadMemory(const ttstr &filename)
{
	clearData();

	// まるごとメモリに読み込んで処理
	isLoaded = false;
	iTJSBinaryStream *stream = TVPCreateStream(filename, TJS_BS_READ);
	if (stream) {
		try {
			// 全部メモリに読み込む
			tjs_uint64 qsize = stream->GetSize();
			if (qsize < 0xFFFFFFFF) {
				tjs_uint size = (tjs_uint)qsize;
				mBuffer = new unsigned char[size];
				if (mBuffer) {
					stream->Read(mBuffer, size);
					psd::Parser<unsigned char*> parser(*this);
					unsigned char *begin = mBuffer;
					unsigned char *end   = begin + size;
					bool r = parse(begin , end,  parser);
					if (r && begin == end) {
						dprint("succeeded\n");
						isLoaded = processParsed();
					}
					if (!isLoaded) {
						clearData();
					}
				}
			}
		} catch(...) {
			clearData();
			stream->Destruct();
			throw;
		}
		stream->Destruct();
	}
	return isLoaded;
}

#endif
