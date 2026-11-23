// In-memory implementation of the storage API in stubs/orcsdr_storage.hpp. Files are whole strings in a map;
// a File reads a copy and a writer commits on flush/close, which is how the library code uses it (it writes a
// temporary file, verifies it, then renames).
#include "orcsdr_storage.hpp"
#include "web_storage_api.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>

namespace orcsdr::storage {
namespace {
std::map<std::string, std::string>& files() {
  static std::map<std::string, std::string> f;
  return f;
}
std::set<std::string>& dirs() {
  static std::set<std::string> d;
  return d;
}
unsigned g_revision = 0;
}  // namespace

struct File::State {
  std::string path;
  std::string data;
  size_t pos = 0;
  bool writable = false;
  bool open = true;
  bool dirty = false;
};

size_t File::read(uint8_t* data, size_t size) {
  if (!state_ || !state_->open) return 0;
  const size_t n = std::min(size, state_->data.size() - std::min(state_->pos, state_->data.size()));
  memcpy(data, state_->data.data() + state_->pos, n);
  state_->pos += n;
  return n;
}

size_t File::readBytesUntil(char delimiter, char* data, size_t size) {
  size_t n = 0;
  while (state_ && n < size && state_->pos < state_->data.size()) {
    const char c = state_->data[state_->pos++];
    if (c == delimiter) break;
    data[n++] = c;
  }
  return n;
}

size_t File::write(const uint8_t* data, size_t size) {
  if (!state_ || !state_->open || !state_->writable) return 0;
  if (state_->pos > state_->data.size()) state_->data.resize(state_->pos);
  state_->data.replace(state_->pos, std::min(size, state_->data.size() - state_->pos), reinterpret_cast<const char*>(data), size);
  state_->pos += size;
  state_->dirty = true;
  return size;
}

size_t File::printf(const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  const int n = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  return n > 0 ? write(reinterpret_cast<const uint8_t*>(buffer), std::min<size_t>(n, sizeof(buffer) - 1)) : 0;
}

size_t File::print(const char* value) { return write(reinterpret_cast<const uint8_t*>(value), strlen(value)); }
bool File::available() const { return state_ && state_->open && state_->pos < state_->data.size(); }
size_t File::size() const { return state_ ? state_->data.size() : 0; }
size_t File::position() const { return state_ ? state_->pos : 0; }
bool File::seek(size_t position) {
  if (!state_ || position > state_->data.size()) return false;
  state_->pos = position;
  return true;
}
bool File::truncate(size_t size) {
  if (!state_ || !state_->open || !state_->writable || size > state_->data.size()) return false;
  state_->data.resize(size);
  if (state_->pos > size) state_->pos = size;
  state_->dirty = true;
  return true;
}
const char* File::name() const { return state_ ? state_->path.c_str() : ""; }

bool File::flush() {
  if (!state_ || !state_->open) return false;
  if (state_->writable && state_->dirty) {
    files()[state_->path] = state_->data;
    state_->dirty = false;
    ++g_revision;
  }
  return true;
}

bool File::close() {
  if (!state_) return true;
  const bool ok = flush() || !state_->open;
  state_->open = false;
  return ok;
}

File FileSystem::open(const char* path, const char* mode, bool create) const {
  auto state = std::make_shared<File::State>();
  state->path = path;
  const bool write = mode && (mode[0] == 'w' || mode[0] == 'a');
  auto it = files().find(path);
  if (!write) {
    if (it == files().end()) return File();
    state->data = it->second;
  } else {
    if (mode[0] == 'a' && it != files().end()) {
      state->data = it->second;
      state->pos = state->data.size();
    } else if (it == files().end() && !create) {
      return File();
    }
    state->writable = true;
    state->dirty = true;   // opening for write creates/truncates the file when it is closed
  }
  return File(state);
}

bool FileSystem::exists(const char* path) const { return files().count(path) || dirs().count(path); }
EntryStatus FileSystem::status(const char* path) const {
  if (files().count(path)) return EntryStatus::file;
  if (dirs().count(path)) return EntryStatus::directory;
  return EntryStatus::missing;
}
bool FileSystem::mkdir(const char* path) const { dirs().insert(path); return true; }
bool FileSystem::remove(const char* path) const {
  if (!files().erase(path)) return false;
  ++g_revision;
  return true;
}
bool FileSystem::rename(const char* from, const char* to) const {
  auto it = files().find(from);
  if (it == files().end()) return false;
  files()[to] = std::move(it->second);
  files().erase(from);
  ++g_revision;
  return true;
}

bool mounted() { return false; }

FileSystem& filesystem() {
  static FileSystem fs;
  return fs;
}

unsigned revision() { return g_revision; }

std::string export_all() {
  static const char* hex = "0123456789abcdef";
  std::string out;
  for (const auto& kv : files()) {
    out += kv.first;
    out += ':';
    for (unsigned char c : kv.second) { out += hex[c >> 4]; out += hex[c & 15]; }
    out += '\n';
  }
  return out;
}

void import_all(const char* text) {
  auto nib = [](char c) { return c >= 'a' ? c - 'a' + 10 : c - '0'; };
  const char* p = text;
  while (*p) {
    const char* colon = strchr(p, ':');
    const char* end = strchr(p, '\n');
    if (!colon || (end && colon > end)) break;
    std::string key(p, colon - p), value;
    for (const char* q = colon + 1; *q && *q != '\n' && q[1] && q[1] != '\n'; q += 2)
      value.push_back(static_cast<char>(nib(q[0]) * 16 + nib(q[1])));
    files()[key] = value;
    if (!end) break;
    p = end + 1;
  }
  g_revision = 0;
}

std::string read_file(const char* path) {
  auto it = files().find(path);
  return it == files().end() ? std::string() : it->second;
}

std::string list_with_prefix(const char* prefix) {
  std::string out;
  for (const auto& kv : files())
    if (kv.first.compare(0, strlen(prefix), prefix) == 0) { out += kv.first; out += '\n'; }
  return out;
}

}  // namespace orcsdr::storage
