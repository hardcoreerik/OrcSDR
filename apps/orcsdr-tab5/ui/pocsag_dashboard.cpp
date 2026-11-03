#include "pocsag_dashboard.hpp"

#include "dashboard_audio_control.hpp"
#include "orc_badge.hpp"
#include "focus_nav.hpp"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

namespace orcsdr::pocsag {
namespace {

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kPanel = 0x0861;
constexpr uint16_t kBorder = 0x2350;
constexpr uint16_t kCyan = 0x04ff;
constexpr uint16_t kGreen = 0x6fe8;
constexpr uint16_t kYellow = 0xffe0;
constexpr uint16_t kRed = 0xf800;
constexpr uint16_t kMuted = 0x9cf3;
constexpr int kHeaderH = 100;
constexpr int kTabsY = 646;
constexpr int kTabW = 256;
// Content area between the header and tab bar, with a 12px top margin and a
// 12px bottom margin so cards never overlap the tab strip.
constexpr int kContentH = kTabsY - (kHeaderH + 12) - 12;

Settings g_settings;
View g_view = View::live;
bool g_active = false;
bool g_live = false;
uint32_t g_drawn_revision = 0;
size_t g_selected_id = 0;
size_t g_selected_message = 0;
Snapshot* g_snapshot = nullptr;
layout::Layout g_layout=layout::make_layout({0,0,1280,720});
size_t g_list_page[2]{},g_detail_page[2]{};
size_t g_activity_page=0;
size_t g_detail_pages[2]{1,1};
void refresh_focus() {
  if(g_layout.legacy) return;
  focus_nav::Rect prior{};const bool had_focus=focus_nav::focused(&prior);
  focus_nav::clear();
  const auto add=[](layout::Rect r){focus_nav::note(r.x,r.y,r.w,r.h);};
  const auto h=audio_header::make_header_layout(g_layout.viewport);
  add(h.home);add(h.mute);add(h.settings);
  for(const auto r:g_layout.tab) add(r);
  const auto& s=*g_snapshot;
  if(g_view==View::live && (s.scanning || s.candidate_count)) add(g_layout.scan);
  if(g_view==View::signal) {add(g_layout.baud);add(g_layout.polarity);}
  if(g_view==View::activity) {
    const size_t pages=1+std::max<size_t>(1,(std::min<size_t>(s.identity_count,8)+g_layout.activity_row_count-1)/g_layout.activity_row_count);
    if(g_activity_page) add(g_layout.activity_previous);
    if(g_activity_page+1<pages) add(g_layout.activity_next);
  }
  if(g_view==View::ids || g_view==View::archive) {
    const unsigned slot=g_view==View::ids?0:1;
    const size_t count=slot?s.message_count:s.identity_count,rows=g_layout.list_row_count;
    if(g_list_page[slot]) add(g_layout.list_previous);
    if((g_list_page[slot]+1)*rows<count) add(g_layout.list_next);
    if(g_detail_page[slot]) add(g_layout.detail_previous);
    if(g_detail_page[slot]+1<g_detail_pages[slot]) add(g_layout.detail_next);
    for(size_t i=0;i<rows && g_list_page[slot]*rows+i<count;++i) add(g_layout.list_rows[i]);
  }
  if(had_focus) for(size_t i=0;i<focus_nav::count();++i) {
    focus_nav::Rect r{};focus_nav::get(i,&r);
    if(r.x==prior.x && r.y==prior.y && r.w==prior.w && r.h==prior.h) {focus_nav::focus_at(r.x+r.w/2,r.y+r.h/2);break;}
  }
}
uint64_t g_widget_signature[20]{};
bool g_widget_valid[20]{};
void reset_widgets() {std::fill_n(g_widget_valid,20,false);}
uint64_t signature(const void* data,size_t size) {
  auto p=static_cast<const uint8_t*>(data);uint64_t h=1469598103934665603ULL;
  while(size--) {h^=*p++;h*=1099511628211ULL;}
  return h;
}
bool widget(unsigned index,layout::Rect r,uint64_t value) {
  if(g_widget_valid[index] && g_widget_signature[index]==value) return false;
  g_widget_valid[index]=true;g_widget_signature[index]=value;
  M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,8,kPanel);
  M5.Display.drawRoundRect(r.x,r.y,r.w,r.h,8,kBorder);
  return true;
}
bool hit(int32_t x,int32_t y,layout::Rect r) {return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h;}

// Lazily PSRAM-allocated rather than a plain internal-DRAM global: this
// Snapshot (message/identity arrays plus the decoder Stats) is a few KB,
// and a prior revision's plain global -- combined with similar globals in
// main.cpp -- pushed static BSS just far enough to starve ESP-IDF's own
// early internal/DMA heap-pool reservation, producing a boot-time abort
// before setup() even runs (confirmed by flashing an unmodified baseline,
// which boots cleanly on the same hardware).
//
// A prior revision of this function "fixed" that by falling back to a
// `static Snapshot fallback{};` local on allocation failure -- but a
// function-local static of non-trivial type still reserves its full
// sizeof(Snapshot) in internal-DRAM BSS at link time, regardless of
// whether the fallback path is ever taken. That reintroduced the exact
// problem this function exists to avoid. Falling back to an internal-heap
// allocation (still dynamic, so it costs nothing unless actually used) is
// the only fallback that doesn't have this defect; PSRAM is abundant
// enough (32 MB) that neither allocation should realistically fail.
bool ensure_live_snapshot() {
  if (!g_snapshot) {
    void* memory = heap_caps_malloc(sizeof(Snapshot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) memory = heap_caps_malloc(sizeof(Snapshot), MALLOC_CAP_8BIT);
    if (memory) g_snapshot = new (memory) Snapshot();
  }
  return g_snapshot != nullptr;
}

Snapshot& live_snapshot() {
  return *g_snapshot;
}

void text(const char* value, int x, int y, uint16_t color = TFT_WHITE, int size = 2,
          textdatum_t datum = middle_center) {
  switch (size) {
    case 1: M5.Display.setFont(&fonts::DejaVu18); break;
    case 2: M5.Display.setFont(&fonts::DejaVu24); break;
    default: M5.Display.setFont(&fonts::DejaVu40); break;
  }
  M5.Display.setTextDatum(datum);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(color);
  M5.Display.drawString(value, x, y);
}

void card(int x, int y, int w, int h) {
  M5.Display.fillRoundRect(x, y, w, h, 12, kPanel);
  M5.Display.drawRoundRect(x, y, w, h, 12, kBorder);
}

void preview(const char* value, int x, int y, int width, uint16_t color = TFT_WHITE,
             int size = 1) {
  char line[kMaxMessageChars];
  snprintf(line, sizeof(line), "%s", value);
  for (char* p = line; *p; ++p) if (*p < ' ') *p = ' ';
  M5.Display.setFont(size == 1 ? &fonts::DejaVu18 : &fonts::DejaVu24);
  size_t length = std::strlen(line);
  if (M5.Display.textWidth(line) > width) {
    while (length && M5.Display.textWidth(line) + M5.Display.textWidth("...") > width)
      line[--length] = '\0';
    if (length + 3 < sizeof(line)) std::strcat(line, "...");
  }
  text(line, x, y, color, size, middle_left);
}

bool hit(int32_t x, int32_t y, int bx, int by, int bw, int bh) {
  return x >= bx && x < bx + bw && y >= by && y < by + bh;
}

const char* lock_state_label(LockState state) {
  switch (state) {
    case LockState::locked: return "LOCKED";
    case LockState::searching: return "SEARCHING";
    case LockState::lost: return "LOST SYNC";
    default: return "NO SIGNAL";
  }
}

uint16_t lock_state_color(LockState state) {
  switch (state) {
    case LockState::locked: return kGreen;
    case LockState::lost: return kYellow;
    default: return kMuted;
  }
}

const char* message_type_label(MessageType type) {
  switch (type) {
    case MessageType::alpha: return "ALPHA";
    case MessageType::numeric: return "NUMERIC";
    case MessageType::tone_only: return "TONE";
    default: return "UNKNOWN";
  }
}

void tab_icon(int index, int x, int y, uint16_t color) {
  switch (index) {
    case 0:  // LIVE
      M5.Display.drawCircle(x, y, 13, color);
      M5.Display.drawCircle(x, y, 4, color);
      break;
    case 1:  // IDS
      M5.Display.fillCircle(x - 6, y - 4, 6, color);
      M5.Display.fillCircle(x + 6, y - 4, 6, color);
      M5.Display.drawFastHLine(x - 14, y + 10, 28, color);
      break;
    case 2:  // SIGNAL
      for (int i = 0; i < 4; ++i)
        M5.Display.fillRect(x - 16 + i * 9, y + 12 - (i + 1) * 6, 6, (i + 1) * 6, color);
      break;
    case 3:  // ACTIVITY
      M5.Display.drawLine(x - 16, y + 6, x - 6, y - 8, color);
      M5.Display.drawLine(x - 6, y - 8, x + 4, y + 2, color);
      M5.Display.drawLine(x + 4, y + 2, x + 16, y - 12, color);
      break;
    default:  // ARCHIVE
      M5.Display.drawRoundRect(x - 16, y - 10, 32, 22, 3, color);
      M5.Display.drawFastHLine(x - 16, y - 2, 32, color);
      break;
  }
}

void native_button(layout::Rect r,const char* label,bool enabled=true) {
  M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,6,enabled?TFT_DARKCYAN:TFT_DARKGREY);
  preview(label,r.x+8,r.y+r.h/2,r.w-16,enabled?TFT_WHITE:kMuted);
}
void native_pager(unsigned slot,layout::Rect previous,layout::Rect next,
                  layout::Rect page_rect,size_t page,size_t count,const char* title="") {
  if(!widget(slot,{previous.x,previous.y,next.x+next.w-previous.x,previous.h},page*257+count)) return;
  native_button(previous,"<",page>0);native_button(next,">",page+1<count);
  char value[40];snprintf(value,sizeof(value),"%s%s%u / %u",title,title[0]?"  ":"",unsigned(page+1),unsigned(count));
  text(value,page_rect.x+page_rect.w/2,page_rect.y+page_rect.h/2,kMuted,1);
}
size_t wrapped_detail(layout::Rect r,const char* source,size_t page,bool paint) {
  const size_t rows=std::max(1,r.h/24);size_t row=0;
  M5.Display.setFont(&fonts::DejaVu18);M5.Display.setTextSize(1);
  while(*source) {
    char line[96]{};size_t n=0,last_space=0;
    while(source[n] && source[n]!='\n' && n+1<sizeof(line)) {
      line[n]=source[n]<' '?' ':source[n];line[n+1]='\0';
      if(M5.Display.textWidth(line)>r.w-16) {line[n]='\0';break;}
      if(source[n]==' ') last_space=n;
      ++n;
    }
    if(source[n] && source[n]!='\n' && last_space) {n=last_space;line[n]='\0';}
    if(!n && *source!='\n') {n=1;line[0]=*source;line[1]='\0';}
    if(paint && row/rows==page) text(line,r.x+8,r.y+12+int(row%rows)*24,TFT_WHITE,1,middle_left);
    source+=n;if(*source=='\n') ++source;else while(*source==' ') ++source;
    ++row;
  }
  return std::max<size_t>(1,(row+rows-1)/rows);
}
void native_soft_plot(layout::Rect r,const Stats& stats,bool fec) {
  const int top=r.y+28,bottom=r.y+r.h-8;
  if(bottom<=top) return;
  preview(fec?"FEC: CLEAN / CORRECTED / BAD":"2-FSK SOFT SYMBOLS",r.x+8,r.y+14,r.w-16,kCyan);
  const int w=r.w-16,h=bottom-top;
  if(fec) {
    const size_t count=std::min(stats.fec_history_count,Stats::kFecHistoryCapacity);
    for(size_t i=0;i<count;++i) {
      const size_t index=(stats.fec_history_write+Stats::kFecHistoryCapacity-count+i)%Stats::kFecHistoryCapacity;
      const auto outcome=stats.fec_history[index];
      const int x=r.x+8+int(i*w/Stats::kFecHistoryCapacity);
      M5.Display.fillRect(x,top,std::max(1,w/int(Stats::kFecHistoryCapacity)-1),h,outcome==0?kGreen:outcome==1?kYellow:kRed);
    }
    if(!count) text("NO CODEWORDS YET",r.x+r.w/2,top+h/2,kMuted,1);
  } else {
    const size_t count=std::min(stats.soft_symbol_count,Stats::kSoftSymbolCapacity);
    if(count) M5.Display.drawFastHLine(r.x+8,top+h/2,w,kBorder);
    for(size_t i=0;i<count;++i) {
      const size_t index=(stats.soft_symbol_write+Stats::kSoftSymbolCapacity-count+i)%Stats::kSoftSymbolCapacity;
      const float sample=std::clamp(stats.soft_symbols[index],-1.0f,1.0f);
      M5.Display.fillCircle(r.x+8+int(i*(w-4)/Stats::kSoftSymbolCapacity),top+h/2-int(sample*(h/2-3)),1,sample>=0?kGreen:kCyan);
    }
    if(!count) text("NO SAMPLES YET",r.x+r.w/2,top+h/2,kMuted,1);
  }
}
void native_live() {
  const auto& s=live_snapshot();const auto& stats=s.decoder_stats;
  auto r=g_layout.live_hero;char value[160];
  const uint64_t hero=signature(&s.messages[0],sizeof(s.messages[0]))^uint64_t(s.scanning)^s.scan_index^
      stats.batches_synced^(uint64_t(stats.codewords_total)<<32)^uint64_t(stats.lock)^s.scan_frequency_hz^
      (uint64_t(s.scan_count)<<16)^s.message_count^(s.message_count?millis()/1000:0)^(uint64_t(s.receiving)<<63);
  if(widget(0,r,hero)) {
    preview(s.scanning?"SCANNING POCSAG":s.message_count?"NEWEST MESSAGE":"MONITORING",r.x+8,r.y+18,r.w-16,kCyan);
    if(s.scanning) {
      snprintf(value,sizeof(value),"%.4f MHz / %u OF %u",s.scan_frequency_hz/1000000.0,unsigned(s.scan_index+1),unsigned(s.scan_count));
      preview(value,r.x+8,r.y+48,r.w-16,TFT_WHITE);
      const int width=s.scan_count?int(uint64_t(s.scan_index+1)*(r.w-20)/s.scan_count):0;
      M5.Display.fillRect(r.x+10,r.y+r.h-24,std::min(width,r.w-20),12,kYellow);
    } else if(s.message_count) {
      const auto& m=s.messages[0];
      snprintf(value,sizeof(value),"%lu / %s / F%u",static_cast<unsigned long>(m.capcode),message_type_label(m.type),m.function);
      preview(value,r.x+8,r.y+48,r.w-16,kCyan);
      preview(m.text_length?m.text:"(no text)",r.x+8,r.y+78,r.w-16,TFT_WHITE);
      snprintf(value,sizeof(value),"%u bps / %lu s / %s",m.baud,static_cast<unsigned long>((millis()-uint32_t(m.timestamp_ms))/1000),m.uncorrectable_words?"BAD":m.corrected_bits?"CORRECTED":"CLEAN");
      preview(value,r.x+8,r.y+r.h-18,r.w-16,kMuted);
    } else {
      preview(lock_state_label(stats.lock),r.x+8,r.y+48,r.w-16,lock_state_color(stats.lock),2);
      snprintf(value,sizeof(value),"%lu batches / %lu words",static_cast<unsigned long>(stats.batches_synced),static_cast<unsigned long>(stats.codewords_total));
      preview(value,r.x+8,r.y+78,r.w-16,kMuted);
      if(r.h>=130) preview(s.receiving?"RECEIVER ACTIVE / WAITING":"NOT RECEIVING",r.x+8,r.y+r.h-18,r.w-16,kMuted);
    }
  }
  r=g_layout.live_status;
  if(widget(1,r,signature(&stats,sizeof(stats)))) {
    snprintf(value,sizeof(value),"DECODE / %u bps",stats.detected_baud);preview(value,r.x+8,r.y+18,r.w-16,kCyan);
    const char* labels[]={"VALID","CORRECTED","BAD"};
    const uint32_t counts[]={stats.codewords_valid,stats.codewords_corrected,stats.codewords_uncorrectable};
    for(int i=0;i<3;++i) {
      text(labels[i],r.x+8,r.y+44+i*22,kMuted,1,middle_left);
      snprintf(value,sizeof(value),"%lu",static_cast<unsigned long>(counts[i]));
      text(value,r.x+r.w-8,r.y+44+i*22,TFT_WHITE,1,middle_right);
    }
    native_soft_plot({r.x,r.y+100,r.w,r.h-100},stats,false);
  }
  r=g_layout.live_inbox;
  const size_t visible=std::min<size_t>(s.message_count,std::max(0,(r.h-56)/48));
  const uint64_t inbox=signature(s.messages,visible*sizeof(DisplayMessage))^s.message_count^(uint64_t(s.candidate_count)<<32)^uint64_t(s.scanning);
  if(widget(2,r,inbox)) {
    preview("MESSAGE INBOX",r.x+8,r.y+22,std::max(1,g_layout.scan.x-r.x-16),kCyan);
    snprintf(value,sizeof(value),s.scanning?"STOP SCAN":s.candidate_count?"SCAN %u FREQS":"SCAN N/A",unsigned(s.candidate_count));
    native_button(g_layout.scan,value,s.scanning || s.candidate_count>0);
    if(!s.message_count) preview("No messages decoded yet",r.x+8,r.y+82,r.w-16,kMuted);
    for(size_t i=0;i<visible;++i) {
      const auto& m=s.messages[i];const int y=r.y+68+int(i)*48;
      snprintf(value,sizeof(value),"%lu / %s",static_cast<unsigned long>(m.capcode),message_type_label(m.type));
      preview(value,r.x+8,y,r.w-16,kCyan);
      preview(m.text_length?m.text:"(no text)",r.x+8,y+22,r.w-16,TFT_WHITE);
    }
  }
  refresh_focus();
}
void native_signal() {
  const auto& s=live_snapshot().decoder_stats;const auto r=g_layout.signal_stats;
  if(widget(0,r,signature(&s,sizeof(s)))) {
    preview("DECODE STATUS",r.x+8,r.y+16,r.w-16,kCyan);
    const char* labels[]={"SYNC","BAUD","POLARITY","FSK Hz","BATCHES","SYNC LOST","WORDS","VALID","FEC W/B","BAD WORDS","PARITY FAIL"};
    const int row_h=std::min(24,(r.h-34)/11);
    for(int i=0;i<11;++i) {
      char value[48];
      switch(i) {
        case 0:snprintf(value,sizeof(value),"%s",lock_state_label(s.lock));break;
        case 1:snprintf(value,sizeof(value),"%u",s.detected_baud);break;
        case 2:snprintf(value,sizeof(value),"%s",s.inverted?"INVERTED":"NORMAL");break;
        case 3:snprintf(value,sizeof(value),"%+.1f",static_cast<double>(s.fsk_deviation_hz));break;
        case 8:snprintf(value,sizeof(value),"%lu/%lu",static_cast<unsigned long>(s.codewords_corrected),static_cast<unsigned long>(s.corrected_bit_count));break;
        default:{const uint32_t values[]={s.batches_synced,s.sync_losses,s.codewords_total,s.codewords_valid,s.codewords_corrected,s.codewords_uncorrectable,s.parity_failures};snprintf(value,sizeof(value),"%lu",static_cast<unsigned long>(values[i-4]));break;}
      }
      const int y=r.y+34+i*row_h;
      M5.Display.setFont(&fonts::DejaVu18);M5.Display.setTextSize(1);
      preview(labels[i],r.x+8,y,std::max(1,r.w-24-int(M5.Display.textWidth(value))),kMuted);
      text(value,r.x+r.w-8,y,TFT_WHITE,1,middle_right);
    }
  }
  const auto p=g_layout.signal_plot;
  if(widget(1,p,signature(&s,sizeof(s)))) {
    const int half=p.h*2/3;
    native_soft_plot({p.x,p.y,p.w,half},s,false);
    native_soft_plot({p.x,p.y+half,p.w,p.h-half},s,true);
  }
  if(widget(2,{g_layout.baud.x,g_layout.baud.y,g_layout.polarity.x+g_layout.polarity.w-g_layout.baud.x,44},uint64_t(g_settings.baud_bps)*4+g_settings.polarity_mode)) {
    char value[40];snprintf(value,sizeof(value),"BAUD: %s",g_settings.baud_bps==0?"AUTO":g_settings.baud_bps==512?"512":g_settings.baud_bps==1200?"1200":"2400");
    native_button(g_layout.baud,value);
    native_button(g_layout.polarity,g_settings.polarity_mode==0?"POLARITY: AUTO":g_settings.polarity_mode==1?"POLARITY: NORMAL":"POLARITY: INVERTED");
  }
  refresh_focus();
}

void native_activity() {
  const auto& snapshot=live_snapshot();const auto& s=snapshot.decoder_stats;
  size_t order[kIdentityCapacity];const size_t count=std::min(snapshot.identity_count,kIdentityCapacity);
  for(size_t i=0;i<count;++i) order[i]=i;
  std::sort(order,order+count,[&](size_t a,size_t b){return snapshot.identities[a].hit_count>snapshot.identities[b].hit_count;});
  const size_t top=std::min<size_t>(count,8),rows=std::max(1,g_layout.activity_row_count);
  const size_t pages=1+std::max<size_t>(1,(top+rows-1)/rows);
  g_activity_page=std::min(g_activity_page,pages-1);
  if(!g_activity_page) {
    const char* labels[]={"TOTAL MESSAGES","ACTIVE IDS","VALID WORDS","BAD WORDS","ALPHA","NUMERIC","TONE ONLY","TRUNCATED"};
    const uint32_t values[]={s.messages_decoded,uint32_t(count),0,s.codewords_uncorrectable,s.messages_alpha,s.messages_numeric,s.messages_tone_only,s.messages_truncated};
    for(int i=0;i<8;++i) {
      char value[32];
      if(i==2) snprintf(value,sizeof(value),"%.1f%%",s.codewords_total?100.0*double(s.codewords_valid)/s.codewords_total:0.0);
      else snprintf(value,sizeof(value),"%lu",static_cast<unsigned long>(values[i]));
      const auto r=g_layout.activity[i];
      const uint64_t hash=signature(value,strlen(value))^(i>=4 && i<=6?uint64_t(s.messages_decoded)<<32:0);
      if(!widget(i,r,hash)) continue;
      preview(labels[i],r.x+8,r.y+16,r.w-16,kMuted);
      M5.Display.setFont(&fonts::DejaVu24);
      const int size=M5.Display.textWidth(value)<=r.w-16?2:1;
      text(value,r.x+r.w/2,r.y+44,i==3?kRed:i==2?kGreen:TFT_WHITE,size);
      if(i>=4 && i<=6 && s.messages_decoded) {
        const int width=int(uint64_t(values[i])*(r.w-16)/std::max<uint32_t>(1,s.messages_decoded));
        M5.Display.fillRect(r.x+8,r.y+r.h-8,std::min(width,r.w-16),3,i==4?kGreen:i==5?kCyan:kMuted);
      }
    }
  } else {
    const size_t first=(g_activity_page-1)*rows;
    for(size_t row=0;row<rows;++row) {
      const auto r=g_layout.activity_row[row];char value[160];
      if(first+row<top) {
        const auto& id=snapshot.identities[order[first+row]];
        snprintf(value,sizeof(value),"%u. %lu / %.19s / %lu HITS",unsigned(first+row+1),static_cast<unsigned long>(id.capcode),id.alias[0]?id.alias:"UNASSIGNED",static_cast<unsigned long>(id.hit_count));
      } else snprintf(value,sizeof(value),"%s",row==0 && !top?"NO CAPCODES OBSERVED YET":"");
      if(widget(unsigned(row),r,signature(value,strlen(value)))) preview(value,r.x+8,r.y+r.h/2,r.w-16,kCyan);
    }
  }
  native_pager(13,g_layout.activity_previous,g_layout.activity_next,g_layout.activity_page,g_activity_page,pages,g_activity_page?"TOP IDS":"METRICS");
  refresh_focus();
}
void native_list(bool ids) {
  const auto& s=live_snapshot();const unsigned slot=ids?0:1;
  const size_t count=ids?std::min(s.identity_count,kIdentityCapacity):std::min(s.message_count,kRecentMessageCapacity);
  const size_t rows=size_t(std::max(1,g_layout.list_row_count));
  const size_t pages=std::max<size_t>(1,(count+rows-1)/rows);
  auto& page=g_list_page[slot];page=std::min(page,pages-1);
  auto& selected=ids?g_selected_id:g_selected_message;selected=count?std::min(selected,count-1):0;
  const size_t first=page*rows;
  if(widget(16,g_layout.list_title,count)) preview(ids?"CAPCODE DIRECTORY":"SESSION / RAM ONLY",g_layout.list_title.x+8,g_layout.list_title.y+22,g_layout.list_title.w-16,kCyan);
  for(size_t row=0;row<rows;++row) {
    const auto r=g_layout.list_rows[row];const size_t index=first+row;
    uint64_t hash=index==selected?1:0;
    if(index<count) hash^=ids?signature(&s.identities[index],sizeof(IdentitySummary)):signature(&s.messages[index],sizeof(DisplayMessage));
    if(!widget(unsigned(row),r,hash)) continue;
    if(index>=count) {
      if(row==0 && !count) preview(ids?"No IDs received yet":"No messages received yet",r.x+8,r.y+r.h/2,r.w-16,kMuted);
      continue;
    }
    if(index==selected) M5.Display.drawRoundRect(r.x+1,r.y+1,r.w-2,r.h-2,7,kCyan);
    char value[96];
    const uint32_t capcode=ids?s.identities[index].capcode:s.messages[index].capcode;
    if(ids) snprintf(value,sizeof(value),"%lu / %lu HITS",static_cast<unsigned long>(capcode),static_cast<unsigned long>(s.identities[index].hit_count));
    else snprintf(value,sizeof(value),"%lu / %s",static_cast<unsigned long>(capcode),message_type_label(s.messages[index].type));
    preview(value,r.x+8,r.y+17,r.w-16,kCyan);
    preview(ids?(s.identities[index].alias[0]?s.identities[index].alias:"Unassigned"):s.messages[index].text,r.x+8,r.y+41,r.w-16,TFT_WHITE);
  }
  native_pager(13,g_layout.list_previous,g_layout.list_next,g_layout.list_page,page,pages);
  char detail[768];
  if(!count) snprintf(detail,sizeof(detail),"%s",ids?"Details appear after a decoded message.":"Recent messages are retained in RAM and cleared on restart.");
  else if(ids) {
    const auto& id=s.identities[selected];const char* latest="No message retained in recent history";
    for(size_t i=0;i<std::min(s.message_count,kRecentMessageCapacity);++i) if(s.messages[i].capcode==id.capcode) {latest=s.messages[i].text;break;}
    snprintf(detail,sizeof(detail),"CAPCODE %lu\nAlias: %.19s\nGroup: %.11s\nHits: %lu / %lu s ago\nWatch: %s / Mute: %s\nLATEST MESSAGE\n%.95s",static_cast<unsigned long>(id.capcode),id.alias[0]?id.alias:"Unassigned",id.group[0]?id.group:"Unassigned",static_cast<unsigned long>(id.hit_count),static_cast<unsigned long>((millis()-uint32_t(id.last_seen_ms))/1000),id.watched?"ON":"OFF",id.muted?"ON":"OFF",latest);
  } else {
    const auto& m=s.messages[selected];
    snprintf(detail,sizeof(detail),"CAPCODE %lu\n%u bps / %s / FUNCTION %u\nPolarity: %s / %lu s ago\nCorrected bits: %u / bad words: %u\nTruncated: %s\nMESSAGE TEXT\n%.95s",static_cast<unsigned long>(m.capcode),m.baud,message_type_label(m.type),m.function,m.inverted?"INVERTED":"NORMAL",static_cast<unsigned long>((millis()-uint32_t(m.timestamp_ms))/1000),m.corrected_bits,m.uncorrectable_words,m.truncated?"YES":"NO",m.text_length?m.text:"(no text)");
  }
  if(widget(18,g_layout.detail_title,ids?0:1)) preview(ids?"ID DETAILS":"MESSAGE DETAILS",g_layout.detail_title.x+8,g_layout.detail_title.y+22,g_layout.detail_title.w-16,kCyan);
  const size_t detail_pages=wrapped_detail(g_layout.detail,detail,0,false);
  g_detail_pages[slot]=detail_pages;
  g_detail_page[slot]=std::min(g_detail_page[slot],detail_pages-1);
  if(widget(12,g_layout.detail,signature(detail,strlen(detail))^g_detail_page[slot])) wrapped_detail(g_layout.detail,detail,g_detail_page[slot],true);
  native_pager(14,g_layout.detail_previous,g_layout.detail_next,g_layout.detail_page,g_detail_page[slot],detail_pages);
  refresh_focus();
}

// Frequency/lock-badge/message-count region, redrawn both on full header
// entry and on every live update (~1/s) without touching the Home/Settings
// buttons either side of it.
void draw_header_live_values() {
  if(!g_layout.legacy) {
    const auto& s=live_snapshot();const auto r=g_layout.header_status;
    char value[80];const uint32_t hz=s.scanning?s.scan_frequency_hz:s.frequency_hz?s.frequency_hz:g_settings.frequency_hz;
    snprintf(value,sizeof(value),"%.4f MHz / %s",hz/1000000.0,s.scanning?"SCANNING":lock_state_label(s.decoder_stats.lock));
    const uint64_t hash=signature(value,strlen(value))^s.decoder_stats.messages_decoded^(uint64_t(s.decoder_stats.detected_baud)<<32);
    if(widget(15,r,hash)) {
      preview(value,r.x+8,r.y+15,r.w-16,TFT_WHITE);
      snprintf(value,sizeof(value),"%lu MESSAGES / %u bps",static_cast<unsigned long>(s.decoder_stats.messages_decoded),s.decoder_stats.detected_baud);
      preview(value,r.x+8,r.y+39,r.w-16,kCyan);
    }
    return;
  }
  M5.Display.fillRect(370, 12, 430, 52, kBg);
  M5.Display.fillRect(750, 12, 200, 52, kBg);
  const Snapshot& snapshot = live_snapshot();
  char freq[24];
  const uint32_t display_hz = snapshot.scanning ? snapshot.scan_frequency_hz :
      snapshot.frequency_hz ? snapshot.frequency_hz : g_settings.frequency_hz;
  snprintf(freq, sizeof(freq), "%.4f MHz", display_hz / 1000000.0);
  text(freq, 390, 36, TFT_WHITE, 2, middle_left);
  if (snapshot.scanning) {
    M5.Display.fillRoundRect(580, 18, 150, 40, 8, TFT_DARKGREY);
    char scan_label[16];
    snprintf(scan_label, sizeof(scan_label), "%u/%u", static_cast<unsigned>(snapshot.scan_index + 1),
             static_cast<unsigned>(snapshot.scan_count));
    text("SCANNING", 655, 30, kYellow, 1);
    text(scan_label, 655, 46, TFT_WHITE, 1);
    return;
  }
  const LockState lock = snapshot.decoder_stats.lock;
  M5.Display.fillRoundRect(580, 18, 150, 40, 8,
                            lock == LockState::locked ? TFT_DARKGREEN : TFT_DARKGREY);
  text(lock_state_label(lock), 655, 38, lock_state_color(lock), 1);
  char msgs[24];
  snprintf(msgs, sizeof(msgs), "%u MSGS",
           static_cast<unsigned>(snapshot.decoder_stats.messages_decoded));
  text(msgs, 770, 36, TFT_WHITE, 2, middle_left);
}

void draw_header() {
  if(!g_layout.legacy) {
    audio_header::draw_brand("POCSAG");draw_header_live_values();
    audio_header::draw_home_button();audio_header::draw_battery(M5.Power.getBatteryLevel());
    audio_header::draw_mute_button(true);audio_header::draw_settings_button();return;
  }
  M5.Display.fillRect(0, 0, 1280, kHeaderH, kBg);
  M5.Display.drawFastHLine(20, kHeaderH - 1, 1240, kBorder);
  audio_header::draw_brand("POCSAG PAGER MONITOR");
  M5.Display.drawFastVLine(370, 12, 76, kBorder);
  M5.Display.drawFastVLine(560, 12, 52, kBorder);
  M5.Display.drawFastVLine(750, 12, 52, kBorder);
  draw_header_live_values();
  audio_header::draw_home_button();
  audio_header::draw_battery(M5.Power.getBatteryLevel());
  audio_header::draw_mute_button(true);
  audio_header::draw_visualizer_button(g_live);
  audio_header::draw_settings_button();
}

void draw_tabs() {
  static constexpr const char* labels[] = {"LIVE", "IDS", "SIGNAL", "ACTIVITY", "SESSION"};
  if(!g_layout.legacy) {
    for(int i=0;i<5;++i) {
      const auto r=g_layout.tab[i];const bool selected=static_cast<int>(g_view)==i;
      M5.Display.fillRoundRect(r.x,r.y,r.w,r.h,6,selected?TFT_DARKGREEN:TFT_BLACK);
      M5.Display.drawRoundRect(r.x,r.y,r.w,r.h,6,kCyan);
      text(labels[i],r.x+r.w/2,r.y+r.h/2,TFT_WHITE,1);
    }
    return;
  }
  M5.Display.fillRect(0, kTabsY, 1280, 74, kBg);
  M5.Display.drawFastHLine(0, kTabsY, 1280, kBorder);
  for (int i = 0; i < 5; ++i) {
    const bool selected = static_cast<int>(g_view) == i;
    if (selected) {
      M5.Display.fillRect(i * kTabW, kTabsY + 1, kTabW, 73, 0x08a4);
      M5.Display.fillRect(i * kTabW, kTabsY + 1, kTabW, 3, kCyan);
    }
    if (i) M5.Display.drawFastVLine(i * kTabW, kTabsY, 74, kBorder);
    const uint16_t color = selected ? kCyan : TFT_LIGHTGREY;
    tab_icon(i, i * kTabW + 68, kTabsY + 38, color);
    text(labels[i], i * kTabW + 96, kTabsY + 39, color, 1, middle_left);
  }
}

// Owned by this dashboard's content area, not the shared header row --
// architecture.md reserves the header for Home/Settings/Battery/Volume and
// requires new controls to extend dashboard_audio_control rather than add
// independent header geometry. FIND PAGERS sits inside the message-inbox
// card below, not the header.
constexpr int kHeroX = 20, kHeroY = kHeaderH + 12, kHeroW = 740, kHeroH = 180;
constexpr int kStatusX = kHeroX + kHeroW + 16, kStatusY = kHeroY;
constexpr int kStatusW = 20 + 1240 - kHeroW - 16 - kHeroX, kStatusH = kHeroH;
constexpr int kInboxX = 20, kInboxY = kHeroY + kHeroH + 16, kInboxW = 1240;
constexpr int kInboxH = kContentH - kHeroH - 16;
constexpr int kScanButtonW = 170, kScanButtonH = 36;
constexpr int kScanButtonX = kInboxX + kInboxW - 20 - kScanButtonW;
constexpr int kScanButtonY = kInboxY + 14;

// Hero: the newest decoded page, big and legible -- or, absent one yet, a
// live "still listening" readout (lock state + a ticking batch/codeword
// count) so the tab visibly proves the receiver is doing something rather
// than sitting on dead text. Never fabricates a page that wasn't decoded.
void draw_hero(const Snapshot& snapshot) {
  card(kHeroX, kHeroY, kHeroW, kHeroH);
  if (snapshot.scanning) {
    M5.Display.fillRoundRect(kHeroX + 8, kHeroY + 10, 6, kHeroH - 20, 3, kYellow);
    text("SCANNING POCSAG FREQUENCIES", kHeroX + 30, kHeroY + 24, kYellow, 1, middle_left);
    char freq[24];
    snprintf(freq, sizeof(freq), "%.4f MHz", snapshot.scan_frequency_hz / 1000000.0);
    text(freq, kHeroX + 30, kHeroY + 70, TFT_WHITE, 3, middle_left);
    char label[32];
    snprintf(label, sizeof(label), "CHANNEL %u OF %u",
             static_cast<unsigned>(snapshot.scan_index + 1),
             static_cast<unsigned>(snapshot.scan_count));
    text(label, kHeroX + kHeroW - 24, kHeroY + 24, kMuted, 1, middle_right);
    // Progress bar across the candidate list, not decoration -- the fill
    // fraction is the actual scan_index/scan_count position.
    const int bar_x = kHeroX + 30, bar_y = kHeroY + 132, bar_w = kHeroW - 60, bar_h = 16;
    M5.Display.drawRoundRect(bar_x, bar_y, bar_w, bar_h, 4, kBorder);
    const int fill_w = snapshot.scan_count
                           ? static_cast<int>(static_cast<uint64_t>(snapshot.scan_index + 1) *
                                               (bar_w - 4) / snapshot.scan_count)
                           : 0;
    if (fill_w > 0) M5.Display.fillRoundRect(bar_x + 2, bar_y + 2, fill_w, bar_h - 4, 2, kYellow);
    text("HUNTING FOR AN ACTIVE PAGING CHANNEL...", kHeroX + 30, kHeroY + 160, kMuted, 1,
         middle_left);
    return;
  }
  if (snapshot.message_count > 0) {
    const DisplayMessage& m = snapshot.messages[0];
    const uint16_t accent = m.type == MessageType::alpha
                                 ? kGreen
                                 : m.type == MessageType::numeric ? kCyan : kMuted;
    M5.Display.fillRoundRect(kHeroX + 8, kHeroY + 10, 6, kHeroH - 20, 3, accent);
    text("NEWEST MESSAGE", kHeroX + 30, kHeroY + 24, kCyan, 1, middle_left);
    char meta[64];
    snprintf(meta, sizeof(meta), "CAPCODE %lu  *  %s",
             static_cast<unsigned long>(m.capcode), message_type_label(m.type));
    text(meta, kHeroX + 30, kHeroY + 54, TFT_LIGHTGREY, 1, middle_left);
    preview(m.text_length ? m.text : "(no text)", kHeroX + 30, kHeroY + 100,
            kHeroW - 60, TFT_WHITE, 2);
    char age[64];
    snprintf(age, sizeof(age), "%u bps  /  FUNCTION %u  /  %lu s AGO", m.baud, m.function,
             static_cast<unsigned long>((millis() - static_cast<uint32_t>(m.timestamp_ms)) / 1000));
    text(age, kHeroX + 30, kHeroY + 151, kMuted, 1, middle_left);
    const char* quality = m.uncorrectable_words ? "UNCORRECTABLE"
                           : m.corrected_bits    ? "FEC CORRECTED"
                                                  : "CLEAN DECODE";
    const uint16_t quality_color =
        m.uncorrectable_words ? kRed : m.corrected_bits ? kYellow : kGreen;
    text(quality, kHeroX + kHeroW - 24, kHeroY + 24, quality_color, 1, middle_right);
    return;
  }
  const Stats& stats = snapshot.decoder_stats;
  M5.Display.fillRoundRect(kHeroX + 8, kHeroY + 10, 6, kHeroH - 20, 3,
                            lock_state_color(stats.lock));
  text("MONITORING", kHeroX + 30, kHeroY + 24, kCyan, 1, middle_left);
  text(lock_state_label(stats.lock), kHeroX + 30, kHeroY + 68, lock_state_color(stats.lock), 3,
       middle_left);
  char detail[96];
  snprintf(detail, sizeof(detail), "%lu BATCHES SYNCED  *  %lu CODEWORDS SEEN",
           static_cast<unsigned long>(stats.batches_synced),
           static_cast<unsigned long>(stats.codewords_total));
  text(detail, kHeroX + 30, kHeroY + 124, kMuted, 1, middle_left);
  text(snapshot.receiving ? "RECEIVER ACTIVE -- WAITING FOR THE NEXT PAGE" : "NOT RECEIVING",
       kHeroX + 30, kHeroY + 154, kMuted, 1, middle_left);
}

// Always-visible decode-health readout plus a live soft-symbol sparkline --
// a second proof-of-life signal (actual measured FSK samples, not a
// decoration) even while the hero card above is still waiting for a page.
void draw_decode_status_card(const Stats& stats) {
  card(kStatusX, kStatusY, kStatusW, kStatusH);
  text("DECODE STATUS", kStatusX + 20, kStatusY + 24, kCyan, 1, middle_left);
  int y = kStatusY + 54;
  char line[48];
  text("BAUD", kStatusX + 20, y, kMuted, 1, middle_left);
  snprintf(line, sizeof(line), "%u bps", stats.detected_baud);
  text(line, kStatusX + kStatusW - 20, y, TFT_WHITE, 1, middle_right);
  y += 28;
  text("VALID / CORR / BAD", kStatusX + 20, y, kMuted, 1, middle_left);
  snprintf(line, sizeof(line), "%lu / %lu / %lu",
           static_cast<unsigned long>(stats.codewords_valid),
           static_cast<unsigned long>(stats.codewords_corrected),
           static_cast<unsigned long>(stats.codewords_uncorrectable));
  text(line, kStatusX + kStatusW - 20, y, TFT_WHITE, 1, middle_right);

  const int plot_y0 = y + 30;
  const int plot_x0 = kStatusX + 20, plot_w = kStatusW - 40;
  const int plot_h = kStatusY + kStatusH - 16 - plot_y0;
  M5.Display.drawFastHLine(plot_x0, plot_y0 + plot_h / 2, plot_w, kBorder);
  if (stats.soft_symbol_count > 0) {
    const size_t count = std::min(stats.soft_symbol_count, Stats::kSoftSymbolCapacity);
    for (size_t i = 0; i < count; ++i) {
      const size_t index = (stats.soft_symbol_write + Stats::kSoftSymbolCapacity - count + i) %
                            Stats::kSoftSymbolCapacity;
      const float sample = std::clamp(stats.soft_symbols[index], -1.0f, 1.0f);
      const int px = plot_x0 + static_cast<int>(i * plot_w / Stats::kSoftSymbolCapacity);
      const int py = plot_y0 + plot_h / 2 - static_cast<int>(sample * (plot_h / 2 - 4));
      M5.Display.fillCircle(px, py, 2, sample >= 0.0f ? kGreen : kCyan);
    }
  } else {
    text("NO SAMPLES YET", plot_x0 + plot_w / 2, plot_y0 + plot_h / 2, kMuted, 1);
  }
}

void draw_live() {
  if(!g_layout.legacy) {native_live();return;}
  const Snapshot& snapshot = live_snapshot();
  draw_hero(snapshot);
  draw_decode_status_card(snapshot.decoder_stats);

  card(kInboxX, kInboxY, kInboxW, kInboxH);
  text("MESSAGE INBOX", kInboxX + 24, kInboxY + 24, kCyan, 1, middle_left);
  const bool scanning = snapshot.scanning;
  M5.Display.fillRoundRect(kScanButtonX, kScanButtonY, kScanButtonW, kScanButtonH, 8,
                            scanning ? TFT_DARKGREY : TFT_DARKCYAN);
  if (scanning) {
    char label[24];
    snprintf(label, sizeof(label), "STOP SCAN");
    text(label, kScanButtonX + kScanButtonW / 2, kScanButtonY + kScanButtonH / 2, TFT_WHITE, 1);
  } else {
    char label[24];
    snprintf(label, sizeof(label), "SCAN %u FREQS",
             static_cast<unsigned>(snapshot.candidate_count));
    text(label, kScanButtonX + kScanButtonW / 2, kScanButtonY + kScanButtonH / 2, TFT_WHITE, 1);
  }

  if (snapshot.message_count == 0) {
    text("NO MESSAGES DECODED YET", kInboxX + kInboxW / 2, kInboxY + kInboxH / 2 + 10, kMuted, 1);
    return;
  }
  const int row_h = 34;
  const size_t max_rows = std::max<int>(0, (kInboxH - 64) / row_h);
  const size_t visible = std::min<size_t>(snapshot.message_count, max_rows);
  for (size_t i = 0; i < visible; ++i) {
    const DisplayMessage& m = snapshot.messages[i];
    const int y = kInboxY + 64 + static_cast<int>(i) * row_h;
    if (i == 0) M5.Display.fillRoundRect(kInboxX + 10, y - 15, kInboxW - 20, 31, 5, 0x0928);
    else M5.Display.drawFastHLine(kInboxX + 14, y + 17, kInboxW - 28, kBorder);
    char capcode[16];
    snprintf(capcode, sizeof(capcode), "%lu", static_cast<unsigned long>(m.capcode));
    text(capcode, kInboxX + 24, y, TFT_WHITE, 1, middle_left);
    text(message_type_label(m.type), kInboxX + 170, y,
         m.type == MessageType::alpha ? kGreen : m.type == MessageType::numeric ? kCyan : kMuted,
         1, middle_left);
    preview(m.text_length ? m.text : "(no text)", kInboxX + 310, y, kInboxW - 360, TFT_LIGHTGREY);
    const int dot_x = kInboxX + kInboxW - 20;
    if (m.uncorrectable_words) M5.Display.fillCircle(dot_x, y, 6, kRed);
    else if (m.corrected_bits) M5.Display.fillCircle(dot_x, y, 6, kYellow);
    else M5.Display.fillCircle(dot_x, y, 6, kGreen);
  }
}

void draw_signal() {
  if(!g_layout.legacy) {native_signal();return;}
  const Stats& stats = live_snapshot().decoder_stats;

  // Left: decode status readout.
  card(20, kHeaderH + 12, 500, kContentH);
  text("DECODE STATUS", 44, kHeaderH + 34, kCyan, 1, middle_left);
  int y = kHeaderH + 74;
  char line[64];
  text("SYNC STATE", 44, y, kMuted, 1, middle_left);
  text(lock_state_label(stats.lock), 400, y, lock_state_color(stats.lock), 1, middle_right);
  y += 36;
  text("BAUD", 44, y, kMuted, 1, middle_left);
  snprintf(line, sizeof(line), "%u bps", stats.detected_baud);
  text(line, 400, y, TFT_WHITE, 1, middle_right);
  y += 36;
  text("POLARITY", 44, y, kMuted, 1, middle_left);
  text(stats.inverted ? "INVERTED" : "NORMAL", 400, y, TFT_WHITE, 1, middle_right);
  y += 36;
  text("FSK DEVIATION", 44, y, kMuted, 1, middle_left);
  snprintf(line, sizeof(line), "%+.1f Hz", static_cast<double>(stats.fsk_deviation_hz));
  text(line, 400, y, TFT_WHITE, 1, middle_right);
  y += 48;
  text("BATCHES SYNCED", 44, y, kMuted, 1, middle_left);
  snprintf(line, sizeof(line), "%lu", static_cast<unsigned long>(stats.batches_synced));
  text(line, 400, y, TFT_WHITE, 1, middle_right);
  y += 36;
  text("SYNC LOSSES", 44, y, kMuted, 1, middle_left);
  snprintf(line, sizeof(line), "%lu", static_cast<unsigned long>(stats.sync_losses));
  text(line, 400, y, TFT_WHITE, 1, middle_right);
  y += 48;
  text("CODEWORDS", 44, y, kMuted, 1, middle_left);
  snprintf(line, sizeof(line), "%lu", static_cast<unsigned long>(stats.codewords_total));
  text(line, 400, y, TFT_WHITE, 1, middle_right);
  y += 36;
  text("VALID", 44, y, kMuted, 1, middle_left);
  snprintf(line, sizeof(line), "%lu", static_cast<unsigned long>(stats.codewords_valid));
  text(line, 400, y, kGreen, 1, middle_right);
  y += 36;
  text("CORRECTED", 44, y, kMuted, 1, middle_left);
  snprintf(line, sizeof(line), "%lu bits=%lu", static_cast<unsigned long>(stats.codewords_corrected),
           static_cast<unsigned long>(stats.corrected_bit_count));
  text(line, 400, y, kYellow, 1, middle_right);
  y += 36;
  text("UNCORRECTABLE", 44, y, kMuted, 1, middle_left);
  snprintf(line, sizeof(line), "%lu", static_cast<unsigned long>(stats.codewords_uncorrectable));
  text(line, 400, y, kRed, 1, middle_right);
  y += 36;
  text("PARITY FAILURES", 44, y, kMuted, 1, middle_left);
  snprintf(line, sizeof(line), "%lu", static_cast<unsigned long>(stats.parity_failures));
  text(line, 400, y, kRed, 1, middle_right);

  // Right, top: 2-FSK soft-decision symbol plot -- an actual measured
  // instrument (recent discriminator samples), not a decorative waveform.
  constexpr int kPlotX = 540, kPlotY = kHeaderH + 12, kPlotW = 720, kPlotH = 280;
  card(kPlotX, kPlotY, kPlotW, kPlotH);
  text("2-FSK SYMBOL PLOT (SOFT DECISIONS)", kPlotX + 20, kPlotY + 24, kCyan, 1, middle_left);
  const int plot_x0 = kPlotX + 20, plot_y0 = kPlotY + 50;
  const int plot_w = kPlotW - 40, plot_h = kPlotH - 70;
  const int mid_y = plot_y0 + plot_h / 2;
  M5.Display.drawFastHLine(plot_x0, mid_y, plot_w, kBorder);
  if (stats.soft_symbol_count > 0) {
    const size_t count = std::min(stats.soft_symbol_count, Stats::kSoftSymbolCapacity);
    for (size_t i = 0; i < count; ++i) {
      const size_t index = (stats.soft_symbol_write + Stats::kSoftSymbolCapacity - count + i) %
                            Stats::kSoftSymbolCapacity;
      const float sample = std::clamp(stats.soft_symbols[index], -1.0f, 1.0f);
      const int px = plot_x0 + static_cast<int>(i * plot_w / Stats::kSoftSymbolCapacity);
      const int py = mid_y - static_cast<int>(sample * (plot_h / 2 - 4));
      M5.Display.fillCircle(px, py, 2, sample >= 0.0f ? kGreen : kCyan);
    }
  } else {
    text("NO SAMPLES YET", plot_x0 + plot_w / 2, mid_y, kMuted, 1);
  }

  // Right, bottom: FEC-quality strip over the most recent codewords.
  const int strip_x = kPlotX, strip_y = kPlotY + kPlotH + 20;
  const int strip_w = kPlotW, strip_h = kContentH - kPlotH - 20;
  card(strip_x, strip_y, strip_w, strip_h);
  text("FEC QUALITY (RECENT CODEWORDS)", strip_x + 20, strip_y + 24, kCyan, 1, middle_left);
  const int bars_x0 = strip_x + 20, bars_y0 = strip_y + 50;
  const int bars_w = strip_w - 40, bars_h = strip_h - 130;
  if (stats.fec_history_count > 0) {
    const size_t count = std::min(stats.fec_history_count, Stats::kFecHistoryCapacity);
    const int bar_w = std::max(1, bars_w / static_cast<int>(Stats::kFecHistoryCapacity));
    for (size_t i = 0; i < count; ++i) {
      const size_t index = (stats.fec_history_write + Stats::kFecHistoryCapacity - count + i) %
                            Stats::kFecHistoryCapacity;
      const uint8_t outcome = stats.fec_history[index];
      const uint16_t color = outcome == 0 ? kGreen : outcome == 1 ? kYellow : kRed;
      M5.Display.fillRect(bars_x0 + static_cast<int>(i) * bar_w, bars_y0, bar_w - 1, bars_h, color);
    }
  } else {
    text("NO CODEWORDS YET", bars_x0 + bars_w / 2, bars_y0 + bars_h / 2, kMuted, 1);
  }
  M5.Display.fillRoundRect(560, 578, 325, 42, 7, 0x0928);
  M5.Display.drawRoundRect(560, 578, 325, 42, 7, kCyan);
  snprintf(line, sizeof(line), "BAUD: %s", g_settings.baud_bps == 0 ? "AUTO" :
           g_settings.baud_bps == 512 ? "512" : g_settings.baud_bps == 1200 ? "1200" : "2400");
  text(line, 722, 599, kCyan, 1);
  M5.Display.fillRoundRect(905, 578, 335, 42, 7, 0x0928);
  M5.Display.drawRoundRect(905, 578, 335, 42, 7, kCyan);
  text(g_settings.polarity_mode == 0 ? "POLARITY: AUTO" :
       g_settings.polarity_mode == 1 ? "POLARITY: NORMAL" : "POLARITY: INVERTED",
       1072, 599, kCyan, 1);
}

void kpi_card(int x, int y, int w, int h, const char* label, const char* value,
              uint16_t value_color) {
  card(x, y, w, h);
  text(label, x + 16, y + 22, kMuted, 1, middle_left);
  text(value, x + w / 2, y + h / 2 + 10, value_color, 2);
}

void draw_activity() {
  if(!g_layout.legacy) {native_activity();return;}
  const Stats& stats = live_snapshot().decoder_stats;

  const int kpi_y = kHeaderH + 12, kpi_h = 90, kpi_gap = 16;
  const int kpi_w = (1240 - kpi_gap * 3) / 4;
  char value[24];

  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.messages_decoded));
  kpi_card(20, kpi_y, kpi_w, kpi_h, "TOTAL MESSAGES", value, TFT_WHITE);

  snprintf(value, sizeof(value), "%u", static_cast<unsigned>(live_snapshot().identity_count));
  kpi_card(20 + (kpi_w + kpi_gap), kpi_y, kpi_w, kpi_h, "ACTIVE IDS", value, TFT_WHITE);

  const uint32_t total_codewords = stats.codewords_total;
  const float valid_pct = total_codewords
                               ? 100.0f * static_cast<float>(stats.codewords_valid) /
                                     static_cast<float>(total_codewords)
                               : 0.0f;
  snprintf(value, sizeof(value), "%.1f%%", static_cast<double>(valid_pct));
  kpi_card(20 + 2 * (kpi_w + kpi_gap), kpi_y, kpi_w, kpi_h, "VALID CODEWORDS", value, kGreen);

  snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(stats.codewords_uncorrectable));
  kpi_card(20 + 3 * (kpi_w + kpi_gap), kpi_y, kpi_w, kpi_h, "UNCORRECTABLE", value, kRed);

  const int panel_y = kpi_y + kpi_h + 16;
  const int panel_h = kHeaderH + 12 + kContentH - panel_y;
  const int panel_gap = 16;
  const int panel_w = (1240 - panel_gap) / 2;

  // Left: message-type distribution.
  card(20, panel_y, panel_w, panel_h);
  text("MESSAGE TYPE DISTRIBUTION", 44, panel_y + 24, kCyan, 1, middle_left);
  const uint32_t type_counts[3] = {stats.messages_alpha, stats.messages_numeric,
                                    stats.messages_tone_only};
  const char* type_labels[3] = {"ALPHA", "NUMERIC", "TONE ONLY"};
  const uint16_t type_colors[3] = {kGreen, kCyan, kMuted};
  uint32_t type_max = 1;
  for (uint32_t count : type_counts) type_max = std::max(type_max, count);
  const int bar_x0 = 160, bar_max_w = panel_w - 200;
  for (int i = 0; i < 3; ++i) {
    const int y = panel_y + 66 + i * 56;
    text(type_labels[i], 44, y, TFT_WHITE, 1, middle_left);
    const int bar_w = static_cast<int>(static_cast<uint64_t>(type_counts[i]) * bar_max_w / type_max);
    M5.Display.fillRect(bar_x0, y - 12, std::max(bar_w, 2), 24, type_colors[i]);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(type_counts[i]));
    text(value, bar_x0 + bar_max_w + 16, y, TFT_WHITE, 1, middle_left);
  }
  if (stats.messages_decoded == 0)
    text("NO MESSAGES YET", 44 + panel_w / 2 - 44, panel_y + panel_h / 2 + 40, kMuted, 1);

  // Right: top CAPCODEs by hit count (sorted from the bounded identity
  // snapshot -- a small, cheap sort of at most kIdentityCapacity entries).
  const int right_x = 20 + panel_w + panel_gap;
  card(right_x, panel_y, panel_w, panel_h);
  text("TOP CAPCODES", right_x + 24, panel_y + 24, kCyan, 1, middle_left);
  if (live_snapshot().identity_count == 0) {
    text("NO CAPCODES OBSERVED YET", right_x + panel_w / 2, panel_y + panel_h / 2, kMuted, 1);
    return;
  }
  size_t order[kIdentityCapacity];
  const size_t identity_count = std::min(live_snapshot().identity_count, kIdentityCapacity);
  for (size_t i = 0; i < identity_count; ++i) order[i] = i;
  std::sort(order, order + identity_count, [](size_t a, size_t b) {
    return live_snapshot().identities[a].hit_count > live_snapshot().identities[b].hit_count;
  });
  const size_t top_count = std::min<size_t>(identity_count, 8);
  uint32_t top_max = 1;
  for (size_t i = 0; i < top_count; ++i)
    top_max = std::max(top_max, live_snapshot().identities[order[i]].hit_count);
  const int top_bar_x0 = right_x + 200, top_bar_max_w = panel_w - 240;
  for (size_t i = 0; i < top_count; ++i) {
    const IdentitySummary& id = live_snapshot().identities[order[i]];
    const int y = panel_y + 66 + static_cast<int>(i) * 44;
    char capcode[16];
    snprintf(capcode, sizeof(capcode), "%lu", static_cast<unsigned long>(id.capcode));
    text(id.alias[0] ? id.alias : capcode, right_x + 24, y, TFT_WHITE, 1, middle_left);
    const int bar_w =
        static_cast<int>(static_cast<uint64_t>(id.hit_count) * top_bar_max_w / top_max);
    M5.Display.fillRect(top_bar_x0, y - 10, std::max(bar_w, 2), 20, kGreen);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(id.hit_count));
    text(value, top_bar_x0 + top_bar_max_w + 16, y, TFT_WHITE, 1, middle_left);
  }
}

