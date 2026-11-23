// Host test for wifi_supervisor: the link-loss, recovery-budget, health, keep-alive and reconnect rules, with a fake clock.
// The expected behaviour was read off poll_wifi() in main.cpp (commit 73624679).
#include "wifi_supervisor.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
namespace ws = orcsdr::wifi_supervisor;
using Log = std::vector<std::string>;
int g_failures = 0;
Log g_log;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); ++g_failures; } } while (0)
#define CHECK_LOG(...) do { const Log e{__VA_ARGS__}; if (g_log != e) { std::fprintf(stderr, "FAIL line %d: log differs\n", __LINE__); for (auto& l : g_log) std::fprintf(stderr, "  got  %s\n", l.c_str()); for (auto& l : e) std::fprintf(stderr, "  want %s\n", l.c_str()); ++g_failures; } } while (0)

void note(const char* f, ...) __attribute__((format(printf, 1, 2)));
void note(const char* f, ...) { char b[300]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a); g_log.emplace_back(b); }

uint32_t g_now = 0;
bool g_power = true, g_link_failed = false, g_remote = false, g_keepalive = false, g_ssid_empty = false;
uint8_t g_profiles = 1;
bool station_ready, connected, connecting, scan_running, armed, save_after;
uint8_t recoveries, attempts;
uint32_t recovery_due, reconnect_due, connected_since, scan_revision;

ws::Vars vars() {
  ws::Vars v;
  v.station_ready = &station_ready; v.connected = &connected; v.connecting = &connecting; v.scan_running = &scan_running;
  v.auto_reconnect_armed = &armed; v.save_after_connect = &save_after; v.link_recoveries = &recoveries;
  v.reconnect_attempts = &attempts; v.link_recovery_due_ms = &recovery_due; v.reconnect_due_ms = &reconnect_due;
  v.connected_since_ms = &connected_since; v.scan_revision = &scan_revision; v.link_recovery_max = 3;
  return v;
}
ws::Hooks hooks() {
  ws::Hooks h;
  h.io.write = [](const char* t) { note("W:%s", t); };
  h.io.write_line = [](const char* t) { note("L:%s", t); };
  h.now_ms = []() { return g_now; };
  h.power_enabled = []() { return g_power; };
  h.clear_requests = []() { note("clear_requests"); };
  h.link_failed = []() { return g_link_failed; };
  h.resume_radio_connect_pause = []() { note("resume_radio"); };
  h.set_status = [](const char* t) { note("status:%s", t); };
  h.draw_state = []() { note("draw"); };
  h.recover_link = []() { note("recover"); };
  h.remote_may_need_wifi = []() { return g_remote; };
  h.dma_free_bytes = []() { return 12345u; };
  h.stream_state_name = []() { return "streaming"; };
  h.keepalive_enabled = []() { return g_keepalive; };
  h.schedule_reconnect = [](const char* w) { note("schedule:%s", w); };
  h.resume_deferred_radio = []() { note("deferred"); };
  h.profile_ssid_empty = []() { return g_ssid_empty; };
  h.profile_count = []() { return g_profiles; };
  h.select_first_profile = []() { note("select0"); };
  h.request_connect = []() { note("request_connect"); };
  return h;
}
void reset() {
  g_log.clear(); g_now = 100001; g_power = true; g_link_failed = false; g_remote = false; g_keepalive = false; g_ssid_empty = false; g_profiles = 1;
  station_ready = true; connected = true; connecting = false; scan_running = false; armed = true; save_after = true;
  recoveries = 0; attempts = 0; recovery_due = 0; reconnect_due = 0; connected_since = 1; scan_revision = 0;
}
bool run(ws::State& s) { return ws::service(s, vars(), hooks()); }
}  // namespace

