#include "data_screens.hpp"

namespace dscreens {

extern const Screen kPocsag;
extern const Screen kAdsb;
extern const Screen kAcars;
extern const Screen kP25;
extern const Screen kFt8;
extern const Screen kSstv;
extern const Screen kWifi;
extern const Screen kRfLab;
extern const Screen kViz;
extern const Screen kDeadAir;
extern const Screen kSettings;
extern const Screen kLora;

namespace {
const Screen* const kAll[] = {&kPocsag, &kAdsb, &kAcars, &kP25, &kFt8, &kSstv, &kWifi, &kRfLab, &kViz, &kDeadAir, &kSettings, &kLora};
}

const Screen* find(orcsdr::dashboards::Id id) {
  for (const Screen* s : kAll)
    if (s->id == id) return s;
  return nullptr;
}

const Screen* find_decoder(const char* decoder) {
  for (const Screen* s : kAll)
    if (!strcmp(s->decoder, decoder)) return s;
  return nullptr;
}

}  // namespace dscreens
