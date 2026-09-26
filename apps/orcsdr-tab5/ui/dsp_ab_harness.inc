// Stage-1 DSP A/B harness (test builds only: ORCSDR_DSP_AB=1).
//
// Captures N consecutive live IQ blocks plus the exact demodulator state at
// the first one, then runs the reference (pre-optimization) and current
// demodulators over the same saved blocks from the same starting state.
// Audio output, every FM MPX sample handed to RDS, meter/stereo values and
// the full RtlAudioState after every block are compared bit for bit;
// per-block state comparison proves continuity across block boundaries.
// Included inside main.cpp's anonymous namespace after the demodulators.
//
// The *_ref functions are verbatim copies of the code before Stage 1
// (extracted by script), renamed only.

void update_signal_level_from_iq_ref(const uint8_t* iq, size_t bytes) {
  if (iq == nullptr || bytes < 4) return;
  uint64_t sum = 0;
  size_t pairs = 0;
  size_t clipped = 0;
  for (size_t i = 0; i + 1 < bytes; i += 2) {
    if (iq[i] == 0 || iq[i] == 255 || iq[i + 1] == 0 || iq[i + 1] == 255)
      ++clipped;
    /* Power remains strided; clipping needs every sample near the 0.1% limit. */
    if ((i & 31u) != 0) continue;
    const int32_t ii = static_cast<int32_t>(iq[i]) - 128;
    const int32_t qq = static_cast<int32_t>(iq[i + 1]) - 128;
    sum += static_cast<uint32_t>(ii * ii + qq * qq);
    ++pairs;
  }
  if (pairs == 0) return;
  const float power = static_cast<float>(sum) / static_cast<float>(pairs);
  /* Full-scale CU8 complex: 2 * 127.5^2 */
  constexpr float kFullScale = 2.0f * 127.5f * 127.5f;
  const float dbfs = 10.0f * log10f((power / kFullScale) + 1.0e-12f);
  rtl_signal_dbfs.store(dbfs, std::memory_order_relaxed);
  rtl_signal_dbfs_smooth = 0.88f * rtl_signal_dbfs_smooth + 0.12f * dbfs;
  const float clipping = 100.0f * static_cast<float>(clipped) /
                         static_cast<float>(bytes / 2u);
  const float previous = rtl_iq_clipping_percent.load(std::memory_order_relaxed);
  rtl_iq_clipping_percent.store(0.98f * previous + 0.02f * clipping,
                                std::memory_order_relaxed);
}

