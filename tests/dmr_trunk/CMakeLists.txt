cmake_minimum_required(VERSION 3.16)
project(orcsdr_dmr_trunk LANGUAGES CXX)
enable_testing()
add_library(trunk ../../components/orcsdr_dmr_trunk/trunk.cpp)
target_include_directories(trunk PUBLIC ../../components/orcsdr_dmr_trunk/include)
target_compile_features(trunk PUBLIC cxx_std_17)
add_executable(trunk_tests trunk_tests.cpp)
target_link_libraries(trunk_tests PRIVATE trunk)
add_test(NAME trunk_tests COMMAND trunk_tests)

set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(DMR ${ROOT}/components/orcsdr_dmr)
set(UI ${ROOT}/apps/orcsdr-tab5/ui)
add_executable(trunk_native_replay native_replay.cpp
    ${UI}/dv_channelizer.cpp
    ${DMR}/src/d0.cpp
    ${DMR}/d1_protocol/src/d1.cpp
    ${DMR}/d1_frontend/src/frontend.cpp
    ${DMR}/d1_integration/src/d1_bridge.cpp)
target_include_directories(trunk_native_replay PRIVATE
    ${UI} ${DMR}/include ${DMR}/d1_protocol/include
    ${DMR}/d1_frontend/include ${DMR}/d1_integration/include)
target_link_libraries(trunk_native_replay PRIVATE trunk)
add_test(NAME trunk_native_replay COMMAND trunk_native_replay ${ROOT}/tests/digital_voice_corpus/iq)
