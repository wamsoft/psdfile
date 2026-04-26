#ifndef LOAD_MEMORY

#include "psdclass.h"

void
PSD::clearStream()
{
	if (pStream) {
		pStream->Destruct();
		pStream = 0;
	}
	mStreamSize = 0;
	mBufferPos  = 0;
	mBufferSize = 0;
}


unsigned char &
PSD::getStreamValue(const tTVInteger &pos)
{
	static unsigned char eof = 0;
	if (pos >=0 && pos < mStreamSize) {
		if (pos >= mBufferPos && pos < mBufferPos + mBufferSize) {
			return mBuffer[pos - mBufferPos];
		}
		mBufferPos = pos;
		pStream->Seek((tjs_int64)pos, TJS_BS_SEEK_SET);
		mBufferSize = pStream->Read(mBuffer, sizeof mBuffer);
		return mBuffer[0];
	}
	return eof;
}

void
PSD::copyToBuffer(uint8_t *buf, tTVInteger pos, int size)
{
	if (pos >=0 && pos < mStreamSize) {
		if (size <= 16) {
			if (pos >= mBufferPos && pos+size < mBufferPos + mBufferSize) {
				memcpy(buf, &mBuffer[pos - mBufferPos], size);
				return;
			}
			mBufferPos = pos;
			pStream->Seek((tjs_int64)pos, TJS_BS_SEEK_SET);
			mBufferSize = pStream->Read(mBuffer, sizeof mBuffer);
			memcpy(buf, mBuffer, size);
			return;
		}
		// 直接読み込む
		pStream->Seek((tjs_int64)pos, TJS_BS_SEEK_SET);
		pStream->Read(buf, size);
	}
}

#endif