void draw_ids() {
  if(!g_layout.legacy) {native_list(true);return;}
  const auto& snapshot = live_snapshot();
  card(20, 88, 570, kContentH);
  card(606, 88, 654, kContentH);
  text("CAPCODE DIRECTORY", 40, 115, kCyan, 1, middle_left);
  text("ID DETAILS", 628, 115, kCyan, 1, middle_left);
  const size_t count = std::min(snapshot.identity_count, kIdentityCapacity);
  if (!count) {
    text("No pager IDs received yet", 305, 340, kMuted, 1);
    text("Details appear after a decoded message", 933, 340, kMuted, 1);
    return;
  }
  g_selected_id = std::min(g_selected_id, count - 1);
  text("CAPCODE", 42, 151, kMuted, 1, middle_left);
  text("ALIAS", 200, 151, kMuted, 1, middle_left);
  text("HITS", 558, 151, kMuted, 1, middle_right);
  for (size_t i = 0; i < count; ++i) {
    const auto& id = snapshot.identities[i];
    const int y = 184 + static_cast<int>(i) * 36;
    if (i == g_selected_id) {
      M5.Display.fillRoundRect(30, y - 16, 550, 33, 5, 0x0928);
      M5.Display.drawRoundRect(30, y - 16, 550, 33, 5, kCyan);
    }
    char value[24];
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(id.capcode));
    text(value, 42, y, TFT_WHITE, 1, middle_left);
    preview(id.alias[0] ? id.alias : "Unassigned", 200, y, 245, id.alias[0] ? kCyan : kMuted);
    snprintf(value, sizeof(value), "%lu", static_cast<unsigned long>(id.hit_count));
    text(value, 558, y, kGreen, 1, middle_right);
  }
  const auto& id = snapshot.identities[g_selected_id];
  preview(id.alias[0] ? id.alias : "Unassigned pager", 630, 166, 600, kCyan, 2);
  char value[80];
  snprintf(value, sizeof(value), "CAPCODE   %lu", static_cast<unsigned long>(id.capcode));
  text(value, 630, 212, TFT_WHITE, 1, middle_left);
  snprintf(value, sizeof(value), "GROUP     %s", id.group[0] ? id.group : "Unassigned");
  text(value, 630, 250, kMuted, 1, middle_left);
  snprintf(value, sizeof(value), "TOTAL HITS   %lu", static_cast<unsigned long>(id.hit_count));
  text(value, 630, 288, kGreen, 1, middle_left);
  snprintf(value, sizeof(value), "LAST SEEN   %lu s ago",
           static_cast<unsigned long>((millis() - static_cast<uint32_t>(id.last_seen_ms)) / 1000));
  text(value, 630, 326, kMuted, 1, middle_left);
  text("LATEST MESSAGE IN SESSION", 630, 380, kCyan, 1, middle_left);
  for (size_t i = 0; i < snapshot.message_count; ++i) {
    if (snapshot.messages[i].capcode == id.capcode) {
      preview(snapshot.messages[i].text, 630, 420, 596);
      return;
    }
  }
  text("No message retained in recent history", 630, 420, kMuted, 1, middle_left);
}

