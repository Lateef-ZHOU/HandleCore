# 某些 Config 包仅导出目标，没有 ConfigVersion 文件；两种入口均检查实际头文件。
find_package(libserialport CONFIG QUIET GLOBAL)
if(NOT TARGET libserialport::libserialport)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HANDLECORE_LIBSERIALPORT REQUIRED IMPORTED_TARGET GLOBAL libserialport>=0.1.1)
    add_library(libserialport::libserialport ALIAS PkgConfig::HANDLECORE_LIBSERIALPORT)
endif()

include(CMakePushCheckState)
include(CheckCXXSourceCompiles)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_LIBRARIES libserialport::libserialport)
# 改换依赖路径后重新检查，不能沿用另一套头文件的缓存结果。
unset(HANDLECORE_SERIAL_HEADERS_SUPPORTED CACHE)
check_cxx_source_compiles("
#include <libserialport.h>
#if !defined(SP_PACKAGE_VERSION_MAJOR) || !defined(SP_PACKAGE_VERSION_MINOR) || !defined(SP_PACKAGE_VERSION_MICRO)
#error libserialport version macros are required
#endif
static_assert(SP_PACKAGE_VERSION_MAJOR * 10000 + SP_PACKAGE_VERSION_MINOR * 100 + SP_PACKAGE_VERSION_MICRO >= 101,
              \"libserialport >= 0.1.1 required\");
int main() { return 0; }
" HANDLECORE_SERIAL_HEADERS_SUPPORTED)
cmake_pop_check_state()
if(NOT HANDLECORE_SERIAL_HEADERS_SUPPORTED)
    message(FATAL_ERROR "Serial requires usable libserialport headers >= 0.1.1; check the selected Config/pkg-config package and configure log")
endif()
