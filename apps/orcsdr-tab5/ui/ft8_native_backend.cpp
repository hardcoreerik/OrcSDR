#include "ft8_native_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace orcsdr::ftx::native {
namespace {

constexpr float kLowHz = 200.0f;
constexpr float kHighHz = 3000.0f;

struct Refined {
  uint16_t start_row;   // fine grid row
  uint16_t base_bin;    // fine grid bin of tone 0
  float score;
  float coarse;
};

}  // namespace

Backend::~Backend() { end(); }

void* Backend::grab(size_t bytes) {
  void* p = memory_.alloc != nullptr ? memory_.alloc(bytes) : std::malloc(bytes);
  if (p != nullptr) std::memset(p, 0, bytes);
  return p;
}

void Backend::drop(void* pointer) {
  if (pointer == nullptr) return;
  if (memory_.release != nullptr) memory_.release(pointer);
  else std::free(pointer);
}

void Backend::end() {
  drop(samples_);
  drop(fine_);
  drop(coarse_);
  drop(candidates_);
  if (workspace_ != nullptr) {
    workspace_->~Workspace();
    drop(workspace_);
  }
  samples_ = nullptr;
  fine_ = nullptr;
  coarse_ = nullptr;
  candidates_ = nullptr;
  workspace_ = nullptr;
  capacity_ = filled_ = 0;
  fine_rows_capacity_ = coarse_rows_capacity_ = 0;
  slot_open_ = false;
}

bool Backend::configure_mode(Mode mode) {
  if (!implementation_ready(mode)) return false;
  const ModeProfile& p = profile(mode);
  if (p.sample_rate_hz != sample_rate_) return false;
  symbol_samples_ = p.symbol_samples;
  fine_rows_ = config_.fine_rows == 8 ? 8 : 4;
  fine_hop_ = symbol_samples_ / fine_rows_;
  // The fine grid is a zero-padded 2n-point transform of each n-sample window: bins every fs / (2n).
  if (!spectral_fft::make_plan(&plan_, 2 * symbol_samples_)) return false;
  bin_hz_ = static_cast<double>(sample_rate_) / static_cast<double>(symbol_samples_);
  first_bin_ = static_cast<size_t>(std::ceil(kLowHz / bin_hz_));
  bin_count_ = static_cast<size_t>(std::floor(kHighHz / bin_hz_)) - first_bin_ + 1;
  fine_bin_count_ = 2 * bin_count_ - 1;

  const size_t needed_samples = static_cast<size_t>(p.slot_ms) * sample_rate_ / 1000u + sample_rate_ / 2u;
  if (needed_samples > capacity_) {
    drop(samples_);
    samples_ = static_cast<int16_t*>(grab(needed_samples * sizeof(int16_t)));
    capacity_ = samples_ != nullptr ? needed_samples : 0;
    if (samples_ == nullptr) return false;
  }
  const size_t fine_rows = 1 + (capacity_ - symbol_samples_) / fine_hop_;
  drop(fine_);
  fine_ = static_cast<float*>(grab(fine_rows * fine_bin_count_ * sizeof(float)));
  fine_rows_capacity_ = fine_ != nullptr ? fine_rows : 0;
  const size_t coarse_rows = 1 + (fine_rows - 1) / (fine_rows_ / 2);
  drop(coarse_);
  coarse_ = static_cast<float*>(grab(coarse_rows * bin_count_ * sizeof(float)));
  coarse_rows_capacity_ = coarse_ != nullptr ? coarse_rows : 0;
  if (fine_ == nullptr || coarse_ == nullptr) return false;
  mode_ = mode;
  return true;
}

