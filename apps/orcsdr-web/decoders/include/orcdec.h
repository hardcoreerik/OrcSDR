/* orcdec: one C ABI over OrcSDR's decoders, built from the same source for a PC (host), the browser (WASM) and the
 * ESP32-P4 firmware (ESP-IDF component). A decoder takes samples, queues events, and the caller polls them.
 *
 *   orcdec_t *d = orcdec_create(ORCDEC_POCSAG, "baud=auto");
 *   orcdec_feed(d, iq_bytes, n);            // any chunk size; the result must not depend on chunking
 *   orcdec_command(d, "finish", 0);         // flush: end of stream or end of a time slot
 *   char line[1024];
 *   while (orcdec_poll(d, line, sizeof line)) puts(line);   // one JSON object per call
 *
 * Events follow docs/decoders/EVENT_SCHEMA.md. Nothing here allocates per sample. All functions are thread-compatible
 * (one thread per decoder instance). */
#ifndef ORCDEC_H
#define ORCDEC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum orcdec_kind {
    ORCDEC_POCSAG = 1, /* CU8 IQ at 960000 S/s; pager messages */
    ORCDEC_FT8 = 2,    /* mono s16 PCM at 12000 S/s, slot based; FT8 decodes */
    ORCDEC_SSTV = 3,   /* mono s16 PCM at 48000 S/s; SSTV image lines and finished images */
    ORCDEC_ADSB = 4,   /* CU8 IQ at 2048000 S/s centred on 1090 MHz; Mode S frames and aircraft positions */
    ORCDEC_ACARS = 5,  /* CU8 IQ at 240000 S/s, AM carrier 60 kHz above the centre; ACARS blocks */
    ORCDEC_LORA = 8,   /* CU8 IQ at 960000 S/s on the channel; LoRa (SF7..12, 250 kHz) and Meshtastic packets, preamble-triggered captures */
    ORCDEC_JS8 = 7,    /* mono s16 PCM at 12000 S/s, 15 s slots; JS8 Normal-speed HEARTBEAT SNR decodes */
    ORCDEC_P25 = 6,    /* CU8 IQ at 960000 S/s on the channel; P25 Phase I control channel (identity, grants) */
};

enum orcdec_format {
    ORCDEC_FMT_CU8 = 1,  /* interleaved unsigned 8-bit I,Q */
    ORCDEC_FMT_S16 = 2,  /* little-endian signed 16-bit mono */
};

typedef struct orcdec orcdec_t;

typedef struct {
    int kind;
    int format;               /* enum orcdec_format */
    uint32_t sample_rate_hz;  /* of the input */
    uint32_t bytes_per_sample;/* 2 for CU8 (one I,Q pair), 2 for S16 */
    const char *name;
} orcdec_info_t;

/* Returns 0 on success, -1 for an unknown kind. */
int orcdec_info(int kind, orcdec_info_t *out);

/* options: "key=value;key=value" (see docs/decoders/EVENT_SCHEMA.md per decoder), or NULL. NULL on failure. */
orcdec_t *orcdec_create(int kind, const char *options);
void orcdec_destroy(orcdec_t *decoder);
void orcdec_reset(orcdec_t *decoder);

/* Appends samples in the decoder's input format. `bytes` must be a multiple of the sample size. Returns the number of
 * events queued by this call, or -1 on a bad argument. */
int orcdec_feed(orcdec_t *decoder, const void *data, size_t bytes);

/* Control: "slot_begin" (arg = slot start, Unix milliseconds; FT8), "finish" (flush; FT8 decodes the slot, others emit
 * their final state). Returns events queued, or -1 for an unknown command. */
int orcdec_command(orcdec_t *decoder, const char *command, uint64_t arg);

/* Pops the oldest event as a NUL-terminated JSON object into `json`. Returns its length, 0 when none is queued. If the
 * event does not fit, it is dropped and -1 is returned. */
int orcdec_poll(orcdec_t *decoder, char *json, size_t capacity);

/* Binary data that belongs to an event (an SSTV image as RGB565, little-endian, row-major). Valid for the event with that
 * `seq` until the next feed/command; NULL when the event has none. */
const void *orcdec_attachment(orcdec_t *decoder, uint32_t seq, size_t *bytes);

/* Events dropped because the queue was full (the caller was not polling). */
uint32_t orcdec_dropped(const orcdec_t *decoder);

const char *orcdec_version(void);

#ifdef __cplusplus
}
#endif
#endif
