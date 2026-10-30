#pragma once
// Shared command layer (docs/hardware/WEB_LAYER_DESIGN.md). One table of commands; the serial CLI and, later, the HTTP
// server are two front ends over it. A command fills a Result (records of typed name/value fields); the serial renderer
// prints it in the legacy "LABEL name=value ..." form and the JSON renderer emits the same data for the web layer.
// Handlers must not block and must be callable from any task.
#include <cstddef>
#include <cstdint>

namespace orcsdr::commands {

enum class Access : uint8_t { read, control, admin };
enum class Transport : uint8_t { serial, http };
enum class Status : uint8_t { ok, auth_required, forbidden, invalid, unavailable };

struct Caller {
  Transport via;
  bool authed;  // has completed the PAIR/AUTH handshake (serial) or a signed session (http)
};
struct Args {
  int rx = 0;  // receiver index
};

constexpr int kMaxFields = 14, kMaxRecords = 5;

enum class Kind : uint8_t { integer, unsigned_int, real, text };
struct Field {
  const char* name;
  Kind kind;
  int8_t precision;  // real: digits after the point
  bool quoted;       // text: serial output wraps it in double quotes
  union {
    int64_t i;
    uint64_t u;
    double f;
    const char* s;
  };
};
struct Record {
  const char* label;  // serial label, e.g. "RTL_RX_STATUS"
  Field fields[kMaxFields];
  uint8_t count = 0;
};
struct Result {
  Status status = Status::ok;
  Record records[kMaxRecords];
  uint8_t count = 0;
  char arena[384];  // text fields are copied here so they outlive the handler
  size_t arena_used = 0;
};

Record* add_record(Result& r, const char* label);
void put_int(Record& rec, const char* name, int64_t v);
void put_uint(Record& rec, const char* name, uint64_t v);
void put_real(Record& rec, const char* name, double v, int precision);
void put_text(Result& r, Record& rec, const char* name, const char* v, bool quoted = false);

struct Command {
  const char* serial_line;  // exact serial line, e.g. "RTL_MEM STATUS"
  const char* http_path;    // e.g. "mem" (the web layer will serve /api/v1/<path>)
  Access access;
  Status (*run)(const Caller&, const Args&, Result&);
};

const Command* commands(size_t* count);
const Command* find_serial(const char* line);
const Command* find_path(const char* path);

// Runs a command with the access check done once, here. A refused command leaves `out.status` set and no records.
Status run(const Command& cmd, const Caller& caller, const Args& args, Result& out);

// One shared result buffer for the serial task, allocated once in PSRAM (null if that fails). Not for concurrent callers: the
// web layer will keep its own per-request buffers in PSRAM.
Result* serial_result();
// Serial front end: if `line` is a table command, runs it, prints the legacy lines and returns true.
bool run_serial(const char* line, const Caller& caller);
// Prints a result in the legacy serial form: "LABEL name=value name=value" per record.
void print_serial(const Result& r);
// Renders {"ok":true,"records":[{"_":"LABEL","name":value,...}]}; returns the length, or 0 if `cap` is too small.
size_t to_json(const Result& r, char* out, size_t cap);

}  // namespace orcsdr::commands
