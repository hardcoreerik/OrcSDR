#include "fake_storage.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace orcsdr::storage {

struct File::State {
  std::string path;
  bool is_dir = false;
  bool writable = false;
  bool closed = false;
  size_t pos = 0;
  std::vector<uint8_t> buffer;
  std::vector<std::string> dir_entries;
  size_t dir_index = 0;
};

File::File(std::shared_ptr<State> state) : state_(std::move(state)) {}
File::operator bool() const { return static_cast<bool>(state_) && !state_->closed; }

size_t File::read(uint8_t* data, size_t size) {
  if (!state_ || state_->closed || state_->is_dir || test_fs::instance().fail_read) return 0;
  const size_t avail = state_->pos < state_->buffer.size() ? state_->buffer.size() - state_->pos : 0;
  const size_t take = std::min(avail, size);
  if (take > 0) {
    std::memcpy(data, state_->buffer.data() + state_->pos, take);
    state_->pos += take;
  }
  return take;
}

size_t File::read(char* data, size_t size) { return read(reinterpret_cast<uint8_t*>(data), size); }
size_t File::readBytes(char* data, size_t size) { return read(data, size); }
size_t File::readBytesUntil(char, char*, size_t) { return 0; }

size_t File::write(const uint8_t* data, size_t size) {
  if (!state_ || state_->closed || !state_->writable || test_fs::instance().fail_write) return 0;
  if (state_->pos + size > state_->buffer.size()) state_->buffer.resize(state_->pos + size);
  std::memcpy(state_->buffer.data() + state_->pos, data, size);
  state_->pos += size;
  test_fs::instance().files[state_->path].data = state_->buffer;
  return size;
}

size_t File::printf(const char*, ...) { return 0; }
size_t File::print(const char*) { return 0; }
bool File::available() const { return state_ && !state_->closed && state_->pos < state_->buffer.size(); }
size_t File::size() const { return state_ ? state_->buffer.size() : 0; }
size_t File::position() const { return state_ ? state_->pos : 0; }
bool File::seek(size_t position) {
  if (!state_ || state_->closed || position > state_->buffer.size()) return false;
  state_->pos = position;
  return true;
}

bool File::flush() {
  if (!state_ || state_->closed) return false;
  if (test_fs::instance().fail_flush) return false;
  if (state_->writable) test_fs::instance().files[state_->path].data = state_->buffer;
  return true;
}

bool File::sync() { return flush(); }
bool File::truncate(size_t) { return false; }

bool File::close() {
  if (!state_) return true;
  if (state_->closed) return true;
  if (state_->writable && !test_fs::instance().fail_flush && !test_fs::instance().fail_close) {
    test_fs::instance().files[state_->path].data = state_->buffer;
  }
  const bool fail = state_->writable && test_fs::instance().fail_close;
  state_->closed = true;
  state_.reset();
  return !fail;
}

bool File::isDirectory() const { return state_ && state_->is_dir; }
const char* File::name() const { return state_ ? state_->path.c_str() : ""; }
uint64_t File::getLastWrite() const {
  if (!state_) return 0;
  auto it = test_fs::instance().files.find(state_->path);
  return it != test_fs::instance().files.end() ? it->second.modified : 0;
}

File File::openNextFile() {
  if (!state_ || !state_->is_dir || state_->closed) return {};
  if (state_->dir_index >= state_->dir_entries.size()) return {};
  const std::string& p = state_->dir_entries[state_->dir_index++];
  return FileSystem{}.open(p.c_str(), FILE_READ);
}

File FileSystem::open(const char* path, const char* mode, bool) const {
  if (!path) return {};
  auto& fs = test_fs::instance();
  if (std::strcmp(path, "/orcsdr") == 0) {
    auto s = std::make_shared<File::State>();
    s->path = path;
    s->is_dir = true;
    if (!fs.dir_order.empty()) {
      s->dir_entries = fs.dir_order;
    } else {
      for (const auto& kv : fs.files) {
        if (kv.first.rfind("/orcsdr/", 0) == 0) s->dir_entries.push_back(kv.first);
      }
    }
    return File{std::move(s)};
  }
  const bool writing = mode && (mode[0] == 'w' || mode[0] == 'a' || std::strchr(mode, '+'));
  if (!writing) {
    auto it = fs.files.find(path);
    if (it == fs.files.end()) return {};
    auto s = std::make_shared<File::State>();
    s->path = path;
    s->buffer = it->second.data;
    return File{std::move(s)};
  }
  auto s = std::make_shared<File::State>();
  s->path = path;
  s->writable = true;
  fs.files[path] = test_fs::Entry{};
  return File{std::move(s)};
}

bool FileSystem::exists(const char* path) const {
  if (!path) return false;
  if (std::strcmp(path, "/orcsdr") == 0) return !test_fs::instance().fail_exists_or_mkdir;
  return test_fs::instance().files.count(path) > 0;
}

EntryStatus FileSystem::status(const char* path) const {
  if (std::strcmp(path, "/orcsdr") == 0) return EntryStatus::directory;
  return exists(path) ? EntryStatus::file : EntryStatus::missing;
}

bool FileSystem::mkdir(const char*) const { return !test_fs::instance().fail_exists_or_mkdir; }

bool FileSystem::remove(const char* path) const {
  if (!path) return false;
  return test_fs::instance().files.erase(path) > 0;
}

bool FileSystem::rename(const char* from, const char* to) const {
  auto& fs = test_fs::instance();
  if (!from || !to) return false;
  if ((!fs.fail_rename_from.empty() && fs.fail_rename_from == from) ||
      (!fs.fail_rename_from_2.empty() && fs.fail_rename_from_2 == from)) {
    return false;
  }
  auto it = fs.files.find(from);
  if (it == fs.files.end()) return false;
  auto entry = std::move(it->second);
  fs.files.erase(it);
  fs.files[to] = std::move(entry);
  return true;
}

}  // namespace orcsdr::storage
