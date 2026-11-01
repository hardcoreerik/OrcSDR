// Compile the actual shared implementation with the recording display. The
// ELF-only asset symbol spelling is irrelevant to this MSVC host fixture.
#include "../apps/orcsdr-tab5/ui/dashboard_audio_control.cpp"
namespace orcsdr::badge {
const uint8_t raw_start[2]={0,0};
const uint8_t raw_end[2]={0,0};
}
namespace orcsdr::map_download_notice {
void invalidate(){} // no download worker in this focused UI renderer fixture
}
