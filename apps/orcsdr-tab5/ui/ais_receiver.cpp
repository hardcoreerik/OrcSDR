#include "ais_receiver.hpp"
#include <new>
#include <cmath>
#include <cstring>

namespace orcsdr::ais_rx {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr uint16_t kFcsResidue = 0xf0b8;
constexpr uint8_t kFlag = 0x7e;
constexpr uint32_t kDuplicateWindow = 3840; // 80 ms at 48 kHz; one packet from adjacent timing hypotheses.
uint16_t fcs_bit(uint16_t crc, bool bit) {
  const bool mix = ((crc & 1u) != 0) != bit;
  crc = static_cast<uint16_t>(crc >> 1);
  if (mix) crc ^= 0x8408u;
  return crc;
}
uint32_t hash_frame(const Packet& p) {
  uint32_t h = 2166136261u;
  h = (h ^ p.bit_count) * 16777619u;
  for (size_t i = 0; i < p.bit_count / 8; ++i) h = (h ^ p.bytes[i]) * 16777619u;
  return h;
}
}

void Hdlc::reset() {
  count_ = 0; window_ = window_count_ = 0;
  in_frame_ = previous_level_ = level_primed_ = false;
  stats_ = {};
}
void Hdlc::finish(uint8_t channel, PacketCallback callback, void* context) {
  // Between flags: bit-stuffed HDLC payload + reflected CRC-16/X25 FCS.
  // Every wire byte is LSB-first, reconstructed into MSB-first field bytes.
  if (count_ <= 8) return;
  ++stats_.candidate_frames;
  const size_t raw_bits = count_ - 8;
  if (raw_bits > kMaxRawBits) { ++stats_.overflow; return; }
  Packet packet{};
  packet.channel = channel;
  uint16_t crc = 0xffff;
  uint16_t bits = 0;
  unsigned ones = 0;
  bool malformed = false;
  for (size_t i = 0; i < raw_bits; ++i) {
    const bool value = raw_[i] != 0;
    if (ones == 5) {
      if (value) { malformed = true; break; }
      ones = 0;
      continue;
    }
    if (value) ++ones; else ones = 0;
    if (bits >= kMaxPayloadBits + 16) { ++stats_.overflow; return; }
    crc = fcs_bit(crc, value);
    // FCS is not delivered. Pack temporary bytes in the same LSB-first order
    // as the physical link (after destuffing), and trim the final 2 FCS bytes.
    if (bits < kMaxPayloadBits) {
      const size_t byte = bits / 8;
      packet.bytes[byte] |= static_cast<uint8_t>(uint8_t(value) << (bits % 8));
    }
    ++bits;
  }
  if (malformed) { ++stats_.bad_stuffing; return; }
  if (bits < 16 + 38 || (bits & 7u) != 0) { ++stats_.invalid_length; return; }
  if (crc != kFcsResidue) { ++stats_.crc_failed; return; }
  const size_t payload_bits = bits - 16;
  if (payload_bits > kMaxPayloadBits) { ++stats_.overflow; return; }
  packet.bit_count = static_cast<uint16_t>(payload_bits);
  ++stats_.crc_ok;
  if (callback) callback(packet, context);
}
void Hdlc::process_level(bool level, uint8_t channel, PacketCallback callback, void* context) {
  if (!level_primed_) { previous_level_ = level; level_primed_ = true; return; }
  const bool bit = (level == previous_level_); // NRZI: hold=1, transition=0.
  previous_level_ = level;
  window_ = static_cast<uint8_t>((window_ << 1) | (bit ? 1u : 0u));
  if (window_count_ < 8) ++window_count_;
  if (in_frame_) {
    if (count_ == kMaxRawBits) {
      ++stats_.overflow; in_frame_ = false; count_ = 0;
    } else {
      raw_[count_++] = static_cast<uint8_t>(bit);
    }
  }
  if (window_count_ == 8 && window_ == kFlag) {
    if (in_frame_) finish(channel, callback, context);
    in_frame_ = true;
    count_ = 0;
  }
}

