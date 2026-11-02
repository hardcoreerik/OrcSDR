#pragma once

#include <algorithm>
#include <cstddef>
#include "dashboard_header_layout.hpp"

namespace orcsdr::settings {
using Rect = audio_header::HeaderRect;

struct SettingsRow {
  Rect bounds, label, value, action, minus, plus, angles[4];
  int divider_y = 0;
};

struct SectionValueRow {
  Rect bounds, label, value, action;
};

struct RadioDefaultsLayout {
  Rect title, note;
  SectionValueRow rows[6];
  bool two_columns = false;
};

struct CompanionLayout {
  Rect title, web_status, web_action, url, discovery, note, bluetooth;
  Rect orcdial_title, orcdial_status, orcdial_prompt, orcdial_error;
  Rect primary_action, secondary_action, boot_action;
  bool dial_page = false;
};

struct SettingsFormLayout {
  Rect content;
  Rect line(int y,int h=24) const {return {content.x+8,content.y+y,content.w-16,h};}
  Rect location_name() const {return line(28,48);}
  Rect coordinate(bool longitude) const {return line(longitude?132:80,44);}
  Rect location_map() const {return line(184,44);}
  Rect lookup_message() const {return line(80,content.w<800?96:48);}
  Rect lookup_result() const {const auto r=lookup_message();return line(r.y-content.y+r.h+8,44);}
  Rect lookup_credit() const {const auto r=lookup_result();return line(r.y-content.y+r.h+8,content.w<800?64:32);}
  Rect catalog_status() const {return line(28,content.w<800?144:48);}
  Rect catalog_check() const {const auto r=catalog_status();return line(r.y-content.y+r.h+8,44);}
  Rect catalog_local() const {const auto r=catalog_check();return line(r.y-content.y+52,44);}
  Rect catalog_notice() const {const auto r=catalog_local();return line(r.y-content.y+52,content.w<800?64:32);}
  Rect half(int y,bool right=false) const {
    const int w=(content.w-24)/2;
    return {content.x+8+(right?w+8:0),content.y+y,right?content.w-24-w:w,44};
  }
};
struct SavedWifiLayout { Rect connect, up, down, forget; };
struct ConnectivityLayout {
  Rect title, status, message, scan, hidden, power, boot, antenna, notice;
  SavedWifiLayout saved[4];
  Rect networks[6];
};
inline ConnectivityLayout make_connectivity_layout(Rect c) {
  ConnectivityLayout l{};
  const int x=c.x+8,w=c.w-16;
  l.title={x,c.y,w,24};l.status={x,c.y+28,w,48};
  l.message={x,c.y+76,w,48};l.notice={x,c.y+28,w,c.h-28};
  const int scan=(w-16)*28/100,hidden=(w-16)*37/100;
  l.scan={x,c.y+132,scan,44};l.hidden={x+scan+8,c.y+132,hidden,44};
  l.power={x+scan+hidden+16,c.y+132,w-scan-hidden-16,44};
  const int half=(w-8)/2;
  l.boot={x,c.y+184,half,44};l.antenna={x+half+8,c.y+184,w-half-8,44};
  const bool wide=c.w>=800;
  for(int i=0;i<4;++i) {
    const int cols=wide?2:1,card_w=(w-(cols-1)*8)/cols;
    const int cx=x+(i%cols)*(card_w+8),y=c.y+28+(i/cols)*108;
    auto& row=l.saved[i];row.connect={cx,y,card_w,52};
    row.up={cx,y+56,44,44};row.down={cx+52,y+56,44,44};
    row.forget={cx+104,y+56,card_w-104,44};
  }
  for(int i=0;i<6;++i) {
    const int cols=wide?2:1,col=i%cols,row=i/cols;
    const int cell=(w-(cols-1)*8)/cols;
    l.networks[i]={x+col*(cell+8),c.y+28+row*68,cell,64};
  }
  return l;
}

struct SystemLayout {
  Rect title, set_clock, run_setup, note;
  SectionValueRow power[4], status[7];
  Rect power_bounds;
  bool shallow = false;
  std::size_t page = 0;
};

struct ClockEditorLayout {
  Rect bounds, title, field_label[5], field_value[5], field_plus[5], field_minus[5];
  Rect offset_label, offset_value, offset_minus, offset_plus;
  Rect preview, note, ntp, cancel, save;
  bool legacy = true;
};

// Pure geometry, shared by drawing, touch and recording-display tests.
struct SettingsLayout {
  Rect viewport, header, navigation, content, footer, close;
  Rect previous_section, next_section, section_title, previous_page, next_page;
  bool legacy = true, valid = false;
  std::size_t rows_per_page = 5;