bool Backend::begin(uint32_t sample_rate_hz, Mode mode, const Config& config, const Memory& memory) {
  end();
  if (sample_rate_hz != 12000u) return false;
  config_ = config;
  memory_ = memory;
  sample_rate_ = sample_rate_hz;
  if (config_.candidate_k == 0 || config_.gate == 0 || config_.candidate_k > kMaxCandidateK || config_.gate > kMaxCandidateK)
    return false;
  candidates_ = static_cast<sync::Candidate*>(grab(sizeof(sync::Candidate) * kMaxCandidateK));
  void* ws = grab(sizeof(pipeline::Workspace));
  if (candidates_ == nullptr || ws == nullptr) {
    drop(ws);
    end();
    return false;
  }
  workspace_ = new (ws) pipeline::Workspace();
  if (!configure_mode(mode)) {
    end();
    return false;
  }
  return true;
}

void Backend::set_config(const Config& config) {
  const uint8_t previous_rows = config_.fine_rows;
  config_ = config;
  config_.candidate_k = std::min<uint16_t>(std::max<uint16_t>(config_.candidate_k, 1), kMaxCandidateK);
  config_.gate = std::min<uint16_t>(std::max<uint16_t>(config_.gate, 1), kMaxCandidateK);
  config_.fine_rows = config_.fine_rows == 8 ? 8 : 4;
  if (config_.fine_rows != previous_rows && sample_rate_ != 0) {
    filled_ = 0;
    slot_open_ = false;
    (void)configure_mode(mode_);   // reallocates the grids for the new resolution
  }
}

bool Backend::set_mode(Mode mode) {
  if (sample_rate_ == 0) return false;
  filled_ = 0;
  slot_open_ = false;
  return configure_mode(mode);
}

void Backend::reset() {
  filled_ = 0;
  slot_open_ = false;
}

bool Backend::begin_slot(uint64_t slot_epoch_ms) {
  if (samples_ == nullptr) return false;
  slot_epoch_ms_ = slot_epoch_ms;
  filled_ = 0;
  slot_open_ = true;
  return true;
}

bool Backend::offer_audio(const int16_t* samples, size_t count) {
  if (!slot_open_ || samples == nullptr) return false;
  const size_t room = capacity_ - filled_;
  const size_t take = std::min(room, count);
  std::memcpy(samples_ + filled_, samples, take * sizeof(int16_t));
  filled_ += take;
  return take == count;
}

