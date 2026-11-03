#include "web_server_state.hpp"
#include <atomic>
#include <cassert>
#include <cstring>
#include <thread>
#include <cstdio>

int main() {
  web_server::State state;
  auto s=state.snapshot();
  assert(!s.enabled&&!s.listening&&!std::strcmp(s.url,"offline"));
  state.set_enabled_after_persist(true,false);
  assert(!state.snapshot().enabled);
  state.set_enabled_after_persist(true,true);
  state.set_enabled_after_persist(false,false);
  assert(state.snapshot().enabled);
  assert(state.claim_manager());
  assert(!state.claim_manager());
  state.manager_creation_failed();
  assert(state.claim_manager());
  assert(!state.claim_manager());
  state.set_listening(true,"192.168.1.174");
  const auto saved=state.snapshot();
  state.set_listening(false);
  assert(saved.listening&&!std::strcmp(saved.url,"http://192.168.1.174/"));
  assert(!state.snapshot().listening&&!std::strcmp(state.snapshot().url,"offline"));
  std::atomic<bool> done{false};
  std::thread writer([&] {
    for(int i=0;i<10000;++i) {
      state.set_listening(true,"192.168.1.174");
      state.set_listening(false);
    }
    done=true;
  });
  do {
    s=state.snapshot();
    assert(s.enabled);
    assert(!std::strcmp(s.url,s.listening?"http://192.168.1.174/":"offline"));
  } while(!done.load());
  writer.join();
  std::puts("P4 web state snapshot/lifecycle/persistence-publication PASS");
}