void draw_archive() {
  if(!g_layout.legacy) {native_list(false);return;}
  const auto& snapshot = live_snapshot();
  card(20, 88, 740, kContentH);
  card(776, 88, 484, kContentH);
  text("SESSION MESSAGES", 42, 115, kCyan, 1, middle_left);
  text("Recent messages in RAM / cleared on restart", 42, 145, kMuted, 1, middle_left);
  text("MESSAGE DETAILS", 798, 115, kCyan, 1, middle_left);
  const size_t count = std::min(snapshot.message_count, kRecentMessageCapacity);
  if (!count) {
    text("No messages received yet", 390, 340, kMuted, 1);
    return;
  }
  g_selected_message = std::min(g_selected_message, count - 1);
  for (size_t i = 0; i < count; ++i) {
    const auto& m = snapshot.messages[i];
    const int y = 186 + static_cast<int>(i) * 36;
    if (i == g_selected_message) {
      M5.Display.fillRoundRect(30, y - 16, 720, 33, 5, 0x0928);
      M5.Display.drawRoundRect(30, y - 16, 720, 33, 5, kCyan);
    }
    char code[24];
    snprintf(code, sizeof(code), "%lu", static_cast<unsigned long>(m.capcode));
    text(code, 42, y, kCyan, 1, middle_left);
    preview(m.text_length ? m.text : "(no text)", 196, y, 530);
  }
  const auto& m = snapshot.messages[g_selected_message];
  char value[64];
  snprintf(value, sizeof(value), "CAPCODE %lu", static_cast<unsigned long>(m.capcode));
  text(value, 800, 166, TFT_WHITE, 1, middle_left);
  snprintf(value, sizeof(value), "%u bps / %s / FUNC %u", m.baud, message_type_label(m.type), m.function);
  text(value, 800, 204, kMuted, 1, middle_left);
  snprintf(value, sizeof(value), "Corrected bits %u / bad words %u", m.corrected_bits, m.uncorrectable_words);
  text(value, 800, 242, m.uncorrectable_words ? kRed : kGreen, 1, middle_left);
  text("MESSAGE TEXT", 800, 292, kCyan, 1, middle_left);
  // Fixed-width chunks fit the bounded detail card; all stored text is short.
  M5.Display.setFont(&fonts::DejaVu18);
  const char* cursor = m.text;
  int y = 330;
  while (*cursor && y < 610) {
    char line[kMaxMessageChars]{};
    size_t n = 0;
    while (cursor[n] && cursor[n] != '\n' && n + 1 < sizeof(line)) {
      line[n] = cursor[n] < ' ' ? ' ' : cursor[n];
      line[n + 1] = '\0';
      if (M5.Display.textWidth(line) > 430) { line[n] = '\0'; break; }
      ++n;
    }
    if (!n && *cursor != '\n') break;
    text(line, 800, y, TFT_WHITE, 1, middle_left);
    cursor += n;
    if (*cursor == '\n') ++cursor;
    y += 26;
  }
  if (*cursor) text("...", 800, 612, kMuted, 1, middle_left);
}
void redraw_content() {
  reset_widgets();
  if(g_layout.legacy) M5.Display.fillRect(0, kHeaderH, 1280, kTabsY - kHeaderH, kBg);
  else {const auto r=g_layout.content;M5.Display.fillRect(r.x,r.y,r.w,r.h,kBg);}
  switch (g_view) {
    case View::live: draw_live(); break;
    case View::ids: draw_ids(); break;
    case View::signal: draw_signal(); break;
    case View::activity: draw_activity(); break;
    case View::archive: draw_archive(); break;
    default: break;
  }
}