Receiver::Receiver() { reset(); }
void Receiver::reset() {
  bad_blocks_ = sample_index_ = 0;
  osc_phase_ = decim_phase_ = 0;
  const float cutoff = 10500.f / static_cast<float>(kInputRateHz); // 10.5kHz baseband.
  float gain = 0;
  for (size_t i = 0; i < kTaps; ++i) {
    const int index = static_cast<int>(i) - static_cast<int>((kTaps-1)/2);
    const float sinc = index ? std::sin(2.f*kPi*cutoff*index)/(kPi*index) : 2.f*cutoff;
    const float window = 0.54f - 0.46f*std::cos(2.f*kPi*i/(kTaps-1));
    taps_[i] = sinc * window;
    gain += taps_[i];
  }
  for (auto& tap : taps_) tap /= gain;
  for (unsigned channel = 0; channel < 2; ++channel) {
    new (&channels_[channel]) Channel();   // in place: a Channel temporary is ~8 KB of stack
    call_context_[channel] = {this, channel};
    const float channel_hz = channel ? 25000.f : -25000.f;
    for (unsigned index = 0; index < 48; ++index) {
      const float phase = -2.f*kPi*channel_hz*index/kInputRateHz;
      osc_[channel][index] = {std::cos(phase),std::sin(phase)};
    }
    for (auto& h : channels_[channel].hypotheses) h.reset();
  }
  callback_ = nullptr; context_ = nullptr;
}
void Receiver::discontinuity() {
  // Does not erase cumulative counters or the user's configured callback.
  for (auto& ch : channels_) {
    for (auto& v : ch.fir_i) v = 0;
    for (auto& v : ch.fir_q) v = 0;
    ch.fir_pos = 0; ch.hypothesis = 0; ch.prev = {}; ch.have_prev = false;
    ch.dc = 0; ch.smooth[0]=ch.smooth[1]=ch.smooth[2]=0; ch.smooth_pos=0;
    ch.output_index = 0; ch.have_hash = false;
    for (auto& h : ch.hypotheses) {
      const auto& observed = h.stats();
      ch.aggregate.candidate_frames += observed.candidate_frames;
      ch.aggregate.crc_ok += observed.crc_ok;
      ch.aggregate.crc_failed += observed.crc_failed;
      ch.aggregate.bad_stuffing += observed.bad_stuffing;
      ch.aggregate.invalid_length += observed.invalid_length;
      ch.aggregate.overflow += observed.overflow;
      h.reset();
    }
  }
  osc_phase_ = decim_phase_ = 0;
}
// FIR, decimation and demodulation: once per 5 source samples (48 kS/s per channel).
void Receiver::demodulate(unsigned c) {
  Channel& ch = channels_[c];
  // fir_pos is the next write slot, so the newest sample is at fir_pos - 1 (+ kTaps in the mirrored half).
  const float* newest_i = &ch.fir_i[ch.fir_pos + kTaps - 1];
  const float* newest_q = &ch.fir_q[ch.fir_pos + kTaps - 1];
  Complex out{};
  for (size_t tap = 0; tap < kTaps; ++tap) {
    out.i += taps_[tap]*newest_i[-static_cast<ptrdiff_t>(tap)];
    out.q += taps_[tap]*newest_q[-static_cast<ptrdiff_t>(tap)];
  }
  if (!ch.have_prev) { ch.prev=out; ch.have_prev=true; return; }
  const float cross = ch.prev.i*out.q - ch.prev.q*out.i;
  const float dot = ch.prev.i*out.i + ch.prev.q*out.q;
  ch.prev = out;
  // Exact quadrature discriminator avoids small-angle approximations at low SNR.
  const float angle = std::atan2(cross,dot);
  // Slow DC tracking rejects remaining CFO without removing symbol-level shifts.
  ch.dc += 0.00012f * (angle - ch.dc);
  ch.smooth[ch.smooth_pos] = angle - ch.dc;
  if (++ch.smooth_pos == 3) ch.smooth_pos = 0;
  const float soft = (ch.smooth[0]+ch.smooth[1]+ch.smooth[2]) * (1.f/3.f);
  const unsigned phase = ch.hypothesis;
  if (++ch.hypothesis == kPhases) ch.hypothesis = 0;
  // Parallel fixed-phase clock hypotheses: a 9600 Bd burst spans only ~270 ms,
  // so oscillator error <50ppm causes much less than one bit of drift.
  ch.hypotheses[phase].process_level(soft >= 0,static_cast<uint8_t>(c+1),on_packet,&call_context_[c]);
  ++ch.output_index;
}
void Receiver::on_packet(const Packet& packet, void* context) {
  auto* c = static_cast<CallContext*>(context);
  c->owner->accepted(c->channel,packet);
}
void Receiver::accepted(unsigned c, const Packet& packet) {
  Channel& ch = channels_[c];
  const uint32_t hash = hash_frame(packet);
  if (ch.have_hash && hash == ch.last_hash &&
      ch.output_index >= ch.last_packet_index &&
      ch.output_index-ch.last_packet_index <= kDuplicateWindow) {
    ++ch.aggregate.duplicates;
    return;
  }
  ch.have_hash = true;ch.last_hash=hash;ch.last_packet_index=ch.output_index;
  if (callback_) callback_(packet,context_);
}
bool Receiver::process_cu8(const uint8_t* bytes, size_t length,uint32_t sample_rate,
                           PacketCallback callback,void* context) {
  if ((length && !bytes) || (length & 1u) || sample_rate != kInputRateHz) {
    ++bad_blocks_;
    return false;
  }
  callback_ = callback;context_ = context;
  for (size_t i=0;i<length;i+=2) {
    const float in_i = (static_cast<float>(bytes[i])-127.5f)*(1.f/128.f);
    const float in_q = (static_cast<float>(bytes[i+1])-127.5f)*(1.f/128.f);
    for (unsigned c=0;c<2;++c) {
      const Complex rot=osc_[c][osc_phase_];
      Channel& ch=channels_[c];
      const float mixed_i=in_i*rot.i-in_q*rot.q, mixed_q=in_i*rot.q+in_q*rot.i;
      ch.fir_i[ch.fir_pos]=ch.fir_i[ch.fir_pos+kTaps]=mixed_i;
      ch.fir_q[ch.fir_pos]=ch.fir_q[ch.fir_pos+kTaps]=mixed_q;
      if (++ch.fir_pos == kTaps) ch.fir_pos = 0;
    }
    if (++osc_phase_ == 48) osc_phase_ = 0;
    if (++decim_phase_ == 5) {
      decim_phase_ = 0;
      demodulate(0);
      demodulate(1);
    }
  }
  sample_index_ += length/2;
  return true;
}
Stats Receiver::stats() const {
  Stats result{};
  result.iq_samples=sample_index_;
  result.bad_blocks=bad_blocks_;
  for(unsigned c=0;c<2;++c) {
    ChannelStats s=channels_[c].aggregate;
    for(const auto& h:channels_[c].hypotheses) {
      const ChannelStats& hs=h.stats();
      s.candidate_frames+=hs.candidate_frames;
      s.crc_ok+=hs.crc_ok;
      s.crc_failed+=hs.crc_failed;
      s.bad_stuffing+=hs.bad_stuffing;
      s.invalid_length+=hs.invalid_length;
      s.overflow+=hs.overflow;
    }
    // crc_ok above counts timing hypotheses. Remove duplicate successfully decoded frames.
    if(s.crc_ok>=s.duplicates)s.crc_ok-=s.duplicates;
    s.discriminator_samples=channels_[c].output_index;
    result.channels[c]=s;
  }
  return result;
}
} // namespace orcsdr::ais_rx