void demodulate_fm_ref(const uint8_t* iq, size_t bytes, float audio_scale, bool wbfm,
                   uint32_t sample_rate_sps) {
  int16_t* audio = rtl_audio_buffers[rtl_audio.buffer];
  size_t audio_count = 0;
  const uint8_t rf_decimation = rtl_rf_decimation(sample_rate_sps);
  if (!rf_decimation) return;
  const float iq_lpf_k = rtl_filter_alpha(wbfm ? RtlBand::fm : RtlBand::wx,
                                          sample_rate_sps);
  const float audio_lpf_k = wbfm ? kWbfmAudioLpfK : kNfmAudioLpfK;
  const float deemph_k = wbfm ? kWbfmDeemphK : kNfmDeemphK;
  const float inv_rf_decim = 1.0f / static_cast<float>(rf_decimation);
  const float inv_audio_decim = 1.0f / static_cast<float>(kFmAudioDecim);

  /* Stereo meter accumulators, local to this IQ chunk (same call granularity
   * as update_signal_level_from_iq — no persistent window/reset bookkeeping). */
  float meter_peak_l = 0.0f;
  float meter_peak_r = 0.0f;
  bool meter_any = false;
  bool capture_mpx = false;
  size_t capture_write = 0;
  if (wbfm && g_rds_capture_active.load(std::memory_order_acquire)) {
    g_rds_capture_writing.store(true, std::memory_order_release);
    if (g_rds_capture_active.load(std::memory_order_acquire)) {
      capture_mpx = true;
      capture_write = g_rds_capture_write.load(std::memory_order_relaxed);
    } else {
      g_rds_capture_writing.store(false, std::memory_order_release);
    }
  }

  for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
    /* Center CU8 and complex channel LPF (pre-demod adjacent-channel relief). */
    const float i_in = static_cast<float>(static_cast<int32_t>(iq[offset]) - 128);
    const float q_in = static_cast<float>(static_cast<int32_t>(iq[offset + 1]) - 128);
    rtl_audio.iq_i_lpf += iq_lpf_k * (i_in - rtl_audio.iq_i_lpf);
    rtl_audio.iq_q_lpf += iq_lpf_k * (q_in - rtl_audio.iq_q_lpf);

    rtl_audio.i_sum += rtl_audio.iq_i_lpf;
    rtl_audio.q_sum += rtl_audio.iq_q_lpf;
    if (++rtl_audio.rf_phase != rf_decimation) continue;

    const float i = rtl_audio.i_sum * inv_rf_decim;
    const float q = rtl_audio.q_sum * inv_rf_decim;
    rtl_audio.i_sum = 0;
    rtl_audio.q_sum = 0;
    rtl_audio.rf_phase = 0;
    if (rtl_audio.have_previous) {
      const float phase = fast_phase(rtl_audio.previous_i * q - rtl_audio.previous_q * i,
                                     rtl_audio.previous_i * i + rtl_audio.previous_q * q);
      if (capture_mpx && capture_write < kRdsCaptureSamples) {
        const float scaled = constrain(phase * kRdsMpxToInt16, -32767.0f, 32767.0f);
        g_rds_capture_buf[capture_write++] = static_cast<int16_t>(scaled);
      }

      /*
       * Stereo pilot/L-R recovery, tapped from the full-bandwidth composite
       * BEFORE the ~16 kHz mono LPF below discards everything past 15 kHz.
       * See the kPilotTwoRCos / kSubTwoRCos comment block for the derivation.
       */
      float diff = 0.0f;
      if (wbfm) {
        /* All-pole resonator at 19 kHz: output is the newest y[n] itself. */
        const float pilot_y0 =
            phase + kPilotTwoRCos * rtl_audio.pilot_y1 - kPilotR2 * rtl_audio.pilot_y2;
        rtl_audio.pilot_y2 = rtl_audio.pilot_y1;
        rtl_audio.pilot_y1 = pilot_y0;

        /* Slow rectified envelope for lock hysteresis (~30 ms time constant). */
        rtl_audio.pilot_env += kStereoEnvK * (fabsf(pilot_y0) - rtl_audio.pilot_env);
        if (rtl_audio.stereo_locked) {
          if (rtl_audio.pilot_env < kStereoLockOff) rtl_audio.stereo_locked = false;
        } else {
          if (rtl_audio.pilot_env > kStereoLockOn) rtl_audio.stereo_locked = true;
        }

        /*
         * Regenerate the 38 kHz demod carrier by squaring the pilot:
         *   cos²θ = ½ + ½cos(2θ)
         * The resonator at 38 kHz rejects the ½ DC term and passes the
         * cos(2θ) term — a clean sinusoid at exactly 2× the pilot frequency,
         * phase-locked to it with no separate PLL loop.
         */
        const float pilot_sq = pilot_y0 * pilot_y0;
        const float sub_y0 =
            pilot_sq + kSubTwoRCos * rtl_audio.sub_y1 - kSubR2 * rtl_audio.sub_y2;
        rtl_audio.sub_y2 = rtl_audio.sub_y1;
        rtl_audio.sub_y1 = sub_y0;

        /*
         * Normalize the regenerated carrier to ~unit amplitude before using it
         * to demodulate: the squarer's AC term has amplitude ½·pilot_env², so
         * dividing by that keeps L-R gain from drifting with signal strength
         * relative to the L+R (mono) path, which is unit-scaled by definition
         * of the discriminator itself.
         */
        const float carrier_amp = 0.5f * rtl_audio.pilot_env * rtl_audio.pilot_env + 1.0e-6f;
        const float carrier = sub_y0 / carrier_amp;

        /*
         * Coherent (synchronous) demod of the 38 kHz DSB-SC L-R subcarrier:
         * multiplying by the in-phase carrier folds it to baseband at half
         * amplitude (cos·cos = ½cos(Δ) + ½cos(Σ)); the ×2 undoes that loss.
         * The Σ image lands at 76 kHz and the mono/pilot/RDS content lands
         * elsewhere in the spectrum — both are rejected by the same ~16 kHz
         * LPF that already shapes the mono path below.
         */
        diff = rtl_audio.stereo_locked ? 2.0f * phase * carrier : 0.0f;

        rds_process_mpx_sample(phase, pilot_y0);
      }

      /* Post-discriminator mono audio LPF, then boxcar to 48 kHz. */
      rtl_audio.channel_filter += audio_lpf_k * (phase - rtl_audio.channel_filter);
      rtl_audio.audio_sum += rtl_audio.channel_filter;
      if (wbfm) {
        rtl_audio.channel_filter_diff += audio_lpf_k * (diff - rtl_audio.channel_filter_diff);
        rtl_audio.audio_sum_diff += rtl_audio.channel_filter_diff;
      }
      if (++rtl_audio.audio_phase == kFmAudioDecim) {
        const float demod_sum = rtl_audio.audio_sum * inv_audio_decim;
        const float demod_diff = rtl_audio.audio_sum_diff * inv_audio_decim;
        rtl_audio.audio_sum = 0;
        rtl_audio.audio_sum_diff = 0;
        rtl_audio.audio_phase = 0;

        /* Mono (L+R) path — this is what actually reaches the speaker. */
        rtl_audio.deemphasis += deemph_k * (demod_sum - rtl_audio.deemphasis);
        rtl_audio.dc += 0.0008f * (rtl_audio.deemphasis - rtl_audio.dc);
        const int16_t sample =
            shape_audio_sample(rtl_audio.deemphasis - rtl_audio.dc, audio_scale);
        audio[audio_count++] = sample;
        const int16_t magnitude = sample < 0 ? -sample : sample;
        if (magnitude > rtl_audio.peak) rtl_audio.peak = magnitude;
        rtl_audio.square_sum += static_cast<uint32_t>(sample * sample);
        ++rtl_audio.samples;

        /*
         * L/R for the dashboard meters only — never touches audio[]/playRaw.
         * Reuses agc_gain read-only so the meter sits in the same numeric
         * scale shape_audio_sample uses for the mono readout, without
         * mutating the AGC state that call owns.
         */
        if (wbfm) {
          const float scale = audio_scale * rtl_audio.agc_gain;
          float l, r;
          if (rtl_audio.stereo_locked) {
            rtl_audio.deemphasis_l += deemph_k * ((demod_sum + demod_diff) - rtl_audio.deemphasis_l);
            rtl_audio.deemphasis_r += deemph_k * ((demod_sum - demod_diff) - rtl_audio.deemphasis_r);
            rtl_audio.dc_l += 0.0008f * (rtl_audio.deemphasis_l - rtl_audio.dc_l);
            rtl_audio.dc_r += 0.0008f * (rtl_audio.deemphasis_r - rtl_audio.dc_r);
            l = (rtl_audio.deemphasis_l - rtl_audio.dc_l) * scale;
            r = (rtl_audio.deemphasis_r - rtl_audio.dc_r) * scale;
          } else {
            /* Not locked: mirror mono so L=R, matching the "MONO" UI state. */
            l = r = (rtl_audio.deemphasis - rtl_audio.dc) * scale;
          }
          // Meter the programme before AGC and the soft limiter: after them
          // every block peaks at the 12000 ceiling (-8.7 dBFS), which pinned
          // the stereo VU in place.
          const float meter_gain = rtl_audio.agc_gain > 0.0f ? 1.0f / rtl_audio.agc_gain : 1.0f;
          const float al = fabsf(l) * meter_gain, ar = fabsf(r) * meter_gain;
          if (al > meter_peak_l) meter_peak_l = al;
          if (ar > meter_peak_r) meter_peak_r = ar;
          meter_any = true;
        }
      }
    }
    rtl_audio.previous_i = i;
    rtl_audio.previous_q = q;
    rtl_audio.have_previous = true;
  }
  if (capture_mpx) {
    g_rds_capture_write.store(capture_write, std::memory_order_release);
    if (capture_write >= kRdsCaptureSamples) {
      g_rds_capture_active.store(false, std::memory_order_release);
      g_rds_capture_ready.store(true, std::memory_order_release);
      Serial.printf("RTL_RDS_CAPTURE_FULL samples=%u seconds=%u note=send_stop_to_export\n",
                    static_cast<unsigned>(capture_write),
                    static_cast<unsigned>(kRdsCaptureSeconds));
    }
    g_rds_capture_writing.store(false, std::memory_order_release);
  }
  if (wbfm) {
    /* dBFS relative to int16 full scale; matches the mono peak's own units. */
    constexpr float kFullScaleInv = 1.0f / 32768.0f;
    rtl_stereo_locked.store(rtl_audio.stereo_locked, std::memory_order_relaxed);
    rtl_pilot_env.store(rtl_audio.pilot_env, std::memory_order_relaxed);
    if (meter_any) {
      const float l_dbfs = 20.0f * log10f(meter_peak_l * kFullScaleInv + 1.0e-6f);
      const float r_dbfs = 20.0f * log10f(meter_peak_r * kFullScaleInv + 1.0e-6f);
      rtl_audio_left_dbfs.store(l_dbfs, std::memory_order_relaxed);
      rtl_audio_right_dbfs.store(r_dbfs, std::memory_order_relaxed);
    }
    rds_publish_state();
  }
  queue_audio_samples(audio, audio_count);
}

