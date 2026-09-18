#!/usr/bin/env python3
"""Generate receive-only test vectors with the pinned, independent SoftRF encoder.

Downloads GPL-3.0-or-later reference sources into a temporary directory; never
links the transmit encoder into OrcSDR. Requires a native C++ compiler.
"""
import pathlib
import subprocess
import tempfile
import urllib.request

REV = 'a4c21fc45b251ab2e3b3a543ab69771e63328ef6'
BASE = f'https://raw.githubusercontent.com/lyusupov/SoftRF/{REV}/software/firmware/source/SoftRF/src/protocol/radio/'

def function(source, name):
    start = source.index(name)
    start = source.rfind('\n', 0, start) + 1
    brace = source.index('{', start)
    level = 1
    end = brace + 1
    while level:
        level += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

cpp = urllib.request.urlopen(BASE + 'Legacy.cpp').read().decode()
h = urllib.request.urlopen(BASE + 'Legacy.h').read().decode()
# The original packet layout and encoder are the oracle, not the implementation
# under test. Compile with aliasing disabled as upstream casts packed packets.
constants = h[h.index('#define LEGACY_KEY1'):h.index('/* FTD-12')]
packets = h[h.index('typedef struct {'):h.index('bool   legacy_decode')]
prelude = r'''
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#define PROGMEM
#define pgm_read_byte(p) (*(p))
#define radians(x) ((x)*3.14159265358979323846/180)
#define _GPS_MPS_PER_KNOT 0.514444444444
#define _GPS_FEET_PER_METER 3.280839895
#define AIRCRAFT_TYPE_STATIC 15
#define AIRCRAFT_TYPE_UNKNOWN 0
#define ADDR_TYPE_FLARM 2
#define ADDR_TYPE_ICAO 1
struct ufo_t {
 uint32_t addr=0x123456, timestamp=1788868800;
 uint8_t aircraft_type=1;
 float latitude=46.05f, longitude=14.5f, altitude=1250, geoid_separation=0;
 float course=90, speed=40/_GPS_MPS_PER_KNOT, vs=-2.4*_GPS_FEET_PER_METER*60;
 bool stealth=false, no_track=false;
};
uint8_t parity(uint32_t x) { return __builtin_parity(x); }
'''
source = prelude + constants + packets
start = cpp.index('static const uint8_t legacy_GS_threshold')
source += cpp[start:cpp.index('};', start) + 2] + '\n'
for name in ('void btea(', 'long obscure('): source += function(cpp, name)
source += 'static const uint32_t table[12] = LEGACY_KEY1;\n'
for name in ('void make_v6_key(', 'void make_v7_key(', 'static size_t legacy_v6_encode('): source += function(cpp, name)
start = cpp.index('static const uint16_t lon_div_table')
source += cpp[start:cpp.index('};', start) + 2] + '\n'
for name in ('static unsigned int enscale_unsigned(', 'static unsigned int enscale_signed('): source += function(cpp, name)
# There are two legacy_encode definitions: the full V7 encoder is the last.
source += function(cpp[cpp.rindex('size_t legacy_encode(')-1:], 'size_t legacy_encode(')
source += r'''
int main() {
 static_assert(sizeof(legacy_v6_packet_t)==24 && sizeof(legacy_v7_packet_t)==24);
 for (int location=0; location<4; ++location) {
  ufo_t a;
  if(location==1) { a.latitude=-33.9f; a.longitude=18.4f; a.course=270; }
  if(location==2) { a.latitude=72.1f; a.longitude=179.99f; a.course=359; }
  if(location==3) { a.timestamp=1788868799; a.stealth=true; a.no_track=true; }
  for (int version : {6,7}) {
   alignas(4) uint8_t packet[24]{};
   if(version==6) legacy_v6_encode(packet,&a); else legacy_encode(packet,&a);
   printf("%d %u %.7f %.7f ", version,a.timestamp,a.latitude,a.longitude);
   for(auto b:packet) printf("%02x", b);
   puts("");
  }
 }
}
'''
source = '#include <initializer_list>\n' + source
with tempfile.TemporaryDirectory() as temp:
    path = pathlib.Path(temp)
    (path/'oracle.cpp').write_text(source)
    subprocess.run(['c++','-std=c++17','-O0','-fno-strict-aliasing',str(path/'oracle.cpp'),'-o',str(path/'oracle')],check=True)
    result = subprocess.check_output([str(path/'oracle')], text=True)
output = pathlib.Path(__file__).resolve().parents[1]/'tests/fixtures/flarm_packets.txt'
output.write_text('# SoftRF '+REV+'; GPL-3.0-or-later\n# generation unix_seconds latitude longitude encrypted_payload_hex (CRC appended by test harness)\n'+result)
print(output)
