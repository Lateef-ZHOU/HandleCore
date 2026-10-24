execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env VSLANG=1033 "${CMAKE_COMMAND}"
        --build "${BUILD_DIR}" --config "${CONFIG}" --target "handle_reject_${CASE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors ENCODING UTF-8)
file(WRITE "${BUILD_DIR}/reject-${CASE}.log" "${output}\n${errors}")
if(result EQUAL 0)
    message(FATAL_ERROR "Invalid connection parameters were accepted (case ${CASE})")
endif()
if(NOT "${output}${errors}" MATCHES "HandleParameters")
    message(FATAL_ERROR "Build failed without identifying the parameter constraint: ${output}${errors}")
endif()
