cmake_minimum_required(VERSION 3.16)
project(orcsdr_drone_watch_host LANGUAGES C CXX)
enable_testing()
add_executable(drone_watch_host_test
  host_test.cpp
  ../../ui/drone_watch_model.cpp
  ../../ui/drone_watch_dashboard.cpp
  ../../ui/screen_controller.cpp
)
target_compile_features(drone_watch_host_test PRIVATE cxx_std_17)
target_include_directories(drone_watch_host_test PRIVATE
  mocks
  ../../ui
)
if(MSVC)
  target_compile_options(drone_watch_host_test PRIVATE /W4 /WX)
else()
  target_compile_options(drone_watch_host_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME drone_watch_host COMMAND drone_watch_host_test)

# P1 Remote ID conformance: actual pinned C reference decoder + OrcSDR wrapper.
add_executable(drone_watch_rid_test
  rid_test.cpp
  ../../ui/drone_watch_rid.cpp
  ../../ui/third_party/opendroneid/opendroneid.c
)
target_compile_features(drone_watch_rid_test PRIVATE cxx_std_17)
target_include_directories(drone_watch_rid_test PRIVATE ../../ui)
if(MSVC)
  target_compile_options(drone_watch_rid_test PRIVATE /W4)
else()
  target_compile_options(drone_watch_rid_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Werror>")
endif()
if(UNIX)
  target_link_libraries(drone_watch_rid_test PRIVATE m)
endif()
add_test(NAME drone_watch_rid COMMAND drone_watch_rid_test)

add_executable(drone_watch_store_test
  store_test.cpp
  ../../ui/drone_watch_rid_store.cpp
  ../../ui/drone_watch_rid.cpp
  ../../ui/third_party/opendroneid/opendroneid.c
)
target_compile_features(drone_watch_store_test PRIVATE cxx_std_17)
target_include_directories(drone_watch_store_test PRIVATE ../../ui)
if(MSVC)
  target_compile_options(drone_watch_store_test PRIVATE /W4)
else()
  target_compile_options(drone_watch_store_test PRIVATE
    "$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic;-Werror>")
endif()
if(UNIX)
  target_link_libraries(drone_watch_store_test PRIVATE m)
endif()
add_test(NAME drone_watch_store COMMAND drone_watch_store_test)
