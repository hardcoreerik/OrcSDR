#include "orcdec_internal.hpp"

extern "C" {

int orcdec_info(int kind, orcdec_info_t* out) {
  if (!out) return -1;
  switch (kind) {
    case ORCDEC_POCSAG: *out = {kind, ORCDEC_FMT_CU8, 960000, 2, "pocsag"}; return 0;
    case ORCDEC_FT8: *out = {kind, ORCDEC_FMT_S16, 12000, 2, "ft8"}; return 0;
    case ORCDEC_SSTV: *out = {kind, ORCDEC_FMT_S16, 48000, 2, "sstv"}; return 0;
    case ORCDEC_ADSB: *out = {kind, ORCDEC_FMT_CU8, 2048000, 2, "adsb"}; return 0;
    case ORCDEC_ACARS: *out = {kind, ORCDEC_FMT_CU8, 240000, 2, "acars"}; return 0;
    case ORCDEC_LORA: *out = {kind, ORCDEC_FMT_CU8, 960000, 2, "lora"}; return 0;
    case ORCDEC_JS8: *out = {kind, ORCDEC_FMT_S16, 12000, 2, "js8"}; return 0;
    case ORCDEC_P25: *out = {kind, ORCDEC_FMT_CU8, 960000, 2, "p25"}; return 0;
  }
  return -1;
}

orcdec_t* orcdec_create(int kind, const char* options) {
  switch (kind) {
    case ORCDEC_POCSAG: return orcdec_make_pocsag(options);
    case ORCDEC_FT8: return orcdec_make_ft8(options);
    case ORCDEC_SSTV: return orcdec_make_sstv(options);
    case ORCDEC_ADSB: return orcdec_make_adsb(options);
    case ORCDEC_ACARS: return orcdec_make_acars(options);
    case ORCDEC_LORA: return orcdec_make_lora(options);
    case ORCDEC_JS8: return orcdec_make_js8(options);
    case ORCDEC_P25: return orcdec_make_p25(options);
  }
  return nullptr;
}

void orcdec_destroy(orcdec_t* d) { delete d; }
void orcdec_reset(orcdec_t* d) { if (d) { d->events.clear(); d->dropped = 0; d->sequence = 0; d->reset(); } }

int orcdec_feed(orcdec_t* d, const void* data, size_t bytes) {
  if (!d || (!data && bytes)) return -1;
  const size_t before = d->events.size();
  const int rc = d->feed(data, bytes);
  return rc < 0 ? rc : static_cast<int>(d->events.size() - before);
}

int orcdec_command(orcdec_t* d, const char* command, uint64_t arg) {
  if (!d || !command) return -1;
  const size_t before = d->events.size();
  const int rc = d->command(command, arg);
  return rc < 0 ? rc : static_cast<int>(d->events.size() - before);
}

int orcdec_poll(orcdec_t* d, char* json, size_t capacity) {
  if (!d || !json || !capacity || d->events.empty()) return 0;
  const std::string e = std::move(d->events.front());
  d->events.pop_front();
  if (e.size() + 1 > capacity) return -1;
  memcpy(json, e.c_str(), e.size() + 1);
  return static_cast<int>(e.size());
}

const void* orcdec_attachment(orcdec_t* d, uint32_t seq, size_t* bytes) {
  if (!d || !bytes) return nullptr;
  return d->attachment(seq, bytes);
}

uint32_t orcdec_dropped(const orcdec_t* d) { return d ? d->dropped : 0; }
const char* orcdec_version(void) { return "orcdec 0.1"; }

}  // extern "C"