void redraw() {
  reset_widgets();
  if(g_layout.legacy) M5.Display.fillScreen(kBg);
  else {const auto r=g_layout.viewport;M5.Display.fillRect(r.x,r.y,r.w,r.h,kBg);}
  draw_header();
  redraw_content();
  draw_tabs();
}

}  // namespace

bool set_viewport(layout::Rect viewport) {
  const auto next=layout::make_layout(viewport);
  if(!next.valid || !audio_header::set_viewport(viewport)) return false;
  g_layout=next;reset_widgets();
  focus_nav::set_viewport(viewport.x+viewport.w,viewport.y+viewport.h);
  if(!next.legacy) {
    const size_t rows=size_t(std::max(1,next.list_row_count));
    g_list_page[0]=g_selected_id/rows;g_list_page[1]=g_selected_message/rows;
  }
  return true;
}
void enter(const Settings& settings_value) {
  g_settings = settings_value;
  g_view = View::live;
  std::fill(std::begin(g_list_page),std::end(g_list_page),0);
  std::fill(std::begin(g_detail_page),std::end(g_detail_page),0);g_activity_page=0;
  if (!ensure_live_snapshot()) {
    g_active = false;
    g_live = false;
    return;
  }
  g_active = true;
  redraw();
}

void leave() {
  g_active = false;
  M5.Display.setFont(nullptr);
}

