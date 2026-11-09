// Web build: the Tab5's DSI panel, reduced to what the RF visualizer asks of it (its size, two frame buffers, a handle for the draw call).
#pragma once
#include <stdint.h>
namespace lgfx {
inline namespace v1 {
struct Panel_DSI {
  struct Config { uint16_t panel_width = 1280; uint16_t panel_height = 720; };
  const Config& config() const { return config_; }
  void* getPanelHandle() { return this; }
  void* getFrameBuffer(int index) {
    static uint16_t buffers[2][1280 * 720];
    return index == 0 || index == 1 ? buffers[index] : nullptr;
  }
 private:
  Config config_;
};
}  // namespace v1
}  // namespace lgfx
