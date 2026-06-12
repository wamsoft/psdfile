// Smoke test CLI for psdparse.
//
// Usage: psdparse_cli <path-to-psd>
//
// Loads the file, prints header info, layer count, and pulls each layer's
// pixels into a temp buffer (verifies no crash through the read/decode path).

#include "psdfile.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

#ifdef _WIN32
// Win32: wmain で UTF-16 argv を受けて UTF-8 に変換してから psd::load に渡す。
// 通常の main(argv) は ACP 解釈なので Japanese 等が落ちる。
int wmain(int argc, wchar_t *wargv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: psdparse_cli <psd-file>\n");
    return 2;
  }
  int n = WideCharToMultiByte(CP_UTF8, 0, wargv[1], -1, nullptr, 0, nullptr, nullptr);
  if (n <= 0) { std::fprintf(stderr, "bad path encoding\n"); return 2; }
  std::string path((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, wargv[1], -1, path.data(), n, nullptr, nullptr);
  if (!path.empty() && path.back() == '\0') path.pop_back();
  const char *filename = path.c_str();
#else
int main(int argc, const char *argv[]) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <psd-file>\n", argv[0]);
    return 2;
  }
  const char *filename = argv[1];
#endif

  psd::PSDFile psd;
  if (!psd.load(filename)) {
    std::fprintf(stderr, "load failed: %s\n", filename);
    return 1;
  }

  std::printf("loaded: %s\n", filename);
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
