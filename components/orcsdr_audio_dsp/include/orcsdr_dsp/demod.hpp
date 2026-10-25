#pragma once
// Receiver audio demodulation for the P4 console: wideband FM (broadcast), narrow FM and AM from
// 8-bit IQ at 960 kS/s down to 48 kHz mono audio. Pure C++ (no ESP-IDF, no graphics), so it is
// unit-tested on the host with synthetic signals.
//
//   input  : interleaved unsigned 8-bit IQ (what the RTL-SDR delivers), 960 kS/s
//   stage A: complex low-pass + decimate by 4  -> 240 kS/s   (100 kHz cutoff, all modes)
//   WFM    : FM discriminator at 240 kS/s -> 15 kHz low-pass, decimate by 5 -> 48 kHz -> 75 us de-emphasis
//   NFM    : complex low-pass, decimate by 5 -> 48 kS/s -> FM discriminator (+/-5 kHz deviation)
//   AM     : complex low-pass, decimate by 5 -> 48 kS/s -> envelope, DC removal, slow AGC
//
// This is a first, deliberately simple chain: mono audio only (no FM stereo or RDS yet), no
// squelch, no SSB. The Tab5's demodulators live inside its main.cpp and are not shared.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace orcsdr::dsp {

enum class Mode : uint8_t { wfm, nfm, am };

class Demod {
 public:
  static constexpr uint32_t kInputRate = 960000;
  static constexpr uint32_t kOutputRate = 48000;

  Demod();
  void set_mode(Mode mode);
  Mode mode() const { return mode_; }
  void reset();

  // Consumes `bytes` of interleaved CU8 IQ (an even number) and appends mono audio (-1..1) at 48 kHz
  // to `out`, never more than `capacity` samples. Returns the number written. Feed at most
  // bytes/2/20 + 2 samples of capacity per call.
  size_t process(const uint8_t* iq, size_t bytes, float* out, size_t capacity);

 private:
  struct ComplexFir {
    std::vector<float> taps;
    std::vector<float> hist_i, hist_q;  // doubled circular history for contiguous reads
    size_t pos = 0;
    unsigned decim = 1, phase = 0;
    void init(const std::vector<float>& t, unsigned d);
    void reset();
    // Pushes a sample; returns true and writes the filtered sample when an output is due.
    bool push(float i, float q, float* oi, float* oq);
  };
  struct RealFir {
    std::vector<float> taps;
    std::vector<float> hist;
    size_t pos = 0;
    unsigned decim = 1, phase = 0;
    void init(const std::vector<float>& t, unsigned d);
    void reset();
    bool push(float x, float* out);
  };

  size_t stage_b(float ai, float aq, float* out, size_t room);  // one 240 kS/s sample in, 0 or 1 audio sample out

  Mode mode_ = Mode::wfm;
  // Stage A runs on whole blocks: 33 symmetric taps, 960k -> 240k, 100 kHz. The block form keeps the
  // samples contiguous and halves the multiplies (the taps are symmetric), which matters at 960 kS/s.
  static constexpr size_t kATaps = 33, kChunk = 2048;
  float a_taps_[kATaps] = {};
  float a_hist_i_[kATaps - 1] = {}, a_hist_q_[kATaps - 1] = {};
  unsigned a_phase_ = 0;
  std::vector<float> xi_, xq_;
  ComplexFir stage_b_nb_;    // 240k -> 48k, narrow-band modes
  RealFir stage_b_wfm_;      // 240k -> 48k, 15 kHz audio after the discriminator
  float prev_i_ = 0, prev_q_ = 0;      // discriminator memory
  float deemph_ = 0;                   // WFM de-emphasis state
  float dc_ = 0, level_ = 0.1f;        // AM DC estimate and envelope level (AGC)
  float nfm_lp_ = 0;                   // NFM audio smoothing
};

// Phase of a complex sample, radians, polynomial approximation (error below 1e-4 rad).
float fast_atan2(float y, float x);

}  // namespace orcsdr::dsp
