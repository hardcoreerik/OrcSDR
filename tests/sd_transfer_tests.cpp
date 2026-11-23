#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "fake_storage.hpp"
#include "hex_codec.hpp"
#include "mbedtls/sha256.h"
#include "sd_transfer.hpp"

namespace {

int failures = 0;
#define CHECK(condition) \
  do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

std::string g_text_out;
std::vector<uint8_t> g_bin_out;
std::vector<uint8_t> g_bin_in;
size_t g_bin_in_pos = 0;
bool g_radio_busy = false;
bool g_sd_ok = true;
orcsdr::storage::FileSystem g_fs_obj;

void reset_io() {
  g_text_out.clear();
  g_bin_out.clear();
  g_bin_in.clear();
  g_bin_in_pos = 0;
  g_radio_busy = false;
  g_sd_ok = true;
  orcsdr::test_sha::fail_start = false;
  orcsdr::test_sha::fail_update = false;
  orcsdr::test_sha::fail_finish = false;
}

const orcsdr::sd_transfer::Hooks kHooks{
    [](const char* t) { g_text_out += t; },
    [](const char* t) { g_text_out += t; g_text_out += '\n'; },
    [](const uint8_t* v, size_t n) {
      static const char hex[] = "0123456789abcdef";
      for (size_t i = 0; i < n; ++i) {
        g_text_out.push_back(hex[v[i] >> 4]);
        g_text_out.push_back(hex[v[i] & 0xf]);
      }
    },
    [](const uint8_t* d, size_t n) -> size_t {
      g_bin_out.insert(g_bin_out.end(), d, d + n);
      return n;
    },
    [](uint8_t* out, size_t n, uint32_t) -> size_t {
      const size_t avail = g_bin_in_pos < g_bin_in.size() ? g_bin_in.size() - g_bin_in_pos : 0;
      const size_t take = std::min(avail, n);
      if (take > 0) {
        std::memcpy(out, g_bin_in.data() + g_bin_in_pos, take);
        g_bin_in_pos += take;
      }
      return take;
    },
    orcsdr::hex_codec::decode,
    orcsdr::hex_codec::decode_text,
    [] { return g_radio_busy; },
    [] {},
    [] { return g_sd_ok; },
    []() -> orcsdr::storage::FileSystem* { return &g_fs_obj; },
};

std::string hex_of(const std::string& s) {
  static const char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(s.size() * 2);
  for (unsigned char c : s) {
    out.push_back(hex[c >> 4]);
    out.push_back(hex[c & 0xf]);
  }
  return out;
}

std::string sha_hex_of(const std::vector<uint8_t>& data) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, data.data(), data.size());
  uint8_t dig[32]{};
  mbedtls_sha256_finish(&ctx, dig);
  mbedtls_sha256_free(&ctx);
  static const char hex[] = "0123456789abcdef";
  std::string out;
  for (uint8_t b : dig) {
    out.push_back(hex[b >> 4]);
    out.push_back(hex[b & 0xf]);
  }
  return out;
}

void start_put(const std::string& path, const std::vector<uint8_t>& payload) {
  std::string args = std::to_string(payload.size()) + " " + sha_hex_of(payload) + " " + hex_of(path);
  orcsdr::sd_transfer::put_begin(kHooks, args.data());
  g_bin_in = payload;
  g_bin_in_pos = 0;
}

}  // namespace

