// Stations-heard table: first/last heard, counts, band/mode masks, SNR, colour-code classes, journal round trip, corruption handling, full table.
#include "ft8_heard_db.hpp"

#include <algorithm>
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

  // Replay does not depend on record order: an older snapshot after a newer one cannot lose first-heard, bands, modes or the best SNR.
  {
    std::vector<HeardEntry> memory5(1024);
    HeardDb ordered, shuffled;
    CHECK(ordered.begin(memory5.data(), memory5.size() * sizeof(HeardEntry)));
    std::vector<HeardEntry> memory6(1024);
    CHECK(shuffled.begin(memory6.data(), memory6.size() * sizeof(HeardEntry)));
    HeardDb source;
    std::vector<HeardEntry> memory7(1024);
    CHECK(source.begin(memory7.data(), memory7.size() * sizeof(HeardEntry)));
    uint8_t snap[3][kHeardRecordBytes];
    source.observe(obs("N7EAL", 1000, 3, 0, -15, "CN87"));
    CHECK(source.drain_dirty(snap[0], 1) == 1);
    source.observe(obs("N7EAL", 2000, 5, 2, -4, ""));
    CHECK(source.drain_dirty(snap[1], 1) == 1);
    source.observe(obs("N7EAL", 3000, 3, 0, -9, ""));
    CHECK(source.drain_dirty(snap[2], 1) == 1);
    for (int i = 0; i < 3; ++i) CHECK(ordered.load(snap[i], kHeardRecordBytes) == 1);
    for (int i = 2; i >= 0; --i) CHECK(shuffled.load(snap[i], kHeardRecordBytes) == 1);   // newest first, oldest last
    const HeardEntry* a = ordered.find("N7EAL");
    const HeardEntry* b = shuffled.find("N7EAL");
    CHECK(a != nullptr && b != nullptr);
    CHECK(a->first_utc == b->first_utc && a->first_utc == 1000 && a->last_utc == b->last_utc && a->last_utc == 3000);
    CHECK(a->count == b->count && a->count == 3 && a->band_mask == b->band_mask && a->band_mask == ((1u << 3) | (1u << 5)));
    CHECK(a->mode_mask == b->mode_mask && a->best_snr == b->best_snr && a->best_snr == -4);
    CHECK(a->last_snr == b->last_snr && a->last_snr == -9);                    // last SNR follows the newest snapshot, whatever the order
    CHECK(std::strcmp(a->grid, "CN87") == 0 && std::strcmp(b->grid, "CN87") == 0);
  }

  // Replay through a reader that returns short reads (the way a file read can at a sector boundary): every record must still load, whatever the read sizes.
  {
    struct Reader {
      const std::vector<uint8_t>* data;
      size_t pos = 0;
      size_t call = 0;
      static size_t read(void* context, uint8_t* out, size_t max_bytes) {
        Reader* r = static_cast<Reader*>(context);
        static const size_t kSizes[] = {1, 7, 13, 32, 40, 41, 3, 512};   // deliberately not multiples of the 40-byte record
        size_t want = kSizes[r->call++ % (sizeof(kSizes) / sizeof(kSizes[0]))];
        want = std::min(want, std::min(max_bytes, r->data->size() - r->pos));
        std::memcpy(out, r->data->data() + r->pos, want);
        r->pos += want;
        return want;
      }
    };
    std::vector<HeardEntry> memory8(1024);
    HeardDb streamed;
    CHECK(streamed.begin(memory8.data(), memory8.size() * sizeof(HeardEntry)));
    Reader reader{&journal};
    uint8_t scratch[200];   // five records: smaller than the reads above can add up to, so the carry path is exercised
    const HeardDb::ReplayResult result = streamed.replay(Reader::read, &reader, scratch, sizeof(scratch));
    CHECK(result.recognised && result.applied == 4);                            // the same four records the one-shot load applied
    CHECK(streamed.size() == 3 && streamed.rejected() == 0);
    CHECK(streamed.find("K8IMT") != nullptr && streamed.find("K8IMT")->last_utc == 5000 && streamed.find("KD7WPQ") != nullptr);
    // A journal that does not start with our header is reported, not guessed at.
    std::vector<uint8_t> foreign(journal);
    foreign[0] = 'X';
    std::vector<HeardEntry> memory9(1024);
    HeardDb refused2;
    CHECK(refused2.begin(memory9.data(), memory9.size() * sizeof(HeardEntry)));
    Reader foreign_reader{&foreign};
    const HeardDb::ReplayResult rejected = refused2.replay(Reader::read, &foreign_reader, scratch, sizeof(scratch));
    CHECK(!rejected.recognised && rejected.applied == 0 && refused2.size() == 0);
  }

  // A failed or short write must not lose a save: records handed back with requeue() go out with the next drain.
  {
    std::vector<HeardEntry> memory10(1024);
    HeardDb d;
    CHECK(d.begin(memory10.data(), memory10.size() * sizeof(HeardEntry)));
    d.observe(obs("WD5EED", 100, 3, 2, -18, "EM12"));
    d.observe(obs("KS1DMD", 200, 3, 2, -16, ""));
    uint8_t batch[4 * kHeardRecordBytes];
    CHECK(d.drain_dirty(batch, 4) == 2);
    CHECK(d.drain_dirty(batch, 4) == 0);                                       // the mark is cleared by the copy...
    CHECK(d.requeue(batch, 2) == 2);                                           // ...and restored when the append did not happen
    CHECK(d.drain_dirty(batch, 4) == 2);
    uint8_t garbage[kHeardRecordBytes] = {0};
    CHECK(d.requeue(garbage, 1) == 0);                                         // a padded or damaged fragment marks nothing
    uint8_t unknown[kHeardRecordBytes] = {0};
    std::memcpy(unknown, "ZZ9ZZZ", 6);
    CHECK(d.requeue(unknown, 1) == 0);                                         // a callsign the table does not hold marks nothing
    CHECK(d.drain_dirty(batch, 4) == 0);
  }

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
