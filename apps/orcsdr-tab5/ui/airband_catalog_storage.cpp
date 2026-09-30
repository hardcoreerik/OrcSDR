#include "airband_catalog.hpp"

#include "orcsdr_storage.hpp"

#include <esp_heap_caps.h>

namespace orcsdr::airband {
namespace {

// Adapts an SD file to LineSource with chunked reads. File::available() seeks to the end and
// back, which is O(cluster chain) on FAT, so it must never be called per line on a large index.
class FileLineSource final : public LineSource {
 public:
  FileLineSource(storage::File& file, char* chunk, size_t chunk_size)
      : file_(file), chunk_(chunk), chunk_size_(chunk_size) {}

  int read_line(char* buffer, size_t capacity) override {
    if (capacity == 0) return -1;
    size_t length = 0;
    bool any = false;
    while (true) {
      if (position_ == filled_) {
        if (eof_) break;
        filled_ = file_.read(chunk_, chunk_size_);
        position_ = 0;
        if (filled_ == 0) {
          eof_ = true;
          break;
        }
      }
      const char c = chunk_[position_++];
      any = true;
      if (c == '\n') {
        buffer[length] = '\0';
        return static_cast<int>(length);
      }
      if (length + 1 < capacity) buffer[length++] = c;
    }
    if (!any) return -1;
    buffer[length] = '\0';
    return static_cast<int>(length);
  }

 private:
  storage::File& file_;
  char* chunk_;
  size_t chunk_size_;
  size_t position_ = 0;
  size_t filled_ = 0;
  bool eof_ = false;
};

}  // namespace

bool Catalog::load(storage::FileSystem* filesystem, const Location& location) {
  clear();
  location_configured_ = location.configured;
  if (!location.configured) {
    result_ = LoadResult::no_location;
    return false;
  }
  if (!filesystem) {
    result_ = LoadResult::no_source;
    return false;
  }
  storage::File file = filesystem->open(kCatalogPath);
  if (!file) file = filesystem->open(kLegacyCatalogPath);
  if (!file) {
    result_ = LoadResult::no_source;
    return false;
  }
  // Large reads amortise the FAT/SD command overhead; fall back to a small stack buffer if the
  // heap is tight (a catalog load must never fail for lack of a 4 KB buffer).
  constexpr size_t kBigChunk = 4096;
  char small[256];
  char* big = static_cast<char*>(
      heap_caps_malloc(kBigChunk, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  FileLineSource source(file, big != nullptr ? big : small,
                        big != nullptr ? kBigChunk : sizeof(small));
  const bool loaded = load_from(source, location);
  file.close();
  if (big != nullptr) heap_caps_free(big);
  return loaded;
}

}  // namespace orcsdr::airband
