#include "airband_catalog.hpp"

#include "orcsdr_storage.hpp"

namespace orcsdr::airband {
namespace {

// Adapts an SD file to LineSource. Empty lines are reported as length 0, end of file as -1.
class FileLineSource final : public LineSource {
 public:
  explicit FileLineSource(storage::File& file) : file_(file) {}

  int read_line(char* buffer, size_t capacity) override {
    if (capacity == 0) return -1;
    if (!file_.available()) return -1;
    const size_t size = file_.readBytesUntil('\n', buffer, capacity - 1);
    buffer[size] = '\0';
    return static_cast<int>(size);
  }

 private:
  storage::File& file_;
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
