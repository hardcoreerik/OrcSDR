#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "orcsdr_storage.hpp"

namespace orcsdr::test_fs {

struct Entry {
  std::vector<uint8_t> data;
  uint64_t modified = 1700000000ULL;
};

struct State {
  std::map<std::string, Entry> files;
  std::vector<std::string> dir_order;
  bool fail_flush = false;
  bool fail_close = false;
  bool fail_write = false;
  bool fail_read = false;
  std::string fail_rename_from;
  std::string fail_rename_from_2;
  bool fail_exists_or_mkdir = false;
};

inline State& instance() {
  static State s;
  return s;
}

inline void reset() { instance() = State{}; }

}  // namespace orcsdr::test_fs
