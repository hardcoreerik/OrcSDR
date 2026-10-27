cmake_minimum_required(VERSION 3.16)
project(orcsdr_drone_watch_host LANGUAGES CXX)
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