void demodulate_am_ref(const uint8_t* iq, size_t bytes, float audio_scale,
                   uint32_t sample_rate_sps) {
  int16_t* audio = rtl_audio_buffers[rtl_audio.buffer];
  size_t audio_count = 0;
  const uint8_t rf_decimation = rtl_rf_decimation(sample_rate_sps);
  if (!rf_decimation) return;
  const float inv_rf_decim = 1.0f / static_cast<float>(rf_decimation);
  const float iq_lpf_k = rtl_filter_alpha(RtlBand::am, sample_rate_sps);
  for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
    const float i_in = static_cast<float>(static_cast<int32_t>(iq[offset]) - 128);
    const float q_in = static_cast<float>(static_cast<int32_t>(iq[offset + 1]) - 128);
    rtl_audio.iq_i_lpf += iq_lpf_k * (i_in - rtl_audio.iq_i_lpf);
    rtl_audio.iq_q_lpf += iq_lpf_k * (q_in - rtl_audio.iq_q_lpf);
    rtl_audio.iq_i_lpf2 += iq_lpf_k * (rtl_audio.iq_i_lpf - rtl_audio.iq_i_lpf2);
    rtl_audio.iq_q_lpf2 += iq_lpf_k * (rtl_audio.iq_q_lpf - rtl_audio.iq_q_lpf2);
    rtl_audio.i_sum += rtl_audio.iq_i_lpf2;
    rtl_audio.q_sum += rtl_audio.iq_q_lpf2;
    if (++rtl_audio.rf_phase != rf_decimation) continue;

    const float i = rtl_audio.i_sum * inv_rf_decim;
    const float q = rtl_audio.q_sum * inv_rf_decim;
    rtl_audio.i_sum = 0;
    rtl_audio.q_sum = 0;
    rtl_audio.rf_phase = 0;
    const float envelope = sqrtf(i * i + q * q);
    rtl_audio.envelope_filter += 0.35f * (envelope - rtl_audio.envelope_filter);
    rtl_audio.audio_sum += rtl_audio.envelope_filter;
    if (++rtl_audio.audio_phase == 5) {
      const float demodulated = rtl_audio.audio_sum * 0.2f;
      rtl_audio.audio_sum = 0;
      rtl_audio.audio_phase = 0;
      rtl_audio.dc += 0.002f * (demodulated - rtl_audio.dc);
      const int16_t sample =
          shape_audio_sample(demodulated - rtl_audio.dc, audio_scale);
      audio[audio_count++] = sample;
      const int16_t magnitude = sample < 0 ? -sample : sample;
      if (magnitude > rtl_audio.peak) rtl_audio.peak = magnitude;
      rtl_audio.square_sum += static_cast<uint32_t>(sample * sample);
      ++rtl_audio.samples;
    }
  }
  if (g_stream_band == RtlBand::shortwave)
    orcsdr::shortwave::audio_dsp::process(audio, audio_count,
                                         rtl_signal_dbfs_smooth);
  queue_audio_samples(audio, audio_count);
}

