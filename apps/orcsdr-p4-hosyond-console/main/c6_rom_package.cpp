#include "c6_rom_package.hpp"
#if !ORCSDR_HAS_C6_ROM_PACKAGE
extern "C" const orc_c6_rom_manifest_t* orcsdr_embedded_c6_rom_manifest() {return nullptr;}
#endif
