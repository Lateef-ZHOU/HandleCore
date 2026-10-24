# Only used by the optional Pcap target. Core and Serial do not discover libpcap.
if(WIN32)
    find_path(HANDLECORE_PCAP_INCLUDE_DIR pcap.h PATH_SUFFIXES Include REQUIRED)
    if(CMAKE_GENERATOR_PLATFORM MATCHES "[Aa][Rr][Mm]64" OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(_pcap_lib_suffix Lib/ARM64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_pcap_lib_suffix Lib/x64)
    else()
        set(_pcap_lib_suffix Lib)
    endif()
    get_filename_component(_pcap_sdk "${HANDLECORE_PCAP_INCLUDE_DIR}/.." ABSOLUTE)
    # The SDK's plain Lib directory is x86; do not let default prefix search select it for x64/ARM64.
    find_library(HANDLECORE_PCAP_LIBRARY wpcap PATHS "${_pcap_sdk}/${_pcap_lib_suffix}" NO_DEFAULT_PATH REQUIRED)
    find_file(HANDLECORE_PCAP_RUNTIME wpcap.dll PATH_SUFFIXES Bin bin
        HINTS "$ENV{WINDIR}/System32/Npcap" REQUIRED)
    # Npcap's product version is not the bundled libpcap version. Inspect the selected DLL.
    if(CMAKE_CROSSCOMPILING)
        set(HANDLECORE_PCAP_VERSION "" CACHE STRING "libpcap version in the target Npcap runtime (cross compilation only)")
        if(NOT HANDLECORE_PCAP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
            message(FATAL_ERROR "Cross compilation requires HANDLECORE_PCAP_VERSION from the target Npcap runtime")
        endif()
    else()
        file(TO_NATIVE_PATH "${HANDLECORE_PCAP_RUNTIME}" _pcap_runtime_native)
        try_run(_pcap_version_run _pcap_version_build
            SOURCES "${CMAKE_CURRENT_LIST_DIR}/pcap_version.cpp"
            ARGS "${_pcap_runtime_native}"
            RUN_OUTPUT_VARIABLE _pcap_version_text)
        if(NOT _pcap_version_build OR NOT _pcap_version_run EQUAL 0)
            message(FATAL_ERROR "Cannot inspect libpcap version in ${HANDLECORE_PCAP_RUNTIME}: ${_pcap_version_text}")
        endif()
        if(NOT _pcap_version_text MATCHES "libpcap version ([0-9]+\\.[0-9]+\\.[0-9]+)")
            message(FATAL_ERROR "Unrecognized libpcap version: ${_pcap_version_text}")
        endif()
        set(HANDLECORE_PCAP_VERSION "${CMAKE_MATCH_1}")
    endif()
    if(HANDLECORE_PCAP_VERSION VERSION_LESS 1.10.0)
        message(FATAL_ERROR "Pcap requires libpcap >= 1.10.0, found ${HANDLECORE_PCAP_VERSION}")
    endif()
    message(STATUS "Pcap runtime: libpcap ${HANDLECORE_PCAP_VERSION} (${HANDLECORE_PCAP_RUNTIME})")
    add_library(HandlePcapDependency SHARED IMPORTED GLOBAL)
    set_target_properties(HandlePcapDependency PROPERTIES
        IMPORTED_IMPLIB "${HANDLECORE_PCAP_LIBRARY}"
        IMPORTED_LOCATION "${HANDLECORE_PCAP_RUNTIME}"
        INTERFACE_INCLUDE_DIRECTORIES "${HANDLECORE_PCAP_INCLUDE_DIR}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HANDLECORE_LIBPCAP REQUIRED IMPORTED_TARGET GLOBAL libpcap>=1.10.0)
    add_library(HandlePcapDependency ALIAS PkgConfig::HANDLECORE_LIBPCAP)
else()
    message(FATAL_ERROR "The Pcap module currently supports Windows/Npcap and Linux/libpcap")
endif()
