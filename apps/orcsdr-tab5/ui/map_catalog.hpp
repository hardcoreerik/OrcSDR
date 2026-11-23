#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "orcmap/pack.hpp"

namespace orcsdr::maps::catalog {
enum class Error {none,limit,signature,json,schema,revision,clock,expired,future,
                  rollback,manifest,identity,url,allocation};
// Signed catalogs may list up to this many packs (all 50 US states plus extras).
constexpr size_t kMaximumPacks=64;
struct Policy {
  uint64_t accepted_revision=0,utc_now=0;
  std::string_view accepted_sha256;
  bool clock_trusted=false;
};
struct Pack {
  std::string product_id; // Stable catalog identity across artifact versions.
  orcmap::PackManifest manifest;
  std::string manifest_json;
  std::vector<std::string> urls;
};
struct Catalog {
  // expires_at=0 represents signed JSON null (no expiry), never a malformed date.
  uint64_t revision=0,generated_at=0,expires_at=0;
  std::string sha256;
  std::vector<Pack> packs;
};
// Worker-only operation. Caller supplies its approved public key and previously
// persisted revision+payload hash. No NVS/HTTP/SD operations or global JSON
// allocator changes. Output remains unchanged on every rejection.
Error read_signed(std::string_view payload,std::string_view signature_base64,
                  std::string_view public_key_pem,const Policy&,Catalog*) noexcept;
const char* error_name(Error);
} // namespace orcsdr::maps::catalog
