// Host-only DMR replay executable for tests/dmr_replay_acceptance/acceptance.py.
// Reads genuine little-endian CF32 IQ from stdin; emits strict JSONL v1 to stdout.
// No IQ capture or file output; all metadata comes from real D1-C callbacks.
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "orcsdr_dmr/d1_bridge.hpp"
#include "orcsdr_dmr/d0.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace {
namespace dmr = orcsdr::dmr;
namespace d1 = orcsdr::dmr::d1;
namespace integration = orcsdr::dmr::d1::integration;

constexpr const char* kSchema = "orcsdr-dmr-d1-bridge-event-v1";
constexpr std::size_t kBlockSamples = 256;
constexpr std::uint64_t kMaxSamples = 0xffffffffULL;  // Acceptance JSONL v1 uses u32 offsets.

constexpr const char* kSyncNames[8] = {
    "bs_voice", "bs_data", "ms_voice", "ms_data",
    "direct_ts1_voice", "direct_ts1_data",
    "direct_ts2_voice", "direct_ts2_data"
};

bool args_valid(int argc, char** argv) {
    bool format = false, rate = false, fixture = false;
    if (argc != 7) return false;
    for (int i = 1; i < argc; i += 2) {
        if (!argv[i] || !argv[i+1]) return false;
        if (std::strcmp(argv[i], "--input-format") == 0 && !format) {
            if (std::strcmp(argv[i+1], "cf32-le") != 0) return false;
            format = true;
        } else if (std::strcmp(argv[i], "--sample-rate-hz") == 0 && !rate) {
            if (std::strcmp(argv[i+1], "48000") != 0) return false;
            rate = true;
        } else if (std::strcmp(argv[i], "--fixture-id") == 0 && !fixture) {
            // Used only to validate the invocation; never added to JSON output.
            const char* name = argv[i+1];
            std::size_t length = 0;
            while (name[length] && length <= 127U) {
                const unsigned char c = static_cast<unsigned char>(name[length]);
                if (c < 33U || c > 126U) return false;
                ++length;
            }
            if (length == 0U || length > 127U) return false;
            fixture = true;
        } else return false;
    }
    return format && rate && fixture;
}

float float_le(const std::uint8_t* p) noexcept {
    const std::uint32_t bits = static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8U) |
        (static_cast<std::uint32_t>(p[2]) << 16U) |
        (static_cast<std::uint32_t>(p[3]) << 24U);
    float sample = 0.0F;
    static_assert(sizeof(sample) == sizeof(bits), "32-bit IEEE float required");
    std::memcpy(&sample, &bits, sizeof(sample));
    return sample;
}

bool sync_is_data(dmr::SyncKind sync) noexcept {
    return (static_cast<unsigned>(sync) % 2U) == 1U;
}

struct Output {
    integration::Receiver* receiver = nullptr;
    bool write_failed = false;
    std::uint64_t previous_offset = 0;
    bool have_event = false;
    std::uint64_t event_count = 0;

