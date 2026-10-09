#include "js8_native_backend.hpp"

#include "js8_frontend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace orcsdr::js8::native {
namespace {
constexpr float kFirstHz = 200.0f;
constexpr float kBinSpacingHz = 6.25f;
constexpr size_t kBinCount = 449;        // 200 Hz to 3000 Hz in 6.25 Hz steps, the same passband the FT8 waterfall uses
constexpr uint8_t kRowsPerSymbol = 2;    // the sync search expects a half-symbol time step
constexpr float kStrongScore = 0.45f;
}  // namespace

Backend::~Backend() { end(); }

void* Backend::grab(size_t bytes) {
  return memory_.alloc != nullptr ? memory_.alloc(bytes) : std::malloc(bytes);
}

void Backend::drop(void* pointer) {
  if (pointer == nullptr) return;
  if (memory_.release != nullptr) memory_.release(pointer);
  else std::free(pointer);
}

bool Backend::begin(uint32_t sample_rate_hz, Submode submode, const Config& config, const Memory& memory) {
  end();
  if (sample_rate_hz != 12000u || !physical_layer_ready(submode)) return false;
  config_ = config;
  config_.candidate_limit = std::min<uint16_t>(std::max<uint16_t>(config_.candidate_limit, 1), kMaxCandidates);
  memory_ = memory;
  sample_rate_ = sample_rate_hz;
  submode_ = submode;

  const Profile& p = profile(submode);
  symbol_samples_ = p.symbol_samples;
  hop_ = symbol_samples_ / kRowsPerSymbol;
  capacity_ = static_cast<size_t>(p.slot_ms) * (sample_rate_hz / 1000u);
  first_bin_ = static_cast<size_t>(std::lround(kFirstHz / kBinSpacingHz));
  bin_count_ = kBinCount;
  first_hz_ = kFirstHz;
  grid_row_capacity_ = (capacity_ - symbol_samples_) / hop_ + 1;

  if (!orcsdr::ftx::spectral_fft::make_plan(&plan_, symbol_samples_)) return false;
  samples_ = static_cast<int16_t*>(grab(capacity_ * sizeof(int16_t)));
  grid_ = static_cast<float*>(grab(grid_row_capacity_ * bin_count_ * sizeof(float)));
  candidates_ = static_cast<sync::Candidate*>(grab(sizeof(sync::Candidate) * kMaxCandidates));
  decoder_ws_ = static_cast<decoder::Workspace*>(grab(sizeof(decoder::Workspace)));
  energy_ = reinterpret_cast<float (*)[8]>(grab(sizeof(float) * kChannelSymbols * 8));
  raw_energy_ = static_cast<float (*)[kChannelSymbols][8]>(grab(sizeof(float) * kChannelSymbols * 8 * kMaxCandidates));
  if (samples_ == nullptr || grid_ == nullptr || candidates_ == nullptr || decoder_ws_ == nullptr || raw_energy_ == nullptr) {
    end();
    return false;
  }
  filled_ = 0;
  slot_open_ = false;
  raw_count_ = 0;
  stats_ = Stats{};
  return true;
}

void Backend::end() {
  drop(samples_);
  drop(grid_);
  drop(candidates_);
  drop(decoder_ws_);
  drop(energy_);
  drop(raw_energy_);
  raw_energy_ = nullptr;
  decoder_ws_ = nullptr;
  energy_ = nullptr;
  samples_ = nullptr;
  grid_ = nullptr;
  candidates_ = nullptr;
  capacity_ = 0;
  filled_ = 0;
  slot_open_ = false;
  raw_count_ = 0;
  sample_rate_ = 0;
}

bool Backend::set_submode(Submode submode) {
  if (sample_rate_ == 0 || !physical_layer_ready(submode)) return false;
  if (submode == submode_) return true;
  const Config keep = config_;
  const Memory memory = memory_;
  return begin(sample_rate_, submode, keep, memory);   // reallocates for the new slot length
}

void Backend::reset() {
  filled_ = 0;
  slot_open_ = false;
  raw_count_ = 0;
  stats_ = Stats{};
}

void Backend::set_config(const Config& config) {
  config_ = config;
  config_.candidate_limit = std::min<uint16_t>(std::max<uint16_t>(config_.candidate_limit, 1), kMaxCandidates);
}

bool Backend::begin_slot(uint64_t slot_epoch_ms) {
  if (samples_ == nullptr) return false;
  slot_epoch_ms_ = slot_epoch_ms;
  filled_ = 0;
  slot_open_ = true;
  raw_count_ = 0;
  return true;
}

bool Backend::offer_audio(const int16_t* samples, size_t count) {
  if (!slot_open_ || samples == nullptr) return false;
  const size_t take = std::min(capacity_ - filled_, count);
  std::memcpy(samples_ + filled_, samples, take * sizeof(int16_t));
  filled_ += take;
  return take == count;
}

