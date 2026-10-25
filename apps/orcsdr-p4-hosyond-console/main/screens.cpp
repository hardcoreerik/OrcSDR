#include "screens.hpp"
#include <cctype>
#include <cstring>

namespace screens {
namespace {
const Screen* const* table(size_t* count) {
  static const Screen* const kAll[] = {&fm_screen(), &am_screen(), &cb_screen(), &airband_screen(), &shortwave_screen(), &rflab_screen(), &settings_screen()};
  *count = sizeof(kAll) / sizeof(kAll[0]);
  return kAll;
}
}  // namespace

const Screen* const* all(size_t* count) { return table(count); }

const Screen* find(orcsdr::dashboards::Id id) {
  size_t n = 0;
  const Screen* const* t = table(&n);
  for (size_t i = 0; i < n; ++i)
    if (t[i]->id == id) return t[i];
  return nullptr;
}

const Screen* find_name(const char* name) {
  size_t n = 0;
  const Screen* const* t = table(&n);
  for (size_t i = 0; i < n; ++i) {
    const char* a = t[i]->name;
    const char* b = name;
    while (*a && *b && std::toupper(static_cast<unsigned char>(*a)) == std::toupper(static_cast<unsigned char>(*b))) ++a, ++b;
    if (!*a && !*b) return t[i];
  }
  return nullptr;
}
}  // namespace screens
