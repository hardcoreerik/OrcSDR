#pragma once
// Hex text helpers of the serial protocols (session, SD transfer, IQ transfer, LoRa lines, the LoRa key file).
// Pure functions, host-tested in tests/hex_codec_tests.cpp. Moved unchanged from main.cpp.
#include <cstddef>
#include <cstdint>

namespace orcsdr::hex_codec {

// Exactly output_size * 2 hex digits (either case) into output_size bytes. `value` must not be null.
bool decode(const char* value, uint8_t* output, size_t output_size);

// An even number of hex digits into a C string; the decoded text and its terminator must fit output_size.
// The empty string decodes to "". `value` must not be null.
bool decode_text(const char* value, char* output, size_t output_size);

// Exactly 8 characters that strtoul(base 16) consumes to the end. False for null arguments.
bool parse_u32_exact(const char* value, uint32_t* output);

// Lower-case hex, two calls of `put` per byte and no line ending (main.cpp's print_hex wrote character by character).
void write(const uint8_t* value, size_t size, void (*put)(char digit));

}  // namespace orcsdr::hex_codec
