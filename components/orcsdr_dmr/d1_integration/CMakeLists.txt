cmake_minimum_required(VERSION 3.16)
project(orcsdr_dmr_d1_integration LANGUAGES CXX)
option(DMR_D1_INTEGRATION_SANITIZE "Enable ASan/UBSan with GCC/Clang" OFF)
add_library(orcsdr_dmr_d1_integration STATIC
    src/d1_bridge.cpp
    ../d1_frontend/src/frontend.cpp
    ../d1_protocol/src/d1.cpp
    ../src/d0.cpp)
target_compile_features(orcsdr_dmr_d1_integration PUBLIC cxx_std_17)
target_include_directories(orcsdr_dmr_d1_integration PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include"
    "${CMAKE_CURRENT_LIST_DIR}/../d1_frontend/include"
    "${CMAKE_CURRENT_LIST_DIR}/../d1_protocol/include"
    "${CMAKE_CURRENT_LIST_DIR}/../include")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(orcsdr_dmr_d1_integration PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(DMR_D1_INTEGRATION_SANITIZE)
        target_compile_options(orcsdr_dmr_d1_integration PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(orcsdr_dmr_d1_integration PUBLIC -fsanitize=address,undefined)
    endif()
endif()
enable_testing()
add_executable(dmr_d1_integration_tests tests/test_d1_bridge.cpp)
target_link_libraries(dmr_d1_integration_tests PRIVATE orcsdr_dmr_d1_integration)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(dmr_d1_integration_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME dmr_d1_integration_tests COMMAND dmr_d1_integration_tests)