void draw() {
  if (g_active) redraw();
}

void update() {
  if (!g_active || !g_live || g_drawn_revision == live_snapshot().revision) return;
  static uint32_t last_draw_ms = 0;
  if (millis() - last_draw_ms < 1000) return;
  last_draw_ms = millis();
  g_drawn_revision = live_snapshot().revision;
  draw_header_live_values();
  // Each view's own card(s) already fill their entire content rect before
  // drawing on top of it, so a blanket background wipe here just doubles
  // the paint work and produces a visible full-area flash on every update
  // tick -- pure regression, not needed for correctness.
  if (g_view == View::live) {
    draw_live();
  } else if (g_view == View::ids) {
    draw_ids();
  } else if (g_view == View::signal) {
    draw_signal();
  } else if (g_view == View::activity) {
    draw_activity();
  } else if (g_view == View::archive) {
    draw_archive();
  }
}

void set_live_snapshot(const Snapshot& snapshot) {
  if (!ensure_live_snapshot()) return;
  const uint32_t old_id=g_selected_id<live_snapshot().identity_count?live_snapshot().identities[g_selected_id].capcode:0;
  const auto old_message=g_selected_message<live_snapshot().message_count?live_snapshot().messages[g_selected_message]:DisplayMessage{};
  live_snapshot() = snapshot;
  live_snapshot().message_count=std::min(snapshot.message_count,kRecentMessageCapacity);
  live_snapshot().identity_count=std::min(snapshot.identity_count,kIdentityCapacity);
  for(size_t i=0;i<live_snapshot().identity_count;++i) if(live_snapshot().identities[i].capcode==old_id) {g_selected_id=i;break;}
  if(g_view==View::archive) for(size_t i=0;i<live_snapshot().message_count;++i) {
    const auto& m=live_snapshot().messages[i];
    if(m.timestamp_ms==old_message.timestamp_ms && m.capcode==old_message.capcode) {g_selected_message=i;break;}
  }
  g_live = true;
}

