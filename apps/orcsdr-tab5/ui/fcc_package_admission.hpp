#pragma once
#include <cstdint>
namespace orcsdr::fcc {
// Pure Settings/Data & Maps policy. A future serialized delivery worker supplies
// authenticated offer data and measured free space; UI and worker both apply this
// check. No I/O and no radio control. A previously verified generation is retained.
enum class InstallAdmission : uint8_t {
  ready, busy, reception_active, no_sd, no_authenticated_offer, size_unknown,
  space_unknown, size_overflow, insufficient_space
};
struct InstallInputs {
  bool busy=false, reception_idle=false, sd_ready=false, authenticated_offer=false;
  bool sizes_known=false, free_known=false;
  // Additional space, not total card use: old installed package remains allocated.
  // Archive, extracted staging and journal/headroom may coexist.
  uint64_t archive_bytes=0, staging_bytes=0, reserve_bytes=0, free_bytes=0;
};
struct InstallDecision {
  InstallAdmission admission=InstallAdmission::size_unknown;
  uint64_t required_bytes=0;
};
inline InstallDecision admit_install(const InstallInputs& i) {
  if (i.busy) return {InstallAdmission::busy,0};
  if (!i.reception_idle) return {InstallAdmission::reception_active,0};
  if (!i.sd_ready) return {InstallAdmission::no_sd,0};
  if (!i.authenticated_offer) return {InstallAdmission::no_authenticated_offer,0};
  if (!i.sizes_known || !i.staging_bytes) return {InstallAdmission::size_unknown,0};
  if (i.archive_bytes>UINT64_MAX-i.staging_bytes) return {InstallAdmission::size_overflow,0};
  const uint64_t staging=i.archive_bytes+i.staging_bytes;
  if (i.reserve_bytes>UINT64_MAX-staging) return {InstallAdmission::size_overflow,0};
  const uint64_t need=staging+i.reserve_bytes;
  if (!i.free_known) return {InstallAdmission::space_unknown,need};
  return {i.free_bytes>=need ? InstallAdmission::ready : InstallAdmission::insufficient_space,need};
}
// Only authenticated, format-compatible, verified active metadata establishes
// a usable local generation. Network/offer/update failure does not revoke it.
struct InstalledGeneration {
  bool sd_ready=false, active_verified=false, format_supported=false;
  uint64_t generation=0;
  bool usable() const { return sd_ready && active_verified && format_supported && generation!=0; }
};
} // namespace orcsdr::fcc
