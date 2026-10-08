#include "js8_native_backend.hpp"

#include <algorithm>
#include <cmath>
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
  if (samples_ == nullptr || grid_ == nullptr || candidates_ == nullptr) {
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
  const size_t frame_rows = static_cast<size_t>(p.channel_symbols) * kRowsPerSymbol;
  sync::EnergyGrid grid{grid_, rows, bin_count_, bin_count_};
  sync::SearchConfig search{};
  search.first_start_row = 0;
  search.last_start_row_exclusive = static_cast<uint16_t>(rows > frame_rows ? rows - frame_rows + 1 : 0);
  search.first_base_bin = 0;
  search.last_base_bin_exclusive = static_cast<uint16_t>(bin_count_ - (p.tone_count - 1));
  search.min_score = config_.min_score;
  const sync::Geometry geometry{kRowsPerSymbol, 1};
  const size_t found = rows > frame_rows ? sync::search(p, grid, geometry, search, candidates_, config_.candidate_limit) : 0;
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
    RawFrame frame{};
    DemodStats demod_stats{};
    if (!demodulate_tones(samples_, filled_, submode_, demod, &frame, &demod_stats)) continue;
    RawResult& result = raw_[raw_count_++];
    result.audio_hz = demod.base_hz;
    result.dt_ms = static_cast<int32_t>(std::lround((static_cast<double>(demod.start_sample) / sample_rate_ - 0.5) * 1000.0));
    result.sync_score = demod_stats.sync_score;
    result.sync_hits = demod_stats.sync_hits;
    result.mean_margin = demod_stats.mean_margin;
    result.frame = frame;
  }
  stats_.raw_frames = static_cast<uint16_t>(raw_count_);
  const uint32_t t_end = clock_ms();
  stats_.demod_ms = t_end - t_search;
  stats_.total_ms = t_end - t_begin;

  // Acceptance layer goes here once reconstructed: raw frame -> tone-to-bit map -> FEC -> CRC -> supported frame parser.
  // Until all four are accepted no Decode may be written.
  stats_.decodes = 0;
  return 0;
}

bool self_check() { return kBinCount == 449 && kRowsPerSymbol == 2; }

}  // namespace orcsdr::js8::native
