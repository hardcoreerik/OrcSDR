// D1-S CACH cadence model: synthetic BS-sync event inputs, no radio proof.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr/cach_cadence.hpp"
#include <cstdint>
#include <cstdio>
using namespace orcsdr::dmr::d1;
#define CHECK(c) do {if(!(c)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c);return 1;}}while(false)
int main() {
 CachCadence c;
 auto x=c.observe(12,true,true,1,0);CHECK(x.confidence==CachConfidence::tentative&&x.cadence_bursts==1);
 x=c.observe(156,true,true,2,0);CHECK(x.confidence==CachConfidence::tentative&&x.cadence_bursts==2);
 x=c.observe(300,true,true,1,0);CHECK(x.confidence==CachConfidence::tentative&&x.cadence_bursts==3);
 x=c.observe(444,true,true,2,0);CHECK(x.confidence==CachConfidence::cadence_validated&&x.candidate_slot==2);
 x=c.observe(588,true,true,1,0);CHECK(x.confidence==CachConfidence::cadence_validated);
 x=c.observe(732,true,true,2,1);CHECK(x.confidence==CachConfidence::tentative&&x.cadence_bursts==0);
 x=c.observe(876,true,true,1,0);CHECK(x.confidence==CachConfidence::tentative&&x.cadence_bursts==1);
 x=c.observe(1020,true,true,1,0);CHECK(x.confidence==CachConfidence::tentative&&x.cadence_bursts==1);
 x=c.observe(1164,false,true,2,0);CHECK(x.confidence==CachConfidence::none);
 x=c.observe(1308,true,true,1,0);CHECK(x.cadence_bursts==1);
 x=c.observe(1453,true,true,2,0);CHECK(x.cadence_bursts==1); // off by one
 x=c.observe(1597,true,true,1,0);CHECK(x.cadence_bursts==2);
 x=c.observe(1741,true,false,2,0);CHECK(x.confidence==CachConfidence::none);
 x=c.observe(1885,true,true,1,0);CHECK(x.cadence_bursts==1);
 c.reset();x=c.observe(0,true,true,2,0);CHECK(x.cadence_bursts==1);
 std::puts("PASS D1-S CACH cadence and confidence");return 0;
}
