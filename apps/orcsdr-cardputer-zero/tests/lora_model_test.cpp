// Exercises the protocol-neutral LoRa model the dashboard screens read.
// No radio: this is the contract between an adapter and the UI, so it is worth
// pinning down before any adapter is written against it.
#include <cstdio>
#include <cstring>

#include "mesh/lora_model.hpp"

namespace m = orc::zero::mesh;

namespace {
int failures = 0;
void check(bool ok, const char* what) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}
}  // namespace

int main() {
  std::printf("LoRa model\n");
  m::Model model;

  // Two Meshtastic frames from the same sender: the node appears once.
  m::Traffic first;
  first.from = 0xE1297660;
  first.protocol = m::Protocol::meshtastic;
  first.portnum = 3;
  first.has_signal = true;
  first.snr_db = -22;  // stored as tenths-of-dB integer in the caller
  first.rssi_dbm = -92;
  first.hops = 3;
  first.heard_ms = 1000;
  const size_t index = model.observe(first);
  check(model.size() == 1, "first frame creates one node");
  check(model.nodes()[index].packets == 1, "packet counter increments");
  check(std::strcmp(model.nodes()[index].name, "!E1297660") == 0, "fallback name is hex id");

  m::Traffic second = first;
  second.heard_ms = 2000;
  model.observe(second);
  check(model.size() == 1, "a second frame from the same sender does not duplicate");

  // A different protocol reusing the same numeric id is still one node here:
  // ids are protocol-scoped, and making them collide is the adapter's job to
  // avoid by namespacing. Documented by assertion so a future change is noticed.
  m::Traffic other;
  other.from = 0x11223344;
  other.protocol = m::Protocol::meshcore;
  other.heard_ms = 3000;
  model.observe(other);
  check(model.size() == 2, "a different sender adds a node");

  // Positions and links.
  model.observe([] {
    m::Traffic t;
    t.from = 1;
    t.has_position = true;
    t.latitude_e7 = 440521000;
    t.longitude_e7 = -1230868000;
    return t;
  }());
  model.observe([] {
    m::Traffic t;
    t.from = 2;
    t.has_position = true;
    t.latitude_e7 = 440700000;   // ~2 km north
    t.longitude_e7 = -1230868000;
    return t;
  }());
  model.observe([] {
    m::Traffic t;
    t.from = 3;
    t.has_position = true;
    t.latitude_e7 = 450521000;   // ~111 km north
    t.longitude_e7 = -1230868000;
    return t;
  }());

  const auto nearest = model.nearest(44.0521, -123.0868, 2);
  check(nearest.size() == 2, "nearest returns the requested count");

  const auto links = model.links(10.0);
  std::printf("       %zu link(s) within 10 km, %zu within 200 km\n", links.size(),
              model.links(200.0).size());
  check(links.size() >= 1, "nearby pair produces a map link");
  bool all_close = true;
  for (const auto& link : links)
    if (link.km > 10.0) all_close = false;
  check(all_close, "no link exceeds the radius");

  model.name_node(0x11223344, "REPEATER-1");
  check(std::strcmp(model.nodes()[1].name, "REPEATER-1") == 0, "protocol name overrides the hex fallback");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "PASSED", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
