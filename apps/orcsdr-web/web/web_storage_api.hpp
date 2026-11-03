// Page-facing helpers of the in-memory storage (web_storage.cpp).
#pragma once

#include <string>

namespace orcsdr::storage {
std::string export_all();                      // "path:hex" lines for every file
void import_all(const char* text);             // replaces the store from such lines
std::string read_file(const char* path);
std::string list_with_prefix(const char* prefix);   // newline-separated paths
}  // namespace orcsdr::storage
