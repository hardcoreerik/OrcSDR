#include "weather_report_store.hpp"

#include "orcsdr_storage.hpp"

#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstring>

namespace orcsdr::weather {
namespace {
constexpr char kRoot[] = "/orcsdr/weather";
constexpr char kReports[] = "/orcsdr/weather/reports";
constexpr char kHistory[] = "/orcsdr/weather/history.jsonl";
constexpr size_t kBuffer = 6144;

void set_error(ReportSaveResult* out, const char* text) {
  if (!out) return;
  out->ok = false;
  std::snprintf(out->error, sizeof(out->error), "%s", text ? text : "error");
}
bool write_file(storage::FileSystem& fs, const char* path, const char* data) {
  auto file = fs.open(path, FILE_WRITE, true);
  if (!file) return false;
  const size_t n = std::strlen(data);
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(data), n) == n &&
                  file.flush() && file.close();
  return ok;
}
void sha256_hex(const char* data, char out[65]) {
  uint8_t digest[32]{};
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  if (mbedtls_sha256_starts(&ctx, 0) == 0 &&
      mbedtls_sha256_update(&ctx, reinterpret_cast<const unsigned char*>(data),
                            std::strlen(data)) == 0)
    (void)mbedtls_sha256_finish(&ctx, digest);
  mbedtls_sha256_free(&ctx);
  for (size_t i=0;i<32;++i) std::snprintf(out + i*2, 3, "%02x", digest[i]);
  out[64] = '\0';
}
bool ensure_dirs(storage::FileSystem& fs) {
  return fs.mkdir("/orcsdr") && fs.mkdir(kRoot) && fs.mkdir(kReports);
}
bool rewrite_history(storage::FileSystem& fs, const char* row) {
  char* content = new (std::nothrow) char[kBuffer];
  if (!content) return false;
  size_t used = 0;
  if (fs.exists(kHistory)) {
    auto in = fs.open(kHistory, FILE_READ);
    if (in) {
      used = std::min(in.size(), kBuffer - 1);
      used = in.read(content, used);
      in.close();
    }
  }
  content[used] = '\0';
  const size_t row_len = std::strlen(row);
  if (used + row_len + 2 >= kBuffer) {
    const char* first_nl = std::strchr(content, '\n');
    if (first_nl) {
      const size_t drop = static_cast<size_t>(first_nl - content + 1);
      std::memmove(content, content + drop, used - drop + 1);
      used -= drop;
    }
  }
  if (used + row_len + 2 >= kBuffer) { delete[] content; return false; }
  std::memcpy(content + used, row, row_len);
  used += row_len;
  content[used++] = '\n';
  content[used] = '\0';
  constexpr char part[] = "/orcsdr/weather/history.jsonl.part";
  if (!write_file(fs, part, content)) { delete[] content; return false; }
  if (fs.exists(kHistory)) (void)fs.remove(kHistory);
  const bool ok = fs.rename(part, kHistory);
  delete[] content;
  return ok;
}
}

bool save_report_bundle(storage::FileSystem& fs, const ReportSnapshot& report,
                        ReportSaveResult* result) {
  if (result) *result = {};
  if (!ensure_dirs(fs)) { set_error(result, "mkdir failed"); return false; }

  char id[40];
  if (report.wallclock_valid && report.created_utc)
    std::snprintf(id, sizeof(id), "weather-%lu", static_cast<unsigned long>(report.created_utc));
  else
    std::snprintf(id, sizeof(id), "weather-up-%lu",
                  static_cast<unsigned long>(report.created_uptime_ms));

  char directory[96];
  std::snprintf(directory, sizeof(directory), "%s/%s", kReports, id);
  if (!fs.mkdir(directory)) { set_error(result, "report mkdir failed"); return false; }

  char* json = new (std::nothrow) char[2048];
  char* csv = new (std::nothrow) char[2048];
  char* html = new (std::nothrow) char[4096];
  if (!json || !csv || !html) {
    delete[] json; delete[] csv; delete[] html;
    set_error(result, "buffer allocation failed"); return false;
  }
  const bool encoded = encode_report_json(report,json,2048) &&
                       encode_report_csv(report,csv,2048) &&
                       encode_report_html(report,html,4096);
  if (!encoded) {
    delete[] json; delete[] csv; delete[] html;
    set_error(result, "report encode failed"); return false;
  }

  char pjson[128], pcsv[128], phtml[128], pmanifest[128];
  std::snprintf(pjson,sizeof(pjson),"%s/session.json",directory);
  std::snprintf(pcsv,sizeof(pcsv),"%s/observations.csv",directory);
  std::snprintf(phtml,sizeof(phtml),"%s/report.html",directory);
  std::snprintf(pmanifest,sizeof(pmanifest),"%s/manifest.sha256",directory);
  bool ok = write_file(fs,pjson,json) && write_file(fs,pcsv,csv) && write_file(fs,phtml,html);
  char hjson[65], hcsv[65], hhtml[65], manifest[512];
  sha256_hex(json,hjson); sha256_hex(csv,hcsv); sha256_hex(html,hhtml);
  std::snprintf(manifest,sizeof(manifest),"%s  session.json\n%s  observations.csv\n%s  report.html\n",
                hjson,hcsv,hhtml);
  ok = ok && write_file(fs,pmanifest,manifest);

  char history[512];
  std::snprintf(history,sizeof(history),
                "{\"id\":\"%s\",\"wallclock_valid\":%s,\"created_utc\":%lu,"
                "\"created_uptime_ms\":%lu,\"noaa_valid\":%s,\"frequency_hz\":%lu}",
                id, report.wallclock_valid ? "true":"false",
                static_cast<unsigned long>(report.created_utc),
                static_cast<unsigned long>(report.created_uptime_ms),
                report.noaa_valid ? "true":"false",
                static_cast<unsigned long>(report.noaa_frequency_hz));
  ok = ok && rewrite_history(fs, history);
  delete[] json; delete[] csv; delete[] html;
  if (!ok) { set_error(result, "write failed"); return false; }
  if (result) {
    result->ok = true;
    std::snprintf(result->id,sizeof(result->id),"%s",id);
  }
  return true;
}

size_t report_history_count(storage::FileSystem& fs) {
  if (!fs.exists(kHistory)) return 0;
  auto f = fs.open(kHistory, FILE_READ);
  if (!f) return 0;
  size_t count = 0;
  char buffer[256];
  while (f.available()) {
    const size_t n = f.readBytesUntil('\n', buffer, sizeof(buffer)-1);
    if (n) ++count;
  }
  f.close();
  return count;
}

bool latest_report_id(storage::FileSystem& fs, char* output, size_t capacity) {
  if (!output || capacity == 0 || !fs.exists(kHistory)) return false;
  output[0] = '\0';
  auto f = fs.open(kHistory, FILE_READ);
  if (!f) return false;
  char line[512]{};
  while (f.available()) {
    const size_t n=f.readBytesUntil('\n',line,sizeof(line)-1);
    line[n]='\0';
    const char* id=std::strstr(line,"\"id\":\"");
    if (!id) continue;
    id += 6;
    const char* end=std::strchr(id,'"');
    if (!end) continue;
    const size_t len=std::min(static_cast<size_t>(end-id),capacity-1);
    std::memcpy(output,id,len); output[len]='\0';
  }
  f.close();
  return output[0] != '\0';
}

}  // namespace orcsdr::weather
