#pragma once
// The host has one kind of memory.
static inline bool esp_ptr_external_ram(const void*) { return true; }
