#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace orcsdr::storage::benchmark {

constexpr std::array<size_t, 5> kChunkBytes{
    4 * 1024, 16 * 1024, 32 * 1024, 64 * 1024, 128 * 1024};

constexpr size_t repetitions(size_t chunk_bytes) {
  return chunk_bytes == 32 * 1024 || chunk_bytes == 64 * 1024 ? 3 : 1;
}

constexpr double mib_per_second(uint64_t bytes, uint64_t elapsed_us) {
  return elapsed_us == 0 ? 0.0
                         : static_cast<double>(bytes) * 1000000.0 /
                               (1048576.0 * static_cast<double>(elapsed_us));
}

}  // namespace orcsdr::storage::benchmark
