cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32p4" CACHE STRING "Vehicle display target")
set(PROJECT_VER "0.4.0")
# This directory is the complete P4 display-board project. The OBD interface
# board is a separate ESP-IDF project under obd_interface.
set(COMPONENT_DIRS
    "$ENV{IDF_PATH}/components"
    "${CMAKE_CURRENT_LIST_DIR}/main")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(odb2_dash)
if(NOT IDF_TARGET STREQUAL "esp32p4")
    message(FATAL_ERROR "Only ESP32-P4 Function EV Board is supported. Run idf.py set-target esp32p4.")
endif()
if(NOT IDF_VERSION_MAJOR EQUAL 5 OR NOT IDF_VERSION_MINOR EQUAL 5)
    message(FATAL_ERROR "This project requires ESP-IDF v5.5.x.")
endif()
