# ============================================================
# CMakeLists.txt —— RK3566 智能中控终端
# 值守终端：采集/通信/存储模块 + Qt 界面
# ============================================================

cmake_minimum_required(VERSION 3.16)

project(rk3566_hub
    VERSION 0.6
    DESCRIPTION "RK3566 值守终端（门扉哨）"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(Qt5 REQUIRED COMPONENTS Core Gui Widgets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

# ---- RKNN Runtime（板子 /usr/lib/librknnrt.so）----
option(HUB_ENABLE_RKNN "Link librknnrt for NPU inference" ON)
set(HUB_HAVE_RKNN 0)
set(RKNN_RT_LIB "")

if(HUB_ENABLE_RKNN)
    find_library(RKNN_RT_LIB rknnrt
        PATHS
            /usr/lib
            /usr/local/lib
            ${CMAKE_SOURCE_DIR}/../rknpu2-master/rknpu2-master/runtime/RK356X/Linux/librknn_api/aarch64
            ${CMAKE_SOURCE_DIR}/third_party/rknn/lib
    )
    if(RKNN_RT_LIB)
        set(HUB_HAVE_RKNN 1)
        message(STATUS "Found librknnrt: ${RKNN_RT_LIB}")
    else()
        message(WARNING "librknnrt not found — NPU code compiles as stub. "
                        "On board: copy librknnrt.so to /usr/lib && sudo ldconfig")
    endif()
endif()

add_executable(rk3566_hub
    src/main.cpp
    src/ui/main_window.cpp
    # vision
    src/vision/hub_v4l2_capture.c
    src/vision/hub_presence.c
    src/vision/camera_worker.cpp
    src/vision/presence_guard.cpp
    src/vision/detector.cpp
    src/vision/rknn_engine.c
    # net
    src/net/hub_mqtt.c
    src/net/hub_protocol.c
    src/net/mqtt_client.cpp
    src/net/protocol.cpp
    # storage
    src/storage/cJSON.c
    src/storage/hub_db.c
    src/storage/database.cpp
)

target_include_directories(rk3566_hub PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rknn/include
)

target_compile_definitions(rk3566_hub PRIVATE
    HUB_HAVE_RKNN=${HUB_HAVE_RKNN}
)

target_link_libraries(rk3566_hub PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    PkgConfig::MOSQUITTO
    PkgConfig::SQLITE3
    m
)

if(HUB_HAVE_RKNN)
    target_link_libraries(rk3566_hub PRIVATE ${RKNN_RT_LIB})
endif()

target_compile_options(rk3566_hub PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic>>
    $<$<COMPILE_LANGUAGE:CXX>:$<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra -Wpedantic>>
    $<$<COMPILE_LANGUAGE:C>:$<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>>
)

include(GNUInstallDirs)
install(TARGETS rk3566_hub RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/data)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/models)

message(STATUS "============================================")
message(STATUS "  RK3566 Hub — CMake Configuration")
message(STATUS "============================================")
message(STATUS "  Qt5:        ${Qt5Core_VERSION}")
message(STATUS "  mosquitto:  ${MOSQUITTO_VERSION}")
message(STATUS "  sqlite3:    ${SQLITE3_VERSION}")
message(STATUS "  HUB_HAVE_RKNN: ${HUB_HAVE_RKNN}")
message(STATUS "  Output:     ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "============================================")