void demodulate_ssb_ref(const uint8_t* iq, size_t bytes, float audio_scale, CbMode mode,
                    uint32_t sample_rate_sps) {
  int16_t* audio = rtl_audio_buffers[rtl_audio.buffer];
  size_t audio_count = 0;
  const uint8_t rf_decimation = rtl_rf_decimation(sample_rate_sps);
  if (!rf_decimation) return;
  const float inv_rf_decim = 1.0f / static_cast<float>(rf_decimation);
  const float iq_lpf_k = rtl_filter_alpha(RtlBand::cb, sample_rate_sps);
  constexpr float kPi = 3.14159265358979323846f;
  const float bfo_hz = 1500.0f + cb_clarifier_hz.load(std::memory_order_relaxed);
  const float direction = mode == CbMode::usb ? -1.0f : 1.0f;
  const float step = direction * 2.0f * kPi * bfo_hz / kRtlDemodRateSps;
  const float step_cos = cosf(step);
  const float step_sin = sinf(step);

  for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
    const float i_in = static_cast<float>(static_cast<int32_t>(iq[offset]) - 128);
    const float q_in = static_cast<float>(static_cast<int32_t>(iq[offset + 1]) - 128);
    rtl_audio.iq_i_lpf += iq_lpf_k * (i_in - rtl_audio.iq_i_lpf);
    rtl_audio.iq_q_lpf += iq_lpf_k * (q_in - rtl_audio.iq_q_lpf);
    rtl_audio.i_sum += rtl_audio.iq_i_lpf;
    rtl_audio.q_sum += rtl_audio.iq_q_lpf;
    if (++rtl_audio.rf_phase != rf_decimation) continue;

    const float i = rtl_audio.i_sum * inv_rf_decim;
    const float q = rtl_audio.q_sum * inv_rf_decim;
    rtl_audio.i_sum = 0;
    rtl_audio.q_sum = 0;
    rtl_audio.rf_phase = 0;
    rtl_audio.audio_sum += i * rtl_audio.ssb_cos - q * rtl_audio.ssb_sin;
    const float next_cos = rtl_audio.ssb_cos * step_cos - rtl_audio.ssb_sin * step_sin;
    rtl_audio.ssb_sin = rtl_audio.ssb_sin * step_cos + rtl_audio.ssb_cos * step_sin;
    rtl_audio.ssb_cos = next_cos;
    if (++rtl_audio.audio_phase != 5) continue;

    const float demodulated = rtl_audio.audio_sum * 0.2f;
    rtl_audio.audio_sum = 0;
    rtl_audio.audio_phase = 0;
    rtl_audio.dc += 0.002f * (demodulated - rtl_audio.dc);
    const int16_t sample = shape_audio_sample(demodulated - rtl_audio.dc, audio_scale);
    audio[audio_count++] = sample;
    const int16_t magnitude = sample < 0 ? -sample : sample;
    if (magnitude > rtl_audio.peak) rtl_audio.peak = magnitude;
    rtl_audio.square_sum += static_cast<uint32_t>(sample * sample);
    ++rtl_audio.samples;
  }
  const float norm = sqrtf(rtl_audio.ssb_cos * rtl_audio.ssb_cos +
                           rtl_audio.ssb_sin * rtl_audio.ssb_sin);
  if (norm > 0.5f) {
    rtl_audio.ssb_cos /= norm;
    rtl_audio.ssb_sin /= norm;
  }
  queue_audio_samples(audio, audio_count);
}

constexpr size_t kDspAbMaxBlocks = 64;
constexpr size_t kDspAbBlockBytes = sizeof(rtl_iq_processing);
constexpr size_t kDspAbAudioCap = kDspAbMaxBlocks * 16384;       // >= 48k samples per block
constexpr size_t kDspAbMpxCap = kDspAbMaxBlocks * 16384 * 2;     // phase + pilot at 240k

struct DspAbBlockMeta {
  size_t bytes;
  RtlBand band;
  float audio_scale;
  uint32_t sample_rate_sps;
  CbMode cb_mode;
};

struct DspAbPass {
  int16_t* audio = nullptr;
  size_t audio_n = 0;
  float* mpx = nullptr;
  size_t mpx_n = 0;
  RtlAudioState* states = nullptr;      // after every block
  float* meters = nullptr;              // 4 per block: L, R, pilot_env, stereo_locked
  float* level = nullptr;               // 3 per block: dbfs, smooth, clipping
  uint32_t elapsed_us = 0;
  uint32_t level_us = 0;
  bool overflow = false;
};

struct DspAbCapture {
  uint8_t* blocks = nullptr;
  DspAbBlockMeta meta[kDspAbMaxBlocks]{};
  size_t count = 0;
  size_t target = 0;
  RtlAudioState start{};
  float level_start[3]{};
  std::atomic<bool> armed{false};
  std::atomic<bool> run_requested{false};
} g_dsp_ab;

DspAbPass* g_dsp_ab_sink = nullptr;

bool dsp_ab_alloc(DspAbPass& p) {
  auto alloc = [](size_t bytes) { return heap_caps_calloc(1, bytes, MALLOC_CAP_SPIRAM); };
  if (!p.audio) p.audio = static_cast<int16_t*>(alloc(kDspAbAudioCap * sizeof(int16_t)));
  if (!p.mpx) p.mpx = static_cast<float*>(alloc(kDspAbMpxCap * sizeof(float)));
  if (!p.states)
    p.states = static_cast<RtlAudioState*>(alloc(kDspAbMaxBlocks * sizeof(RtlAudioState)));
  if (!p.meters) p.meters = static_cast<float*>(alloc(kDspAbMaxBlocks * 4 * sizeof(float)));
  if (!p.level) p.level = static_cast<float*>(alloc(kDspAbMaxBlocks * 3 * sizeof(float)));
  p.audio_n = p.mpx_n = 0;
  p.overflow = false;
  return p.audio && p.mpx && p.states && p.meters && p.level;
}

// Hooks called from queue_audio_samples / rds_process_mpx_sample while a pass runs.
bool dsp_ab_take_audio(const int16_t* audio, size_t count) {
  DspAbPass* p = g_dsp_ab_sink;
  if (!p) return false;
  if (p->audio_n + count > kDspAbAudioCap) { p->overflow = true; return true; }
  memcpy(p->audio + p->audio_n, audio, count * sizeof(int16_t));
  p->audio_n += count;
  return true;
}

