#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

// Flicker control for periodic dashboard refreshes.
//
// The Tab5 panel scans its framebuffer continuously, so clearing a widget and
// drawing it again is visible as a flash whenever the scan lands in between.
// A dashboard that repaints on a timer should keep one RedrawGuard per widget,
// fold that widget's inputs into a Signature, and repaint only when it changed.
// Invalidate the guards whenever the static layout is redrawn.
namespace orcsdr::ui {

class Signature {
 public:
  // Any integer or bool (uint32_t is unsigned long on this RISC-V toolchain,
  // so fixed overloads are ambiguous for plain int/unsigned).
  template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
  Signature& add(T value) {
    const uint64_t bits = static_cast<uint64_t>(value);
    for (size_t i = 0; i < sizeof(T); ++i) mix(static_cast<uint8_t>(bits >> (8 * i)));
    return *this;
  }
  Signature& add(const char* value) {
    for (const char* p = value ? value : ""; *p; ++p) mix(static_cast<uint8_t>(*p));
    mix(0);
    return *this;
  }
  uint32_t value() const { return hash_; }

 private:
  void mix(uint8_t byte) { hash_ = (hash_ ^ byte) * 16777619u; }  // FNV-1a
  uint32_t hash_ = 2166136261u;
};

class RedrawGuard {
 public:
  // True when the widget must repaint: first use, after invalidate(), or when
  // its inputs changed since the last repaint.
  bool changed(const Signature& signature) {
    if (valid_ && signature.value() == last_) return false;
    last_ = signature.value();
    valid_ = true;
    return true;
  }
  void invalidate() { valid_ = false; }

 private:
  uint32_t last_ = 0;
  bool valid_ = false;
};

inline bool self_check() {
  RedrawGuard guard;
  const bool first = guard.changed(Signature().add(96100000u).add("KXYZ"));
  const bool same = guard.changed(Signature().add(96100000u).add("KXYZ"));
  const bool moved = guard.changed(Signature().add(96300000u).add("KXYZ"));
  guard.invalidate();
  const bool after_invalidate = guard.changed(Signature().add(96300000u).add("KXYZ"));
  return first && !same && moved && after_invalidate &&
         Signature().add("AB").value() != Signature().add("A").add("B").value();
}

}  // namespace orcsdr::ui