int main() {
  {  // power off: queued requests are dropped, nothing else happens
    reset(); g_power = false; ws::State s;
    CHECK(!run(s));
    CHECK_LOG("clear_requests");
  }
  {  // a quiet link: only the deferred radio resume runs, and the caller goes on
    reset(); ws::State s;
    CHECK(run(s));
    CHECK_LOG("deferred");
  }
  {  // link lost: handled once; recovery due in 2 s; polling again does not postpone it
    reset(); ws::State s; g_link_failed = true;
    CHECK(!run(s));
    CHECK_LOG("L:RTL_WIFI_LINK_LOST", "resume_radio", "status:Wi-Fi link lost - recovering", "draw", "deferred");
    CHECK(!station_ready && !connected && !connecting && !scan_running && reconnect_due == 0);
    CHECK(recovery_due == 102001 && scan_revision == 1);
    g_log.clear(); g_now += 1000;
    CHECK(!run(s));
    CHECK_LOG("deferred");
    CHECK(recovery_due == 102001);
    g_log.clear(); g_now = 102001;   // exactly due
    CHECK(!run(s));
    CHECK_LOG("recover", "deferred");
    CHECK(recovery_due == 0);
  }
  {  // recoveries: the first 2 s after the loss, every later one 30 s after the last; no wait that grows
    CHECK(ws::lost_recovery_delay_ms(0) == 2000 && ws::lost_recovery_delay_ms(1) == 30000 &&
          ws::lost_recovery_delay_ms(2) == 30000 && ws::lost_recovery_delay_ms(9) == 30000);
    CHECK(ws::slow_retry_delay_ms(0) == 30000 && ws::slow_retry_delay_ms(1) == 30000 && ws::slow_retry_delay_ms(255) == 30000);
    reset(); ws::State s; g_link_failed = true; recoveries = 1;
    run(s); CHECK(recovery_due == 100001 + 30000);
    reset(); s = ws::State{}; g_link_failed = true; recoveries = 2;
    run(s); CHECK(recovery_due == 100001 + 30000);
  }
  {  // budget spent: the retries carry on every 30 s for as long as the link is down
    reset(); ws::State s; g_link_failed = true; recoveries = 3;
    run(s);
    CHECK_LOG("L:RTL_WIFI_LINK_LOST", "resume_radio", "status:Wi-Fi link lost - restart Wi-Fi", "draw",
              "status:Wi-Fi link lost - retrying in 30 s", "draw", "W:RTL_WIFI_LINK_RETRY_SCHEDULED retry=1 delay_s=30\n",
              "deferred");
    CHECK(recovery_due == 100001 + 30000 && recoveries == 2 && s.slow_retries == 1);
    g_log.clear(); g_now += 29999; run(s); CHECK_LOG("deferred");                   // not before it is due
    g_log.clear(); g_now += 1; run(s); CHECK_LOG("recover", "deferred");           // due: one recovery attempt
    CHECK(recovery_due == 0);
    // that attempt fails (main.cpp's recover counts it: 3 again, link still failed): the next one is 30 s later again
    recoveries = 3; g_log.clear(); run(s);
    CHECK_LOG("status:Wi-Fi link lost - retrying in 30 s", "draw", "W:RTL_WIFI_LINK_RETRY_SCHEDULED retry=2 delay_s=30\n",
              "deferred");
    CHECK(recovery_due == g_now + 30000 && s.slow_retries == 2);
  }
  {  // a recovery that brought the link back but left the station down also gets the retry
    reset(); ws::State s; recoveries = 3; station_ready = false; connected = false;
    run(s);
    CHECK(recovery_due == 100001 + 30000 && s.slow_retries == 1);
  }
  {  // budget spent while the link is up: nothing scheduled
    reset(); ws::State s; recoveries = 3;
    run(s); CHECK_LOG("deferred"); CHECK(recovery_due == 0 && s.slow_retries == 0);
  }
  {  // a slow retry that comes due after Wi-Fi came back some other way does not tear the link down
    reset(); ws::State s; recovery_due = 100001; connected = true;
    run(s); CHECK_LOG("W:RTL_WIFI_LINK_RETRY_SKIPPED reason=connected\n", "deferred"); CHECK(recovery_due == 0);
  }
  {  // the retry count (for the log line) starts again after a link up 5 minutes
    reset(); ws::State s; s.slow_retries = 4; connected_since = 1000; g_now = 1000 + 299999;
    run(s); CHECK(s.slow_retries == 4);
    g_now = 1000 + 300000; run(s); CHECK(s.slow_retries == 0);
  }
  {  // a link back up for 20 s earns back one quick recovery, once per connection; 5 minutes earns back the rest
    reset(); ws::State s; recoveries = 3; connected_since = 1000; g_now = 1000 + 19999;
    run(s); CHECK(recoveries == 3);
    g_now = 1000 + 20000; run(s); CHECK(recoveries == 2 && s.earned_one);
    g_now += 60000; run(s); CHECK(recoveries == 2);                          // not again on the same connection
    connected = false; run(s); CHECK(!s.earned_one && recoveries == 2);      // lost: the next connection may earn one
    connected = true; connected_since = g_now; g_now += 20000; run(s); CHECK(recoveries == 1);
    // the 2026-11-22 case: budget spent at boot, back up 31 s, then a hub blip: a quick recovery, not the 5-minute retry
    reset(); s = ws::State{}; recoveries = 3; connected_since = 1000; g_now = 1000 + 31000; run(s);
    connected = false; g_link_failed = true; g_log.clear(); run(s);
    CHECK(recovery_due == g_now + 30000 && s.slow_retries == 0);
  }
  {  // a link up for 5 minutes earns back the budget, not a millisecond sooner
    reset(); ws::State s; recoveries = 2; connected_since = 1000; g_now = 1000 + 299999;
    run(s); CHECK(recoveries == 1);   // one already earned back at 20 s
    g_now = 1000 + 300000; run(s); CHECK(recoveries == 0);
    reset(); recoveries = 2; connected = false; connected_since = 1000; g_now = 400000; run(s); CHECK(recoveries == 2);
    reset(); recoveries = 2; connected_since = 0; g_now = 400000; run(s); CHECK(recoveries == 2);
  }
  {  // recovery due wraps round the clock
    reset(); ws::State s; g_now = 0xFFFFFFF0u; recovery_due = 0x00000010u;
    run(s); CHECK(recovery_due == 0x10u);          // not due yet
    g_now = 0x00000010u; run(s); CHECK(recovery_due == 0);
  }
  {  // health line: only while a remote receiver may need Wi-Fi, once a minute, with the right fields
    reset(); ws::State s; g_remote = true; attempts = 2; recoveries = 1; connected = false; station_ready = false;
    run(s);   // first poll: down_since set, line printed (last_health 0, now large)
    CHECK(g_log.size() == 2 && g_log[0].rfind("W:RTL_WIFI_HEALTH connected=0 ready=0 link_failed=0 reconnect_attempts=2 recoveries=1 down_s=0 dma_free=12345 stream=streaming", 0) == 0);
    g_log.clear(); g_now += 59999; run(s); CHECK_LOG("deferred");
    g_now += 1; g_log.clear(); run(s); CHECK(g_log.size() == 2 && g_log[0].find("down_s=60 ") != std::string::npos);
    reset(); g_remote = false; run(s); CHECK_LOG("deferred");
  }
  {  // down time is exact on an EVEN clock too (the old `millis() | 1` made it 1 ms short, so 60 s read as 59)
    reset(); ws::State s; g_remote = true; connected = false; g_now = 200000;
    run(s); g_log.clear(); g_now += 60000; run(s);
    CHECK(g_log.size() == 2 && g_log[0].find("down_s=60 ") != std::string::npos);
    g_now += 5; connected = true; g_log.clear(); run(s); CHECK(!s.down);   // link back: not down any more
    connected = false; g_now = 0; run(s); CHECK(s.down && s.down_since_ms == 0);   // down at clock 0 still counts as down
  }
  {  // keep-alive: needs remote, the option, being down 5 min, and 5 min since the last re-arm
    reset(); ws::State s; g_remote = true; g_keepalive = true; connected = false; connecting = false; recoveries = 3; attempts = 9;
    s.last_health_ms = g_now;   // keep the health line out of this test
    run(s);                     // down_since = now|1
    g_now += 299999; s.last_health_ms = g_now; g_log.clear(); run(s); CHECK_LOG("deferred");
    g_now += 1; s.last_health_ms = g_now; g_log.clear(); run(s);
    CHECK_LOG("W:RTL_WIFI_KEEPALIVE rearm down_s=300 link_failed=0\n", "schedule:keepalive", "deferred");
    CHECK(recoveries == 0 && attempts == 0);
    g_now += 1000; s.last_health_ms = g_now; g_log.clear(); run(s); CHECK_LOG("deferred");   // not again for 5 min
  }
  {  // keep-alive with the link marked failed runs the C6 recovery again instead of scheduling a reconnect
    reset(); ws::State s; g_remote = true; g_keepalive = true; connected = false; s.link_failure_handled = true; g_link_failed = true;
    s.last_health_ms = 0xFFFFFFFFu;   // no health line
    run(s); g_now += 300000; s.last_health_ms = g_now; g_log.clear();
    run(s);
    CHECK(g_log.size() == 2 && g_log[0] == "W:RTL_WIFI_KEEPALIVE rearm down_s=300 link_failed=1\n" && g_log[1] == "deferred");
    CHECK(!s.link_failure_handled);
  }
  {  // keep-alive off, or connecting, or scanning: nothing
    for (int what = 0; what < 3; ++what) {
      reset(); ws::State s; g_remote = true; g_keepalive = what != 0; connected = false; connecting = what == 1; scan_running = what == 2;
      s.last_health_ms = 1; s.down = true; s.down_since_ms = 1; s.last_rearm_ms = 0; g_now = 1000000; s.last_health_ms = g_now;
      run(s);
      CHECK_LOG("deferred");
    }
  }
  {  // timed reconnect: only when due and idle, with the saved profile, one request, one line
    reset(); ws::State s; connected = false; reconnect_due = 100001; attempts = 4; g_ssid_empty = true;
    CHECK(run(s));
    CHECK_LOG("deferred", "select0", "request_connect", "W:RTL_WIFI_RECONNECT attempt=4\n");
    CHECK(reconnect_due == 0 && !save_after);
    reset(); connected = false; reconnect_due = 100002; run(s); CHECK_LOG("deferred"); CHECK(reconnect_due == 100002);
    reset(); connected = false; reconnect_due = 100001; connecting = true; run(s); CHECK(reconnect_due == 100001);
    reset(); connected = false; reconnect_due = 100001; armed = false; run(s); CHECK_LOG("deferred"); CHECK(reconnect_due == 0);
    reset(); connected = false; reconnect_due = 100001; g_profiles = 0; run(s); CHECK_LOG("deferred"); CHECK(reconnect_due == 0);
    reset(); connected = false; reconnect_due = 100001; g_ssid_empty = false; run(s); CHECK_LOG("deferred", "request_connect", "W:RTL_WIFI_RECONNECT attempt=0\n");
  }
  {  // Connect pressed during a retry wait: fresh budget, recovery runs on this poll
    reset(); ws::State s; g_link_failed = true; recoveries = 3; connected = false;
    run(s); CHECK(recovery_due == g_now + 30000 && s.slow_retries == 1);
    g_now += 10000; attempts = 4;
    CHECK(ws::user_connect_now(s, vars(), true, g_now));
    CHECK(recovery_due == g_now && recoveries == 0 && attempts == 0 && s.slow_retries == 0);
    g_log.clear(); run(s); CHECK_LOG("recover", "deferred");
    // link failed and already handled, nothing pending (a recovery that failed to deinit): due now as well
    reset(); s = ws::State{}; s.link_failure_handled = true; recoveries = 3;
    CHECK(ws::user_connect_now(s, vars(), true, g_now) && recovery_due == g_now && recoveries == 0);
    // link up: only the counters are cleared; the queued connect runs as usual
    reset(); s = ws::State{}; recoveries = 2; s.slow_retries = 3;
    CHECK(!ws::user_connect_now(s, vars(), false, g_now) && recovery_due == 0 && recoveries == 0 && s.slow_retries == 0);
  }
  {  // a pending recovery stops the caller before any reconnect
    reset(); ws::State s; connected = false; reconnect_due = 100001; recovery_due = 200000;
    CHECK(!run(s)); CHECK(reconnect_due == 100001);
  }
  if (g_failures) { std::fprintf(stderr, "%d failure(s)\n", g_failures); return 1; }
  std::puts("PASS wifi_supervisor");
  return 0;
}