bool dsp_ab_take_mpx(float phase, float pilot) {
  DspAbPass* p = g_dsp_ab_sink;
  if (!p) return false;
  if (p->mpx_n + 2 > kDspAbMpxCap) { p->overflow = true; return true; }
  p->mpx[p->mpx_n++] = phase;
  p->mpx[p->mpx_n++] = pilot;
  return true;
}

bool dsp_ab_arm(size_t blocks) {
  if (blocks == 0 || blocks > kDspAbMaxBlocks) return false;
  if (!g_dsp_ab.blocks)
    g_dsp_ab.blocks = static_cast<uint8_t*>(
        heap_caps_malloc(kDspAbMaxBlocks * kDspAbBlockBytes, MALLOC_CAP_SPIRAM));
  if (!g_dsp_ab.blocks) return false;
  g_dsp_ab.count = 0;
  g_dsp_ab.target = blocks;
  g_dsp_ab.armed.store(true, std::memory_order_release);
  return true;
}

// DSP task, before the block is processed live.
void dsp_ab_on_block(const RtlIqBlock& block) {
  if (!g_dsp_ab.armed.load(std::memory_order_acquire)) return;
  const size_t k = g_dsp_ab.count;
  if (k == 0) {
    g_dsp_ab.start = rtl_audio;
    g_dsp_ab.level_start[0] = rtl_signal_dbfs.load(std::memory_order_relaxed);
    g_dsp_ab.level_start[1] = rtl_signal_dbfs_smooth;
    g_dsp_ab.level_start[2] = rtl_iq_clipping_percent.load(std::memory_order_relaxed);
  }
  memcpy(g_dsp_ab.blocks + k * kDspAbBlockBytes, block.data, block.bytes);
  g_dsp_ab.meta[k] = {block.bytes, block.band, block.audio_scale, block.sample_rate_sps,
                      cb_mode.load(std::memory_order_relaxed)};
  if (++g_dsp_ab.count >= g_dsp_ab.target) {
    g_dsp_ab.armed.store(false, std::memory_order_release);
    Serial.printf("RTL_DSP_AB_CAPTURED blocks=%u band=%s rate=%lu\n",
                  static_cast<unsigned>(g_dsp_ab.count), rtl_band_name(g_dsp_ab.meta[0].band),
                  static_cast<unsigned long>(g_dsp_ab.meta[0].sample_rate_sps));
  }
}

void dsp_ab_demod(bool ref, const uint8_t* iq, const DspAbBlockMeta& m) {
  const bool am = m.band == RtlBand::am || (m.band == RtlBand::cb && m.cb_mode == CbMode::am);
  if (am) {
    if (ref) demodulate_am_ref(iq, m.bytes, m.audio_scale, m.sample_rate_sps);
    else demodulate_am(iq, m.bytes, m.audio_scale, m.sample_rate_sps);
  } else if (m.band == RtlBand::cb) {
    if (ref) demodulate_ssb_ref(iq, m.bytes, m.audio_scale, m.cb_mode, m.sample_rate_sps);
    else demodulate_ssb(iq, m.bytes, m.audio_scale, m.cb_mode, m.sample_rate_sps);
  } else if (ref) {
    demodulate_fm_ref(iq, m.bytes, m.audio_scale, m.band == RtlBand::fm, m.sample_rate_sps);
  } else {
    demodulate_fm(iq, m.bytes, m.audio_scale, m.band == RtlBand::fm, m.sample_rate_sps);
  }
}

void dsp_ab_pass(bool ref, DspAbPass& p) {
  rtl_audio = g_dsp_ab.start;
  rtl_signal_dbfs.store(g_dsp_ab.level_start[0], std::memory_order_relaxed);
  rtl_signal_dbfs_smooth = g_dsp_ab.level_start[1];
  rtl_iq_clipping_percent.store(g_dsp_ab.level_start[2], std::memory_order_relaxed);
  g_dsp_ab_sink = &p;
  p.elapsed_us = p.level_us = 0;
  for (size_t k = 0; k < g_dsp_ab.count; ++k) {
    const uint8_t* iq = g_dsp_ab.blocks + k * kDspAbBlockBytes;
    const DspAbBlockMeta& m = g_dsp_ab.meta[k];
    uint32_t t0 = micros();
    bool level_ok = false;
    if (ref) {
      update_signal_level_from_iq_ref(iq, m.bytes);
    } else {
      level_ok = update_signal_power_from_iq(iq, m.bytes);
      rtl_block_clip.reset();
    }
    p.level_us += micros() - t0;
    t0 = micros();
    dsp_ab_demod(ref, iq, m);
    p.elapsed_us += micros() - t0;
    t0 = micros();
    if (!ref && level_ok)
      update_clipping_from_count(rtl_block_clip.counted ? rtl_block_clip.pairs
                                                        : count_clipped_pairs(iq, m.bytes),
                                 m.bytes);
    p.level_us += micros() - t0;
    p.level[k * 3 + 0] = rtl_signal_dbfs.load(std::memory_order_relaxed);
    p.level[k * 3 + 1] = rtl_signal_dbfs_smooth;
    p.level[k * 3 + 2] = rtl_iq_clipping_percent.load(std::memory_order_relaxed);
    p.states[k] = rtl_audio;
    p.meters[k * 4 + 0] = rtl_audio_left_dbfs.load(std::memory_order_relaxed);
    p.meters[k * 4 + 1] = rtl_audio_right_dbfs.load(std::memory_order_relaxed);
    p.meters[k * 4 + 2] = rtl_pilot_env.load(std::memory_order_relaxed);
    p.meters[k * 4 + 3] = rtl_stereo_locked.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
  }
  g_dsp_ab_sink = nullptr;
}

