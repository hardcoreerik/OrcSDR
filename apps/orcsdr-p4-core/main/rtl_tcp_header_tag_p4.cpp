// P4 Core has no rtl_tcp remote receiver, so the header never carries an RTL-TCP tag. The shared header code
// (apps/orcsdr-tab5/ui/dashboard_audio_control.cpp) asks the tag to repaint after it redraws the header; with no tag there is nothing to
// repaint. This is the only symbol of rtl_tcp_header_tag.cpp that the shared code references outside the Tab5 build.
#include "rtl_tcp_header_tag.hpp"

namespace orcsdr::rtl_tcp_header_tag {
void invalidate() {}
}  // namespace orcsdr::rtl_tcp_header_tag
