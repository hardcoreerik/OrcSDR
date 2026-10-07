#include "setup_wizard_store.hpp"

namespace orcsdr::setup_wizard_store {
namespace {

using setup_wizard::LocationMethod;
using setup_wizard::SetupRecord;
using setup_wizard::Step;

// NVS can hand back any byte for a key written by another firmware, so both
// enums are range-checked rather than cast blindly. An out-of-range value
// leaves the field at its default, and SetupRecordValid() then rejects the
// record as a whole instead of the app acting on a half-understood one.
Step StepFromByte(uint8_t value) {
  return value < setup_wizard::kStepCount ? static_cast<Step>(value)
                                          : Step::welcome;
}

bool StepByteValid(uint8_t value) { return value < setup_wizard::kStepCount; }

LocationMethod MethodFromByte(uint8_t value) {
  switch (static_cast<LocationMethod>(value)) {
    case LocationMethod::none:
    case LocationMethod::map_pin:
    case LocationMethod::coordinates:
    case LocationMethod::postal_code:
    case LocationMethod::city_address:
    case LocationMethod::network_estimate:
      return static_cast<LocationMethod>(value);
  }
  return LocationMethod::none;
}

}  // namespace

SetupRecord Load(NvsStore& preferences) {
  SetupRecord record;
  // A device that has never run setup has none of these keys. Defaulting the
  // version to 0 rather than the current one means "absent" reads back as an
  // invalid record, which NeedsSetup() reports as needing setup -- no special
  // first-boot case anywhere.
  record.version = preferences.get_u8(kVersionKey, 0);
  record.completed = preferences.get_bool(kCompletedKey, false);

  const uint8_t step = preferences.get_u8(kStepKey, 0);
  record.step = StepFromByte(step);
  record.skips = preferences.get_u8(kSkipsKey, 0);
  record.method = MethodFromByte(preferences.get_u8(kMethodKey, 0));

  // Upgrade case: a device configured before the wizard existed has the
  // location keys set but none of the wizard keys. That reads back as a
  // record with a location and no method, which SetupRecordValid rejects, so
  // the user is shown setup once. Their location is never touched -- the
  // picker opens on it because the UI reads the receiver location directly,
  // not through this record.
  record.location_valid = preferences.get_bool(kLocationSetKey, false);
  record.latitude_e7 = preferences.get_i32(kLatitudeKey, 0);
  record.longitude_e7 = preferences.get_i32(kLongitudeKey, 0);

  // A stored step byte this firmware does not recognise is not something to
  // silently round down to Welcome and then treat as a valid record; force
  // the whole record to be rejected.
  if (!StepByteValid(step)) record.version = 0;
  return record;
}

bool Save(NvsStore& preferences, const SetupRecord& record) {
  bool ok = preferences.put_u8(kVersionKey, record.version);
  ok = preferences.put_bool(kCompletedKey, record.completed) && ok;
  ok = preferences.put_u8(kStepKey, static_cast<uint8_t>(record.step)) && ok;
  ok = preferences.put_u8(kSkipsKey, record.skips) && ok;
  ok = preferences.put_u8(kMethodKey, static_cast<uint8_t>(record.method)) && ok;

  // The location is written to the shared receiver-location keys, so the
  // radar and the map read the same values the wizard just set.
  ok = preferences.put_bool(kLocationSetKey, record.location_valid) && ok;
  if (record.location_valid) {
    ok = preferences.put_i32(kLatitudeKey, record.latitude_e7) && ok;
    ok = preferences.put_i32(kLongitudeKey, record.longitude_e7) && ok;
  }
  return ok;
}

bool MarkIncomplete(NvsStore& preferences) {
  // Only the completion flag and the step are reset. The location keys are
  // deliberately untouched: a user re-opening setup from Settings and then
  // backing out must keep the configuration they already had.
  bool ok = preferences.put_u8(kVersionKey, setup_wizard::kSetupRecordVersion);
  ok = preferences.put_bool(kCompletedKey, false) && ok;
  ok = preferences.put_u8(kStepKey,
                          static_cast<uint8_t>(Step::welcome)) && ok;
  ok = preferences.put_u8(kSkipsKey, 0) && ok;
  return ok;
}

}  // namespace orcsdr::setup_wizard_store
