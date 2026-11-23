#pragma once
#include "map_activation.hpp"
#include "map_delivery_state.hpp"

namespace orcsdr::maps::install {
enum class Error {none,input,state,transfer,activation,allocation};
struct Result {
  Error error=Error::none;
  delivery::Error state=delivery::Error::none;
  transfer::Error transfer=transfer::Error::none;
  activation::Error activation=activation::Error::none;
};
// Serialized worker only; never draws UI or changes receiver/location settings.
// Catalog must come from accept_catalog and match its durable revision/hash.
Result run(storage::FileSystem&,delivery::Store&,const catalog::Catalog&,size_t pack,size_t mirror,
           transfer::Stream&,uint64_t free_bytes,uint8_t*,size_t,bool select,uint64_t trusted_utc,const transfer::Hooks&);
Result reset_pending(storage::FileSystem*,delivery::Store&);
// No network required: reconcile durable intent and verified staging/journals.
// Partial downloads without activation intent resume via run after catalog fetch.
Result resume(storage::FileSystem&,delivery::Store&,uint8_t*,size_t,const transfer::Hooks&);
}
