#pragma once
namespace orcsdr::waveshare4b::screen_capture {
// Runs under the BSP display lock. Return false for an unsupported view.
using Prepare=bool (*)(const char* view);
bool start(Prepare prepare=nullptr);
}
