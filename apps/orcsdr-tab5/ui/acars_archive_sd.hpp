#pragma once
// SD-card file operations for the ACARS archive (firmware only). The archive itself (acars_archive) is portable and host-tested;
// this is the thin layer that maps its FileOps onto the Tab5's storage layer.
#include "acars_archive.hpp"

namespace orcsdr::acars_archive_sd {

const acars_archive::FileOps& file_ops();

// Writes a few synthetic records to a scratch folder on the real SD card, reads them back, compares, and deletes the files.
// Never touches the real log. Fills `out` with one RTL_ACARS_ARCHIVETEST line; returns true on PASS.
bool self_test(char* out, size_t size);

}  // namespace orcsdr::acars_archive_sd
