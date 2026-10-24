cmake_minimum_required(VERSION 3.28)
project(LidarHandleCore LANGUAGES CXX)

# 支持基线由 CI 同步验证；更早版本不属于当前承诺范围。
find_package(Qt6 6.8.3 REQUIRED COMPONENTS Core)

# 直接使用生产头；此目标不包含设备、QML 登记或任何具体传输实现。
add_library(HandleCore INTERFACE)
add_library(Lidar::HandleCore ALIAS HandleCore)
target_compile_features(HandleCore INTERFACE cxx_std_20)
target_include_directories(HandleCore INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_sources(HandleCore INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include/HandleBase.h")
target_link_libraries(HandleCore INTERFACE Qt6::Core)

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
    add_library(HandlePcap INTERFACE)
    add_library(Lidar::Pcap ALIAS HandlePcap)
    target_sources(HandlePcap INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/PcapHandle.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/PcapProtocol.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/PcapInterfaces.h")
    target_link_libraries(HandlePcap INTERFACE Lidar::HandleCore HandlePcapDependency)
    if(WIN32)
        target_link_libraries(HandlePcap INTERFACE iphlpapi)
        target_compile_definitions(HandlePcap INTERFACE NOMINMAX)
    endif()
endif()

option(HANDLECORE_WITH_SIM_GIMBAL "Build the Qt Core simulated two-axis gimbal module and its tests" OFF)
if(HANDLECORE_WITH_SIM_GIMBAL)
    add_library(HandleSimGimbal INTERFACE)
    add_library(Lidar::SimGimbal ALIAS HandleSimGimbal)
    target_sources(HandleSimGimbal INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/SimGimbalHandle.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/SimGimbalProtocol.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/GimbalMotion.h")
    target_link_libraries(HandleSimGimbal INTERFACE Lidar::HandleCore)
endif()

option(HANDLECORE_WITH_SIM_CARD "Build the typed simulated acquisition executor and its tests" OFF)
if(HANDLECORE_WITH_SIM_CARD)
    add_library(HandleSimCard INTERFACE)
    add_library(Lidar::SimCard ALIAS HandleSimCard)
    target_sources(HandleSimCard INTERFACE
        "${CMAKE_CURRENT_SOURCE_DIR}/include/SimCardHandle.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/include/SimCardProtocol.h")
    target_link_libraries(HandleSimCard INTERFACE Lidar::HandleCore)
endif()

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
