#pragma once

// Shared by the host-only FT8/FT4 WAV tools (benchmark and diagnose): a strict reader for the recordings the decoder is
// measured on (RIFF PCM, mono, 12 kHz, 16-bit) and a small integer argument parser.

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace ft8_wav_tools {

uint16_t le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         static_cast<uint16_t>(p[1] << 8u);
}

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8u) |
         (static_cast<uint32_t>(p[2]) << 16u) |
         (static_cast<uint32_t>(p[3]) << 24u);
}

bool read_exact(std::ifstream* file, void* out, std::size_t bytes) {
  file->read(static_cast<char*>(out), static_cast<std::streamsize>(bytes));
  return file->good() || static_cast<std::size_t>(file->gcount()) == bytes;
}

struct Wav {
  uint32_t sample_rate = 0;
  std::vector<int16_t> samples;
};

bool read_wav(const char* path, Wav* wav) {
  if (path == nullptr || wav == nullptr) return false;
  std::ifstream file(path, std::ios::binary);
  if (!file) return false;

  std::array<uint8_t, 12> header{};
  if (!read_exact(&file, header.data(), header.size()) ||
      std::memcmp(header.data(), "RIFF", 4) != 0 ||
      std::memcmp(header.data() + 8, "WAVE", 4) != 0)
    return false;

  bool have_fmt = false;
  bool have_data = false;
  uint16_t format = 0, channels = 0, bits = 0, block_align = 0;
  uint32_t sample_rate = 0;
  std::vector<uint8_t> data;

  while (file && !(have_fmt && have_data)) {
    std::array<uint8_t, 8> chunk{};
    if (!read_exact(&file, chunk.data(), chunk.size())) break;
    const uint32_t size = le32(chunk.data() + 4);
    if (size > 16u * 1024u * 1024u) return false;

    if (std::memcmp(chunk.data(), "fmt ", 4) == 0) {
      if (size < 16) return false;
      std::vector<uint8_t> fmt(size);
      if (!read_exact(&file, fmt.data(), fmt.size())) return false;
      format = le16(fmt.data());
      channels = le16(fmt.data() + 2);
      sample_rate = le32(fmt.data() + 4);
      block_align = le16(fmt.data() + 12);
      bits = le16(fmt.data() + 14);
      have_fmt = true;
    } else if (std::memcmp(chunk.data(), "data", 4) == 0) {
      data.resize(size);
      if (!read_exact(&file, data.data(), data.size())) return false;
      have_data = true;
    } else {
      file.seekg(size, std::ios::cur);
      if (!file) return false;
    }
    if ((size & 1u) != 0) file.seekg(1, std::ios::cur);
  }

  if (!have_fmt || !have_data || format != 1 || channels != 1 ||
      sample_rate != 12000 || bits != 16 || block_align != 2 ||
      (data.size() & 1u) != 0)
    return false;

  wav->sample_rate = sample_rate;
  wav->samples.resize(data.size() / 2);
  for (std::size_t i = 0; i < wav->samples.size(); ++i)
    wav->samples[i] = static_cast<int16_t>(le16(data.data() + i * 2));
  return true;
}

bool parse_u8(const char* text, uint8_t* value) {
  if (text == nullptr || value == nullptr || *text == '\0') return false;
  unsigned long v = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + static_cast<unsigned long>(*p - '0');
    if (v > 255) return false;
  }
  *value = static_cast<uint8_t>(v);
  return true;
}

}  // namespace ft8_wav_tools
