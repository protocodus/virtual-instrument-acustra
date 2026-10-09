foreach(layout IN ITEMS normal extended)
    execute_process(COMMAND "${${layout}}" "${WORK}-${layout}.f32"
                    RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${layout} layout parity renderer failed: ${result}")
    endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
                "${WORK}-normal.f32" "${WORK}-extended.f32"
                RESULT_VARIABLE difference)
if(NOT difference EQUAL 0)
    message(FATAL_ERROR "Normal and extended storage changed ordinary-rate audio bytes")
endif()
message(STATUS "Normal and extended output bytes match at 44.1/48/96/192/384 kHz")
