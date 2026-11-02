#include "hfdl_decoder.hpp"

#include <cstring>

namespace orcsdr::hfdl {

Decoder::Decoder() { reset(); }

void Decoder::reset() {
  frames_seen_ = 0;
  spdus_seen_ = 0;
  messages_posted_ = 0;
  sample_count_ = 0;
}

void Decoder::set_config(const Config& cfg) { cfg_ = cfg; }

void Decoder::set_model(hfdl_ui::Model* model) { model_ = model; }

bool Decoder::process(const float* /*i*/, const float* /*q*/, size_t count) {
  sample_count_ += static_cast<uint32_t>(count);
  // Placeholder: every ~32 s of samples, invent a squitter for UI bring-up.
  const uint32_t samples_per_frame = cfg_.sample_rate * kFrameSeconds;
  if (samples_per_frame > 0 && (sample_count_ % samples_per_frame) < count) {
    ++frames_seen_;
    ++spdus_seen_;
    post_placeholder("SPDU squitter (placeholder)");
    return true;
  }
  return false;
}

bool Decoder::process_audio(const float* audio, size_t count) {
  // Treat mono audio as I for now; Q = 0. Real demod will do frequency translation to 1440 Hz.
  return process(audio, nullptr, count);
}

void Decoder::post_placeholder(const char* preview) {
  if (!model_) return;
  hfdl_ui::MessageRecord msg{};
  msg.frequency_hz = cfg_.center_hz;
  msg.uptime_ms = sample_count_ * 1000u / (cfg_.sample_rate ? cfg_.sample_rate : 1);
  msg.text_length = static_cast<uint16_t>(std::strlen(preview));
  std::strncpy(msg.preview, preview, hfdl_ui::kPreviewChars - 1);
  std::strncpy(msg.label, "SQ", 2);
  std::strncpy(msg.tail, "GS", 2);
  if (model_->post_message(msg)) ++messages_posted_;
}

}  // namespace orcsdr::hfdl