Action handle_touch(int32_t x, int32_t y) {
  if (!g_active) return Action::none;
  if (audio_header::home_hit(x, y)) return Action::exit;
  if (audio_header::settings_hit(x, y)) return Action::open_settings;
  if(!g_layout.legacy) {
    for(int i=0;i<5;++i) if(hit(x,y,g_layout.tab[i])) {
      if(static_cast<int>(g_view)!=i) {g_view=static_cast<View>(i);redraw_content();draw_tabs();}
      return Action::none;
    }
    if(g_view==View::live && hit(x,y,g_layout.scan) && (live_snapshot().scanning || live_snapshot().candidate_count))
      return live_snapshot().scanning?Action::scan_cancelled:Action::scan_requested;
    if(g_view==View::signal) {
      if(hit(x,y,g_layout.baud)) {g_settings.baud_bps=g_settings.baud_bps==0?512:g_settings.baud_bps==512?1200:g_settings.baud_bps==1200?2400:0;draw_signal();return Action::settings_changed;}
      if(hit(x,y,g_layout.polarity)) {g_settings.polarity_mode=(g_settings.polarity_mode+1)%3;draw_signal();return Action::settings_changed;}
    }
    if(g_view==View::activity) {
      const auto before=g_activity_page;
      const size_t top=std::min<size_t>(live_snapshot().identity_count,8),rows=std::max(1,g_layout.activity_row_count);
      const size_t pages=1+std::max<size_t>(1,(top+rows-1)/rows);
      if(hit(x,y,g_layout.activity_previous) && g_activity_page) --g_activity_page;
      if(hit(x,y,g_layout.activity_next) && g_activity_page+1<pages) ++g_activity_page;
      if(before!=g_activity_page) redraw_content();
    }
    if(g_view==View::ids || g_view==View::archive) {
      const bool ids=g_view==View::ids;const unsigned slot=ids?0:1;
      const size_t count=ids?live_snapshot().identity_count:live_snapshot().message_count;
      const size_t rows=std::max(1,g_layout.list_row_count),pages=std::max<size_t>(1,(count+rows-1)/rows);
      auto& page=g_list_page[slot];
      if(hit(x,y,g_layout.list_previous) && page) --page;
      if(hit(x,y,g_layout.list_next) && page+1<pages) ++page;
      if(hit(x,y,g_layout.detail_previous) && g_detail_page[slot]) --g_detail_page[slot];
      if(hit(x,y,g_layout.detail_next)) ++g_detail_page[slot];
      for(size_t i=0;i<rows;++i) if(hit(x,y,g_layout.list_rows[i]) && page*rows+i<count) {
        (ids?g_selected_id:g_selected_message)=page*rows+i;g_detail_page[slot]=0;
      }
      native_list(ids);
    }
    return Action::none;
  }
  if (g_view == View::live &&
      hit(x, y, kScanButtonX, kScanButtonY, kScanButtonW, kScanButtonH))
    return live_snapshot().scanning ? Action::scan_cancelled : Action::scan_requested;
  if (g_view == View::signal && hit(x, y, 560, 578, 325, 42)) {
    g_settings.baud_bps = g_settings.baud_bps == 0 ? 512 :
        g_settings.baud_bps == 512 ? 1200 : g_settings.baud_bps == 1200 ? 2400 : 0;
    draw_signal();
    return Action::settings_changed;
  }
  if (g_view == View::signal && hit(x, y, 905, 578, 335, 42)) {
    g_settings.polarity_mode = (g_settings.polarity_mode + 1) % 3;
    draw_signal();
    return Action::settings_changed;
  }
  if (g_view == View::ids && x >= 30 && x < 580 && y >= 168 && y < 600) {
    const size_t index = (y - 168) / 36;
    if (index < live_snapshot().identity_count) { g_selected_id = index; redraw_content(); }
    return Action::none;
  }
  if (g_view == View::archive && x >= 30 && x < 750 && y >= 170 && y < 602) {
    const size_t index = (y - 170) / 36;
    if (index < live_snapshot().message_count) { g_selected_message = index; redraw_content(); }
    return Action::none;
  }
  if (y >= kTabsY && y < 720 && x >= 0 && x < 1280) {
    g_view = static_cast<View>(std::clamp<int32_t>(x / kTabW, 0, 4));
    redraw();
    return Action::none;
  }
  return Action::none;
}

