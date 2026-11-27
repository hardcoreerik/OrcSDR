#include "fcc_lookup_gate.hpp"
#include <cassert>
using namespace orcsdr::fcc;
int main() {
  LookupGate gate;
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
  assert(gate.begin(1,2,1,b));
  assert(!(b==c));
  assert(gate.settle(b,LookupOutcome::partial));
  assert(gate.outcome()==LookupOutcome::partial);
}
