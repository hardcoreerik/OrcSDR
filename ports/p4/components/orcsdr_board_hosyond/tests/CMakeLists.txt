cmake_minimum_required(VERSION 3.16)
project(bq27220_gauge_host_tests C)
add_executable(bq27220_gauge_test
    bq27220_gauge_test.c
    bq27220_fake_i2c.c
    ../src/bq27220_gauge.c)
target_include_directories(bq27220_gauge_test PRIVATE bq27220_fake ../include)
target_compile_features(bq27220_gauge_test PRIVATE c_std_11)
if(MSVC)
    target_compile_options(bq27220_gauge_test PRIVATE /W4 /WX)
else()
    target_compile_options(bq27220_gauge_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
enable_testing()
add_test(NAME bq27220_gauge COMMAND bq27220_gauge_test)
