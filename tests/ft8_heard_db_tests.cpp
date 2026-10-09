// Stations-heard table: first/last heard, counts, band/mode masks, SNR, colour-code classes, journal round trip, corruption handling, full table.
#include "ft8_heard_db.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

using namespace orcsdr::ft8;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static HeardObservation obs(const char* call, uint32_t utc, uint8_t band, uint8_t mode, int snr, const char* grid = "") {
  HeardObservation o;
  o.callsign = call;
  o.grid = grid;
  o.utc = utc;
  o.band = band;
  o.mode = mode;
  o.snr = static_cast<int8_t>(snr);
  o.snr_known = true;
  return o;
}

int main() {
  CHECK(heard_db_self_check());
  std::vector<HeardEntry> memory(1024);
  HeardDb db;
  CHECK(db.begin(memory.data(), memory.size() * sizeof(HeardEntry)));
  CHECK(db.capacity() == 1024);

  // Callsign plausibility: rejects junk so a bad decode cannot pollute the table.
  CHECK(!HeardDb::valid_callsign(""));
  CHECK(!HeardDb::valid_callsign("AB"));
  CHECK(!HeardDb::valid_callsign("ABCDEF"));    // no digit
  CHECK(!HeardDb::valid_callsign("123456"));    // no letter
  CHECK(!HeardDb::valid_callsign("w7abc"));     // lower case
  CHECK(HeardDb::valid_callsign("W7ABC"));
  CHECK(HeardDb::valid_callsign("VE3/W7ABC"));
  CHECK(!db.observe(obs("junk", 100, 3, 0, -5)).stored);

  // First sight, then again: the class reports what was known BEFORE the observation.
  HeardUpdate u = db.observe(obs("K8IMT", 1000, 3, 2, -6, "EN82"));
  CHECK(u.stored && u.before == HeardClass::first_time && u.new_band && u.new_mode);
  CHECK(u.entry->first_utc == 1000 && u.entry->last_utc == 1000 && u.entry->count == 1);
  u = db.observe(obs("K8IMT", 2000, 3, 2, -2));
  CHECK(u.before == HeardClass::heard_before && !u.new_band && !u.new_mode);
  CHECK(u.entry->first_utc == 1000 && u.entry->last_utc == 2000 && u.entry->count == 2);
  CHECK(u.entry->best_snr == -2 && u.entry->last_snr == -2);
  u = db.observe(obs("K8IMT", 3000, 5, 0, -20));
  CHECK(u.new_band && u.new_mode);
  CHECK(u.entry->best_snr == -2 && u.entry->last_snr == -20);          // best stays, last follows
  CHECK(u.entry->band_mask == ((1u << 3) | (1u << 5)) && u.entry->mode_mask == ((1u << 2) | 1u));
  CHECK(std::strcmp(u.entry->grid, "EN82") == 0);                       // an empty grid never erases the known one

  // An out-of-order older observation moves first-heard back but not last-heard.
  u = db.observe(obs("K8IMT", 500, 3, 2, -9));
  CHECK(u.entry->first_utc == 500 && u.entry->last_utc == 3000);

  // Worked stations get their own class.
  CHECK(db.mark_worked("K8IMT"));
  CHECK(!db.mark_worked("W1AW"));
  CHECK(db.observe(obs("K8IMT", 4000, 3, 2, -1)).before == HeardClass::worked);

  // Unknown-SNR observations do not invent a number.
  HeardObservation no_snr = obs("WO7I", 100, 3, 2, 0);
  no_snr.snr_known = false;
  u = db.observe(no_snr);
  CHECK(!(u.entry->flags & heard_flag_snr_known));

  // Journal round trip: dirty entries serialise once, a fresh table replays them identically.
  std::vector<uint8_t> journal(kHeardHeaderBytes);
  HeardDb::journal_header(journal.data());
  db.observe(obs("KD7WPQ", 9000, 5, 2, -9, "DN15"));
  uint8_t chunk[8 * kHeardRecordBytes];
  size_t drained = db.drain_dirty(chunk, 8);
  CHECK(drained == 3);                                                   // K8IMT, WO7I, KD7WPQ
  journal.insert(journal.end(), chunk, chunk + drained * kHeardRecordBytes);
  CHECK(db.drain_dirty(chunk, 8) == 0);                                  // nothing changed since
  db.observe(obs("K8IMT", 5000, 3, 2, 0));                               // update again: a later record must win
  drained = db.drain_dirty(chunk, 8);
  CHECK(drained == 1);
  journal.insert(journal.end(), chunk, chunk + drained * kHeardRecordBytes);

  std::vector<HeardEntry> memory2(1024);
  HeardDb copy;
  CHECK(copy.begin(memory2.data(), memory2.size() * sizeof(HeardEntry)));
  CHECK(copy.load(journal.data(), journal.size()) == 4);
  CHECK(copy.size() == 3 && copy.rejected() == 0);
  const HeardEntry* k = copy.find("K8IMT");
  CHECK(k != nullptr && k->count == db.find("K8IMT")->count && k->last_utc == 5000 && k->first_utc == 500 && (k->flags & heard_flag_worked));
  CHECK(copy.find("KD7WPQ") != nullptr && std::strcmp(copy.find("KD7WPQ")->grid, "DN15") == 0);
  CHECK(copy.drain_dirty(chunk, 8) == 0);                                // loaded data is not re-written

  // Corruption: a flipped byte rejects that record only; a torn tail is ignored.
  std::vector<uint8_t> damaged = journal;
  damaged[kHeardHeaderBytes + 5] ^= 0x40;
  std::vector<HeardEntry> memory3(1024);
  HeardDb partial;
  CHECK(partial.begin(memory3.data(), memory3.size() * sizeof(HeardEntry)));
  CHECK(partial.load(damaged.data(), damaged.size() - 7) == 2);          // one bad record, last record truncated
  CHECK(partial.rejected() == 1);
  CHECK(partial.find("WO7I") != nullptr);

  // Unknown journal version is refused, not guessed.
  std::vector<uint8_t> future = journal;
  future[8] = 9;
  std::vector<HeardEntry> memory4(1024);
  HeardDb refused;
  CHECK(refused.begin(memory4.data(), memory4.size() * sizeof(HeardEntry)));
  CHECK(refused.load(future.data(), future.size()) == 0 && refused.size() == 0);

  // A full table keeps updating known stations and drops new ones instead of looping or overwriting.
  std::vector<HeardEntry> small(16);
  HeardDb tiny;
  CHECK(tiny.begin(small.data(), small.size() * sizeof(HeardEntry)));
  char call[16];
  size_t stored = 0;
  for (int i = 0; i < 40; ++i) {
    std::snprintf(call, sizeof(call), "W%dAA", i % 10);
    call[4] = static_cast<char>('A' + (i / 10));
    call[5] = '\0';
    if (tiny.observe(obs(call, 100 + i, 0, 0, -10)).stored) ++stored;
  }
  CHECK(tiny.size() <= 12);                                              // 3/4 of 16
  CHECK(tiny.dropped() > 0 && stored >= tiny.size());
  CHECK(tiny.observe(obs("W0AAA", 999, 0, 0, -3)).stored);                // a known station still updates

  if (failures == 0) std::printf("ft8_heard_db_tests OK (journal %zu bytes, %zu stations)\n", journal.size(), copy.size());
  return failures == 0 ? 0 : 1;
}
