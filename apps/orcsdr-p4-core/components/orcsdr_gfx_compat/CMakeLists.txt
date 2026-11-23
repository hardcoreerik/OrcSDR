# M5GFX-compatible scaled drawing layer: lets the Tab5 UI sources (apps/orcsdr-tab5/ui) run
# unchanged on a smaller display. `shim` provides <M5Unified.h>; do not also depend on the real
# m5stack/m5unified component from a target that uses this one.
idf_component_register(
    SRCS "src/scaled_gfx.cpp" "src/m5_object.cpp"
    INCLUDE_DIRS "include" "shim"
    REQUIRES m5stack__m5gfx esp_psram heap log
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
