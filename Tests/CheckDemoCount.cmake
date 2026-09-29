# The CI step "Render and verify demo audio" (.github/workflows/ci.yml)
# fails unless it finds exactly as many WAVs as it expects. That number is
# written there by hand on purpose - a demo that silently stops writing must
# fail CI - so this check keeps it equal to the renderer's own list.
#
#   cmake -DRENDERER=<AcustraRenderDemos> -DWORKFLOW=<ci.yml> -P CheckDemoCount.cmake
execute_process(COMMAND "${RENDERER}" --list
                RESULT_VARIABLE result
                OUTPUT_VARIABLE listing)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "AcustraRenderDemos --list failed (${result})")
endif()
string(REGEX MATCHALL "[^\n]+\\.wav" names "${listing}")
list(LENGTH names demos)

file(READ "${WORKFLOW}" workflow)
string(REGEX MATCHALL "rendered != [0-9]+" checks "${workflow}")
string(REGEX MATCHALL "expected [0-9]+" messages "${workflow}")
if(NOT checks)
    message(FATAL_ERROR "${WORKFLOW} has no 'rendered != N' demo check")
endif()
foreach(entry IN LISTS checks messages)
    string(REGEX MATCH "[0-9]+" expected "${entry}")
    if(NOT expected EQUAL demos)
        message(FATAL_ERROR
            "${WORKFLOW} expects ${expected} demo WAVs ('${entry}'), "
            "but AcustraRenderDemos renders ${demos}")
    endif()
endforeach()
message(STATUS "CI expects the renderer's ${demos} demos")
