#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>

#include "airband_catalog.hpp"

namespace {

[[noreturn]] void fail(const char* expression, int line) {
  std::fprintf(stderr, "FAIL line=%d check=%s\n", line, expression);
  std::exit(1);
}
#define CHECK(expression) do { if (!(expression)) fail(#expression, __LINE__); } while (false)

// Feeds a fixed text buffer to the catalog, one line at a time.
class StringSource final : public orcsdr::airband::LineSource {
 public:
  explicit StringSource(std::string text) : text_(std::move(text)) {}
  int read_line(char* buffer, size_t capacity) override {
    if (capacity == 0 || position_ >= text_.size()) return -1;
    size_t length = 0;
    while (position_ < text_.size() && text_[position_] != '\n') {
      if (length + 1 < capacity) buffer[length++] = text_[position_];
      ++position_;
    }
    if (position_ < text_.size()) ++position_;  // consume the newline
    buffer[length] = '\0';
    return static_cast<int>(length);
  }

 private:
  std::string text_;
  size_t position_ = 0;
};

// Same as StringSource but counts how many lines were requested.
class CountingSource final : public orcsdr::airband::LineSource {
 public:
  explicit CountingSource(std::string text) : inner_(std::move(text)) {}
  int read_line(char* buffer, size_t capacity) override {
    ++lines_;
    return inner_.read_line(buffer, capacity);
  }
  size_t lines_read() const { return lines_; }

