cmake_minimum_required(VERSION 3.28)
project(LidarHandleCore LANGUAGES CXX)

# 支持基线由 CI 同步验证；更早版本不属于当前承诺范围。
find_package(Qt6 6.8.3 REQUIRED COMPONENTS Core)

# 直接使用生产头；此目标不包含设备、QML 登记或任何具体传输实现。
add_library(HandleCore STATIC include/HandleBase.h)
set_target_properties(HandleCore PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
qt_extract_metatypes(HandleCore)
add_library(Lidar::HandleCore ALIAS HandleCore)
target_compile_features(HandleCore PUBLIC cxx_std_20)
target_include_directories(HandleCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(HandleCore PUBLIC Qt6::Core)

option(HANDLECORE_WITH_SERIAL "Build the portable serial transport target and its tests" OFF)
if(HANDLECORE_WITH_SERIAL)
    include(cmake/SerialDependency.cmake)
    add_library(HandleSerial INTERFACE)
    add_library(Lidar::Serial ALIAS HandleSerial)
    target_sources(HandleSerial INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/SerialHandle.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/SerialPort.h")
    target_link_libraries(HandleSerial INTERFACE Lidar::HandleCore libserialport::libserialport)
endif()

option(HANDLECORE_WITH_PCAP "Build the Ethernet ARP/UDP Pcap module and its tests" OFF)
if(HANDLECORE_WITH_PCAP)
    include(cmake/PcapDependency.cmake)
    add_library(HandlePcap STATIC)
    set_target_properties(HandlePcap PROPERTIES AUTOMOC ON)
    if(MSVC)
        target_compile_options(HandlePcap PRIVATE /W4)
    endif()
    add_library(Lidar::Pcap ALIAS HandlePcap)
    target_sources(HandlePcap PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/PcapHandle.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/PcapProtocol.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/PcapInterfaces.h")
    target_link_libraries(HandlePcap PUBLIC Lidar::HandleCore HandlePcapDependency)
    if(WIN32)
        target_link_libraries(HandlePcap PUBLIC iphlpapi)
        target_compile_definitions(HandlePcap PUBLIC NOMINMAX)
    endif()
endif()

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