int main() {
  using namespace orcsdr::sd_transfer;

  // 1. SD_REMOVE refused during active GET and active PUT; allowed when idle.
  orcsdr::test_fs::reset();
  reset_io();
  orcsdr::test_fs::instance().files["/orcsdr/keep.bin"] = {{1, 2, 3, 4}, 10};
  get_begin(kHooks, hex_of("/orcsdr/keep.bin").c_str());
  CHECK(get_active() && g_text_out.find("SD_GET_READY") != std::string::npos);
  g_text_out.clear();
  remove(kHooks, hex_of("/orcsdr/keep.bin").c_str());
  CHECK(g_text_out == "SD_REMOVE_ERROR transfer_busy\n");
  CHECK(orcsdr::test_fs::instance().files.count("/orcsdr/keep.bin") == 1);
  get_abort(kHooks, "done");

  reset_io();
  start_put("/orcsdr/puttarget.bin", {10, 20, 30});
  CHECK(put_active());
  g_text_out.clear();
  remove(kHooks, hex_of("/orcsdr/puttarget.bin.part").c_str());
  CHECK(g_text_out == "SD_REMOVE_ERROR transfer_busy\n");
  CHECK(orcsdr::test_fs::instance().files.count("/orcsdr/puttarget.bin.part") == 1);
  put_abort(kHooks, "done");

  reset_io();
  remove(kHooks, hex_of("/orcsdr/keep.bin").c_str());
  CHECK(g_text_out.find("SD_REMOVE_DONE") == 0);
  CHECK(orcsdr::test_fs::instance().files.count("/orcsdr/keep.bin") == 0);

  // 2. ACARS radio_busy bypass limited to dated/undated ACARS logs only.
  orcsdr::test_fs::reset();
  reset_io();
  g_radio_busy = true;
  orcsdr::test_fs::instance().files["/orcsdr/acars/20261122.jsonl"] = {{'a', '\n'}, 1};
  orcsdr::test_fs::instance().files["/orcsdr/acars/undated.jsonl"] = {{'b', '\n'}, 1};
  orcsdr::test_fs::instance().files["/orcsdr/acars/other.txt"] = {{'c'}, 1};
  get_begin(kHooks, hex_of("/orcsdr/acars/other.txt").c_str());
  CHECK(g_text_out == "SD_GET_ERROR radio_busy\n");
  g_text_out.clear();
  get_begin(kHooks, hex_of("/orcsdr/acars/20261122.jsonl").c_str());
  CHECK(g_text_out.find("SD_GET_READY") == 0);
  get_abort(kHooks, "done");
  g_text_out.clear();
  get_begin(kHooks, hex_of("/orcsdr/acars/undated.jsonl").c_str());
  CHECK(g_text_out.find("SD_GET_READY") == 0);
  get_abort(kHooks, "done");

  // 3. Quotes in filenames are rejected on GET, PUT, REMOVE.
  orcsdr::test_fs::reset();
  reset_io();
  const std::string bad_quote = hex_of("/orcsdr/bad\"name.txt");
  get_begin(kHooks, bad_quote.c_str());
  CHECK(g_text_out == "SD_GET_ERROR invalid_path\n");
  g_text_out.clear();
  remove(kHooks, bad_quote.c_str());
  CHECK(g_text_out == "SD_REMOVE_ERROR invalid_path\n");
  g_text_out.clear();
  start_put("/orcsdr/bad\"name.txt", {1, 2});
  CHECK(g_text_out == "SD_PUT_ERROR invalid_begin\n");

  // 4. SD_PUT flush() failure and close() failure preserve original file and remove .part.
  orcsdr::test_fs::reset();
  reset_io();
  orcsdr::test_fs::instance().files["/orcsdr/cfg.json"] = {{'o', 'l', 'd'}, 1};
  orcsdr::test_fs::instance().fail_flush = true;
  start_put("/orcsdr/cfg.json", {'n', 'e', 'w'});
  g_text_out.clear();
  put_chunk(kHooks, "3");
  CHECK(!put_active() && !active());
  CHECK(g_text_out.find("SD_PUT_ERROR flush_failed") != std::string::npos);
  CHECK((orcsdr::test_fs::instance().files["/orcsdr/cfg.json"].data == std::vector<uint8_t>{'o', 'l', 'd'}));
  CHECK(orcsdr::test_fs::instance().files.count("/orcsdr/cfg.json.bak") == 0);
  CHECK(orcsdr::test_fs::instance().files.count("/orcsdr/cfg.json.part") == 0);

  orcsdr::test_fs::reset();
  reset_io();
  orcsdr::test_fs::instance().files["/orcsdr/cfg.json"] = {{'o', 'l', 'd'}, 1};
  orcsdr::test_fs::instance().fail_close = true;
  start_put("/orcsdr/cfg.json", {'n', 'e', 'w'});
  g_text_out.clear();
  put_chunk(kHooks, "3");
  CHECK(!put_active() && !active());
  CHECK(g_text_out.find("SD_PUT_ERROR close_failed") != std::string::npos);
  CHECK((orcsdr::test_fs::instance().files["/orcsdr/cfg.json"].data == std::vector<uint8_t>{'o', 'l', 'd'}));
  CHECK(orcsdr::test_fs::instance().files.count("/orcsdr/cfg.json.part") == 0);

  // 5. SD_PUT rename failure: restored vs rename_and_restore_failed.
  orcsdr::test_fs::reset();
  reset_io();
  orcsdr::test_fs::instance().files["/orcsdr/cfg.json"] = {{'o', 'l', 'd'}, 1};
  orcsdr::test_fs::instance().fail_rename_from = "/orcsdr/cfg.json.part";
  start_put("/orcsdr/cfg.json", {'n', 'e', 'w'});
  g_text_out.clear();
  put_chunk(kHooks, "3");
  CHECK(g_text_out.find("SD_PUT_ERROR rename_failed") != std::string::npos);
  CHECK((orcsdr::test_fs::instance().files["/orcsdr/cfg.json"].data == std::vector<uint8_t>{'o', 'l', 'd'}));

  orcsdr::test_fs::reset();
  reset_io();
  orcsdr::test_fs::instance().files["/orcsdr/cfg.json"] = {{'o', 'l', 'd'}, 1};
  orcsdr::test_fs::instance().fail_rename_from = "/orcsdr/cfg.json.part";
  orcsdr::test_fs::instance().fail_rename_from_2 = "/orcsdr/cfg.json.bak";
  start_put("/orcsdr/cfg.json", {'n', 'e', 'w'});
  g_text_out.clear();
  put_chunk(kHooks, "3");
  CHECK(g_text_out.find("SD_PUT_ERROR rename_and_restore_failed") != std::string::npos);

  // 6. SHA update failure reports sha_update on both GET and PUT.
  orcsdr::test_fs::reset();
  reset_io();
  orcsdr::test_fs::instance().files["/orcsdr/a.bin"] = {{1, 2, 3, 4}, 1};
  get_begin(kHooks, hex_of("/orcsdr/a.bin").c_str());
  orcsdr::test_sha::fail_update = true;
  g_text_out.clear();
  get_chunk(kHooks);
  CHECK(g_text_out == "SD_GET_ERROR sha_update\n");

  reset_io();
  start_put("/orcsdr/b.bin", {1, 2, 3, 4});
  orcsdr::test_sha::fail_update = true;
  g_text_out.clear();
  put_chunk(kHooks, "4");
  CHECK(g_text_out.find("SD_PUT_ERROR sha_update") != std::string::npos);

  // 7. SD_LIST includes 127-char paths, reports skipped for 128-char paths, and caps at kMaxListEntries.
  orcsdr::test_fs::reset();
  reset_io();
  const std::string p127 = "/orcsdr/" + std::string(119, 'a');
  const std::string p128 = "/orcsdr/" + std::string(120, 'b');
  orcsdr::test_fs::instance().files[p127] = {{1}, 1};
  orcsdr::test_fs::instance().files[p128] = {{2}, 2};
  list(kHooks);
  CHECK(g_text_out.find(hex_of(p127)) != std::string::npos);
  CHECK(g_text_out.find(hex_of(p128)) == std::string::npos);
  CHECK(g_text_out.find("SD_LIST_DONE count=1 skipped=1\n") != std::string::npos);

  orcsdr::test_fs::reset();
  reset_io();
  for (size_t i = 0; i < kMaxListEntries + 8; ++i) {
    const std::string p = "/orcsdr/f" + std::to_string(i);
    orcsdr::test_fs::instance().files[p] = {{1}, 1};
    orcsdr::test_fs::instance().dir_order.push_back(p);
  }
  list(kHooks);
  const std::string expected_done = "SD_LIST_DONE count=" + std::to_string(kMaxListEntries) + " truncated=1\n";
  CHECK(g_text_out.find(expected_done) != std::string::npos);

  if (failures == 0) std::printf("sd_transfer: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
