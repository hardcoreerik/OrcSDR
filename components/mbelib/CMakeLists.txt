idf_component_register(
    SRCS "mbelib.c" "imbe7200x4400.c" "ecc.c"
    INCLUDE_DIRS "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-unused-variable -Wno-missing-braces)
