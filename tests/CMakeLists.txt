add_executable(handle_core_probe probe.cpp)
set_target_properties(handle_core_probe PROPERTIES AUTOMOC ON)
target_link_libraries(handle_core_probe PRIVATE Lidar::HandleCore)
target_compile_definitions(handle_core_probe PRIVATE QT_NO_KEYWORDS)
if(MSVC)
    target_compile_options(handle_core_probe PRIVATE /W4 /WX)
else()
    target_compile_options(handle_core_probe PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME handle_core COMMAND handle_core_probe)
set_tests_properties(handle_core PROPERTIES TIMEOUT 30)
if(WIN32)
    set_tests_properties(handle_core PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

foreach(case RANGE 1 4)
    add_library(handle_reject_${case} OBJECT EXCLUDE_FROM_ALL invalid_params.cpp)
    target_link_libraries(handle_reject_${case} PRIVATE Lidar::HandleCore)
    target_compile_definitions(handle_reject_${case} PRIVATE INVALID_PARAM_CASE=${case})
    add_test(NAME handle_reject_${case} COMMAND "${CMAKE_COMMAND}"
        "-DBUILD_DIR=${CMAKE_BINARY_DIR}" "-DCONFIG=$<CONFIG>" "-DCASE=${case}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_rejection.cmake")
    set_tests_properties(handle_reject_${case} PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
endforeach()
