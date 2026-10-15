idf_component_register(
  SRCS "main.cpp" "home_v3_ui.cpp" "fm_4b_ui.cpp" "../../orcsdr-tab5/ui/waterfall_style.cpp"
  INCLUDE_DIRS "." "../../orcsdr-tab5/ui"
  REQUIRES lvgl esp_rtl_sdr esp_psram esp_timer
  EMBED_FILES "orc_badge_104.rgb565"
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
