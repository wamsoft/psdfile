// Smoke test CLI for psdparse.
//
// Usage: psdparse_cli <path-to-psd>
//
// Loads the file, prints header info, layer count, and pulls each layer's
// pixels into a temp buffer (verifies no crash through the read/decode path).

#include "psdfile.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, const char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <psd-file>\n", argv[0]);
    return 2;
  }

  psd::PSDFile psd;
  if (!psd.load(argv[1])) {
    std::fprintf(stderr, "load failed: %s\n", argv[1]);
    return 1;
  }

  std::printf("loaded: %s\n", argv[1]);
  std::printf("  version  : %d\n",  psd.header.version);
  std::printf("  size     : %d x %d\n", psd.header.width, psd.header.height);
  std::printf("  channels : %d\n",  psd.header.channels);
  std::printf("  depth    : %d\n",  psd.header.depth);
  std::printf("  mode     : %d\n",  psd.header.mode);
  std::printf("  layers   : %d\n",  (int)psd.layerList.size());

  for (size_t i = 0; i < psd.layerList.size(); i++) {
    psd::LayerInfo &layer = psd.layerList[i];
    std::printf("  [%2zu] type=%d  rect=(%d,%d)-(%d,%d)  channels=%d  name='%s'\n",
                i, (int)layer.layerType,
                layer.left, layer.top, layer.right, layer.bottom,
                (int)layer.channels.size(),
                layer.extraData.layerName.c_str());
    if (layer.layerType != psd::LAYER_TYPE_NORMAL) continue;
    if (layer.width <= 0 || layer.height <= 0) continue;
    std::vector<unsigned char> buf((size_t)layer.width * layer.height * 4);
    psd.getLayerImage(layer, buf.data(), psd::BGRA_LE,
                      layer.width * 4, psd::IMAGE_MODE_MASKEDIMAGE);
  }

  if (psd.imageData) {
    std::vector<unsigned char> buf((size_t)psd.header.width * psd.header.height * 4);
    psd.getMergedImage(buf.data(), psd::BGRA_LE, 0);
    std::printf("  merged image extracted: %zu bytes\n", buf.size());
  }
  return 0;
}
