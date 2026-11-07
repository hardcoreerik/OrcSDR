/* Compile as ISO C11 and C++17 to verify the shared in-process ABI. */
#include "../../contracts/radio_service/v1/include/orc_radio_service_v1.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined(__cplusplus)
static_assert(ORC_RADIO_V1_ABI_MAJOR == 1u, "major must be 1");
#else
_Static_assert(ORC_RADIO_V1_ABI_MAJOR == 1u, "major must be 1");
#endif

static int describe(void *context, const char *id, orc_radio_v1_snapshot_t *out) {
    (void)context;
    if (!id || !out || out->struct_size < sizeof(*out)) return ORC_RADIO_V1_INVALID_ARGUMENT;
    out->state = ORC_RADIO_V1_READY;
    out->identity.struct_size = sizeof(out->identity);
    out->identity.generation = 1u;
    return ORC_RADIO_V1_OK;
}

int main(void) {
    orc_radio_v1_provider_t provider;
    orc_radio_v1_snapshot_t snapshot;
    memset(&provider, 0, sizeof(provider));
    memset(&snapshot, 0, sizeof(snapshot));
    provider.struct_size = sizeof(provider);
    provider.abi_major = ORC_RADIO_V1_ABI_MAJOR;
    provider.snapshot = describe;
    snapshot.struct_size = sizeof(snapshot);
    if (provider.snapshot(NULL, "receiver", &snapshot) != ORC_RADIO_V1_OK) return 1;
    if (snapshot.state != ORC_RADIO_V1_READY || snapshot.identity.generation != 1u) return 2;
    if ((ORC_RADIO_V1_SCOPE_TUNE & ORC_RADIO_V1_SCOPE_OBSERVE) != 0u) return 3;
    if (sizeof(orc_radio_v1_frame_t) <= sizeof(orc_radio_v1_identity_t)) return 4;
    return 0;
}
