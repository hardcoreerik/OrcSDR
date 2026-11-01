// Intermediate board/geometry acceptance diagnostic. This is not the OrcSDR UI.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "M5Unified.h"
#include "freq_keypad.hpp"
#include "dashboard_audio_control.hpp"
#include "focus_nav.hpp"
#include "hosyond_board.h"
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace {
hosyond_display_t panel{};
orcsdr_display_geometry_t geometry{};
auto& display=M5.Display;
bool keypad_mode=false,controls_mode=false,preview_sound=true;
uint8_t preview_volume=128;
orcsdr::audio_header::Control volume_control{};
char keypad_entry[24]="96.1";
constexpr int show_keypad=360,show_grid=361,show_controls=362,submit_keypad=1200,cancel_keypad=1201;
bool swapped=false;
QueueHandle_t commands;
constexpr int status_command=-1;

void status() {
    printf("DISPLAY_STATUS rotation=%ld native=%dx%d view=%ldx%ld safe=%ld,%ld,%ld,%ld psram_free=%u\n",
        (long)geometry.rotation,panel.width,panel.height,(long)geometry.width,(long)geometry.height,
        (long)geometry.safe.x,(long)geometry.safe.y,(long)geometry.safe.width,(long)geometry.safe.height,
        unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}
void present(orcsdr_rect_t rect) {
    orcsdr_rect_t native;
    if (!orcsdr_view_rect_to_native(&geometry,rect,&native)) return;
    const int x0=std::max(0,int(rect.x)),y0=std::max(0,int(rect.y));
    const int x1=std::min(int(geometry.width),int(rect.x+rect.width));
    const int y1=std::min(int(geometry.height),int(rect.y+rect.height));
    const uint16_t *src=display.buffer();
    for (int y=y0;y<y1;++y) for (int x=x0;x<x1;++x) {
        orcsdr_point_t n;
        if (!orcsdr_view_to_native(&geometry,{x,y},&n)) continue;
        uint16_t v=src[size_t(y)*geometry.width+x];
        if (swapped) v=uint16_t(v<<8|v>>8);
        uint8_t *dst=panel.fb[0]+size_t(n.y)*panel.stride_bytes+size_t(n.x)*3;
        unsigned blue=v&31,green=(v>>5)&63,red=v>>11;
        dst[0]=uint8_t(blue<<3|blue>>2);dst[1]=uint8_t(green<<2|green>>4);dst[2]=uint8_t(red<<3|red>>2);
    }
    // Native columns after a quarter turn: write back only the touched row spans.
    for (int y=native.y;y<native.y+native.height;++y)
        esp_cache_msync(panel.fb[0]+size_t(y)*panel.stride_bytes+size_t(native.x)*3,size_t(native.width)*3,
            ESP_CACHE_MSYNC_FLAG_DIR_C2M|ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}
orcsdr_rect_t button(int index) {
    const auto &s=geometry.safe;int width=s.width/4;
    return {s.x+index*width+2,s.y+56,width-4,48};
}
bool rotate_to(int rotation) {
    orcsdr_display_geometry_t next;
    // Conservative inset for the AMOLED's rounded corners; final shared layouts
    // can use more of the centre while respecting the physical visible shape.
    int inset=std::min(72,std::min(panel.width,panel.height)/4);
    if (!orcsdr_geometry_init(&next,panel.width,panel.height,rotation,inset,inset,inset,inset)) return false;
    if (!display.begin(next.width,next.height,next.width,next.height)) return false;
    geometry=next;
    auto &c=display.sprite();
    c.drawPixel(0,0,uint16_t(0xf800));swapped=display.buffer()[0]==0x00f8;
    c.fillScreen(uint16_t(0));c.setFont(&lgfx::fonts::Font0);c.setTextSize(2);c.setTextColor(uint16_t(0xffff));
    const auto &s=geometry.safe;
    if (keypad_mode) {
        orcsdr::focus_nav::clear();
        int top=int(s.y);
        if (controls_mode) {
            namespace header=orcsdr::audio_header;
            volume_control={};
            if (!header::set_viewport({int(s.x),int(s.y),int(s.width),int(s.height)})) return false;
            header::draw_brand("HOME");header::draw_battery(75);header::draw_home_button();
            header::draw_mute_button(preview_sound);header::draw_visualizer_button(false);header::draw_settings_button();
            top=header::layout().content_top;
            printf("DISPLAY_HEADER_CHECK result=%s diagnostic_only=1\n",header::self_check()?"PASS":"FAIL");
        }
        const orcsdr::freq_keypad::Rect bounds{int(s.x),top,int(s.width),int(s.y+s.height)-top};
        orcsdr::freq_keypad::draw(top,0,"ENTER FREQUENCY","Frequency","MHz",keypad_entry,&bounds);
        present({0,0,geometry.width,geometry.height});
        printf("DISPLAY_KEYPAD_READY view=%ldx%ld entry=%s shared_source=1\n",(long)geometry.width,(long)geometry.height,keypad_entry);
        status();return orcsdr::freq_keypad::self_check();
    }
    c.setCursor(s.x,s.y);c.printf("OrcSDR display / touch probe\n");
    c.setCursor(s.x,s.y+24);c.printf("%ld x %ld   %ld degrees",(long)geometry.width,(long)geometry.height,(long)rotation);
    for (int i=0;i<4;++i) {
        auto b=button(i);c.drawRoundRect(b.x,b.y,b.width,b.height,6,uint16_t(0x07ff));
        c.setCursor(b.x+10,b.y+16);c.printf("%d",i*90);
    }
    const int cx=s.x+s.width/2,cy=s.y+(s.height+104)/2;
    const int radius=int(std::max<int32_t>(12,std::min(s.width,s.height-164)/3));
    c.drawCircle(cx,cy,radius,uint16_t(0xffff));
    c.drawFastHLine(s.x,cy,s.width,uint16_t(0x4208));
    c.drawFastVLine(cx,s.y+112,s.height-164,uint16_t(0x4208));
    c.fillRect(s.x,s.y+112,16,16,uint16_t(0xf800));
    c.fillRect(s.x+s.width-16,s.y+112,16,16,uint16_t(0x07e0));
    c.fillRect(s.x,s.y+s.height-68,16,16,uint16_t(0x001f));
    c.fillRect(s.x+s.width-16,s.y+s.height-68,16,16,uint16_t(0xffe0));
    present({0,0,geometry.width,geometry.height});
    status();return true;
}
void keypad_action(int x,int y) {
    using orcsdr::freq_keypad::Result;
    const auto result=orcsdr::freq_keypad::handle_touch(x,y,keypad_entry,sizeof(keypad_entry));
    if (result==Result::changed) {
        const auto& f=orcsdr::freq_keypad::layout().field;
        present({f.x,f.y,f.w,f.h});
        printf("DISPLAY_KEYPAD_ENTRY value=%s\n",keypad_entry);
    } else if (result==Result::submitted || result==Result::cancelled) {
        printf("DISPLAY_KEYPAD_RESULT action=%s entry=%s diagnostic_only=1\n",result==Result::submitted?"submit":"cancel",keypad_entry);
        keypad_mode=false;rotate_to(geometry.rotation);
    }
}
bool header_touch(int x,int y,bool new_press) {
    if (!controls_mode) return false;
    namespace header=orcsdr::audio_header;
    const bool was_open=volume_control.expanded;
    const auto& slider=header::layout();
    const bool on_slider=x>=slider.slider_x-12 && x<slider.slider_x+slider.slider_w+12 &&
        y>=slider.slider_y-22 && y<slider.slider_y+22;
    if (!new_press && !(was_open && on_slider)) return was_open;
    const auto action=header::handle_touch(volume_control,x,y,uint32_t(esp_timer_get_time()/1000),preview_volume);
    if (action==header::Action::opened) header::capture_volume_background();
    if (action==header::Action::closed) header::restore_volume_background();
    if (action==header::Action::volume_set) preview_volume=volume_control.volume;
    if (action==header::Action::mute_toggle) preview_sound=!preview_sound;
    if (action!=header::Action::none) {
        header::draw(volume_control,preview_volume,preview_sound,75);header::draw_mute_button(preview_sound);
        const auto& l=header::layout();
        present({l.battery.x,l.battery.y,l.battery.w,l.battery.h});
        present({l.mute.x,l.mute.y,l.mute.w,l.mute.h});present({l.tray.x,l.tray.y,l.tray.w,l.tray.h});
        printf("DISPLAY_HEADER_ACTION action=%u volume=%u sound=%u diagnostic_only=1\n",unsigned(action),preview_volume,preview_sound);
        return true;
    }
    if (header::home_hit(x,y) || header::settings_hit(x,y) || header::visualizer_hit(x,y)) {
        printf("DISPLAY_HEADER_NAV target=%s diagnostic_only=1\n",header::home_hit(x,y)?"home":header::settings_hit(x,y)?"settings":"visualizer");
        return true;
    }
    return was_open;
}
void console(void *) {
    char line[96];
    while (fgets(line,sizeof(line),stdin)) {
        line[strcspn(line,"\r\n")]=0;
        int command=-2;
        if (!strcmp(line,"DISPLAY KEYPAD")) command=show_keypad;
        if (!strcmp(line,"DISPLAY GRID")) command=show_grid;
        if (!strcmp(line,"DISPLAY CONTROLS")) command=show_controls;
        if (!strcmp(line,"DISPLAY SUBMIT")) command=submit_keypad;
        if (!strcmp(line,"DISPLAY CANCEL")) command=cancel_keypad;
        if (!strcmp(line,"DISPLAY KEY BACKSPACE")) command=1000+'\b';
        if (!strncmp(line,"DISPLAY KEY ",12) && strlen(line)==13 &&
            ((line[12]>='0' && line[12]<='9') || line[12]=='.')) command=1000+line[12];
        if (!strcmp(line,"DISPLAY STATUS")) command=status_command;
        for (int i=0;i<4;++i) {
            char expected[40];snprintf(expected,sizeof(expected),"DISPLAY ROTATION %d",i*90);
            if (!strcmp(line,expected)) command=i*90;
        }
        if (command>=-1 && xQueueSend(commands,&command,0)==pdTRUE) printf("DISPLAY_QUEUED\n");
        else if (*line) printf("DISPLAY_REJECT use STATUS, ROTATION 0|90|180|270, KEYPAD, GRID, KEY digit|.|BACKSPACE, SUBMIT, CANCEL\n");
    }
    vTaskDelete(nullptr);
}
} // namespace

extern "C" void app_main() {
    setvbuf(stdout,nullptr,_IONBF,0);setvbuf(stdin,nullptr,_IONBF,0);
    esp_err_t e=hosyond_board_start(&panel);
    if (e!=ESP_OK) {printf("DISPLAY_FAILED stage=panel error=%s\n",esp_err_to_name(e));return;}
    if (panel.bytes_per_pixel!=3 || panel.fb_count<1) {printf("DISPLAY_FAILED format\n");return;}
    if (!rotate_to(0)) {printf("DISPLAY_FAILED stage=canvas\n");return;}
    e=hosyond_board_touch_start();
    const bool touch_ready=e==ESP_OK;
    printf("DISPLAY_TOUCH status=%s\n",esp_err_to_name(e));
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0,1024,0,0,nullptr,0));uart_vfs_dev_use_driver(UART_NUM_0);
    commands=xQueueCreate(8,sizeof(int));
    if (!commands || xTaskCreate(console,"display_console",4096,nullptr,3,nullptr)!=pdPASS) {
        printf("DISPLAY_FAILED stage=console\n");return;
    }
    printf("DISPLAY_READY diagnostic=1 shared_geometry=1 uart=921600\n");
    bool was_down=false;
    for (;;) {
        int command;
        if (xQueueReceive(commands,&command,0)==pdTRUE) {
            if (command==status_command) status();
            else if (command==show_keypad || command==show_grid || command==show_controls) {
                controls_mode=command==show_controls;keypad_mode=command!=show_grid;rotate_to(geometry.rotation);was_down=true;
            } else if (command>=1000 && keypad_mode) {
                int x=-1,y=-1;
                if (command==submit_keypad) orcsdr::freq_keypad::tune_point(&x,&y);
                else if (command==cancel_keypad) orcsdr::freq_keypad::cancel_point(&x,&y);
                else orcsdr::freq_keypad::key_point(char(command-1000),&x,&y);
                keypad_action(x,y);
            } else if (command>=0 && command<=270) {printf("DISPLAY_ROTATION_BEGIN degrees=%d\n",command);
                bool ok=rotate_to(command);was_down=true;
                printf("DISPLAY_ROTATION_RESULT result=%s\n",ok?"PASS":"FAIL");}
        }
        orcsdr_touch_sample_t sample{};
        e=touch_ready?hosyond_board_touch_poll(&sample):ESP_ERR_NOT_FOUND;
        if (e==ESP_OK) {
            orcsdr_point_t view;
            bool mapped=orcsdr_native_to_view(&geometry,sample.position,&view);
            if (sample.pressed && mapped && keypad_mode) {
                if (!header_touch(int(view.x),int(view.y),!was_down) && !was_down) keypad_action(int(view.x),int(view.y));
            } else if (sample.pressed && mapped) {
                auto &c=display.sprite();const auto &s=geometry.safe;
                c.fillRect(s.x,s.y+s.height-40,s.width,40,uint16_t(0));
                c.setCursor(s.x,s.y+s.height-36);
                c.printf("Touch %ld,%ld",(long)view.x,(long)view.y);
                present({s.x,s.y+s.height-40,s.width,40});
                if (!was_down) {
                    printf("DISPLAY_TOUCH_POINT native=%ld,%ld view=%ld,%ld rotation=%ld\n",
                        (long)sample.position.x,(long)sample.position.y,(long)view.x,(long)view.y,(long)geometry.rotation);
                    for (int i=0;i<4;++i) {
                        auto b=button(i);
                        if (view.x>=b.x && view.x<b.x+b.width && view.y>=b.y && view.y<b.y+b.height) {
                            command=i*90;xQueueSend(commands,&command,0);break;
                        }
                    }
                }
            }
            was_down=sample.pressed;
        } else if (e!=ESP_ERR_NOT_FOUND) was_down=true; // require a release before another action after an error
        if (controls_mode && orcsdr::audio_header::service_timeout(volume_control,uint32_t(esp_timer_get_time()/1000))) {
            orcsdr::audio_header::restore_volume_background();
            const auto& t=orcsdr::audio_header::layout().tray;present({t.x,t.y,t.w,t.h});
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
