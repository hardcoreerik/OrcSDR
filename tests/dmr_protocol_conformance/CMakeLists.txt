cmake_minimum_required(VERSION 3.16)
project(orcsdr_dmr_protocol_conformance LANGUAGES CXX)

# Host only. Reuse production files READ-ONLY; no edits to existing CMake.
get_filename_component(ORC_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(DMR_ROOT "${ORC_ROOT}/components/orcsdr_dmr")
foreach(required IN ITEMS
    "${DMR_ROOT}/include/orcsdr_dmr/d0.hpp"
    "${DMR_ROOT}/src/d0.cpp"
    "${DMR_ROOT}/d1_protocol/include/orcsdr_dmr/d1.hpp"
    "${DMR_ROOT}/d1_protocol/src/d1.cpp")
    if(NOT EXISTS "${required}")
        message(FATAL_ERROR "Missing read-only decoder source: ${required}")
    endif()
endforeach()
add_library(dmr_conformance_subject STATIC
    "${DMR_ROOT}/src/d0.cpp"
    "${DMR_ROOT}/d1_protocol/src/d1.cpp")
target_include_directories(dmr_conformance_subject PUBLIC
    "${DMR_ROOT}/include" "${DMR_ROOT}/d1_protocol/include")
target_compile_features(dmr_conformance_subject PUBLIC cxx_std_17)
option(DMR_CONFORMANCE_SANITIZERS "Enable GCC/Clang address+undefined sanitizers" OFF)
if(MSVC)
    target_compile_options(dmr_conformance_subject PRIVATE /W4 /WX)
else()
    target_compile_options(dmr_conformance_subject PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
if(DMR_CONFORMANCE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizer option requires GCC or Clang")
    endif()
    target_compile_options(dmr_conformance_subject PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(dmr_conformance_subject PUBLIC -fsanitize=address,undefined)
endif()
add_executable(dmr_protocol_conformance test_conformance.cpp)
target_link_libraries(dmr_protocol_conformance PRIVATE dmr_conformance_subject)
if(MSVC)
    target_compile_options(dmr_protocol_conformance PRIVATE /W4 /WX)
else()
    target_compile_options(dmr_protocol_conformance PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
enable_testing()
foreach(suite IN ITEMS golay cach_bptc integrity stream late_entry)
    add_test(NAME dmr_conformance_${suite} COMMAND dmr_protocol_conformance ${suite})
endforeach()
