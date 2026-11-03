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

add_executable(orcsdr_battery_service_test
    battery_service/orcsdr_battery_service_test.c
    battery_service/fake_freertos.c
    bq27220_fake_i2c.c
    ../src/bq27220_gauge.c
    ../src/orcsdr_battery.c)
target_include_directories(orcsdr_battery_service_test PRIVATE
    battery_service/fakes
    bq27220_fake
    ../include)
target_compile_features(orcsdr_battery_service_test PRIVATE c_std_11)
target_compile_definitions(orcsdr_battery_service_test PRIVATE ORCSDR_BATTERY_TESTING)
if(MSVC)
    target_compile_options(orcsdr_battery_service_test PRIVATE /W4 /WX)
else()
    target_compile_options(orcsdr_battery_service_test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

foreach(scenario IN ITEMS valid_snapshot absent_cell read_failure stale_snapshot duplicate_start task_failure bus_failure)
    add_test(NAME orcsdr_battery_service_${scenario}
             COMMAND orcsdr_battery_service_test ${scenario})
endforeach()