 private:
  StringSource inner_;
  size_t lines_ = 0;
};

// ORCAIR2 row: COM lat lon hz service country ident airport callsign source_class source label
std::string row(int32_t lat_e7, int32_t lon_e7, uint32_t hz, const char* service,
                const char* country, const char* ident, const char* airport,
                const char* source_class, const char* label) {
  char buffer[400];
  std::snprintf(buffer, sizeof(buffer), "COM\t%d\t%d\t%u\t%s\t%s\t%s\t%s\t\t%s\tOURAIRPORTS\t%s\n",
                lat_e7, lon_e7, hz, service, country, ident, airport, source_class, label);
  return buffer;
}

constexpr int32_t kEugLat = 441246000, kEugLon = -1232119000;   // Eugene, OR area
constexpr int32_t kPdxLat = 455887000, kPdxLon = -1225975000;   // Portland, OR area
constexpr int32_t kSinLat = 13644400, kSinLon = 1039940000;     // Singapore area

void test_distance() {
  using namespace orcsdr::airband;
  CHECK(distance_nm(kEugLat, kEugLon, kEugLat, kEugLon) < 0.01f);
  // Eugene to Portland is roughly 91 nautical miles.
  const float eug_pdx = distance_nm(kEugLat, kEugLon, kPdxLat, kPdxLon);
  CHECK(eug_pdx > 88.0f && eug_pdx < 95.0f);
  CHECK(std::fabs(eug_pdx - distance_nm(kPdxLat, kPdxLon, kEugLat, kEugLon)) < 0.01f);
  // One degree of latitude is 60 nautical miles.
  const float degree = distance_nm(0, 0, 10000000, 0);
  CHECK(degree > 59.8f && degree < 60.4f);
  // Antipodal-ish points must not produce NaN.
  CHECK(std::isfinite(distance_nm(0, 0, 0, 1800000000)));
  // Far-apart longitudes must not overflow 32-bit arithmetic (Eugene to Singapore ~ 7,000 nm).
  const float eug_sin = distance_nm(kEugLat, kEugLon, kSinLat, kSinLon);
  CHECK(eug_sin > 6500.0f && eug_sin < 7500.0f);
  // Across the antimeridian the short way round is used.
  const float across = distance_nm(0, 1790000000, 0, -1790000000);
  CHECK(across > 110.0f && across < 130.0f);
}

void test_parse_and_no_location() {
  using namespace orcsdr::airband;
  std::string text = "ORCAIR2\n";
  text += row(kEugLat, kEugLon, 118900000, "TOWER", "US", "KEUG", "Mahlon Sweet Field", "OFFICIAL", "KEUG TOWER");
  text += row(kSinLat, kSinLon, 118600000, "TOWER", "SG", "WSSS", "Changi", "COMMUNITY", "WSSS TOWER");
  StringSource source(text);
  Catalog catalog;
  CHECK(catalog.load_from(source, Location{}));
  CHECK(catalog.loaded());
  CHECK(catalog.global_schema());
  CHECK(!catalog.location_configured());
  CHECK(catalog.count() == 2);
  // Without a receiver location nothing is presented as "nearby" and no identity is claimed.
  BankEntry bank[8]{};
  CHECK(catalog.make_bank(bank, 8) == 0);
  CHECK(catalog.match(118900000) == nullptr);
}

void test_location_filter_and_provenance() {
  using namespace orcsdr::airband;
  std::string text = "ORCAIR2\n";
  text += row(kEugLat, kEugLon, 118900000, "TOWER", "US", "KEUG", "Mahlon Sweet Field", "OFFICIAL", "KEUG TOWER");
  text += row(kPdxLat, kPdxLon, 118700000, "TOWER", "US", "KPDX", "Portland Intl", "OFFICIAL", "KPDX TOWER");
  text += row(kSinLat, kSinLon, 118600000, "TOWER", "SG", "WSSS", "Changi", "COMMUNITY", "WSSS TOWER");
  StringSource source(text);
  Catalog catalog;
  CHECK(catalog.load_from(source, Location{true, kEugLat, kEugLon}));
  CHECK(catalog.location_configured());
  CHECK(catalog.count() == 3);
  // Sorted nearest first.
  CHECK(std::strcmp(catalog.entry(0)->airport_ident, "KEUG") == 0);
  CHECK(std::strcmp(catalog.entry(1)->airport_ident, "KPDX") == 0);
  CHECK(std::strcmp(catalog.entry(2)->airport_ident, "WSSS") == 0);
  CHECK(catalog.entry(0)->distance_nm < 1.0f);
  CHECK(catalog.entry(0)->source_class == SourceClass::official);
  CHECK(catalog.entry(2)->source_class == SourceClass::community);
  CHECK(catalog.entry(0)->service == Service::tower);
  BankEntry bank[8]{};
  const size_t bank_count = catalog.make_bank(bank, 8);
  CHECK(bank_count == 3);
  CHECK(bank[0].frequency_hz == 118900000);
  // A match within 5 kHz is a database label; further away there is none.
  CHECK(catalog.match(118902000) != nullptr);
  CHECK(catalog.match(119500000) == nullptr);
}

void test_capacity_keeps_nearest() {
  using namespace orcsdr::airband;
  std::string text = "ORCAIR2\n";
  // 100 stations spread north from the receiver; only the nearest kCapacity may be kept.
  for (int i = 0; i < 100; ++i)
    text += row(kEugLat + i * 5000000, kEugLon, 118000000 + static_cast<uint32_t>(i) * 25000,
                "TOWER", "US", "TST", "Test Field", "USER", "TEST TOWER");
  StringSource source(text);
  Catalog catalog;
  CHECK(catalog.load_from(source, Location{true, kEugLat, kEugLon}));
  CHECK(catalog.count() == Catalog::kCapacity);
  CHECK(catalog.entry(0)->frequency_hz == 118000000);
  for (size_t i = 1; i < catalog.count(); ++i)
    CHECK(catalog.entry(i - 1)->distance_nm <= catalog.entry(i)->distance_nm);
  CHECK(catalog.entry(catalog.count() - 1)->frequency_hz ==
        118000000 + (Catalog::kCapacity - 1) * 25000u);
}

void test_malformed_records_are_skipped() {
  using namespace orcsdr::airband;
  std::string text = "ORCAIR2\n";
  text += "COM\t1\t2\n";                                              // too few fields
  text += "XXX\t441246000\t-1232119000\t118900000\tTOWER\tUS\tK\tA\t\tOFFICIAL\tS\tL\n";  // bad tag
  text += row(kEugLat, kEugLon, 99000000, "TOWER", "US", "K", "A", "OFFICIAL", "OUT OF BAND");
  text += row(kEugLat, kEugLon, 140000000, "TOWER", "US", "K", "A", "OFFICIAL", "OUT OF BAND");
  text += row(999999999, kEugLon, 118900000, "TOWER", "US", "K", "A", "OFFICIAL", "BAD LAT");
  text += "COM\tabc\t-1232119000\t118900000\tTOWER\tUS\tK\tA\t\tOFFICIAL\tS\tL\n";  // non-numeric
  text += "COM\t441246000\t-1232119000\t-5\tTOWER\tUS\tK\tA\t\tOFFICIAL\tS\tL\n";     // negative freq
  text += "\n";                                                        // blank line must not end the file
  text += row(kEugLat, kEugLon, 118900000, "TOWER", "US", "KEUG", "Field", "OFFICIAL", "GOOD ROW");
  StringSource source(text);
  Catalog catalog;
  CHECK(catalog.load_from(source, Location{true, kEugLat, kEugLon}));
  CHECK(catalog.count() == 1);
  CHECK(std::strcmp(catalog.entry(0)->label, "GOOD ROW") == 0);
}

void test_bad_headers_and_empty_input() {
  using namespace orcsdr::airband;
  Catalog catalog;
  StringSource empty("");
  CHECK(!catalog.load_from(empty, Location{}));
  CHECK(!catalog.loaded());
  StringSource wrong("NOTACATALOG\nCOM\t1\n");
  CHECK(!catalog.load_from(wrong, Location{}));
  CHECK(!catalog.loaded());
  CHECK(catalog.count() == 0);
  StringSource header_only("ORCAIR2\n");
  CHECK(!catalog.load_from(header_only, Location{true, kEugLat, kEugLon}));
  CHECK(catalog.count() == 0);
  CHECK(!catalog.loaded());
  // Loading a good file after a failed one must fully replace the state.
  StringSource good("ORCAIR2\n" + row(kEugLat, kEugLon, 118900000, "TOWER", "US", "KEUG", "F", "OFFICIAL", "KEUG TOWER"));
  CHECK(catalog.load_from(good, Location{true, kEugLat, kEugLon}));
  CHECK(catalog.count() == 1);
}

void test_crlf_and_legacy_format() {
  using namespace orcsdr::airband;
  std::string crlf = "ORCAIR2\r\n" + row(kEugLat, kEugLon, 118900000, "TOWER", "US", "KEUG", "F", "OFFICIAL", "KEUG TOWER");
  // Convert the row terminators to CRLF.
  std::string converted;
  for (char c : crlf) { if (c == '\n' && (converted.empty() || converted.back() != '\r')) converted += '\r'; converted += c; }
  StringSource crlf_source(converted);
  Catalog catalog;
  CHECK(catalog.load_from(crlf_source, Location{true, kEugLat, kEugLon}));
  CHECK(catalog.count() == 1);
  CHECK(std::strcmp(catalog.entry(0)->label, "KEUG TOWER") == 0);

  // Legacy FAA-only ORCCAT1 index is still accepted and labelled as official FAA data.
  StringSource legacy("ORCCAT1\nATC 441246000 -1232119000 124150000 KEUG TOWER\n");
  Catalog old_catalog;
  CHECK(old_catalog.load_from(legacy, Location{true, kEugLat, kEugLon}));
  CHECK(!old_catalog.global_schema());
  CHECK(old_catalog.count() == 1);
  CHECK(old_catalog.entry(0)->frequency_hz == 124150000);
  CHECK(old_catalog.entry(0)->source_class == SourceClass::official);
  CHECK(std::strcmp(old_catalog.entry(0)->source, "FAA") == 0);
  CHECK(old_catalog.entry(0)->service == Service::tower);
}

void test_radius_limits_nearby_entries() {
  using namespace orcsdr::airband;
  std::string text = "ORCAIR2\n";
  text += row(kEugLat, kEugLon, 118900000, "TOWER", "US", "KEUG", "Field", "OFFICIAL", "KEUG TOWER");
  text += row(kPdxLat, kPdxLon, 118700000, "TOWER", "US", "KPDX", "Portland", "OFFICIAL", "KPDX TOWER");
  text += row(kSinLat, kSinLon, 118600000, "TOWER", "SG", "WSSS", "Changi", "COMMUNITY", "WSSS TOWER");
  {
    StringSource source(text);
    Catalog catalog;
    Location near{true, kEugLat, kEugLon};
    near.radius_nm = 50;   // Portland (~91 nm) and Singapore are outside
    CHECK(catalog.load_from(source, near));
    CHECK(catalog.count() == 1);
    CHECK(std::strcmp(catalog.entry(0)->airport_ident, "KEUG") == 0);
  }
  {
    StringSource source(text);
    Catalog catalog;
    Location wider{true, kEugLat, kEugLon};
    wider.radius_nm = 100;
    CHECK(catalog.load_from(source, wider));
    CHECK(catalog.count() == 2);
  }
  {
    StringSource source(text);
    Catalog catalog;
    Location any{true, kEugLat, kEugLon};
    any.radius_nm = 0;     // no limit
    CHECK(catalog.load_from(source, any));
    CHECK(catalog.count() == 3);
  }
  {
    // Nothing inside the radius: not loaded, and no bank is offered.
    StringSource source(text);
    Catalog catalog;
    Location remote{true, 0, 0};
    remote.radius_nm = 25;
    CHECK(!catalog.load_from(source, remote));
    BankEntry bank[4]{};
    CHECK(catalog.make_bank(bank, 4) == 0);
  }
}

void test_unsupported_schema_bails_early() {
  using namespace orcsdr::airband;
  // The installed FAA record-index pack has an ORCCAT1 header but JSON rows without coordinates.
  std::string text = "ORCCAT1\n{\"schema\":\"orcsdr-record-index-v1\"}\n";
  for (int i = 0; i < 5000; ++i)
    text += "[\"00A\",\"TOTAL RF\",\"BENSALEM\",\"PA\",\"122.9\",\"CTAF\",\"\",\"\"]\n";
  CountingSource source(text);
  Catalog catalog;
  CHECK(!catalog.load_from(source, Location{true, kEugLat, kEugLon}));
  CHECK(catalog.last_result() == LoadResult::unsupported_rows);
  CHECK(!catalog.loaded());
  // It must stop after the probe rows instead of reading all 5,000.
  CHECK(source.lines_read() < 200);
}

void test_load_results() {
  using namespace orcsdr::airband;
  Catalog catalog;
  StringSource wrong("NOTACATALOG\n");
  CHECK(!catalog.load_from(wrong, Location{}));
  CHECK(catalog.last_result() == LoadResult::bad_header);
  StringSource good("ORCAIR2\n" + row(kEugLat, kEugLon, 118900000, "TOWER", "US", "KEUG", "F", "OFFICIAL", "KEUG TOWER"));
  CHECK(catalog.load_from(good, Location{true, kEugLat, kEugLon}));
  CHECK(catalog.last_result() == LoadResult::ok);
  StringSource far("ORCAIR2\n" + row(kSinLat, kSinLon, 118600000, "TOWER", "SG", "WSSS", "Changi", "COMMUNITY", "WSSS TOWER"));
  Location near_eugene{true, kEugLat, kEugLon};
  near_eugene.radius_nm = 25;
  CHECK(!catalog.load_from(far, near_eugene));
  CHECK(catalog.last_result() == LoadResult::no_matching_entries);
  CHECK(std::strcmp(load_result_name(LoadResult::no_source), "no_source") == 0);
}

void test_duplicates_collapse() {
  using namespace orcsdr::airband;
  std::string text = "ORCAIR2\n";
  const std::string same = row(kEugLat, kEugLon, 118900000, "TOWER", "US", "KEUG", "F", "OFFICIAL", "KEUG TOWER");
  text += same + same + same;
  StringSource source(text);
  Catalog catalog;
  CHECK(catalog.load_from(source, Location{true, kEugLat, kEugLon}));
  CHECK(catalog.count() == 1);
}

}  // namespace

int main() {
  test_distance();
  test_parse_and_no_location();
  test_location_filter_and_provenance();
  test_capacity_keeps_nearest();
  test_malformed_records_are_skipped();
  test_bad_headers_and_empty_input();
  test_crlf_and_legacy_format();
  test_duplicates_collapse();
  test_unsupported_schema_bails_early();
  test_load_results();
  test_radius_limits_nearby_entries();
  CHECK(orcsdr::airband::Catalog::self_check());
  std::puts("airband_catalog_tests: PASS");
  return 0;
}
