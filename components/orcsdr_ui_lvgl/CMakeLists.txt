# Pure UI building blocks (layout, plots, painter, icons: no graphics-library dependency, host-tested)
# and the LVGL pixel font. The hardware-independent Tab5 headers they mirror live in
# apps/orcsdr-tab5/ui.
set(TAB5_UI "${CMAKE_CURRENT_LIST_DIR}/../../apps/orcsdr-tab5/ui")
idf_component_register(
    SRCS "src/pixel_font.cpp" "src/home_layout.cpp" "src/home_plots.cpp" "src/painter.cpp" "src/icons.cpp"
    INCLUDE_DIRS "include" "${TAB5_UI}"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES lvgl esp_psram
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
