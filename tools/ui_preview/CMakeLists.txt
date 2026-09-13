cmake_minimum_required(VERSION 3.16)
project(dashboard_preview C)
set(CMAKE_C_STANDARD 99)
get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LVGL "${ROOT}/managed_components/lvgl__lvgl")
file(GLOB_RECURSE LVGL_SOURCES "${LVGL}/src/*.c")
add_library(lvgl_host OBJECT ${LVGL_SOURCES})
target_include_directories(lvgl_host PUBLIC "${LVGL}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(lvgl_host PUBLIC LV_CONF_INCLUDE_SIMPLE LV_KCONFIG_IGNORE)
add_executable(dashboard_preview preview.c
    "${ROOT}/main/dashboard.c"
    "${ROOT}/main/media_model.c"
    "${ROOT}/main/media_panel.c"
    "${ROOT}/main/service_panel.c"
    "${ROOT}/main/wifi_networks.c"
    "${ROOT}/main/fonts/dashboard_text_18.c"
    "${ROOT}/main/fonts/dashboard_text_22.c"
    "${ROOT}/main/fonts/dashboard_wifi_18.c"
    "${ROOT}/main/fonts/dashboard_digits_144.c"
    "${ROOT}/main/fonts/dashboard_digits_72.c")
target_include_directories(dashboard_preview PRIVATE "${ROOT}/main")
target_link_libraries(dashboard_preview PRIVATE lvgl_host)
if(NOT MSVC)
    target_link_libraries(dashboard_preview PRIVATE m)
endif()
