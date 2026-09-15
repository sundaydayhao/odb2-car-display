cmake_minimum_required(VERSION 3.16)
project(dashboard_preview C)
option(ENABLE_CAMERA "Include optional camera UI" OFF)
set(CMAKE_C_STANDARD 99)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(DISPLAY_MAIN "${ROOT}/display_board/main")
set(LVGL "${ROOT}/display_board/managed_components/lvgl__lvgl")
file(GLOB_RECURSE LVGL_SOURCES "${LVGL}/src/*.c")
add_library(lvgl_host OBJECT ${LVGL_SOURCES})
target_include_directories(lvgl_host PUBLIC "${LVGL}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(lvgl_host PUBLIC LV_CONF_INCLUDE_SIMPLE LV_KCONFIG_IGNORE)
add_executable(dashboard_preview preview.c
    "${DISPLAY_MAIN}/dashboard.c"
    "${DISPLAY_MAIN}/media_model.c"
    "${DISPLAY_MAIN}/media_panel.c"
    "${DISPLAY_MAIN}/service_panel.c"
    "${DISPLAY_MAIN}/wifi_networks.c"
    "${DISPLAY_MAIN}/ble_devices.c"
    "${DISPLAY_MAIN}/fonts/dashboard_text_18.c"
    "${DISPLAY_MAIN}/fonts/dashboard_text_22.c"
    "${DISPLAY_MAIN}/fonts/dashboard_wifi_18.c"
    "${DISPLAY_MAIN}/fonts/dashboard_digits_144.c"
    "${DISPLAY_MAIN}/fonts/dashboard_digits_72.c")
target_include_directories(dashboard_preview PRIVATE "${DISPLAY_MAIN}")
target_compile_definitions(dashboard_preview PRIVATE CONFIG_DEVICE_CAMERA_ENABLED=$<BOOL:${ENABLE_CAMERA}>)
target_link_libraries(dashboard_preview PRIVATE lvgl_host)
if(NOT MSVC)
    target_link_libraries(dashboard_preview PRIVATE m)
endif()
