// The two display facts the P4 map service needs, kept apart from it because this file includes the scaled front end's M5Unified
// stand-in and the map service includes real M5GFX; the two define M5Canvas differently and cannot meet in one translation unit.
#include <M5Unified.h>

namespace orcsdr::p4_display {
int panel_width() { return M5.Display.target_width(); }
void mark_dirty(int x0, int y0, int x1, int y1) { M5.Display.mark_dirty_target(x0, y0, x1, y1); }
}  // namespace orcsdr::p4_display
