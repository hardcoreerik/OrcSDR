#pragma once
#include <cstddef>
#include <cstdint>
namespace orcsdr::storage {class FileSystem;}
namespace orcsdr::maps::downloads {
enum class Phase:uint8_t {idle,verifying,complete,cancelled,failed,checking,ready,downloading,unconfigured};
struct Entry {
  char title[64]{},region[80]{},product[64]{};
  uint64_t bytes=0;
  uint8_t min_zoom=0,max_zoom=0;
  bool installed=false,recommended=false;
};
struct Snapshot {
  Phase phase=Phase::idle;
  bool busy=false,pending=false;
  uint32_t revision=0;
  bool configured=false,connected=false,catalog_ready=false,sd_available=false;
  uint8_t count=0;
  uint64_t completed_bytes=0,total_bytes=0;
  uint64_t verified_bytes=0,verify_total_bytes=0;
  uint32_t verify_pass=0;
  char message[96]{};
};
// Shared by UI and worker so offline recovery cannot be hidden by UI gating.
inline bool can_check(const Snapshot& s) {
  return !s.busy&&!s.pending&&s.configured&&s.connected;
}
inline bool can_install(const Snapshot& s,size_t index) {
  return !s.busy&&!s.pending&&s.catalog_ready&&s.sd_available&&index<s.count;
}
inline bool can_retry(const Snapshot& s) {
  return !s.busy&&(s.phase==Phase::failed||s.phase==Phase::cancelled);
}
// A saved install that cannot finish blocks Check/Install. This lets the user
// forget that intent without reflashing or erasing NVS; files are not deleted.
inline bool can_reset(const Snapshot& s) {
  return !s.busy&&s.pending;
}
void begin();
bool snapshot(Snapshot*);
bool entry(size_t index,Entry*);
bool check_catalog();
bool request_install(size_t index);
bool retry();
bool retry_recovery();
bool reset_pending();
void cancel();
// Consumed on the UI loop: SNTP state and RTC writes retain their existing owner.
bool take_time_sync_request();
bool work_pending();
void worker_unavailable(); // UI: preserve retry identity when the internal stack cannot be allocated.
// Called only by the dedicated download worker, without the rendering mutex.
// Discovery/recovery and activation share ownership; immutable archive reads
// continue independently through the filesystem's per-volume locking.
// Returns true after a recovery attempt so its caller can refresh inventory.
bool service(storage::FileSystem*);
}
