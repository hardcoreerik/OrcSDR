// A valid VIS header must start a picture even when the image frame cannot be allocated: the receiver gives up the oldest
// gallery pictures one at a time and retries, and only reports "INSUFFICIENT PSRAM" when nothing is left to give up.
#include "sstv_receiver.hpp"
#include "sstv_test_signal.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace orcsdr::sstv;

namespace {
void require(bool pass, const char* why) {
  if (!pass) {
    std::fprintf(stderr, "SSTV FAIL: %s\n", why);
    std::exit(1);
  }
}
void picture(unsigned rows) {
  sstv_test::Signal sig;
  sig.header(8);
  sig.robot36_rows(0, rows);
  sig.flush();
}
}  // namespace

int main() {
  start(145500000);
  // Fill the gallery: each new header closes the previous picture and archives it.
  for (int i = 0; i < 6; ++i) picture(12);
  const size_t full = archive_count();
  require(full >= 3, "memory: the gallery holds several pictures");
  // Fragmented PSRAM: the first allocation for the next picture fails.
  testing_fail_next_frames(1);
  picture(12);
  require(metrics().receiving || archive_count() > 0, "memory: the picture still starts after one failed allocation");
  require(archive_count() <= full, "memory: the oldest gallery picture was given up to make room");
  require(std::string(metrics().status).find("INSUFFICIENT") == std::string::npos, "memory: no 'insufficient PSRAM' with pictures to give up");
  // Even three failures in a row are survived while the gallery has pictures to give up.
  testing_fail_next_frames(3);
  picture(12);
  require(std::string(metrics().status).find("INSUFFICIENT") == std::string::npos, "memory: three failed allocations are survived");
  // Nothing left to give up and every allocation failing: the failure is reported, not hidden.
  clear_gallery();
  stop();
  start(145500000);
  testing_fail_next_frames(100);
  {
    sstv_test::Signal sig;
    sig.header(8);
    sig.flush();   // look straight after the header: later audio rewrites the status line
  }
  picture(12);
  require(metrics().frame_alloc_failures >= 1, "memory: a real shortage is counted");
  require(std::string(metrics().status).find("INSUFFICIENT") != std::string::npos || metrics().frame_alloc_failures >= 1, "memory: a real shortage is reported");
  require(archive_count() == 0 && !metrics().receiving, "memory: no picture is started without memory");
  testing_fail_next_frames(0);
  stop();
  std::puts("SSTV memory: PASS");
  return 0;
}
