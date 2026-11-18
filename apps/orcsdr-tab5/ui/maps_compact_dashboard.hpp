#pragma once
// Compact MAPS screen (maps_compact_dashboard.cpp) for views smaller than 1280 x 720: the map packs found on the card, paged, with a
// rescan, and a LOCATION page (tap the map to move the crosshair, zoom, USE). The Tab5's map_screen.cpp is separate. Not on this view yet: the
// download page and typed coordinates.
#include "maps_compact_layout.hpp"

namespace orcsdr::maps_ui {
enum class Action : uint8_t { none, rescan, use_location };
void set_header_controls(void (*draw)());    // the shared home/mute/settings controls (the application's header hook)
bool set_viewport(compact::Rect viewport);   // validates the view, moves the shared header, allocates the (PSRAM) state
void set_start_location(int32_t latitude_e7, int32_t longitude_e7);   // where the LOCATION page's map starts
bool selection(int32_t* latitude_e7, int32_t* longitude_e7);          // the point under the crosshair (after Action::use_location)
bool enter();
bool open_picker();
// The five-step setup wizard (welcome, Wi-Fi, location, maps, ready) of the Tab5's map_screen, with the same persisted record
// (setup_flow.hpp). The application supplies the facts it shows and the storage.
struct WizardHooks {
  bool (*wifi_line)(char* out, size_t size) = nullptr;       // "CONNECTED name  ip" or why not; true = connected
  bool (*location_line)(char* out, size_t size) = nullptr;   // "44.0720, -122.9600 Springfield" or "not set"; true = set
  void (*open_wifi)() = nullptr;                             // leave for the Wi-Fi settings
  bool (*save_record)(uint32_t record) = nullptr;            // persists setup::Flow::record()
  void (*finished)() = nullptr;                              // FINISH or CANCEL: leave the wizard
};
void set_wizard_hooks(const WizardHooks& hooks);
bool open_wizard();                          // after enter(): start the wizard on its first step
bool finish_picker();                        // after USE in the picker: back to the wizard when it opened the picker (true)                          // after enter(): go straight to the LOCATION page                                // draws the first frame
void update();                               // redraws when the pack list changed
void leave();
Action touch(int x, int y);
void draw();                                 // full redraw (geometry change)
}  // namespace orcsdr::maps_ui