Action control(Control c,unsigned value,bool* applied) {
  if(applied) *applied=false;
  if(!g_active || !valid_control(c,value)) return Action::none;
  const auto tap=[](layout::Rect r){return handle_touch(r.x+r.w/2,r.y+r.h/2);};
  Action action=Action::none;
  switch(c) {
    case Control::view:action=tap(g_layout.tab[value]);break;
    case Control::baud:
    case Control::polarity: {
      if(g_view!=View::signal) return Action::none;
      const auto r=c==Control::baud?(g_layout.legacy?layout::Rect{560,578,325,42}:g_layout.baud):(g_layout.legacy?layout::Rect{905,578,335,42}:g_layout.polarity);
      for(unsigned i=0;i<4 && unsigned(c==Control::baud?g_settings.baud_bps:g_settings.polarity_mode)!=value;++i) action=tap(r);
      break;
    }
    case Control::select: {
      if(g_view!=View::ids && g_view!=View::archive) return Action::none;
      const bool ids=g_view==View::ids;const size_t count=ids?live_snapshot().identity_count:live_snapshot().message_count;
      if(value>=count) return Action::none;
      if(g_layout.legacy) action=handle_touch(100,(ids?168:170)+int(value)*36+18);
      else {
        const unsigned slot=ids?0:1;g_list_page[slot]=value/unsigned(g_layout.list_row_count);
        action=tap(g_layout.list_rows[value%unsigned(g_layout.list_row_count)]);
      }
      break;
    }
    case Control::page:
      if(g_layout.legacy) return Action::none;
      if(g_view==View::activity) action=tap(value?g_layout.activity_next:g_layout.activity_previous);
      else if(g_view==View::ids || g_view==View::archive) action=tap(value?g_layout.list_next:g_layout.list_previous);
      else return Action::none;
      break;
    case Control::detail:
      if(g_layout.legacy || (g_view!=View::ids && g_view!=View::archive)) return Action::none;
      action=tap(value?g_layout.detail_next:g_layout.detail_previous);break;
    case Control::scan:
      if(g_view!=View::live || (!live_snapshot().scanning && !live_snapshot().candidate_count)) return Action::none;
      action=tap(g_layout.legacy?layout::Rect{kScanButtonX,kScanButtonY,kScanButtonW,kScanButtonH}:g_layout.scan);break;
  }
  if(applied) *applied=true;
  return action;
}
const Settings& settings() { return g_settings; }

