#pragma once

#include <cstdarg>

// Lock-free log queue for code that must never block on the console, such as the
// IQ stream callback. A console write can wait for a USB host that is not
// reading; here a line is only formatted into a fixed slot and dropped when the
// queue is full. A normal task drains the slots and does the actual printing.

#if defined(__GNUC__)
#define ORC_RT_LOG_PRINTF(fmt, args) __attribute__((format(printf, fmt, args)))
#else
#define ORC_RT_LOG_PRINTF(fmt, args)
#endif

namespace orcsdr::realtime_log {

// Safe from any task or callback. The line is dropped if the queue is full.
void log(const char* format, ...) ORC_RT_LOG_PRINTF(1, 2);

// Prints every queued line through `write` (call from a normal task).
void drain(void (*write)(const char* line));

// Lines dropped because the queue was full.
unsigned dropped();

}  // namespace orcsdr::realtime_log
