#include "../apps/orcsdr-tab5/ui/dashboard_registry.hpp"
#include <cassert>
#include <cstdio>
int main(){
  using namespace orcsdr::dashboards;
  assert(self_check());
  assert(count()==20);
  const Descriptor* d=find(Id::hfdl);
  assert(d && d->id==Id::hfdl && d->category==Category::aviation);
  // Explicitly not a live receiver/browser action while real frame decode is absent.
  assert(!d->available);
  std::puts("hfdl_registry_test PASS (lab entry disabled until real frames)");
}