  SettingsRow row(std::size_t visible_row) const {
    SettingsRow r{};
    if (legacy) {
      const int y = 144 + static_cast<int>(visible_row) * 100;
      r.bounds = {330,y,888,68};
      r.label = {330,y+26,480,1};r.value = {880,y+26,1,1};
      r.action = {930,y,288,52};r.minus = {930,y,134,52};r.plus = {1084,y,134,52};
      for (int i=0;i<4;++i) r.angles[i]={710+i*128,y,124,52};
      r.divider_y = y+66;
      return r;
    }
    const int y = content.y + static_cast<int>(visible_row)*104;
    r.bounds={content.x,y,content.w,104};
    r.label={content.x+8,y+18,content.w-16,1};
    r.value={content.x+8,y+68,content.w-164,1};
    r.action={content.x+content.w-152,y+44,144,44};
    r.minus={content.x+content.w-104,y+44,44,44};
    r.plus={content.x+content.w-52,y+44,44,44};
    const int width=(content.w-28)/4;
    for (int i=0;i<4;++i) r.angles[i]={content.x+8+i*(width+4),y+44,width,44};
    r.divider_y=y+103;
    return r;
  }
  Rect firmware_action(bool split=false) const {
    return {content.x+8,content.y+content.h-52,split?(content.w-24)/2:content.w-16,44};
  }
  Rect firmware_cancel() const {
    const auto primary=firmware_action(true);
    return {primary.x+primary.w+8,primary.y,content.w-24-primary.w,44};
  }
  Rect firmware_status() const {return {content.x+8,content.y+88,content.w-16,content.h-96};}
  Rect firmware_help() const {return {content.x+8,content.y+8,content.w-16,content.h-68};}
};

inline SettingsLayout make_settings_layout(Rect v) {
  SettingsLayout l{};l.viewport=v;
  const auto h=audio_header::make_header_layout(v);
  if (!h.valid) return l;
  if (h.legacy) {
    l.header={0,0,1280,100};l.navigation={0,106,286,576};
    l.content={286,100,994,620};l.close={720,13,126,46};
    l.rows_per_page=6;l.valid=true;return l;
  }
  l.legacy=false;
  l.header={v.x,v.y,v.w,h.content_top-v.y};l.close=h.home;
  l.navigation={v.x,h.content_top,v.w,48};
  l.previous_section={v.x,l.navigation.y,44,44};
  l.next_section={v.x+v.w-44,l.navigation.y,44,44};
  l.section_title={v.x+52,l.navigation.y,v.w-104,44};
  l.footer={v.x,v.y+v.h-48,v.w,48};
  l.previous_page={v.x,l.footer.y,88,44};l.next_page={v.x+v.w-88,l.footer.y,88,44};
  l.content={v.x,l.navigation.y+68,v.w,l.footer.y-(l.navigation.y+68)-4};
  if (l.content.h<104) return l;
  l.rows_per_page=static_cast<std::size_t>(l.content.h/104);
  l.valid=audio_header::header_inside(v,l.content) &&
      audio_header::header_inside(v,l.close) && audio_header::header_inside(v,l.next_page);
  return l;
}

inline RadioDefaultsLayout make_radio_defaults_layout(Rect content, bool safe_mode) {
  RadioDefaultsLayout layout{};
  layout.title = {content.x + 8, content.y + 8, content.w - 16, 28};
  layout.two_columns = content.w >= 800 && content.h < 320;
  if (layout.two_columns) {
    const int gap = 24;
    const int column_width = (content.w - 16 - gap) / 2;
    const int left_x = content.x + 8;
    const int right_x = left_x + column_width + gap;
    const int top = content.y + 36;
    constexpr int row_height = 44, row_step = 44;
    for (int i = 0; i < 6; ++i) {
      const bool right = i >= 3;
      const int x = right ? right_x : left_x;
      const int y = top + (i % 3) * row_step;
      auto& row = layout.rows[i];
      row.bounds = {x, y, column_width, row_height};
      const bool action = (i == 1 || i == 4) && !safe_mode;
      const bool retry = i == 5 && safe_mode;
      if (action) {
        row.label = {x, y, column_width - 124, row_height};
        row.action = {x + column_width - 112, y, 112, row_height};
      } else if (retry) {
        row.label = {x, y, 144, row_height};
        row.value = {x + 152, y, column_width - 152 - 216, row_height};
        row.action = {x + column_width - 204, y, 204, row_height};
      } else {
        row.label = {x, y, column_width / 2, row_height};
        row.value = {x + column_width / 2 + 8, y,
                     column_width - column_width / 2 - 8, row_height};
      }
    }
    if (safe_mode)
      layout.note = {content.x + 8, content.y + 176, content.w - 16, 24};
  } else {
    const int x = content.x + 8;
    const int width = content.w - 16;
    const int top = content.y + 40;
    constexpr int row_height = 60, row_step = 64;
    for (int i = 0; i < 6; ++i) {
      const int y = top + i * row_step;
      auto& row = layout.rows[i];
      row.bounds = {x, y, width, row_height};
      const bool action = (i == 1 || i == 4) && !safe_mode;
      const bool retry = i == 5 && safe_mode;
      const int action_width = retry ? 204 : 108;
      const int text_width = (action || retry) ? width - action_width - 12 : width;
      row.label = {x, y, text_width, 24};
      row.value = {x, y + 24, text_width, 24};
      if (action || retry)
        row.action = {x + width - action_width, y + 8, action_width, 44};
    }
    if (safe_mode)
      layout.note = {x, top + 6 * row_step, width, 48};
  }
  return layout;
}

inline CompanionLayout make_companion_layout(Rect content, bool dial_page = false,
                                              bool include_dial = false) {
  CompanionLayout layout{};
  layout.dial_page = dial_page;
  const int x = content.x + 8;
  const int width = content.w - 16;
  layout.title = {x, content.y + 8, width, 28};
  if (dial_page) {
    layout.orcdial_title = layout.title;
    if (content.h < 320) {
      layout.orcdial_status = {x, content.y + 40, width, 32};
      layout.orcdial_prompt = {x, content.y + 76, width, 40};
      layout.primary_action = {x, content.y + 128, width / 3 - 8, 44};
      layout.secondary_action = {x + width / 3, content.y + 128,
                                 width / 3 - 8, 44};
      layout.boot_action = {x + 2 * width / 3, content.y + 128,
                            width / 3, 44};
      layout.orcdial_error = {x, content.y + 176, width, 24};
    } else {
      layout.orcdial_status = {x, content.y + 300, width, 48};
      layout.orcdial_prompt = {x, content.y + 352, width, 48};
      layout.primary_action = {x, content.y + 408, 216, 48};
      layout.secondary_action = {x, content.y + 460, 216, 48};
      layout.boot_action = {x, content.y + 512, 216, 48};
      layout.orcdial_error = {x, content.y + 568, width, 48};
    }
    return layout;
  }

  const bool shallow = content.h < 320;
  layout.web_status = {x, content.y + 40, width - 144, 28};
  layout.web_action = {content.x + content.w - 136, content.y + 36, 128, 44};
  if (shallow) {
    layout.url = {x, content.y + 88, width, 24};
    layout.discovery = {x, content.y + 120, width, 24};
    layout.note = {x, content.y + 152, width, 24};
    layout.bluetooth = {x, content.y + 176, width, 24};
  } else {
    layout.url = {x, content.y + 92, width, 48};
    layout.discovery = {x, content.y + 144, width, 28};
    layout.note = {x, content.y + 176, width, 48};
    layout.bluetooth = {x, content.y + 232, width, 28};
    if (include_dial) {
      layout.orcdial_title = {x, content.y + 268, width, 28};
      layout.orcdial_status = {x, content.y + 300, width, 48};
      layout.orcdial_prompt = {x, content.y + 352, width, 48};
      layout.primary_action = {x, content.y + 408, 216, 48};
      layout.secondary_action = {x, content.y + 460, 216, 48};
      layout.boot_action = {x, content.y + 512, 216, 48};
      layout.orcdial_error = {x, content.y + 568, width, 48};
    }
  }
  return layout;
}

inline SystemLayout make_system_layout(Rect content, std::size_t page = 0) {
  SystemLayout layout{};
  layout.shallow = content.h < 320;
  layout.page = page;
  const int x = content.x + 8;
  const int width = content.w - 16;
  layout.title = {x, content.y + 4, width, 28};
  if (!layout.shallow) {
    const int button_width = (width - 8) / 2;
    layout.set_clock = {x, content.y + 36, button_width, 44};
    layout.run_setup = {x + button_width + 8, content.y + 36,
                        width - button_width - 8, 44};
    for (int i = 0; i < 4; ++i) {
      const int y = content.y + 88 + i * 52;
      layout.power[i] = {{x, y, width, 48}, {x + 4, y, width - 8, 22},
                         {x + 4, y + 24, width - 8, 22}, {}};
    }
    for (int i = 0; i < 7; ++i) {
      const int y = content.y + 88 + (i + 4) * 52;
      layout.status[i] = {{x, y, width, 48}, {x + 4, y, width - 8, 22},
                          {x + 4, y + 24, width - 8, 22}, {}};
    }
    layout.power_bounds = {x, content.y + 88, width, 4 * 52 - 4};
    layout.note = {x, content.y + 88 + 11 * 52, width, 48};
    return layout;
  }

  const int button_width = (width - 8) / 2;
  layout.set_clock = {x, content.y + 32, button_width, 44};
  layout.run_setup = {x + button_width + 8, content.y + 32,
                      width - button_width - 8, 44};
  if (page == 0) {
    constexpr int row_height = 26, row_step = 30;
    const int gap = 12;
    const int column_width = (width - gap) / 2;
    for (int i = 0; i < 7; ++i) {
      const bool right = i >= 4;
      const int column = right ? 1 : 0;
      const int row = right ? i - 4 : i;
      const int row_x = x + column * (column_width + gap);
      const int y = content.y + 82 + row * row_step;
      layout.status[i] = {{row_x, y, column_width, row_height},
                          {row_x, y, column_width / 2, row_height},
                          {row_x + column_width / 2 + 4, y,
                           column_width - column_width / 2 - 4, row_height}, {}};
    }
  } else {
    constexpr int row_height = 28, row_step = 32;
    const int gap = 12;
    const int column_width = (width - gap) / 2;
    for (int i = 0; i < 4; ++i) {
      const int column = i % 2;
      const int row = i / 2;
      const int row_x = x + column * (column_width + gap);
      const int y = content.y + 82 + row * row_step;
      layout.power[i] = {{row_x, y, column_width, row_height},
                         {row_x, y, column_width / 2, row_height},
                         {row_x + column_width / 2 + 4, y,
                          column_width - column_width / 2 - 4, row_height}, {}};
    }
    layout.power_bounds = {x, content.y + 82, width, 60};
    layout.note = {x, content.y + 148, width, 48};
  }
  return layout;
}

inline ClockEditorLayout make_clock_editor_layout(Rect content, Rect footer,
                                                   bool legacy = false) {
  ClockEditorLayout layout{};
  layout.legacy = legacy;
  if (legacy) {
    constexpr int column_x = 360, column_pitch = 172, button_w = 150, button_h = 54;
    constexpr int plus_y = 178, minus_y = 290;
    for (int i = 0; i < 5; ++i) {
      const int x = column_x + i * column_pitch;
      layout.field_label[i] = {x, 160, button_w, 24};
      layout.field_plus[i] = {x, plus_y, button_w, button_h};
      layout.field_value[i] = {x, 238, button_w, 48};
      layout.field_minus[i] = {x, minus_y, button_w, button_h};
    }
    layout.bounds = {286, 100, 994, 620};
    layout.title = {330, 112, 500, 36};
    layout.offset_label = {360, 400, 520, 24};
    layout.offset_minus = {760, 372, 110, 56};
    layout.offset_value = {885, 388, 200, 24};
    layout.offset_plus = {1100, 372, 110, 56};
    layout.preview = {360, 456, 840, 28};
    layout.note = {360, 488, 840, 32};
    layout.ntp = {360, 530, 260, 58};
    layout.cancel = {640, 530, 260, 58};
    layout.save = {920, 530, 260, 58};
    return layout;
  }

  layout.bounds = content;
  const int x = content.x + 8;
  const int width = content.w - 16;
  layout.title = {x, content.y + 4, width, 24};
  if (content.h >= 320) {
    for (int i = 0; i < 5; ++i) {
      const int y = content.y + 32 + i * 96;
      layout.field_label[i] = {x, y, width, 22};
      layout.field_value[i] = {x, y + 22, width, 24};
      layout.field_minus[i] = {x + 68, y + 50, 48, 44};
      layout.field_plus[i] = {x + width - 116, y + 50, 48, 44};
    }
    const int y = content.y + 32 + 5 * 96;
    layout.offset_label = {x, y, 136, 48};
    layout.offset_minus = {x + 144, y + 2, 48, 44};
    layout.offset_value = {x + 200, y + 2, 132, 44};
    layout.offset_plus = {x + 340, y + 2, 48, 44};
    layout.preview = {x, y + 52, width, 28};
    layout.note = {x, y + 84, width, 72};
    const int action_y = y + 160;
    const int action_gap = 8;
    const int action_width = (width - 2 * action_gap) / 3;
    layout.ntp = {x, action_y, action_width, 48};
    layout.cancel = {x + action_width + action_gap, action_y, action_width, 48};
    layout.save = {x + 2 * (action_width + action_gap), action_y,
                   width - 2 * (action_width + action_gap), 48};
    return layout;
  }

  // The modal owns both content and the existing page footer, which yields 260 px
  // in the shallow 1088x424 Settings viewport.
  layout.bounds = {content.x, content.y, content.w,
                   content.h + footer.h + (footer.y - (content.y + content.h))};
  const int column_gap = 8;
  const int column_width = (width - 4 * column_gap) / 5;
  for (int i = 0; i < 5; ++i) {
    const int cx = x + i * (column_width + column_gap);
    layout.field_label[i] = {cx, content.y + 28, column_width, 22};
    layout.field_value[i] = {cx, content.y + 50, column_width, 24};
    const int key_x = cx + (column_width - 96) / 2;
    layout.field_minus[i] = {key_x, content.y + 76, 44, 44};
    layout.field_plus[i] = {key_x + 52, content.y + 76, 44, 44};
  }
  const int offset_y = content.y + 120;
  layout.offset_label = {x, offset_y, 292, 44};
  layout.offset_minus = {content.x + content.w / 2 + 10, offset_y, 44, 44};
  layout.offset_value = {content.x + content.w / 2 + 62, offset_y,
                         132, 44};
  layout.offset_plus = {content.x + content.w / 2 + 202, offset_y, 44, 44};
  layout.preview = {x, content.y + 166, width, 22};
  layout.note = {x, content.y + 188, width, 24};
  const int action_y = content.y + 212;
  const int action_gap = 8;
  const int action_width = (width - 2 * action_gap) / 3;
  layout.ntp = {x, action_y, action_width, 44};
  layout.cancel = {x + action_width + action_gap, action_y, action_width, 44};
  layout.save = {x + 2 * (action_width + action_gap), action_y,
                 width - 2 * (action_width + action_gap), 44};
  return layout;
}

inline std::size_t settings_page_count(std::size_t rows, const SettingsLayout& l) {
  return std::max<std::size_t>(1,(rows+l.rows_per_page-1)/l.rows_per_page);
}
} // namespace orcsdr::settings
