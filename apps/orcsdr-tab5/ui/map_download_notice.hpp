#pragma once
#include "map_downloads.hpp"
#include "screen_controller.hpp"

namespace orcsdr::map_download_notice {
// Shared-header gap: after the brand, before the existing device status bar.
constexpr int x=318,y=12,width=94,height=54;
void invalidate();
struct TuneWarning {
  uint32_t frequency_hz = 0;
  bool nooelec_xtr = false;
};
void update(const maps::downloads::Snapshot&,screens::Id,bool blocked,TuneWarning warning = {});
bool hit(int32_t,int32_t,screens::Id,bool blocked);
}