size_t dsp_ab_diff_count(const void* a, const void* b, size_t bytes) {
  const uint8_t* x = static_cast<const uint8_t*>(a);
  const uint8_t* y = static_cast<const uint8_t*>(b);
  size_t n = 0;
  for (size_t i = 0; i < bytes; ++i) n += x[i] != y[i];
  return n;
}


// --- Cycle benchmark kernels (identical code; one in IRAM, one XIP) --------
#define DSP_AB_LEVEL_BODY                                                     \
  uint32_t clipped = 0;                                                       \
  uint64_t sum = 0;                                                           \
  for (size_t i = 0; i + 3 < bytes; i += 4) {                                 \
    uint32_t word;                                                            \
    memcpy(&word, iq + i, sizeof(word));                                      \
    const uint32_t rails = rtl_rail_bytes(word);                              \
    clipped += (rails & 0x00008080u) != 0;                                    \
    clipped += (rails & 0x80800000u) != 0;                                    \
    if ((i & 31u) != 0) continue;                                             \
    const int32_t ii = static_cast<int32_t>(iq[i]) - 128;                     \
    const int32_t qq = static_cast<int32_t>(iq[i + 1]) - 128;                 \
    sum += static_cast<uint32_t>(ii * ii + qq * qq);                          \
  }                                                                           \
  return clipped + static_cast<uint32_t>(sum);

__attribute__((noinline)) uint32_t dsp_ab_level_xip(const uint8_t* iq, size_t bytes) {
  DSP_AB_LEVEL_BODY
}
__attribute__((noinline)) IRAM_ATTR uint32_t dsp_ab_level_iram(const uint8_t* iq, size_t bytes) {
  DSP_AB_LEVEL_BODY
}
__attribute__((noinline)) uint32_t dsp_ab_sum_xip(const uint8_t* iq, size_t bytes) {
  uint32_t sum = 0;
  for (size_t i = 0; i < bytes; ++i) sum += iq[i];
  return sum;
}

struct DspAbCycles { uint32_t irq_off, irq_on; };

template <typename F>
DspAbCycles dsp_ab_cycles(F&& fn) {
  volatile uint32_t sink = 0;
  DspAbCycles c{};
  const unsigned state = portSET_INTERRUPT_MASK_FROM_ISR();
  uint32_t t0 = esp_cpu_get_cycle_count();
  sink = sink + fn();
  c.irq_off = esp_cpu_get_cycle_count() - t0;
  portCLEAR_INTERRUPT_MASK_FROM_ISR(state);
  t0 = esp_cpu_get_cycle_count();
  sink = sink + fn();
  c.irq_on = esp_cpu_get_cycle_count() - t0;
  (void)sink;
  return c;
}

void dsp_ab_cycle_bench(size_t bytes) {
  const uint8_t* sram = rtl_iq_processing;
  const uint8_t* psram = g_dsp_ab.blocks;
  const size_t samples = bytes / 2;
  const auto p = [samples](const char* name, DspAbCycles c) {
    Serial.printf("RTL_DSP_AB_CYCLES kernel=%s cycles_per_sample_irq_off=%.2f "
                  "cycles_per_sample_irq_on=%.2f\n", name,
                  static_cast<double>(c.irq_off) / samples,
                  static_cast<double>(c.irq_on) / samples);
  };
  p("sum_bytes_xip_sram", dsp_ab_cycles([&] { return dsp_ab_sum_xip(sram, bytes); }));
  p("level_xip_sram", dsp_ab_cycles([&] { return dsp_ab_level_xip(sram, bytes); }));
  p("level_iram_sram", dsp_ab_cycles([&] { return dsp_ab_level_iram(sram, bytes); }));
  p("level_xip_psram", dsp_ab_cycles([&] { return dsp_ab_level_xip(psram, bytes); }));
  p("level_iram_psram", dsp_ab_cycles([&] { return dsp_ab_level_iram(psram, bytes); }));
  const uint32_t c0 = esp_cpu_get_cycle_count();
  const uint32_t u0 = micros();
  while (micros() - u0 < 1000) {
  }
  Serial.printf("RTL_DSP_AB_CLOCK cycles_per_us=%lu\n",
                static_cast<unsigned long>((esp_cpu_get_cycle_count() - c0) / (micros() - u0)));
}


