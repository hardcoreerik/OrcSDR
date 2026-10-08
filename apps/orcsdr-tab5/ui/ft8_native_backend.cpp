#include "ft8_native_backend.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace orcsdr::ftx::native {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kLowHz = 200.0f;
constexpr float kHighHz = 3000.0f;

// Goertzel power of one tone over a window, scaled like the spectral grid (|X|^2 / n^2).
inline float tone_power(const int16_t* x, size_t n, float coeff) {
  float s1 = 0.0f, s2 = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    const float s0 = static_cast<float>(x[i]) + coeff * s1 - s2;
    s2 = s1;
    s1 = s0;
  }
  const float p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
  return std::max(0.0f, p) / (static_cast<float>(n) * static_cast<float>(n));
}

struct Locator {
  const ModeProfile& p;
  const int16_t* x;
  size_t count;
  double rate;
  double tone_hz;

  bool in_range(long start) const {
    return start >= 0 && static_cast<size_t>(start) + static_cast<size_t>(p.channel_symbols) * p.symbol_samples <= count;
  }

  void coefficients(double base_hz, float* coeff) const {
    for (uint8_t t = 0; t < p.tone_count; ++t)
      coeff[t] = 2.0f * std::cos(2.0f * kPi * static_cast<float>((base_hz + t * tone_hz) / rate));
  }

  // Contrast on the protocol sync symbols only: (expected - competing) / (expected + competing).
  float sync_score(long start, double base_hz) const {
    float coeff[8];
    coefficients(base_hz, coeff);
    float expected = 0.0f, total = 0.0f;
    for (size_t b = 0; b < p.sync_block_count; ++b) {
      const auto& block = p.sync[b];
      for (size_t k = 0; k < block.length; ++k) {
        const int16_t* w = x + start + (block.first_symbol + k) * p.symbol_samples;
        for (uint8_t tone = 0; tone < p.tone_count; ++tone) {
          const float e = tone_power(w, p.symbol_samples, coeff[tone]);
          total += e;
          if (tone == block.tones[k]) expected += e;
        }
      }
    }
    const float competing = total - expected;
    return (expected - competing) / (expected + competing + 1.0e-12f);
  }

  // Coordinate (or joint) search around a coarse position.
  float refine(long start0, double hz0, double time_span, double freq_span, bool joint, long* start, double* hz) const {
    long best_t = start0;
    double best_f = hz0;
    float best = in_range(best_t) ? sync_score(best_t, best_f) : -2.0f;
    auto try_point = [&](long t, double f) {
      if (!in_range(t) || f < 100.0) return;
      const float s = sync_score(t, f);
      if (s > best) {
        best = s;
        best_t = t;
        best_f = f;
      }
    };
    if (joint) {
      const long bt = best_t;
      const double bf = best_f;
      for (int ti = -4; ti <= 4; ++ti)
        for (int fi = -4; fi <= 4; ++fi) {
          if (ti == 0 && fi == 0) continue;
          try_point(bt + std::lround(time_span * ti / 4.0), bf + freq_span * fi / 4.0);
        }
    } else {
      for (int pass = 0; pass < 1; ++pass) {
        const long bt = best_t;
        for (int i = -4; i <= 4; ++i)
          if (i != 0) try_point(bt + std::lround(time_span * i / 4.0), best_f);
        const double bf = best_f;
        for (int i = -4; i <= 4; ++i)
          if (i != 0) try_point(best_t, bf + freq_span * i / 4.0);
      }
    }
    {
      const long bt = best_t;
      for (int i = -2; i <= 2; ++i)
        if (i != 0) try_point(bt + std::lround(time_span / 4.0 * i / 2.0), best_f);
      const double bf = best_f;
      for (int i = -2; i <= 2; ++i)
        if (i != 0) try_point(best_t, bf + freq_span / 4.0 * i / 2.0);
    }
    *start = best_t;
    *hz = best_f;
    return best;
  }
};

struct Refined {
  long start;
  double hz;
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
  drop(grid_);
  drop(local_);
  drop(candidates_);
  if (workspace_ != nullptr) {
    workspace_->~Workspace();
    drop(workspace_);
  }
  samples_ = nullptr;
  grid_ = nullptr;
  local_ = nullptr;
  candidates_ = nullptr;
  workspace_ = nullptr;
  capacity_ = filled_ = 0;
  slot_open_ = false;
}

bool Backend::configure_mode(Mode mode) {
  if (!implementation_ready(mode)) return false;
  const ModeProfile& p = profile(mode);
  if (p.sample_rate_hz != sample_rate_) return false;
  if (!spectral_fft::make_plan(&plan_, p.symbol_samples)) return false;
  symbol_samples_ = p.symbol_samples;
  hop_ = symbol_samples_ / 2;
  bin_hz_ = static_cast<double>(sample_rate_) / static_cast<double>(symbol_samples_);
  first_bin_ = static_cast<size_t>(std::ceil(kLowHz / bin_hz_));
  bin_count_ = static_cast<size_t>(std::floor(kHighHz / bin_hz_)) - first_bin_ + 1;

  const size_t needed_samples = static_cast<size_t>(p.slot_ms) * sample_rate_ / 1000u + sample_rate_ / 2u;
  if (needed_samples > capacity_) {
    drop(samples_);
    samples_ = static_cast<int16_t*>(grab(needed_samples * sizeof(int16_t)));
    capacity_ = samples_ != nullptr ? needed_samples : 0;
    if (samples_ == nullptr) return false;
  }
  const size_t rows = 1 + (capacity_ - symbol_samples_) / hop_;
  drop(grid_);
  grid_ = static_cast<float*>(grab(rows * bin_count_ * sizeof(float)));
  grid_rows_capacity_ = grid_ != nullptr ? rows : 0;
  if (grid_ == nullptr) return false;
  drop(local_);
  local_ = static_cast<float*>(grab(static_cast<size_t>(p.channel_symbols) * p.tone_count * sizeof(float)));
  if (local_ == nullptr) return false;
  mode_ = mode;
  return true;
}

