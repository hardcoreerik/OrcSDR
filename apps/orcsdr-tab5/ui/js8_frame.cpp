#include "js8_frame.hpp"

namespace orcsdr::js8 {

bool tones_valid(const RawFrame& frame) {
  const Profile& p = profile(frame.submode);
  for (uint8_t tone : frame.tones)
    if (tone >= p.tone_count) return false;
  return true;
}

bool sync_matches(const RawFrame& frame) {
  const Profile& p = profile(frame.submode);
  if (!p.sync_pattern_verified || !tones_valid(frame)) return false;
  for (const SyncBlock& block : p.sync)
    for (size_t i = 0; i < block.tones.size(); ++i)
      if (frame.tones[block.first_symbol + i] != block.tones[i]) return false;
  return true;
}

bool extract_data_tones(const RawFrame& frame, DataTones* out) {
  if (out == nullptr || !sync_matches(frame)) return false;
  const Profile& p = profile(frame.submode);
  size_t n = 0;
  for (const DataBlock& block : p.data)
    for (size_t i = 0; i < block.length; ++i)
      out->tones[n++] = frame.tones[block.first_symbol + i];
  return n == out->tones.size();
}

bool self_check_frame() {
  // Public JS8Call API documentation TX.FRAME example. This is used as a
  // protocol fixture only; OrcSDR does not contain a JS8 transmitter.
  constexpr std::array<uint8_t, kChannelSymbols> kApiFrame{{
      4,2,5,6,1,3,0, 1,0,2,6,6,3,1,6,6,4,0,1,7,0,7,2,6,2,6,0,4,3,4,5,2,3,5,2,0,
      4,2,5,6,1,3,0, 3,4,2,5,4,5,7,0,1,6,3,6,7,0,2,3,5,6,4,5,7,4,0,0,1,7,3,6,4,
      4,2,5,6,1,3,0}};
  RawFrame f{};
  f.tones = kApiFrame;
  DataTones data{};
  if (!tones_valid(f) || !sync_matches(f) || !extract_data_tones(f, &data)) return false;
  if (data.tones[0] != 1 || data.tones[28] != 0 || data.tones[29] != 3 || data.tones[57] != 4) return false;
  RawFrame bad = f;
  bad.tones[36] = 7;
  if (sync_matches(bad)) return false;
  bad = f;
  bad.tones[20] = 8;
  return !tones_valid(bad) && !extract_data_tones(bad, &data);
}

}  // namespace orcsdr::js8