// Verbatim pre-Stage-1 RDS front end (per-sample), renamed.
void rds_process_mpx_sample_ref(float phase) {
  const float rds_bp0 = phase + kRdsBpTwoRCos * rtl_audio.rds_bp_y1 -
                        kRdsBpR2 * rtl_audio.rds_bp_y2;
  rtl_audio.rds_bp_y2 = rtl_audio.rds_bp_y1;
  rtl_audio.rds_bp_y1 = rds_bp0;
  rtl_audio.rds_env += kRdsEnvK * (fabsf(rds_bp0) - rtl_audio.rds_env);

  rds_renorm_nco(rtl_audio.rds_nco_i, rtl_audio.rds_nco_q,
                 rtl_audio.rds_nco_inc_cos, rtl_audio.rds_nco_inc_sin,
                 rtl_audio.rds_nco_omega, rtl_audio.rds_nco_recalc_counter);
  rtl_audio.rds_i_lpf += kRdsSymLpfK * (phase * rtl_audio.rds_nco_i - rtl_audio.rds_i_lpf);
  rtl_audio.rds_q_lpf += kRdsSymLpfK * (phase * rtl_audio.rds_nco_q - rtl_audio.rds_q_lpf);
  rtl_audio.rds_i_lpf2 += kRdsSymLpfK * (rtl_audio.rds_i_lpf - rtl_audio.rds_i_lpf2);
  rtl_audio.rds_q_lpf2 += kRdsSymLpfK * (rtl_audio.rds_q_lpf - rtl_audio.rds_q_lpf2);

  if (++rtl_audio.rds_slicer_decim < kRdsSlicerDecim) return;
  rtl_audio.rds_slicer_decim = 0;

  const float vi = rtl_audio.rds_i_lpf2;
  const float vq = rtl_audio.rds_q_lpf2;
  bool first_dump = true;
  for (RdsTimingTrack& timing : rtl_audio.rds_timing) {
    timing.chip_i_sum += vi;
    timing.chip_q_sum += vq;
    timing.chip_phase += kRdsChipInc24;
    if (timing.chip_phase < 1.0f) continue;
    timing.chip_phase -= 1.0f;
    const float chip_i = timing.chip_i_sum;
    const float chip_q = timing.chip_q_sum;
    timing.chip_i_sum = 0.0f;
    timing.chip_q_sum = 0.0f;
    if (first_dump) {
      first_dump = false;
      const float pwr = chip_i * chip_i + chip_q * chip_q + 1.0e-9f;
      rtl_audio.rds_nco_omega += 1.2e-6f * (chip_i * chip_q) / pwr;
      if (rtl_audio.rds_nco_omega < kRdsNcoNominal - 0.00035f)
        rtl_audio.rds_nco_omega = kRdsNcoNominal - 0.00035f;
      if (rtl_audio.rds_nco_omega > kRdsNcoNominal + 0.00035f)
        rtl_audio.rds_nco_omega = kRdsNcoNominal + 0.00035f;
    }
    if (timing.have_prev_chip) {
      RdsHypothesis& h = timing.hyp[(timing.chip_index - 1u) & 1u];
      const float pair_i = chip_i - timing.prev_chip_i;
      const float pair_q = chip_q - timing.prev_chip_q;
      if (h.have_prev_pair) {
        const bool data_bit = pair_i * h.prev_pair_i + pair_q * h.prev_pair_q < 0.0f;
        rds_hypothesis_feed(h, data_bit);
      }
      h.prev_pair_i = pair_i;
      h.prev_pair_q = pair_q;
      h.have_prev_pair = true;
    }
    timing.prev_chip_i = chip_i;
    timing.prev_chip_q = chip_q;
    timing.have_prev_chip = true;
    ++timing.chip_index;
    ++rtl_audio.rds_diag_chip_count;
  }
}

// RDS old (per sample) vs new (block) on the reference pass's MPX stream,
// from the same starting state; the whole RtlAudioState (RDS timing tracks and
// hypotheses included) must match.
void dsp_ab_rds_compare(const DspAbPass& ref_pass) {
  const size_t count = ref_pass.mpx_n / 2;
  if (count == 0) return;
  float* phases = static_cast<float*>(heap_caps_malloc(count * sizeof(float), MALLOC_CAP_SPIRAM));
  if (!phases) {
    Serial.printf("RTL_DSP_AB_RDS error=alloc bytes=%u\n", static_cast<unsigned>(count * sizeof(float)));
    return;
  }
  for (size_t k = 0; k < count; ++k) phases[k] = ref_pass.mpx[k * 2];
  static RtlAudioState ref_state, new_state;
  rtl_audio = g_dsp_ab.start;
  uint32_t t0 = micros();
  for (size_t k = 0; k < count; ++k) rds_process_mpx_sample_ref(phases[k]);
  const uint32_t ref_us = micros() - t0;
  ref_state = rtl_audio;
  rtl_audio = g_dsp_ab.start;
  t0 = micros();
  for (size_t off = 0; off < count;) {
    const size_t chunk = std::min<size_t>(1638, count - off);
    rds_process_mpx_block(phases + off, nullptr, chunk);
    off += chunk;
  }
  const uint32_t new_us = micros() - t0;
  new_state = rtl_audio;
  heap_caps_free(phases);
  Serial.printf("RTL_DSP_AB_RDS mpx_samples=%u state_byte_diffs=%u chips=%lu/%lu ref_us=%lu new_us=%lu\n",
                static_cast<unsigned>(count),
                static_cast<unsigned>(dsp_ab_diff_count(&ref_state, &new_state, sizeof(RtlAudioState))),
                static_cast<unsigned long>(ref_state.rds_diag_chip_count),
                static_cast<unsigned long>(new_state.rds_diag_chip_count),
                static_cast<unsigned long>(ref_us), static_cast<unsigned long>(new_us));
}

