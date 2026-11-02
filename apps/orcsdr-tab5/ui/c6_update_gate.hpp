#pragma once
#include <atomic>
namespace orcsdr::c6_update {
enum class Owner {none,hosted,rom};
inline std::atomic<Owner> owner{Owner::none};
inline bool claim(Owner requested) {
  if(requested==Owner::none) return false;
  auto expected=Owner::none;
  return owner.compare_exchange_strong(expected,requested);
}
inline void release(Owner expected) {
  if(expected!=Owner::none) owner.compare_exchange_strong(expected,Owner::none);
}
}
