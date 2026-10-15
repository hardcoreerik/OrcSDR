set(ui_sources "home_v3_ui.cpp" "fm_4b_ui.cpp")
if(CONFIG_ORCUI_ADAPTIVE_HOME)
  set(ui_sources "orcui_4b_adapter.cpp")
endif()
idf_component_register(
  SRCS "main.cpp" ${ui_sources} "../../orcsdr-tab5/ui/waterfall_style.cpp"
  INCLUDE_DIRS "." "../../orcsdr-tab5/ui"
  REQUIRES lvgl esp_rtl_sdr esp_psram esp_timer orcui
  EMBED_FILES "orc_badge_104.rgb565"
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
