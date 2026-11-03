// Replays a capture file through orcdec and prints the events, one JSON object per line. The same driver logic
// exists for WASM (tests/decoders/replay_wasm.mjs) and for the node (POST /api/v1/decoder), so a capture can be run
// through every build and the outputs compared.
//
//   orcdec_replay <pocsag|ft8|sstv> <capture> [--chunk BYTES] [--options "k=v;k=v"] [--slot-seconds S] [--epoch-ms MS] [--attach-dir DIR]
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "orcdec.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <pocsag|ft8|sstv> <capture> [--chunk BYTES] [--options k=v;k=v] [--slot-seconds S] [--epoch-ms MS]\n", argv[0]);
    return 2;
  }
  const std::string name = argv[1];
  const int kind = name == "pocsag" ? ORCDEC_POCSAG : name == "ft8" ? ORCDEC_FT8 : name == "js8" ? ORCDEC_JS8 : name == "lora" ? ORCDEC_LORA : name == "sstv" ? ORCDEC_SSTV : name == "adsb" ? ORCDEC_ADSB : name == "acars" ? ORCDEC_ACARS : name == "p25" ? ORCDEC_P25 : 0;
  if (!kind) { fprintf(stderr, "unknown decoder %s\n", argv[1]); return 2; }
  size_t chunk = 4096;
  std::string options;
  unsigned slot_seconds = 0;
  unsigned long long epoch_ms = 0;
  std::string attach_dir;
  for (int i = 3; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--chunk")) chunk = strtoul(argv[i + 1], nullptr, 10);
    else if (!strcmp(argv[i], "--options")) options = argv[i + 1];
    else if (!strcmp(argv[i], "--slot-seconds")) slot_seconds = strtoul(argv[i + 1], nullptr, 10);
    else if (!strcmp(argv[i], "--epoch-ms")) epoch_ms = strtoull(argv[i + 1], nullptr, 10);
    else if (!strcmp(argv[i], "--attach-dir")) attach_dir = argv[i + 1];
  }
  FILE* f = fopen(argv[2], "rb");
  if (!f) { perror(argv[2]); return 1; }
  std::vector<unsigned char> data;
  unsigned char buf[65536];
  for (size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) data.insert(data.end(), buf, buf + n);
  fclose(f);

  orcdec_info_t info;
  orcdec_info(kind, &info);
  orcdec_t* d = orcdec_create(kind, options.empty() ? nullptr : options.c_str());
  if (!d) { fprintf(stderr, "cannot create decoder\n"); return 1; }
  const size_t sample = info.bytes_per_sample;
  chunk = chunk / sample * sample;
  if (!chunk) chunk = sample;
  const size_t slot_bytes = slot_seconds ? (size_t)slot_seconds * info.sample_rate_hz * sample : 0;
  char line[8192];
  auto drain = [&] {
    for (int n; (n = orcdec_poll(d, line, sizeof line)) != 0;) {
      if (n <= 0) continue;
      puts(line);
      const char* seq = strstr(line, "\"seq\":");
      if (!attach_dir.empty() && seq) {   // binary attachment of this event (an SSTV image), written as <seq>.rgb565
        size_t bytes = 0;
        const unsigned id = (unsigned)strtoul(seq + 6, nullptr, 10);
        const void* a = orcdec_attachment(d, id, &bytes);
        if (a) {
          const std::string path = attach_dir + "/" + std::to_string(id) + ".rgb565";
          if (FILE* o = fopen(path.c_str(), "wb")) { fwrite(a, 1, bytes, o); fclose(o); }
        }
      }
    }
  };
  size_t pos = 0, slot_index = 0;
  while (pos < data.size()) {
    if (slot_bytes && pos % slot_bytes == 0) orcdec_command(d, "slot_begin", epoch_ms + slot_index * slot_seconds * 1000ull);
    size_t n = std::min(chunk, data.size() - pos);
    if (slot_bytes) n = std::min(n, slot_bytes - pos % slot_bytes);
    orcdec_feed(d, data.data() + pos, n);
    pos += n;
    drain();
    if (slot_bytes && pos % slot_bytes == 0) { orcdec_command(d, "finish", 0); ++slot_index; drain(); }
  }
  if (!slot_bytes || pos % slot_bytes) { orcdec_command(d, "finish", 0); drain(); }
  orcdec_destroy(d);
  return 0;
}