size_t Backend::finish_slot(orcsdr::ft8::Decode* output, size_t capacity, bool incomplete) {
  stats_ = Stats{};
  raw_count_ = 0;
  if (!slot_open_ || output == nullptr || capacity == 0) return 0;
  slot_open_ = false;
  if (incomplete) {
    stats_.incomplete = true;
    return 0;
  }

  const Profile& p = profile(submode_);
  const auto clock_ms = [&]() -> uint32_t {
    return config_.now_us != nullptr ? static_cast<uint32_t>(config_.now_us() / 1000u) : 0u;
  };
  const uint32_t t_begin = clock_ms();

  const size_t frame_samples = static_cast<size_t>(p.channel_symbols) * p.symbol_samples;
  if (filled_ < frame_samples) {
    stats_.slot_too_short = true;
    return 0;
  }

  // ---- energy grid: one FFT per half symbol, bins on the 6.25 Hz tone spacing (matches the exact-correlation oracle)
  const size_t rows = orcsdr::ftx::spectral_fft::power_rows(plan_, &scratch_, samples_, filled_, symbol_samples_, hop_, first_bin_,
                                                            bin_count_, grid_, bin_count_, grid_row_capacity_);
  const uint32_t t_spectral = clock_ms();
  stats_.spectral_ms = t_spectral - t_begin;
  stats_.grid_rows = static_cast<uint16_t>(rows);

  // ---- bounded sync search over every start that leaves room for a whole frame
  // Rows a whole frame needs: the first and last symbol rows are (channel_symbols - 1) * kRowsPerSymbol apart, plus the row itself. This is the same span
  // sync::search uses; counting a full extra symbol (channel_symbols * kRowsPerSymbol) skipped the last half-symbol start alignment on every slot and
  // never searched a capture that was exactly one frame long.
  const size_t frame_rows = static_cast<size_t>(p.channel_symbols - 1u) * kRowsPerSymbol + 1u;
  sync::EnergyGrid grid{grid_, rows, bin_count_, bin_count_};
  sync::SearchConfig search{};
  search.first_start_row = 0;
  search.last_start_row_exclusive = static_cast<uint16_t>(rows >= frame_rows ? rows - frame_rows + 1 : 0);
  search.first_base_bin = 0;
  search.last_base_bin_exclusive = static_cast<uint16_t>(bin_count_ - (p.tone_count - 1));
  search.min_score = config_.min_score;
  const sync::Geometry geometry{kRowsPerSymbol, 1};
  const size_t found = rows >= frame_rows ? sync::search(p, grid, geometry, search, candidates_, config_.candidate_limit) : 0;
  const uint32_t t_search = clock_ms();
  stats_.search_ms = t_search - t_spectral;
  stats_.candidates = static_cast<uint16_t>(found);
  for (size_t i = 0; i < found; ++i) {
    if (candidates_[i].score >= kStrongScore) ++stats_.strong_candidates;
    stats_.best_sync_score = std::max(stats_.best_sync_score, candidates_[i].score);
  }

  // ---- candidate-local demodulation to a raw tone frame (no search, no text)
  for (size_t i = 0; i < found && raw_count_ < kMaxCandidates; ++i) {
    if (config_.deadline_ms != 0 && config_.now_us != nullptr && clock_ms() - t_begin > config_.deadline_ms) {
      stats_.deadline_hit = true;
      break;
    }
    DemodConfig demod{};
    demod.start_sample = static_cast<size_t>(candidates_[i].start_row) * hop_;
    demod.base_hz = first_hz_ + static_cast<float>(candidates_[i].base_bin) * kBinSpacingHz;
    demod.min_sync_score = config_.min_score;
    const uint32_t t_refine = clock_ms();
    if (frontend::refine_candidate(samples_, filled_, submode_, frontend::Config{}, &demod.start_sample, &demod.base_hz)) ++stats_.refined;
    stats_.refine_ms += clock_ms() - t_refine;
    // One energy measurement per candidate: the raw tones, the sync statistics and the soft decoder's input all come from it.
    const uint32_t t_energy = clock_ms();
    DemodStats demod_stats{};
    float (*energies)[8] = raw_energy_[raw_count_];
    const bool measured = demodulate_energies(samples_, filled_, submode_, demod.start_sample, demod.base_hz, energies, &demod_stats);
    stats_.energy_ms += clock_ms() - t_energy;
    if (!measured || demod_stats.sync_hits < demod.min_sync_hits || demod_stats.sync_score < demod.min_sync_score) continue;
    RawFrame frame{};
    frame.submode = submode_;
    for (size_t symbol = 0; symbol < kChannelSymbols; ++symbol) {
      uint8_t best = 0;
      for (uint8_t tone = 1; tone < 8; ++tone)
        if (energies[symbol][tone] > energies[symbol][best]) best = tone;
      frame.tones[symbol] = best;
    }
    RawResult& result = raw_[raw_count_++];
    result.audio_hz = demod.base_hz;
    result.start_sample = static_cast<uint32_t>(demod.start_sample);
    result.dt_ms = static_cast<int32_t>(std::lround((static_cast<double>(demod.start_sample) / sample_rate_ - 0.5) * 1000.0));
    result.sync_score = demod_stats.sync_score;
    result.sync_hits = demod_stats.sync_hits;
    result.mean_margin = demod_stats.mean_margin;
    result.frame = frame;
    result.energy_index = static_cast<uint8_t>(raw_count_ - 1);
  }
  // Alias resolution: the three Normal sync blocks are identical, so an alignment one sync period (36 symbols) off scores two blocks of three.
  if (raw_count_ > 1) {
    frontend::AliasItem items[kMaxCandidates];
    bool drop[kMaxCandidates];
    for (size_t i = 0; i < raw_count_; ++i) items[i] = frontend::AliasItem{raw_[i].audio_hz, raw_[i].start_sample, raw_[i].mean_margin, raw_[i].sync_hits};
    frontend::mark_aliases(p, items, raw_count_, drop);
    size_t kept = 0;
    for (size_t i = 0; i < raw_count_; ++i) {
      if (drop[i]) {
        ++stats_.aliases_removed;
        continue;
      }
      if (kept != i) raw_[kept] = raw_[i];
      ++kept;
    }
    raw_count_ = kept;
  }
  stats_.raw_frames = static_cast<uint16_t>(raw_count_);
  const uint32_t t_end = clock_ms();
  stats_.demod_ms = t_end - t_search;
  stats_.total_ms = t_end - t_begin;

  // ---- soft decoding of each raw frame: LLR -> BP -> bounded OSD; a message needs parity AND CRC (see js8_decoder.hpp)
  size_t written = 0;
  const uint32_t t_decode = clock_ms();
  for (size_t i = 0; i < raw_count_; ++i) {
    RawResult& r = raw_[i];
    if (r.sync_hits < config_.min_decode_sync_hits) continue;
    ++stats_.decode_attempts;
    r.attempted = true;
    decoder::Result dr;
    const bool ok = decoder::decode(raw_energy_[r.energy_index], config_.decoder, decoder_ws_, &dr);
    r.initial_syndrome = dr.initial_syndrome;
    r.final_syndrome = dr.final_syndrome;
    r.bp_iterations = dr.bp_iterations;
    r.osd_order = dr.osd_order;
    r.hard_corrections = dr.hard_corrections;
    r.crc_valid = ok && dr.crc_valid;
    r.rendered = ok && dr.rendered;
    r.method = dr.method;
    if (!ok) continue;
    std::memcpy(r.payload, dr.fields.text, sizeof(r.payload));
    r.frame_kind = dr.fields.kind;
    if (!dr.rendered) {
      ++stats_.valid_unrendered;
      continue;
    }
    std::memcpy(r.text, dr.message.text, sizeof(r.text));
    bool duplicate = false;   // the same frame found at two alignments
    for (size_t k = 0; k < written; ++k)
      if (std::strcmp(output[k].message, dr.message.text) == 0) duplicate = true;
    if (duplicate || written >= capacity) continue;
    orcsdr::ft8::Decode& d = output[written++];
    d = orcsdr::ft8::Decode{};
    d.utc_epoch = static_cast<uint32_t>(slot_epoch_ms_ / 1000u);
    d.dt_ms = static_cast<int16_t>(std::clamp<int32_t>(r.dt_ms, -32768, 32767));
    d.audio_hz = static_cast<uint16_t>(std::lround(r.audio_hz));
    d.sync_score = static_cast<int16_t>(std::lround(r.sync_score * 100.0f));
    d.mode = orcsdr::ft8::DigitalMode::js8_normal;
    d.kind = orcsdr::ft8::DecodeKind::unknown;
    d.flags = orcsdr::ft8::decode_flag_snr_unavailable;   // the SNR inside the message is the sender's report, not a measurement
    std::snprintf(d.message, sizeof(d.message), "%s", dr.message.text);
    std::snprintf(d.callsign, sizeof(d.callsign), "%s", dr.message.source);
    if (dr.method == decoder::Method::osd) ++stats_.by_osd;
    else if (dr.method == decoder::Method::bp) ++stats_.by_bp;
    else ++stats_.by_hard;
  }
  stats_.decode_ms = clock_ms() - t_decode;
  stats_.total_ms = clock_ms() - t_begin;
  stats_.decodes = static_cast<uint32_t>(written);
  return written;
}

bool self_check() { return kBinCount == 449 && kRowsPerSymbol == 2; }

}  // namespace orcsdr::js8::native
