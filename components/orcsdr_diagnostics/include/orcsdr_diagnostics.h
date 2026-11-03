#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
// Read-only status is cheap. Heap validation requires authentication and may pause tasks.
bool orcsdr_diagnostics_command(const char* command, bool authenticated, void (*emit)(const char*));
#ifdef __cplusplus
}
#endif
