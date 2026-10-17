# The render tier: run the processor suite with ACUSTRA_EDITOR_SNAPSHOT set,
# then prove a whole PNG of the editor's default size landed. The suite itself
# asserts the render is opaque and detailed (not blank). Removing the old file
# first is what makes the check meaningful: it can only exist afterwards
# because this run wrote it.
#
# -DACUSTRA_PROCESSOR_TESTS=<AcustraPluginProcessorTests> -DACUSTRA_SCREENSHOT=<png>

foreach(required ACUSTRA_PROCESSOR_TESTS ACUSTRA_SCREENSHOT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()

file(REMOVE "${ACUSTRA_SCREENSHOT}")
set(ENV{ACUSTRA_EDITOR_SNAPSHOT} "${ACUSTRA_SCREENSHOT}")
if(WIN32)
    execute_process(COMMAND "${ACUSTRA_PROCESSOR_TESTS}" RESULT_VARIABLE result)
else()
    # The same stack limit as the Acustra.PluginProcessor registration.
    execute_process(COMMAND /bin/sh -c "ulimit -s 8176 2>/dev/null; exec \"$0\""
                            "${ACUSTRA_PROCESSOR_TESTS}"
                    RESULT_VARIABLE result)
endif()
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the processor suite failed (${result}); its screenshot is not published")
endif()
if(NOT EXISTS "${ACUSTRA_SCREENSHOT}")
    message(FATAL_ERROR "the processor suite did not write ${ACUSTRA_SCREENSHOT}")
endif()

file(SIZE "${ACUSTRA_SCREENSHOT}" size)
file(READ "${ACUSTRA_SCREENSHOT}" header LIMIT 24 HEX)
math(EXPR trailer_offset "${size} - 8")
file(READ "${ACUSTRA_SCREENSHOT}" trailer OFFSET ${trailer_offset} HEX)
if(NOT header MATCHES "^89504e470d0a1a0a0000000d49484452")
    message(FATAL_ERROR "${ACUSTRA_SCREENSHOT} is not a PNG")
endif()
# Every PNG ends with an empty IEND chunk and its constant CRC; without it the
# encoder did not finish.
if(NOT trailer STREQUAL "49454e44ae426082")
    message(FATAL_ERROR "${ACUSTRA_SCREENSHOT} has no IEND trailer")
endif()

# The editor opens at 1120 x 980 (editorWidth/editorHeight in
# Tests/PluginProcessorTests.cpp).
string(SUBSTRING "${header}" 32 8 width_hex)
string(SUBSTRING "${header}" 40 8 height_hex)
math(EXPR width "0x${width_hex}")
math(EXPR height "0x${height_hex}")
if(NOT width EQUAL 1120 OR NOT height EQUAL 980)
    message(FATAL_ERROR "${ACUSTRA_SCREENSHOT} is ${width} x ${height}, expected 1120 x 980")
endif()
message(STATUS "Rendered ${ACUSTRA_SCREENSHOT} (${width} x ${height}, ${size} bytes)")
