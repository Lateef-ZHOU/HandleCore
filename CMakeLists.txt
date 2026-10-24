cmake_minimum_required(VERSION 3.28)
project(LidarHandleCore LANGUAGES CXX)

find_package(Qt6 REQUIRED COMPONENTS Core)

# 直接使用生产头；此目标不包含设备、QML 登记或任何具体传输实现。
add_library(HandleCore INTERFACE)
add_library(Lidar::HandleCore ALIAS HandleCore)
target_compile_features(HandleCore INTERFACE cxx_std_20)
target_include_directories(HandleCore INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_sources(HandleCore INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include/HandleBase.h")
target_link_libraries(HandleCore INTERFACE Qt6::Core)

option(HANDLECORE_WITH_SERIAL "Build the portable serial transport target and its tests" OFF)
if(HANDLECORE_WITH_SERIAL)
    find_package(libserialport CONFIG QUIET)
    if(NOT TARGET libserialport::libserialport)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(HANDLECORE_LIBSERIALPORT REQUIRED IMPORTED_TARGET GLOBAL libserialport>=0.1.1)
        add_library(libserialport::libserialport ALIAS PkgConfig::HANDLECORE_LIBSERIALPORT)
    endif()
    add_library(HandleSerial INTERFACE)
    add_library(Lidar::Serial ALIAS HandleSerial)
    target_sources(HandleSerial INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/SerialHandle.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/SerialPort.h")
    target_link_libraries(HandleSerial INTERFACE Lidar::HandleCore libserialport::libserialport)
endif()

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
