#pragma once
// A small registry of memory that a feature holds on to after the user has left it (the SSTV picture gallery, a recording
// buffer, ...) and can hand back on request. A feature registers what it holds and how to release it; the FT8 screen's
// FREE MEMORY button and the RTL_MEM serial command ask the registry to release everything and report what came back.
//
// The registry owns nothing and allocates nothing. It never frees memory a feature is still using: each release
// callback decides what is safe to give up right now and returns the number of bytes it actually released.
#include <cstddef>
#include <cstdint>

namespace orcsdr::memory_reclaim {

constexpr size_t kMaxHolders = 8;

struct Holder {
  const char* name;                 // short, shown to the user ("SSTV pictures")
  size_t (*held_bytes)();           // how much the feature holds that it could give up now (0 = nothing)
  size_t (*release)();              // gives it up and returns the bytes released
};

// Returns false when the table is full or the holder has no callbacks. Registering the same name twice replaces it.
bool register_holder(const Holder& holder);

struct Report {
  const char* name = "";
  size_t held = 0;                  // bytes held now, or released by reclaim_all()
};

// Fills `out` with what each registered feature holds, largest first. Returns the number written.
size_t list(Report* out, size_t capacity);
size_t total_held();

// Asks every holder to release what it can. `out` (may be null) receives the bytes released by each, largest first.
// Returns the total released.
size_t reclaim_all(Report* out, size_t capacity, size_t* out_count);

void clear_for_tests();

}  // namespace orcsdr::memory_reclaim
