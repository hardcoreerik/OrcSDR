#include "screen_capture.hpp"
#include "bsp/esp-bsp.h"
#include "driver/uart.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "mbedtls/base64.h"
#include "png.h"
#include "zlib.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace orcsdr::waveshare4b::screen_capture {
namespace {
Prepare prepare_view=nullptr;
constexpr auto port=static_cast<uart_port_t>(CONFIG_ESP_CONSOLE_UART_NUM);
void error(const char* token,const char* message){
 printf("ORCUI_PNG_ERROR %s %s\n",token,message);
}
void capture(const char* token,const char* view){
 if(!bsp_display_lock(1000)){error(token,"display_busy");return;}
 if(std::strcmp(view,"current")!=0&&(!prepare_view||!prepare_view(view))){
  bsp_display_unlock();error(token,"unsupported_view");return;
 }
 auto* screen=lv_screen_active();
 lv_obj_update_layout(screen);
 const int w=lv_obj_get_width(screen),h=lv_obj_get_height(screen);
 const size_t stride=lv_draw_buf_width_to_stride(w,LV_COLOR_FORMAT_RGB888);
 const size_t bytes=stride*size_t(h);
 if(w<=0||h<=0||bytes>8*1024*1024){
  bsp_display_unlock();error(token,"screen_too_large");return;
 }
 auto* rgb=static_cast<uint8_t*>(heap_caps_aligned_alloc(64,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 lv_draw_buf_t snapshot{};
 const bool ok=rgb&&lv_draw_buf_init(&snapshot,w,h,LV_COLOR_FORMAT_RGB888,stride,rgb,bytes)==LV_RESULT_OK
   &&lv_snapshot_take_to_draw_buf(screen,LV_COLOR_FORMAT_RGB888,&snapshot)==LV_RESULT_OK;
 bsp_display_unlock();
 if(!ok){heap_caps_free(rgb);error(token,"snapshot_failed");return;}
 // Snapshot owns an independent copy. No LVGL lock during PNG encoding/UART I/O.
 png_image png{};
 png.version=PNG_IMAGE_VERSION;png.width=w;png.height=h;
 png.format=PNG_FORMAT_BGR; // LVGL RGB888 is B,G,R in memory.
 png.flags=PNG_IMAGE_FLAG_FAST;
 png_alloc_size_t size=0;
 uint8_t* encoded=nullptr;
 if(png_image_write_to_memory(&png,nullptr,&size,0,rgb,stride,nullptr)&&size<=4*1024*1024)
  encoded=static_cast<uint8_t*>(heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 const bool written=encoded&&png_image_write_to_memory(&png,encoded,&size,0,rgb,stride,nullptr);
 heap_caps_free(rgb);png_image_free(&png);
 if(!written){heap_caps_free(encoded);error(token,"png_failed");return;}
 const auto* app=esp_app_get_description();
 char hash[65]{};
 for(unsigned i=0;i<32;++i)std::snprintf(hash+i*2,3,"%02x",app->app_elf_sha256[i]);
 printf("ORCUI_PNG_BEGIN %s %d %d %u %08lx %s\n",token,w,h,unsigned(size),
        static_cast<unsigned long>(crc32(0,encoded,size)),hash);
 unsigned seq=0;
 for(size_t offset=0;offset<size;offset+=384){
  unsigned char text[513]{};size_t count=0;
  const size_t n=std::min<size_t>(384,size-offset);
  if(mbedtls_base64_encode(text,sizeof(text),&count,encoded+offset,n)!=0)break;
  flockfile(stdout);
  std::fprintf(stdout,"ORCUI_PNG_DATA %s %u ",token,seq++);
  std::fwrite(text,1,count,stdout);std::fputc('\n',stdout);
  funlockfile(stdout);
  vTaskDelay(1);
 }
 printf("ORCUI_PNG_END %s\n",token);
 heap_caps_free(encoded);
}
void task(void*){
 char line[100]{};size_t length=0;
 for(;;){
  uint8_t c=0;
  if(uart_read_bytes(port,&c,1,pdMS_TO_TICKS(100))!=1)continue;
  if(c=='\n'||c=='\r'){
   if(!length)continue;
   line[length]=0;length=0;
   char token[17]{},view[16]{},extra[2]{};
   if(std::sscanf(line,"ORCUI_SCREENSHOT %16[0-9a-f] %15s %1s",token,view,extra)==2&&std::strlen(token)==16)
    capture(token,view);
  }else if(length<sizeof(line)-1)line[length++]=char(c);
  else length=0;
 }
}
}
bool start(Prepare prepare){
 prepare_view=prepare;
 if(!uart_is_driver_installed(port)&&uart_driver_install(port,2048,0,0,nullptr,0)!=ESP_OK)return false;
 return xTaskCreate(task,"screen_capture",8192,nullptr,2,nullptr)==pdPASS;
}
}