size_t Backend::finish_slot(orcsdr::ft8::Decode* output, size_t capacity, bool incomplete) {
  stats_ = Stats{};
  if (!slot_open_ || output == nullptr || capacity == 0) return 0;
  slot_open_ = false;
  if (incomplete) return 0;

  const ModeProfile& p = profile(mode_);
  const auto clock_ms = [&]() -> uint32_t {
    return config_.now_us != nullptr ? static_cast<uint32_t>(config_.now_us() / 1000u) : 0u;
  };
  const uint32_t t_begin = clock_ms();
  const auto over_deadline = [&]() {
    return config_.deadline_ms != 0 && config_.now_us != nullptr && clock_ms() - t_begin > config_.deadline_ms;
  };

  const size_t frame_samples = static_cast<size_t>(p.channel_symbols) * p.symbol_samples;
  if (filled_ < frame_samples) {
    stats_.slot_too_short = true;
    return 0;
  }

  // ---- fine grid: one zero-padded FFT per row (half-bin frequency spacing)
  const size_t fine_rows = spectral_fft::power_rows(plan_, &scratch_, samples_, filled_, symbol_samples_, fine_hop_, 2 * first_bin_,
                                                    fine_bin_count_, fine_, fine_bin_count_, fine_rows_capacity_);
  const uint32_t t_spectral = clock_ms();
  stats_.spectral_ms = t_spectral - t_begin;

  // ---- coarse view (2 rows per symbol, whole bins) and the sync search
  const size_t row_step = fine_rows_ / 2;
  const size_t coarse_rows = fine_rows == 0 ? 0 : 1 + (fine_rows - 1) / row_step;
  for (size_t r = 0; r < coarse_rows; ++r) {
    const float* src = fine_ + r * row_step * fine_bin_count_;
    float* dst = coarse_ + r * bin_count_;
    for (size_t j = 0; j < bin_count_; ++j) dst[j] = src[2 * j];
  }
  sync::EnergyGrid coarse{coarse_, coarse_rows, bin_count_, bin_count_};
  sync::SearchConfig search{};
  search.min_score = config_.min_score;
  search.suppress_time_rows = 2;
  search.suppress_frequency_bins = 1;
  const size_t n = sync::search(p, coarse, sync::Geometry{2, 1}, search, candidates_, config_.candidate_k);
  const uint32_t t_search = clock_ms();
  stats_.search_ms = t_search - t_spectral;
  stats_.coarse_candidates = static_cast<uint16_t>(n);

  // ---- refine each coarse candidate by scoring the fine grid around it (time +-1 coarse row, frequency +-1 coarse bin)
  const sync::Geometry fine_geometry{static_cast<uint8_t>(fine_rows_), 2};
  sync::EnergyGrid fine{fine_, fine_rows, fine_bin_count_, fine_bin_count_};
  Refined refined[kMaxCandidateK];
  size_t produced = 0;
  const int row_span = static_cast<int>(row_step);
  for (size_t i = 0; i < n; ++i) {
    const auto& c = candidates_[i];
    const int centre_row = static_cast<int>(c.start_row * row_step);
    const int centre_bin = 2 * static_cast<int>(c.base_bin);
    Refined best{static_cast<uint16_t>(centre_row), static_cast<uint16_t>(centre_bin), -2.0f, c.score};
    for (int dr = -row_span; dr <= row_span; ++dr)
      for (int db = -2; db <= 2; ++db) {
        const int row = centre_row + dr, bin = centre_bin + db;
        if (row < 0 || bin < 0) continue;
        sync::Candidate sc{};
        if (!sync::score_candidate(p, fine, fine_geometry, static_cast<uint16_t>(row), static_cast<uint16_t>(bin), &sc)) continue;
        if (sc.score > best.score) best = {static_cast<uint16_t>(row), static_cast<uint16_t>(bin), sc.score, c.score};
      }
    if (best.score < -1.5f) continue;
    bool duplicate = false;
    for (size_t k = 0; k < produced; ++k)
      if (std::abs(static_cast<int>(refined[k].start_row) - static_cast<int>(best.start_row)) <= 1 &&
          std::abs(static_cast<int>(refined[k].base_bin) - static_cast<int>(best.base_bin)) <= 1) {
        duplicate = true;
        if (best.score > refined[k].score) refined[k] = best;
        break;
      }
    if (!duplicate) refined[produced++] = best;
  }
  std::stable_sort(refined, refined + produced, [](const Refined& a, const Refined& b) { return a.score > b.score; });
  const uint32_t t_refine = clock_ms();
  stats_.refine_ms = t_refine - t_search;

  // ---- gates on the best candidates, straight on the fine grid
  pipeline::Config pipe{};
  pipe.search = search;
  size_t out_n = 0;
  size_t gated = 0;
  for (size_t i = 0; i < produced && gated < config_.gate && out_n < capacity; ++i) {
    if (over_deadline()) {
      stats_.deadline_hit = true;
      break;
    }
    const Refined& r = refined[i];
    sync::Candidate lc{};
    lc.start_row = r.start_row;
    lc.base_bin = r.base_bin;
    lc.score = r.score;
    pipeline::FrameResult frame{};
    ++gated;
    if (pipeline::try_candidate(p, fine, fine_geometry, lc, pipe, workspace_, &frame, nullptr) != pipeline::Outcome::accepted) continue;

    bool duplicate = false;
    for (size_t k = 0; k < out_n; ++k)
      if (std::strcmp(output[k].message, frame.standard.text) == 0) {
        duplicate = true;
        break;
      }
    if (duplicate) continue;

    const double hz = (static_cast<double>(2 * first_bin_ + r.base_bin)) * bin_hz_ / 2.0;
    const double start_s = static_cast<double>(r.start_row) * static_cast<double>(fine_hop_) / sample_rate_;
    orcsdr::ft8::Decode d{};
    d.utc_epoch = static_cast<uint32_t>(slot_epoch_ms_ / 1000u);
    d.snr_db = 0;
    d.dt_ms = static_cast<int16_t>(std::lround((start_s - 0.5) * 1000.0));
    d.audio_hz = static_cast<uint16_t>(std::lround(hz));
    d.sync_score = static_cast<int16_t>(std::lround(r.score * 100.0f));
    std::snprintf(d.message, sizeof(d.message), "%.47s", frame.standard.text);
    std::snprintf(d.callsign, sizeof(d.callsign), "%.15s", frame.standard.second.text);
    // "RR73" is the acknowledgement, not the Arctic locator RR73.
    if (frame.standard.extra.kind == orcsdr::ft8::message::ExtraKind::grid && std::strcmp(frame.standard.extra.text, "RR73") != 0)
      std::snprintf(d.grid, sizeof(d.grid), "%.8s", frame.standard.extra.text);
    d.kind = orcsdr::ft8::classify_message(d.message);
    d.mode = mode_ == Mode::ft4 ? orcsdr::ft8::DigitalMode::ft4 : orcsdr::ft8::DigitalMode::ft8;
    d.flags = orcsdr::ft8::decode_flag_snr_unavailable;
    output[out_n++] = d;
  }
  stats_.attempted = static_cast<uint16_t>(gated);
  stats_.accepted = static_cast<uint16_t>(out_n);
  const uint32_t t_end = clock_ms();
  stats_.gate_ms = t_end - t_refine;
  stats_.total_ms = t_end - t_begin;
  return out_n;
}

