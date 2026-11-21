#include "dead_air_storage.hpp"
#include "compact/dead_air_screen.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
int main(){
 namespace fs=std::filesystem;
 const auto dir=fs::temp_directory_path()/("orcsdr-journal-test-"+std::to_string(getpid()));
 assert(!fs::exists(dir));
 orc::zero::DeadAirStorage storage;assert(storage.failed());storage.start(dir);
 orc::zero::DeadAirScreen screen;screen.set_event_listener(orcsdr::dead_air::enqueue_event,&storage.queue());
 orcsdr::dead_air::RfObservation o;o.valid=o.settled=true;o.frequency_hz=96500000;o.level_dbfs=-40;o.monotonic_ms=1000;o.unix_time=1795000000;
 screen.measure(o);screen.mark(1200);
 for(int n=0;n<50&&!storage.saved()&&!storage.failed();++n)std::this_thread::sleep_for(std::chrono::milliseconds(20));
 storage.stop();assert(!storage.failed()&&storage.saved()==1&&screen.event_count()==1);
 std::string data;size_t files=0;
 for(const auto& file:fs::directory_iterator(dir)){std::ifstream in(file.path());data.assign(std::istreambuf_iterator<char>(in),{});++files;}
 assert(files==1&&data.find("96500000")!=std::string::npos&&data.find("1795000000")!=std::string::npos);
 screen.storage_status(storage.saved(),0,false);screen.render();const auto clears=screen.frame().full_clears;
 screen.storage_status(storage.saved(),1,true);screen.render();assert(screen.frame().full_clears==clears);
 const auto bad=dir/"not-a-directory";{std::ofstream out(bad);out<<"preserve";}
 orc::zero::DeadAirStorage failure;failure.start(bad/"child");
 for(int n=0;n<50&&!failure.failed();++n)std::this_thread::sleep_for(std::chrono::milliseconds(20));
 failure.stop();assert(failure.failed());
 std::ifstream unchanged(bad);std::string text;unchanged>>text;assert(text=="preserve");
 // Remove only test-created entries in the verified unique temporary directory.
 for(const auto& file:fs::directory_iterator(dir))fs::remove(file.path());fs::remove(dir);
 std::cout<<"Dead Air durable UTC mark, graceful finish, storage failure and bounded redraw PASS\n";
}