uint8_t view() { return static_cast<uint8_t>(g_view); }

bool active() { return g_active; }

bool self_check() {
  // Runs before the splash: validate layout without drawing or seeding
  // the live dashboard with synthetic messages.
  return static_cast<uint8_t>(View::count) == 5 && kTabW * 5 == 1280 &&
         kHeaderH < kTabsY && kTabsY < 720 && kContentH > 0 &&
         audio_header::self_check();
}

bool interaction_check() {
  if (!g_active) return false;
  const Settings saved = g_settings;
  const View saved_view = g_view;
  bool ok = self_check() && g_active && g_view == saved_view;
  for (int i = 0; i < 5; ++i) {
    ok = handle_touch(i * kTabW + 128, 680) == Action::none &&
         static_cast<int>(g_view) == i && ok;
  }
  (void)handle_touch(640, 680);
  for (int i = 0; i < 4; ++i)
    ok = handle_touch(700, 599) == Action::settings_changed && ok;
  for (int i = 0; i < 3; ++i)
    ok = handle_touch(1050, 599) == Action::settings_changed && ok;
  ok = g_settings.baud_bps == saved.baud_bps &&
       g_settings.polarity_mode == saved.polarity_mode && ok;
  (void)handle_touch(128, 680);
  ok = handle_touch(kScanButtonX + 10, kScanButtonY + 10) ==
       (live_snapshot().scanning ? Action::scan_cancelled : Action::scan_requested) && ok;
  g_settings = saved;
  g_view = saved_view;
  redraw();
  return ok;
}

}  // namespace orcsdr::pocsag