namespace {
bool seam_begin(void* c, uint32_t rate) { (void)c; return rate == 12000u; }
void seam_reset(void* c) { static_cast<Backend*>(c)->reset(); }
bool seam_begin_slot(void* c, uint32_t epoch_s) { return static_cast<Backend*>(c)->begin_slot(static_cast<uint64_t>(epoch_s) * 1000u); }
bool seam_begin_slot_ms(void* c, uint64_t epoch_ms) { return static_cast<Backend*>(c)->begin_slot(epoch_ms); }
bool seam_offer(void* c, const int16_t* s, size_t n) { return static_cast<Backend*>(c)->offer_audio(s, n); }
size_t seam_finish(void* c, orcsdr::ft8::Decode* out, size_t cap) { return static_cast<Backend*>(c)->finish_slot(out, cap); }
bool seam_set_mode(void* c, orcsdr::ft8::DigitalMode m) {
  auto* b = static_cast<Backend*>(c);
  if (m == orcsdr::ft8::DigitalMode::ft8) return b->set_mode(Mode::ft8);
  if (m == orcsdr::ft8::DigitalMode::ft4) return b->set_mode(Mode::ft4);
  return false;  // JS8 stays disabled
}
uint32_t seam_caps(void*) { return orcsdr::ft8::decoder_cap_ft8 | orcsdr::ft8::decoder_cap_ft4; }
}  // namespace

orcsdr::ft8::DecoderBackend make_seam(Backend* backend) {
  orcsdr::ft8::DecoderBackend seam;
  seam.context = backend;
  seam.begin = seam_begin;
  seam.reset = seam_reset;
  seam.begin_slot = seam_begin_slot;
  seam.offer_audio = seam_offer;
  seam.finish_slot = seam_finish;
  seam.set_mode = seam_set_mode;
  seam.begin_slot_ms = seam_begin_slot_ms;
  seam.capabilities = seam_caps;
  return seam;
}

bool self_check() {
  Backend b;
  Config config;
  if (!b.begin(12000, Mode::ft8, config) || !b.ready()) return false;
  const orcsdr::ft8::DecoderBackend seam = make_seam(&b);
  return orcsdr::ft8::backend_valid(seam) &&
         orcsdr::ft8::backend_capabilities(seam) == (orcsdr::ft8::decoder_cap_ft8 | orcsdr::ft8::decoder_cap_ft4) &&
         !b.begin(8000, Mode::ft8, config);
}

}  // namespace orcsdr::ftx::native
