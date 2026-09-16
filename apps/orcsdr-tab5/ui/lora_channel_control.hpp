#pragma once

#include <cstddef>
#include <cstdint>

namespace orcsdr {
class NvsStore;
}

namespace orcsdr::lora_channel {

struct Region {
  const char* code;
  uint32_t start_hz;
  uint32_t end_hz;
};

struct Selection {
  uint8_t region_index = 0;
  uint16_t slot = 20;
  uint32_t frequency_hz = 906875000;
  bool persisted = false;
};

struct SurveyStep {
  uint32_t frequency_hz = 0;
  uint32_t sampled_frequency_hz = 0;
  float sampled_level_dbfs = -120.0f;
  uint8_t span = 0;
  bool sampled = false;
  bool restore = false;
};

struct SurveyResult {
  uint32_t frequency_hz = 0;
  float level_dbfs = -120.0f;
};

constexpr uint32_t kLongFastBandwidthHz = 250000;

size_t region_count();
const Region& region(size_t index);
int find_region(const char* code);
uint16_t slot_count(size_t region_index);
uint16_t default_slot(size_t region_index);
uint32_t frequency_hz(size_t region_index, uint16_t slot);
uint16_t slot_for_frequency(size_t region_index, uint32_t frequency_hz);

const Selection& selection();
void load(NvsStore& store);
bool adopt(const char* region_code, uint32_t frequency_hz);
bool choose(size_t region_index, uint16_t slot, NvsStore& store);

void start_survey(uint32_t restore_frequency_hz, uint32_t now_ms);
uint32_t cancel_survey();
SurveyStep service_survey(uint32_t now_ms, bool receiver_is_lora, float level_dbfs);
bool survey_active();
uint8_t survey_progress();
uint8_t survey_span_count();
uint8_t survey_result_count();
SurveyResult survey_result(uint8_t index);

bool self_check();

}  // namespace orcsdr::lora_channel
