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

if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()
