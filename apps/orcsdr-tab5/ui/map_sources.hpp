#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "orcmap/byte_source.hpp"

// Where basemap bytes come from. The embedded world map is a read-only PMTiles archive linked into the app image
// (flash-mapped read-only data); it is read in place and never copied to RAM.
namespace orcsdr::map_sources {

// The embedded world overview, or nullptr when this firmware was built without one.
const uint8_t* embedded_world(size_t* size);

class EmbeddedWorldSource final : public orcmap::ByteSource {
 public:
  EmbeddedWorldSource() { data_ = embedded_world(&size_); }
  size_t Read(uint64_t offset, void* destination, size_t length) override {
    if (data_ == nullptr || destination == nullptr || offset >= size_) return 0;
    const uint64_t available = size_ - offset;
    const size_t count = length < available ? length : static_cast<size_t>(available);
    std::memcpy(destination, data_ + offset, count);
    return count;
  }
  uint64_t Size() const override { return size_; }
  bool Valid() const override { return data_ != nullptr && size_ > 0; }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
};

}  // namespace orcsdr::map_sources