    bool output_record(const d1::Event& e) noexcept {
        // The output timestamp is when a complete burst was *processed*,
        // not a falsely reconstructed start of an omitted acquisition burst.
        const std::uint64_t processed = receiver->stats().iq_samples;
        if (processed == 0 || processed > kMaxSamples || (have_event && processed - 1 < previous_offset)) {
            write_failed = true;
            return false;
        }
        const auto offset = static_cast<unsigned long long>(processed - 1);
        previous_offset = processed - 1;
        have_event = true;
        const unsigned kind_index = static_cast<unsigned>(e.sync);
        if (kind_index >= 8U) {
            write_failed = true;
            return false;
        }
        // Embedded-LC B-E fragments do not contain a new SYNC word.
        // Avoid inventing an extra pattern from this late-entry identity event.
        if (!e.late_entry) {
            if (std::printf("{\"schema\":\"%s\",\"type\":\"sync\",\"sample_offset\":%llu,\"pattern\":\"%s\"}\n",
                kSchema, offset, kSyncNames[kind_index]) < 0) {
                write_failed = true;
                return false;
            }
        }
        const bool eligible = e.integrity_valid && e.evidence == d1::Evidence::identity_valid &&
                              e.identity != d1::IdentityKind::none &&
                              e.source_id != 0U &&
                              (e.identity != d1::IdentityKind::group || e.destination_id != 0U);
        const bool data = sync_is_data(e.sync) && !e.late_entry;
        const char* frame_kind = !data ? "voice" : (e.data_type == 3U ? "control" : "data");
        const char* fec = (e.bptc_valid || (e.late_entry && e.integrity_valid)) ? "pass" :
                          (data && e.golay_valid &&
                          (e.data_type == 1U || e.data_type == 2U ||
                           e.data_type == 3U || e.data_type == 6U || e.data_type == 11U)) ? "fail" : "unknown";
        const char* check_kind = e.late_entry ? "embedded_lc" :
                                 (data && (e.data_type == 1U || e.data_type == 2U)) ? "rs12_9" :
                                 (data && (e.data_type == 3U || e.data_type == 6U || e.data_type == 11U)) ? "crc16" : "none";
        const char* check = std::strcmp(check_kind, "none") == 0 ? "unknown" :
                            e.integrity_valid ? "pass" :
                            (std::strcmp(fec, "pass") == 0 ? "fail" : "unknown");
        // Raw CACH before a single BS sync is a Hamming-inferred slot, not
        // verified to be CACH. JSONL v1 lacks a "candidate" slot field.
        // Report unknown until a separate cadence-verification gate exists.
        const bool known_slot = e.slot >= 1U && e.slot <= 2U && !e.slot_from_cach;
        const bool known_cc = (e.golay_valid && data) || (e.late_entry && eligible);
        if (std::printf("{\"schema\":\"%s\",\"type\":\"burst\",\"sample_offset\":%llu,"
            "\"kind\":\"%s\",\"slot\":", kSchema, offset, frame_kind) < 0) {
            write_failed = true; return false;
        }
        if (known_slot) {
            if (std::printf("%u", static_cast<unsigned>(e.slot)) < 0) { write_failed = true; return false; }
        } else if (std::printf("null") < 0) { write_failed = true; return false; }
        if (std::printf(",\"color_code\":") < 0) { write_failed = true; return false; }
        if (known_cc) {
            if (std::printf("%u", static_cast<unsigned>(e.colour_code)) < 0) { write_failed = true; return false; }
        } else if (std::printf("null") < 0) { write_failed = true; return false; }
        if (std::printf(",\"fec\":\"%s\",\"check\":\"%s\",\"check_kind\":\"%s\",\"identity_valid\":%s,\"identity\":",
            fec, check, check_kind, eligible ? "true" : "false") < 0) {
            write_failed = true; return false;
        }
        if (!eligible) {
            if (std::printf("null") < 0) { write_failed = true; return false; }
        } else if (e.identity == d1::IdentityKind::group) {
            if (std::printf("{\"source_id\":%u,\"talkgroup\":%u}",
                static_cast<unsigned>(e.source_id), static_cast<unsigned>(e.destination_id)) < 0) {
                write_failed = true; return false;
            }
        } else {
            // Private-call destination is a radio ID, not a talkgroup.
            if (std::printf("{\"source_id\":%u}", static_cast<unsigned>(e.source_id)) < 0) {
                write_failed = true; return false;
            }
        }
        if (std::printf("}\n") < 0) { write_failed = true; return false; }
        ++event_count;
        return true;
    }
};

void decoded(const integration::Event& event, void* user) noexcept {
    auto& out = *static_cast<Output*>(user);
    if (!out.write_failed) out.output_record(event.decoded);
}

int fail(const char* reason) {
    std::fprintf(stderr, "dmr_d1_replay_bridge: %s\n", reason);
    return 2;
}
}  // namespace

int main(int argc, char** argv) {
    if (!args_valid(argc, argv)) {
        return fail("expected --input-format cf32-le --sample-rate-hz 48000 --fixture-id ID");
    }
    std::array<std::uint8_t, kBlockSamples * 8 + 8> raw{};
    std::array<d1::ComplexSample, kBlockSamples> iq{};
    integration::Receiver receiver;
    Output output{};
    output.receiver = &receiver;
    std::size_t carried = 0;
    std::uint64_t consumed = 0;
    bool eof = false;
    while (!eof) {
        const std::size_t got = std::fread(raw.data() + carried, 1U, kBlockSamples * 8 - carried, stdin);
        if (got == 0 && std::ferror(stdin)) return fail("stdin read failed");
        eof = std::feof(stdin) != 0;
        const std::size_t have = carried + got;
        const std::size_t samples = have / 8U;
        if (samples == 0 && eof) {
            if (carried != 0U) return fail("trailing partial IQ sample");
            break;
        }
        if (consumed + samples > kMaxSamples) return fail("sample count exceeds JSONL v1 maximum");
        for (std::size_t i = 0; i < samples; ++i) {
            iq[i].re = float_le(raw.data() + 8U*i);
            iq[i].im = float_le(raw.data() + 8U*i + 4U);
        }
        const auto step = receiver.process(iq.data(), samples, decoded, &output);
        if (!step.ok || step.consumed != samples || output.write_failed)
            return fail("D1 receiver or JSONL output failed");
        consumed += samples;
        carried = have - 8U*samples;
        if (carried) std::memmove(raw.data(), raw.data()+8U*samples, carried);
        if (eof && carried) return fail("trailing partial IQ sample");
        // Do not drop the already processed final full block before feof.
    }
    if (!consumed) return fail("no complete IQ samples");
    if (std::printf("{\"schema\":\"%s\",\"type\":\"summary\",\"sample_offset\":%llu,\"consumed_samples\":%llu}\n",
        kSchema, static_cast<unsigned long long>(consumed - 1),
        static_cast<unsigned long long>(consumed)) < 0 || std::fflush(stdout) != 0)
        return fail("stdout write/flush failed");
    return 0;
}
