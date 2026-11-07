#include "iq_queue.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
using orc::linux_sdr::BoundedIqQueue;
using orc::linux_sdr::IqBlock;
using orc::linux_sdr::IqMetadata;
int main() {
  std::array<std::uint8_t,8> data{{1,2,3,4,5,6,7,8}};
  BoundedIqQueue q(2,8);
  IqMetadata m{};m.frequency_hz=131550000;m.sample_rate_sps=240000;m.receiver_generation=7;
  if (!q.try_push(data.data(),data.size(),m) ||
      !q.try_push(data.data(),data.size(),m) ||
      q.try_push(data.data(),data.size(),m) ||
      q.dropped_samples()!=4 || q.queued_blocks()!=2) return 1;
  IqBlock out;
  if (!q.try_pop(out) || out.meta.discontinuity ||
      out.meta.dropped_samples_before || out.cu8.size()!=8 ||
      out.meta.receiver_generation!=7) return 2;
  if (!q.try_push(data.data(),data.size(),m)) return 3;
  if (!q.try_pop(out) || out.meta.discontinuity) return 4;
  if (!q.try_pop(out) || !out.meta.discontinuity ||
      out.meta.dropped_samples_before!=4 ||
      out.meta.frequency_hz!=131550000) return 5;
  if (q.try_pop(out)) return 6;
  if (!q.try_push(data.data(),data.size(),m)) return 7;
  q.flush_discontinuity();
  if (q.dropped_samples()!=8 || q.queued_blocks()) return 8;
  m.receiver_generation=8;
  if (!q.try_push(data.data(),data.size(),m) ||
      !q.try_pop(out) || !out.meta.discontinuity ||
      out.meta.dropped_samples_before!=4 || out.meta.receiver_generation!=8)
    return 9;
  try { (void)q.try_push(data.data(),7,m); return 10; }
  catch (const std::invalid_argument&) {}
  std::cout<<"bounded IQ slots, known loss, flush, generation and gap markers: PASS\n";
  return 0;
}
