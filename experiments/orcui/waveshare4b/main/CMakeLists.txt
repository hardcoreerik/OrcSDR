idf_component_register(
  SRCS "main.cpp"
  INCLUDE_DIRS "."
  REQUIRES orcui esp_timer
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
