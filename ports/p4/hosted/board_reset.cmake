# ESP-Hosted 3.0.6 has a GPIO-only reset port and an SDIO GPIO assertion.
# Compile reviewed, hash-pinned copies with a real board reset; never edit the
# Component Manager checkout. Other profiles keep the upstream GPIO path.
if(NOT CONFIG_ORCSDR_BOARD_LILYGO_T_DISPLAY_P4_AMOLED)
    return()
endif()
if(NOT CONFIG_ESP_HOSTED_HOST_RESET_GPIO STREQUAL "-1")
    message(FATAL_ERROR "LilyGO C6 EN is on XL9535 IO14; Hosted RESET_GPIO must be -1")
endif()
if(NOT TARGET eh_host_port_idf OR NOT TARGET eh_host_mcu_transport)
    message(FATAL_ERROR "LilyGO board reset requires the ESP-Hosted 3.0.6 IDF SDIO host")
endif()

set(reset_adapter_dir "${CMAKE_CURRENT_LIST_DIR}")
set(reset_generated_dir "${CMAKE_BINARY_DIR}/orcsdr_hosted_board_reset")
file(MAKE_DIRECTORY "${reset_generated_dir}")

function(orcsdr_hosted_reset_source target filename expected_sha)
    get_target_property(sources ${target} SOURCES)
    get_target_property(source_dir ${target} SOURCE_DIR)
    set(found FALSE)
    set(replacement_sources "")
    foreach(source IN LISTS sources)
        get_filename_component(name "${source}" NAME)
        if(name STREQUAL filename)
            if(found)
                message(FATAL_ERROR "Duplicate Hosted reset source: ${filename}")
            endif()
            set(found TRUE)
            get_filename_component(original "${source}" ABSOLUTE BASE_DIR "${source_dir}")
            set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
            file(READ "${original}" content)
            string(REPLACE "\r" "" content "${content}")
            file(SHA256 "${original}" sha)
            if(NOT sha STREQUAL expected_sha)
                message(FATAL_ERROR "ESP-Hosted reset source changed: ${original} (${sha}); review the board reset adapter before updating its 3.0.6 pin")
            endif()
            if(filename STREQUAL "eh_host_port_power.c")
                # Preserve the upstream GPIO branch, sleep functions and settle policy.
                set(no_gpio [=[if (rst.pin < 0) {
        /* integrator opted out of in-band reset; not a failure */
        ESP_LOGI(EH_HOST_PORT_RESET_TAG,
                 "RST pin not configured (-1); skipping slave reset");
        return EH_HOST_PORT_ERR_NOT_CONFIGURED;
    }]=])
                string(REPLACE "${no_gpio}" "if (rst.pin < 0) {\n        return orcsdr_hosted_board_reset();\n    }" content "${content}")
            else()
                string(REPLACE "assert(reset_pin.pin != -1);"
                    "if (reset_pin.pin < 0 && !orcsdr_hosted_board_reset_available()) {\n        ESP_LOGE(TAG, \"No board reset available\");\n        return -1;\n    }" content "${content}")
                string(REPLACE "transport_gpio_reset(bus_handle, reset_pin);"
                    "if (transport_gpio_reset(bus_handle, reset_pin) != ESP_OK) return -1;" content "${content}")
                string(REPLACE "ESP_LOGW(TAG, \"Reset co-processor using GPIO[%u]\", reset_pin.pin);"
                    "ESP_LOGW(TAG, \"Reset co-processor using board enable\");" content "${content}")
            endif()
            set(generated "${reset_generated_dir}/${filename}")
            file(CONFIGURE OUTPUT "${generated}" CONTENT "#include \"orcsdr_hosted_board_reset.h\"\n${content}" @ONLY)
            list(APPEND replacement_sources "${generated}")
        else()
            list(APPEND replacement_sources "${source}")
        endif()
    endforeach()
    if(NOT found)
        message(FATAL_ERROR "ESP-Hosted 3.0.6 reset source missing: ${target}/${filename}")
    endif()
    set_property(TARGET ${target} PROPERTY SOURCES "${replacement_sources}")
    target_include_directories(${target} PRIVATE "${reset_adapter_dir}")
endfunction()

orcsdr_hosted_reset_source(eh_host_port_idf eh_host_port_power.c
    120fe73fb6190d65189adacef0742104fdf299c58858a10f100f3c0ebfdce392)
orcsdr_hosted_reset_source(eh_host_mcu_transport eh_host_bus_sdio.c
    4c2118c747b10b41fc02ad47111114a379b597f81e6320e6f289bd5611d4f7c8)
target_sources(eh_host_port_idf PRIVATE "${reset_adapter_dir}/orcsdr_hosted_board_reset.c")
target_link_libraries(eh_host_port_idf PRIVATE idf::orcsdr_board_hosyond)
message(STATUS "ESP-Hosted 3.0.6: board-owned LilyGO C6 expander reset enabled")