// DSP task: run both passes on the saved blocks, compare, restore live state.
void dsp_ab_run_if_requested() {
  if (!g_dsp_ab.run_requested.exchange(false, std::memory_order_acq_rel)) return;
  if (g_dsp_ab.count == 0 || g_dsp_ab.armed.load(std::memory_order_acquire)) {
    Serial.println("RTL_DSP_AB_ERROR no_capture");
    return;
  }
  static DspAbPass ref_pass, new_pass;
  if (!dsp_ab_alloc(ref_pass) || !dsp_ab_alloc(new_pass)) {
    Serial.println("RTL_DSP_AB_ERROR alloc");
    return;
  }
  const RtlAudioState live = rtl_audio;
  const float live_level[3] = {rtl_signal_dbfs.load(std::memory_order_relaxed),
                               rtl_signal_dbfs_smooth,
                               rtl_iq_clipping_percent.load(std::memory_order_relaxed)};
  dsp_ab_pass(true, ref_pass);
  dsp_ab_pass(false, new_pass);
  rtl_audio = live;
  rtl_signal_dbfs.store(live_level[0], std::memory_order_relaxed);
  rtl_signal_dbfs_smooth = live_level[1];
  rtl_iq_clipping_percent.store(live_level[2], std::memory_order_relaxed);

  const size_t n = g_dsp_ab.count;
  const bool audio_len_ok = ref_pass.audio_n == new_pass.audio_n;
  const bool mpx_len_ok = ref_pass.mpx_n == new_pass.mpx_n;
  const long audio_diff = audio_len_ok
      ? static_cast<long>(dsp_ab_diff_count(ref_pass.audio, new_pass.audio,
                                            ref_pass.audio_n * sizeof(int16_t)))
      : -1;
  const long mpx_diff = mpx_len_ok
      ? static_cast<long>(dsp_ab_diff_count(ref_pass.mpx, new_pass.mpx,
                                            ref_pass.mpx_n * sizeof(float)))
      : -1;
  size_t state_blocks_diff = 0;
  for (size_t k = 0; k < n; ++k)
    state_blocks_diff +=
        memcmp(&ref_pass.states[k], &new_pass.states[k], sizeof(RtlAudioState)) != 0;
  const size_t meter_diff =
      dsp_ab_diff_count(ref_pass.meters, new_pass.meters, n * 4 * sizeof(float));
  const size_t level_diff =
      dsp_ab_diff_count(ref_pass.level, new_pass.level, n * 3 * sizeof(float));
  int max_audio_delta = 0;
  if (audio_len_ok)
    for (size_t i = 0; i < ref_pass.audio_n; ++i)
      max_audio_delta = std::max(max_audio_delta, std::abs(ref_pass.audio[i] - new_pass.audio[i]));
  float max_mpx_delta = 0.0f;
  if (mpx_len_ok)
    for (size_t i = 0; i < ref_pass.mpx_n; ++i)
      max_mpx_delta = std::max(max_mpx_delta, fabsf(ref_pass.mpx[i] - new_pass.mpx[i]));
  // Memory placement benchmark: same new code on a PSRAM block vs an internal
  // SRAM copy (rtl_iq_processing), first 16 blocks, outputs discarded.
  uint32_t level_psram_us = 0, level_sram_us = 0, demod_psram_us = 0, demod_sram_us = 0;
  const size_t bench_blocks = std::min<size_t>(n, 16);
  g_dsp_ab_sink = &new_pass;
  for (size_t k = 0; k < bench_blocks; ++k) {
    const uint8_t* ps = g_dsp_ab.blocks + k * kDspAbBlockBytes;
    const DspAbBlockMeta& m = g_dsp_ab.meta[k];
    memcpy(rtl_iq_processing, ps, m.bytes);
    rtl_audio = g_dsp_ab.start;
    new_pass.audio_n = new_pass.mpx_n = 0;
    uint32_t t0 = micros();
    update_signal_level_from_iq(ps, m.bytes);
    level_psram_us += micros() - t0;
    t0 = micros();
    update_signal_level_from_iq(rtl_iq_processing, m.bytes);
    level_sram_us += micros() - t0;
    t0 = micros();
    dsp_ab_demod(false, ps, m);
    demod_psram_us += micros() - t0;
    rtl_audio = g_dsp_ab.start;
    new_pass.audio_n = new_pass.mpx_n = 0;
    t0 = micros();
    dsp_ab_demod(false, rtl_iq_processing, m);
    demod_sram_us += micros() - t0;
  }
  g_dsp_ab_sink = nullptr;
  dsp_ab_cycle_bench(g_dsp_ab.meta[0].bytes);
  dsp_ab_rds_compare(ref_pass);
  rtl_audio = live;
  rtl_signal_dbfs.store(live_level[0], std::memory_order_relaxed);
  rtl_signal_dbfs_smooth = live_level[1];
  rtl_iq_clipping_percent.store(live_level[2], std::memory_order_relaxed);
  Serial.printf("RTL_DSP_AB_MEM blocks=%u level_psram_us=%lu level_sram_us=%lu "
                "demod_psram_us=%lu demod_sram_us=%lu\n",
                static_cast<unsigned>(bench_blocks),
                static_cast<unsigned long>(level_psram_us / std::max<size_t>(1, bench_blocks)),
                static_cast<unsigned long>(level_sram_us / std::max<size_t>(1, bench_blocks)),
                static_cast<unsigned long>(demod_psram_us / std::max<size_t>(1, bench_blocks)),
                static_cast<unsigned long>(demod_sram_us / std::max<size_t>(1, bench_blocks)));
  Serial.printf("RTL_DSP_AB band=%s rate=%lu blocks=%u audio_samples=%u/%u mpx_values=%u/%u "
                "audio_byte_diffs=%ld max_audio_delta=%d mpx_byte_diffs=%ld max_mpx_delta=%.9g "
                "state_blocks_diff=%u meter_byte_diffs=%u level_byte_diffs=%u overflow=%d/%d "
                "ref_demod_us=%lu new_demod_us=%lu ref_level_us=%lu new_level_us=%lu\n",
                rtl_band_name(g_dsp_ab.meta[0].band),
                static_cast<unsigned long>(g_dsp_ab.meta[0].sample_rate_sps),
                static_cast<unsigned>(n), static_cast<unsigned>(ref_pass.audio_n),
                static_cast<unsigned>(new_pass.audio_n), static_cast<unsigned>(ref_pass.mpx_n),
                static_cast<unsigned>(new_pass.mpx_n), audio_diff, max_audio_delta, mpx_diff,
                static_cast<double>(max_mpx_delta), static_cast<unsigned>(state_blocks_diff),
                static_cast<unsigned>(meter_diff), static_cast<unsigned>(level_diff),
                ref_pass.overflow ? 1 : 0, new_pass.overflow ? 1 : 0,
                static_cast<unsigned long>(ref_pass.elapsed_us),
                static_cast<unsigned long>(new_pass.elapsed_us),
                static_cast<unsigned long>(ref_pass.level_us),
                static_cast<unsigned long>(new_pass.level_us));
}
