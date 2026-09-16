#pragma once

#include <cstddef>
#include <climits>
#include <cstdint>

namespace orcsdr::lora_native {

constexpr size_t kMaxPacketsPerCapture = 8;
constexpr size_t kPacketTextBytes = 112;

struct Config {
  const uint8_t* authorized_psk = nullptr;
  size_t authorized_psk_bytes = 0;
  size_t candidate_samples = 0;
  bool trace = false;
};

struct Packet {
  uint32_t sender = 0;
  uint32_t destination = 0;
  uint32_t packet_id = 0;
  int32_t latitude_e7 = INT32_MAX;
  int32_t longitude_e7 = INT32_MAX;
  int16_t cfo_tenths_hz = 0;
  uint16_t port = 0;
  bool encrypted = false;
  char short_name[8]{};
  char long_name[32]{};
  char text[kPacketTextBytes]{};
};

struct PreprocessTracePoint {
  uint32_t output_index = 0;
  uint32_t source_index = 0;
  uint32_t source_remainder = 0;
  uint8_t raw[4]{};
  uint8_t filtered[4]{};
  uint8_t resampled[2]{};
  bool filtered_valid = false;
};

struct FftTracePoint {
  uint16_t symbol_index = 0;
  uint16_t best_bin = 0;
  uint16_t second_bin = 0;
  float best_magnitude = 0;
  float second_magnitude = 0;
  float neighbors[5]{};
};

struct SymbolAlternateTracePoint {
  uint16_t symbol_index = 0;
  uint16_t alternate_symbol = 0;
  uint16_t ratio_milli = 0;
};

struct SymbolAlternateMetric {
  uint16_t symbol_index = 0;
  uint16_t alternate_symbol = 0;
  uint16_t ratio_milli = 0;
  float primary_magnitude = 0;
  float alternate_magnitude = 0;
};

struct Stats {
  uint32_t captures = 0;
  uint32_t preambles = 0;
  uint32_t header_failures = 0;
  uint32_t crc_ok = 0;
  uint32_t crc_failures = 0;
  uint32_t encrypted = 0;
  uint32_t candidate_millis = 0;
  uint32_t decode_millis = 0;
  uint32_t preparation_millis = 0;
  uint32_t filter_millis = 0;
  uint32_t resample_millis = 0;
  uint32_t preamble_search_millis = 0;
  uint32_t sync_cfo_millis = 0;
  uint32_t timing_header_millis = 0;
  uint32_t payload_symbols_millis = 0;
  uint32_t payload_decode_millis = 0;
  uint32_t payload_fec_millis = 0;
  uint32_t crc_millis = 0;
  uint32_t mesh_millis = 0;
  uint32_t fft_calls = 0;
  uint32_t preamble_windows = 0;
  uint32_t timing_offsets = 0;
  uint32_t cfo_hypotheses = 0;
  uint32_t clock_hypotheses = 0;
  uint32_t header_candidates = 0;
  uint32_t payload_candidates = 0;
  uint32_t symbols_processed = 0;
  uint32_t candidate_passes = 0;
  uint32_t full_capture_passes = 0;
  uint32_t full_fallbacks = 0;
  uint32_t cfo_retry_passes = 0;
  uint32_t recovery_attempted = 0;
  uint32_t recovery_symbols_considered = 0;
  uint32_t recovery_candidates_tested = 0;
  uint32_t recovery_success = 0;
  bool recovery_exhausted = false;
  uint16_t trace_symbols[128]{};
  uint16_t trace_symbol_count = 0;
  uint32_t trace_data_start = 0;
  int8_t trace_timing_adjustment = 0;
  uint16_t trace_preamble_peak = 0;
  uint32_t trace_preprocess_fnv1a = 0;
  PreprocessTracePoint trace_preprocess[8]{};
  uint8_t trace_preprocess_count = 0;
  FftTracePoint trace_fft[5]{};
  uint8_t trace_fft_count = 0;
  SymbolAlternateTracePoint trace_alternates[128]{};
  uint8_t trace_alternate_count = 0;
  const SymbolAlternateMetric* trace_alternate_metrics = nullptr;
  bool candidate_accepted = false;
  bool candidate_rejected = false;
  bool candidate_truncated = false;
  int16_t raw_cfo_tenths_hz = 0;
  int16_t cfo_tenths_hz = 0;
  int16_t clock_skew_ppm = 0;
  bool truncated = false;
  bool ready = false;
};

// Allocates fixed decoder scratch space once. Call before starting RTL streaming.
bool initialize();
size_t psram_bytes();
size_t fft_table_bytes();
size_t recovery_workspace_bytes();
size_t task_workspace_bytes();
bool fft_table_in_psram();
bool recovery_workspace_in_psram();
bool task_workspace_in_psram();
Packet* task_packets();
Stats* task_stats();

// Decodes an immutable CU8 capture. This function is intentionally task-only:
// it may take milliseconds and must never run from the RTL IQ callback.
size_t decode_capture(const uint8_t* cu8, size_t bytes, uint32_t sample_rate_sps,
                      uint8_t spreading_factor, uint32_t bandwidth_hz, uint32_t frequency_hz,
                      const Config& config, Packet* packets, size_t packet_capacity,
                      Stats* stats);

bool self_check();

}  // namespace orcsdr::lora_native
