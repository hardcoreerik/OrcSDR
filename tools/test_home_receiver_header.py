"""Compile the actual Home header and device-label functions with a recording stand-in.
Usage: python tools/test_home_receiver_header.py OUTPUT_DIR [--baseline]
Tests only the changed header widget; physical display acceptance is separate.
"""
import pathlib,re,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1];out=pathlib.Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
def read(path):
    if "--baseline" in sys.argv: return subprocess.check_output(["git","-C",str(root),"show","HEAD:"+path],text=True)
    return (root/path).read_text()
def function(source,name):
    m=re.search(r"^(?:void|bool|const char\*) "+name+r"\([^{};]*\)\s*\{",source,re.M);assert m,name
    end=m.end();depth=1
    while depth:
        depth+=(source[end]=="{")-(source[end]=="}");end+=1
    return source[m.start():end]
home=read("apps/orcsdr-tab5/ui/home_dashboard.cpp");main=read("apps/orcsdr-tab5/ui/main.cpp")
code=r'''
#include <cassert>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>
enum esp_rtl_sdr_profile_t { ESP_RTL_SDR_PROFILE_BLOG_V4,ESP_RTL_SDR_PROFILE_BLOG_V4L,ESP_RTL_SDR_PROFILE_BLOG_V3,ESP_RTL_SDR_PROFILE_NOOELEC_SMART_V5,ESP_RTL_SDR_PROFILE_E4000 };
struct esp_rtl_sdr_device_info_t { char manufacturer[32]; char product[64]; } identity{};
std::atomic<bool> g_rtl_is_nooelec_xtr{false};
void* g_rtl=&identity;
constexpr int ESP_OK=0;
int esp_rtl_sdr_get_device_info(void*,esp_rtl_sdr_device_info_t* out) { *out=identity;return ESP_OK; }
struct Snapshot { bool wifi_connected=true,driver_ready=true,gain_direct_sampling=false; char receiver[12]="RTL-SDR",wifi_ip[16]="192.0.2.1",clock[12]="12:00",date[20]="2026-10-31"; } current;
struct Primitive { std::string text;int x,y,size; };
#undef assert
#define assert(x) do { if (!(x)) { std::fprintf(stderr,"FAIL: %s\n",#x); return 1; } } while(0)
std::vector<Primitive> primitives;int full_clears=0,bounded_clears=0;
struct DisplayStub { void fillRect(int,int,int w,int h,int) { if(w>=1280&&h>=720)++full_clears;else ++bounded_clears; } void drawFastVLine(int,int,int,int) {} };
struct { DisplayStub Display; } M5;
constexpr int kHeaderStatusX=420,kHeaderStatusY=12,kHeaderStatusW=446,kHeaderStatusH=54,kCyan=1,kDim=2,TFT_BLACK=0,TFT_WHITE=3,TFT_ORANGE=4,middle_right=1;
void frame(int,int,int,int,int,int) {}
void draw_wifi_icon(int,int,int) {}
void draw_usb_icon(int,int,int) {}
void text(const char* s,int x,int y,int,int size,int=0) { primitives.push_back({s,x,y,size}); }
'''
code+=function(home,"gain_unavailable_message")+"\n"+function(main,"rtl_identity_is_nooelec_xtr")+"\n"+function(main,"rtl_receiver_label")+"\n"+function(home,"draw_header_status")
code+=r'''
int main() {
 assert(std::strstr(gain_unavailable_message(),"tuner or mode")!=nullptr);
 current.gain_direct_sampling=true;assert(std::strstr(gain_unavailable_message(),"24 MHz")!=nullptr);
 current.driver_ready=false;assert(std::strcmp(gain_unavailable_message(),"No receiver connected.")==0);
 current.driver_ready=true;current.gain_direct_sampling=false;
 std::strcpy(identity.manufacturer,"Nooelec");std::strcpy(identity.product,"SMArt XTR v5");
 g_rtl_is_nooelec_xtr.store(rtl_identity_is_nooelec_xtr(&identity));
 assert(std::strcmp(rtl_receiver_label(ESP_RTL_SDR_PROFILE_E4000),"Nooelec XTR")==0);
 std::strcpy(current.receiver,rtl_receiver_label(ESP_RTL_SDR_PROFILE_E4000));draw_header_status();
 bool found=false;
 for(auto &p:primitives) if(p.text=="Nooelec XTR") { found=true;assert(p.size==1);assert(int(p.text.size())*6*p.size<=90); }
 assert(found && full_clears==0 && bounded_clears==1);
 std::strcpy(identity.manufacturer,"Other");g_rtl_is_nooelec_xtr.store(rtl_identity_is_nooelec_xtr(&identity));assert(std::strcmp(rtl_receiver_label(ESP_RTL_SDR_PROFILE_E4000),"E4000")==0);
 std::strcpy(current.receiver,"RTL V4");primitives.clear();draw_header_status();found=false;
 for(auto &p:primitives) if(p.text=="RTL V4")found=true;
 assert(found && full_clears==0 && bounded_clears==2);
 std::puts("PASS actual Home header: model updates, XTR identity, bounded label, zero full-screen clears");
}
'''
(out/"test.cpp").write_text(code)
(out/"CMakeLists.txt").write_text("cmake_minimum_required(VERSION 3.16)\nproject(home_receiver_header LANGUAGES CXX)\nadd_executable(home_receiver_header test.cpp)\ntarget_compile_features(home_receiver_header PRIVATE cxx_std_17)\n")
subprocess.run(["cmake","-S",str(out),"-B",str(out/"build")],check=True,stdout=subprocess.DEVNULL)
subprocess.run(["cmake","--build",str(out/"build"),"--config","Debug"],check=True,stdout=subprocess.DEVNULL)
exe=out/"build/Debug/home_receiver_header.exe" if sys.platform=="win32" else out/"build/home_receiver_header"
subprocess.run([str(exe)],check=True)
