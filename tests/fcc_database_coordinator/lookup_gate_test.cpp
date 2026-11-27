#include "fcc_lookup_gate.hpp"
#include "fcc_package_admission.hpp"
#include <cassert>
#include <type_traits>
using namespace orcsdr::fcc;
int main() {
  static_assert(!std::is_copy_constructible<LookupGate>::value);
  LookupTicketSource tickets;
  LookupGate gate(tickets);
  LookupKey a,b,c;
  assert(gate.outcome()==LookupOutcome::unavailable);
  assert(!gate.begin(0,1,1,a));
  assert(!gate.begin(1,0,1,a));
  assert(gate.begin(1,1,0,a)); // unknown receiver location
  assert(!gate.settle(a,LookupOutcome::pending));
  assert(gate.begin(1,2,0,b));
  assert(!gate.settle(a,LookupOutcome::candidates));
  assert(!gate.settle({b.ticket,2,2,0},LookupOutcome::candidates));
  assert(!gate.settle({b.ticket,1,2,1},LookupOutcome::candidates));
  assert(gate.settle(b,LookupOutcome::no_match));
  assert(!gate.settle(b,LookupOutcome::candidates));
  assert(gate.begin(1,2,1,c));
  gate.invalidate();
  assert(!gate.settle(c,LookupOutcome::candidates));
  // A new gate for the same observation after screen recreation cannot
  // accept a delayed result for the old gate.
  LookupGate recreated(tickets);
  assert(recreated.begin(1,2,1,b));
  assert(!(b==c));
  assert(!recreated.settle(c,LookupOutcome::candidates));
  assert(recreated.settle(b,LookupOutcome::partial));
  assert(recreated.outcome()==LookupOutcome::partial);

  InstallInputs i{};
  assert(admit_install(i).admission==InstallAdmission::reception_active);
  i.busy=true; assert(admit_install(i).admission==InstallAdmission::busy);
  i.busy=false; i.reception_idle=true;
  assert(admit_install(i).admission==InstallAdmission::no_sd);
  i.sd_ready=true;
  assert(admit_install(i).admission==InstallAdmission::no_authenticated_offer);
  i.authenticated_offer=true;
  assert(admit_install(i).admission==InstallAdmission::size_unknown);
  i.sizes_known=true; i.archive_bytes=UINT64_C(5000000000); i.staging_bytes=UINT64_C(7000000000);
  i.reserve_bytes=100;
  assert(admit_install(i).admission==InstallAdmission::space_unknown);
  i.free_known=true; i.free_bytes=UINT64_C(12000000099);
  assert(admit_install(i).admission==InstallAdmission::insufficient_space);
  i.free_bytes++;
  assert(admit_install(i).admission==InstallAdmission::ready);
  assert(admit_install(i).required_bytes==UINT64_C(12000000100));
  i.archive_bytes=UINT64_MAX;
  assert(admit_install(i).admission==InstallAdmission::size_overflow);
  i.archive_bytes=0; i.staging_bytes=UINT64_MAX;
  assert(admit_install(i).admission==InstallAdmission::size_overflow);
  InstalledGeneration g{true,true,true,7};
  assert(g.usable()); // failure of an offer/admission leaves local generation usable
  g.sd_ready=false; assert(!g.usable());
  g.sd_ready=true; g.format_supported=false; assert(!g.usable());
  g.format_supported=true; g.generation=0; assert(!g.usable());
}
