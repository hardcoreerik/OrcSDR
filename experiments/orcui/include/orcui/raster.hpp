#pragma once
#include <cstddef>
#include <cstdint>
namespace orcsdr::orcui {
struct Bitmap565 {
  uint16_t* data=nullptr;
  int width=0,height=0;
  size_t stride_bytes=0;
};
void paint_spectrum(Bitmap565 out,const float* levels,size_t bins,float min_db,float max_db);
void push_waterfall(Bitmap565 out,const float* levels,size_t bins,float min_db,float max_db);
}