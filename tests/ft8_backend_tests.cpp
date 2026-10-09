#include "../apps/orcsdr-tab5/ui/ft8_decoder_backend.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

using namespace orcsdr::ft8;

struct Recorder {
  int begin_slot_calls = 0;
  uint32_t last_epoch_s = 0;
  int begin_slot_ms_calls = 0;
  uint64_t last_epoch_ms = 0;
  DigitalMode mode = DigitalMode::ft8;
  uint32_t caps = decoder_cap_ft8;
};

bool begin(void*, uint32_t) { return true; }
void reset(void*) {}
bool begin_slot(void* c, uint32_t epoch) { auto* r = static_cast<Recorder*>(c); ++r->begin_slot_calls; r->last_epoch_s = epoch; return true; }
bool offer_audio(void*, const int16_t*, size_t) { return true; }
size_t finish_slot(void*, Decode*, size_t) { return 0; }
bool set_mode(void* c, DigitalMode mode) { static_cast<Recorder*>(c)->mode = mode; return true; }
bool begin_slot_ms(void* c, uint64_t ms) { auto* r = static_cast<Recorder*>(c); ++r->begin_slot_ms_calls; r->last_epoch_ms = ms; return true; }
uint32_t capabilities(void* c) { return static_cast<Recorder*>(c)->caps; }

DecoderBackend legacy(Recorder* r) {
  DecoderBackend b;   // only the five required callbacks, exactly as before the multi-mode seam
  b.context = r; b.begin = begin; b.reset = reset; b.begin_slot = begin_slot;
  b.offer_audio = offer_audio; b.finish_slot = finish_slot;
  return b;
}

}  // namespace

int main() {
  // An entirely unbound backend stays unbound: no capabilities, nothing selectable, no slot starts.
  DecoderBackend unbound;
  assert(!backend_valid(unbound) && backend_capabilities(unbound) == 0);
  assert(!mode_supported(0, DigitalMode::ft8) && !backend_set_mode(unbound, DigitalMode::ft8));
  assert(!backend_begin_slot(unbound, 15000));

  // The existing FT8 backend validates with none of the new callbacks and is treated as FT8-only.
  Recorder r;
  DecoderBackend old = legacy(&r);
  assert(backend_valid(old));
  assert(old.set_mode == nullptr && old.begin_slot_ms == nullptr && old.capabilities == nullptr);
  assert(backend_capabilities(old) == decoder_cap_ft8);
  assert(backend_set_mode(old, DigitalMode::ft8));
  assert(!backend_set_mode(old, DigitalMode::ft4));
  assert(!backend_set_mode(old, DigitalMode::js8_normal) && !backend_set_mode(old, DigitalMode::js8_60_experimental));

  // Legacy slot starts use the whole-second callback, unchanged.
  assert(backend_begin_slot(old, 1'700'000'000'000ull) && r.begin_slot_calls == 1 && r.last_epoch_s == 1'700'000'000u);
  // A slot that does not start on a whole second cannot be represented there and must not be rounded.
  assert(!backend_begin_slot(old, 1'700'000'007'500ull) && r.begin_slot_calls == 1);

  // A multi-mode backend: millisecond starts reach begin_slot_ms exactly (FT4's half-second boundaries).
  Recorder multi;
  multi.caps = decoder_cap_ft8 | decoder_cap_ft4;
  DecoderBackend modern = legacy(&multi);
  modern.set_mode = set_mode; modern.begin_slot_ms = begin_slot_ms; modern.capabilities = capabilities;
  assert(backend_valid(modern));
  assert(backend_capabilities(modern) == (decoder_cap_ft8 | decoder_cap_ft4));
  assert(mode_supported(modern.capabilities(&multi), DigitalMode::ft4));
  assert(!mode_supported(multi.caps, DigitalMode::js8_fast));
  assert(backend_set_mode(modern, DigitalMode::ft4) && multi.mode == DigitalMode::ft4);
  assert(!backend_set_mode(modern, DigitalMode::js8_slow) && multi.mode == DigitalMode::ft4);   // unsupported: no side effect
  assert(backend_begin_slot(modern, 1'700'000'007'500ull) && multi.begin_slot_ms_calls == 1);
  assert(multi.last_epoch_ms == 1'700'000'007'500ull && multi.begin_slot_calls == 0);
  // FT4 slot boundaries round-trip: every 7.5 s the millisecond epoch advances by exactly 7500.
  const uint64_t base = 1'700'000'000'000ull;
  for (int i = 0; i < 8; ++i) {
    assert(backend_begin_slot(modern, base + static_cast<uint64_t>(i) * 7500u));
    assert(multi.last_epoch_ms == base + static_cast<uint64_t>(i) * 7500u);
  }

  // JS8 capability covers every JS8 speed, including the experimental one (which the UI still labels experimental).
  Recorder js8;
  js8.caps = decoder_cap_js8;
  assert(mode_supported(js8.caps, DigitalMode::js8_normal) && mode_supported(js8.caps, DigitalMode::js8_60_experimental));
  assert(!mode_supported(js8.caps, DigitalMode::ft8) && !mode_supported(js8.caps, DigitalMode::ft4));
  // JS8 Normal alone enables only Normal; the other submodes need the all-speeds bit.
  assert(mode_supported(decoder_cap_js8_normal, DigitalMode::js8_normal));
  assert(!mode_supported(decoder_cap_js8_normal, DigitalMode::js8_fast) && !mode_supported(decoder_cap_js8_normal, DigitalMode::js8_40) &&
         !mode_supported(decoder_cap_js8_normal, DigitalMode::js8_slow) && !mode_supported(decoder_cap_js8_normal, DigitalMode::js8_60_experimental));
  assert(!mode_supported(decoder_cap_js8_normal, DigitalMode::ft8) && !mode_supported(decoder_cap_ft8 | decoder_cap_ft4, DigitalMode::js8_normal));
  assert(mode_supported(decoder_cap_js8, DigitalMode::js8_fast));
  assert(decoder_cap_ft8 == 1u && decoder_cap_ft4 == 2u && decoder_cap_js8 == 4u &&
         decoder_cap_assisted == 8u && decoder_cap_message_assembly == 16u);   // the decoder workstream's bit values

  return 0;
}
