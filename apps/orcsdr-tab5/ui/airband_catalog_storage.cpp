#include "airband_catalog.hpp"

#include "orcsdr_storage.hpp"

namespace orcsdr::airband {
namespace {

// Adapts an SD file to LineSource with chunked reads. File::available() seeks to the end and
// back, which is O(cluster chain) on FAT, so it must never be called per line on a large index.
class FileLineSource final : public LineSource {
 public:
  explicit FileLineSource(storage::File& file) : file_(file) {}

  int read_line(char* buffer, size_t capacity) override {
    if (capacity == 0) return -1;
    size_t length = 0;
    bool any = false;
    while (true) {
      if (position_ == filled_) {
        if (eof_) break;
        filled_ = file_.read(chunk_, sizeof(chunk_));
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
  char chunk_[512];
  size_t position_ = 0;
  size_t filled_ = 0;
  bool eof_ = false;
};

}  // namespace

bool Catalog::load(storage::FileSystem* filesystem, const Location& location) {
  clear();
  location_configured_ = location.configured;
  if (!filesystem) return false;
  storage::File file = filesystem->open(kCatalogPath);
  if (!file) file = filesystem->open(kLegacyCatalogPath);
  if (!file) return false;
  FileLineSource source(file);
  const bool loaded = load_from(source, location);
  file.close();
  return loaded;
}

}  // namespace orcsdr::airband
