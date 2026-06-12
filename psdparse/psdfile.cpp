#include "stdafx.h"

#include "psdparse.h"
#include "psdfile.h"
#include "psdwrite.h"

#include <cstring>
#include <iostream>

#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <unistd.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
#endif

namespace psd {

// ============================================================================
// PSDFile::Mapping -- pimpl for OS-level memory-mapped file
// ============================================================================
//
// 構築に成功するとファイル全域が読み取り専用 mmap される。MemoryReader が
// data()/size() を覗くだけで使えるので、IteratorBase は OS のページキャッシュ
// 越しに必要な分だけ実ディスクから読まれる。PSDFile 全体が「ファイルを開い
// たまま、構造情報だけ走査し、レイヤピクセルは要求されたときだけページ
// インさせる」設計の心臓部。

struct PSDFile::Mapping {
#ifdef _WIN32
  HANDLE hFile  = INVALID_HANDLE_VALUE;
  HANDLE hMap   = nullptr;
  LPVOID view   = nullptr;
  size_t length = 0;

  static std::unique_ptr<Mapping> open(const wchar_t *wpath) {
    auto m = std::unique_ptr<Mapping>(new Mapping());
    m->hFile = CreateFileW(wpath, GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                           nullptr);
    if (m->hFile == INVALID_HANDLE_VALUE) return nullptr;
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(m->hFile, &sz) || sz.QuadPart == 0 ||
        (uint64_t)sz.QuadPart > 0x7FFFFFFFFFFFFFFFull) {
      return nullptr;
    }
    m->length = (size_t)sz.QuadPart;
    m->hMap = CreateFileMappingW(m->hFile, nullptr, PAGE_READONLY,
                                 0, 0, nullptr);
    if (!m->hMap) return nullptr;
    m->view = MapViewOfFile(m->hMap, FILE_MAP_READ, 0, 0, 0);
    if (!m->view) return nullptr;
    return m;
  }

  ~Mapping() {
    if (view)                       UnmapViewOfFile(view);
    if (hMap)                       CloseHandle(hMap);
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
  }
#else
  void  *ptr    = nullptr;
  size_t length = 0;
  int    fd     = -1;

  static std::unique_ptr<Mapping> open(const char *path) {
    auto m = std::unique_ptr<Mapping>(new Mapping());
    m->fd = ::open(path, O_RDONLY);
    if (m->fd < 0) return nullptr;
    struct stat st{};
    if (fstat(m->fd, &st) < 0 || st.st_size <= 0) return nullptr;
    m->length = (size_t)st.st_size;
    m->ptr = mmap(nullptr, m->length, PROT_READ, MAP_PRIVATE, m->fd, 0);
    if (m->ptr == MAP_FAILED) { m->ptr = nullptr; return nullptr; }
    return m;
  }

  ~Mapping() {
    if (ptr) munmap(ptr, length);
    if (fd >= 0) ::close(fd);
  }
#endif

  const uint8_t *data() const {
#ifdef _WIN32
    return (const uint8_t *)view;
#else
    return (const uint8_t *)ptr;
#endif
  }
  size_t size() const { return length; }
};

// ============================================================================
// PSDFile
// ============================================================================

PSDFile::PSDFile() : isLoaded(false) {}
PSDFile::~PSDFile() = default;

void PSDFile::clearData() {
  Data::clearData();
  isLoaded = false;
  mapping_.reset();
  std::vector<uint8_t>().swap(ownedBuffer_);
  ownedStream_.reset();
}

bool PSDFile::load(const char *filename) {
  clearData();
  isLoaded = false;
#ifdef _WIN32
  std::wstring w = utf8ToWide(filename);
  if (w.empty()) return false;
  mapping_ = Mapping::open(w.c_str());
#else
  mapping_ = Mapping::open(filename);
#endif
  if (!mapping_) {
    std::cerr << "mmap failed for: '" << (filename ? filename : "(null)") << "'\n";
    return false;
  }
  MemoryReader reader(mapping_->data(), (int)mapping_->size());
  if (!parsePSD(reader, *this)) { clearData(); return false; }
  isLoaded = processParsed();
  if (!isLoaded) clearData();
  return isLoaded;
}

bool PSDFile::loadFromMemory(const uint8_t *data, size_t size) {
  clearData();
  isLoaded = false;
  if (data == nullptr || size == 0) return false;
  ownedBuffer_.assign(data, data + size);
  MemoryReader reader(ownedBuffer_.data(), (int)ownedBuffer_.size());
  if (!parsePSD(reader, *this)) {
    clearData();
    return false;
  }
  isLoaded = processParsed();
  if (!isLoaded) clearData();
  return isLoaded;
}

bool PSDFile::loadFromReader(IteratorBase &reader) {
  // 汎用エントリ。reader が指す storage の維持責任は呼び出し元。
  // (kirikiri は iTJSBinaryStream をラップした StreamReader をここに渡す)
  clearData();
  isLoaded = false;
  if (!parsePSD(reader, *this)) {
    clearData();
    return false;
  }
  isLoaded = processParsed();
  if (!isLoaded) clearData();
  return isLoaded;
}

namespace {
// std::istream を StreamReader::Source として晒すアダプタ。
// PSDFile が StreamReader 経由でロードした際、shared_ptr<Source> 経由で
// 全 IteratorBase クローンが同一の istream を共有する (read のたびに
// seek + read; キャッシュは reader 側持ち)。
class IStreamSource : public StreamReader::Source {
public:
  IStreamSource(std::istream &s, size_t totalSize)
    : s_(&s), size_(totalSize) {}
  size_t size() const override { return size_; }
  size_t read(uint8_t *out, size_t offset, size_t len) override {
    s_->clear();
    s_->seekg((std::streamoff)offset, std::ios::beg);
    if (!s_->good()) return 0;
    s_->read(reinterpret_cast<char *>(out), (std::streamsize)len);
    auto got = s_->gcount();
    return (got > 0) ? (size_t)got : 0;
  }
private:
  std::istream *s_;
  size_t size_;
};
} // namespace

bool PSDFile::parseFromStream_(std::istream &stream) {
  // 呼び出し元が clearData() / ownedStream_ の管理を担う。
  isLoaded = false;
  stream.clear();
  stream.seekg(0, std::ios::end);
  std::streamoff total = stream.tellg();
  if (total <= 0) return false;
  stream.seekg(0, std::ios::beg);
  auto src = std::make_shared<IStreamSource>(stream, (size_t)total);
  StreamReader reader(src);
  if (!parsePSD(reader, *this)) return false;
  isLoaded = processParsed();
  return isLoaded;
}

bool PSDFile::loadFromStream(std::istream &stream) {
  clearData();
  bool ok = parseFromStream_(stream);
  if (!ok) clearData();
  return ok;
}

bool PSDFile::save(const char *filename) {
  if (!isLoaded) return false;
  FileWriter w(filename);
  if (!w.ok()) return false;
  return writePSD(w, *this);
}

bool PSDFile::loadFromStream(std::unique_ptr<std::istream> stream) {
  // clearData() を先にやってから ownedStream_ にセットする。
  // 順序を逆にすると委譲先の clearData が握ったばかりの stream を消す (旧バグ)。
  clearData();
  if (!stream) return false;
  ownedStream_ = std::move(stream);
  bool ok = parseFromStream_(*ownedStream_);
  if (!ok) clearData();
  return ok;
}

} // namespace psd
