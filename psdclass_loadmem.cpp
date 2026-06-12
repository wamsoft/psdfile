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

	isLoaded = false;
	iTJSBinaryStream *stream = TVPCreateStream(filename, TJS_BS_READ);
	if (stream) {
		try {
			tjs_uint64 qsize = stream->GetSize();
			if (qsize > 0 && qsize < 0xFFFFFFFF) {
				tjs_uint size = (tjs_uint)qsize;
				mBuffer = new unsigned char[size];
				if (mBuffer) {
					stream->Read(mBuffer, size);
					isLoaded = loadFromMemory(mBuffer, (size_t)size);
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