bool Backend::begin(uint32_t sample_rate_hz, Mode mode, const Config& config, const Memory& memory) {
  end();
  if (sample_rate_hz != 12000u) return false;
  config_ = config;
  memory_ = memory;
  sample_rate_ = sample_rate_hz;
  if (config_.candidate_k == 0 || config_.gate == 0) return false;
  candidates_ = static_cast<sync::Candidate*>(grab(sizeof(sync::Candidate) * config_.candidate_k));
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

  // ---- spectral grid
  const size_t rows = spectral_fft::power_rows(plan_, &scratch_, samples_, filled_, hop_, first_bin_, bin_count_, grid_, bin_count_,
                                               grid_rows_capacity_);
  const uint32_t t_spectral = clock_ms();
  stats_.spectral_ms = t_spectral - t_begin;

  // ---- coarse search
  sync::EnergyGrid grid{grid_, rows, bin_count_, bin_count_};
  const sync::Geometry geometry{2, 1};
  sync::SearchConfig search{};
  search.min_score = config_.min_score;
  search.suppress_time_rows = 2;
  search.suppress_frequency_bins = 1;
  const size_t n = sync::search(p, grid, geometry, search, candidates_, config_.candidate_k);
  const uint32_t t_search = clock_ms();
  stats_.search_ms = t_search - t_spectral;
  stats_.coarse_candidates = static_cast<uint16_t>(n);

  // ---- refinement (host std::vector is avoided: the refined list lives in the workspace-sized local stack array)
  Refined refined_stack[64];
  const size_t refine_n = std::min<size_t>(n, 64);
  const Locator locator{p, samples_, filled_, static_cast<double>(sample_rate_), static_cast<double>(p.tone_spacing_millihz) / 1000.0};
  size_t produced = 0;
  for (size_t i = 0; i < refine_n; ++i) {
    if (over_deadline()) {
      stats_.deadline_hit = true;
      break;
    }
    const auto& c = candidates_[i];
    long start = static_cast<long>(c.start_row) * static_cast<long>(hop_);
    double hz = static_cast<double>(first_bin_ + c.base_bin) * bin_hz_;
    long rs = start;
    double rh = hz;
    const float score = locator.refine(start, hz, static_cast<double>(hop_), bin_hz_, config_.joint_search, &rs, &rh);
    refined_stack[produced++] = {rs, rh, score, c.score};
  }
  std::stable_sort(refined_stack, refined_stack + produced, [](const Refined& a, const Refined& b) { return a.score > b.score; });
  const uint32_t t_refine = clock_ms();
  stats_.refine_ms = t_refine - t_search;

  // ---- gates on the best candidates
  pipeline::Config pipe{};
  pipe.search = search;
  size_t out_n = 0;
  size_t gated = 0;
  for (size_t i = 0; i < produced && gated < config_.gate && out_n < capacity; ++i) {
    if (over_deadline()) {
      stats_.deadline_hit = true;
      break;
    }
    const Refined& r = refined_stack[i];
    if (!locator.in_range(r.start)) continue;
    float coeff[8];
    locator.coefficients(r.hz, coeff);
    for (size_t s = 0; s < p.channel_symbols; ++s)
      for (uint8_t tone = 0; tone < p.tone_count; ++tone)
        local_[s * p.tone_count + tone] = tone_power(samples_ + r.start + s * p.symbol_samples, p.symbol_samples, coeff[tone]);
    sync::EnergyGrid lg{local_, p.channel_symbols, p.tone_count, p.tone_count};
    sync::Candidate lc{};
    lc.score = r.coarse;
    pipeline::FrameResult frame{};
    ++gated;
    if (pipeline::try_candidate(p, lg, sync::Geometry{1, 1}, lc, pipe, workspace_, &frame, nullptr) != pipeline::Outcome::accepted)
      continue;

    bool duplicate = false;
    for (size_t k = 0; k < out_n; ++k)
      if (std::strcmp(output[k].message, frame.standard.text) == 0) {
        duplicate = true;
        break;
      }
    if (duplicate) continue;

    orcsdr::ft8::Decode d{};
    d.utc_epoch = static_cast<uint32_t>(slot_epoch_ms_ / 1000u);
    d.snr_db = 0;
    d.dt_ms = static_cast<int16_t>(std::lround(static_cast<double>(r.start) * 1000.0 / sample_rate_ - 500.0));
    d.audio_hz = static_cast<uint16_t>(std::lround(r.hz));
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
