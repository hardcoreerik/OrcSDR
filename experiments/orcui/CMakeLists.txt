# ESP-IDF opt-in research component. Deliberately not linked by OrcSDR's shipping Tab5 app.
idf_component_register(
  SRCS "layout.cpp" "raster.cpp" "home_lvgl.cpp"
  INCLUDE_DIRS "include"
  REQUIRES lvgl)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
