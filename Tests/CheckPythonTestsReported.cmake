# A configure whose Python cannot import numpy and scipy registers fewer
# tests. It must say so, and ACUSTRA_REQUIRE_PYTHON_TESTS must turn that into
# a configure error (CI sets it). Run on a Unix host:
#
#   cmake -DSOURCE=<repo> -DWORK=<scratch dir> -DPYTHON=<python3> -P CheckPythonTestsReported.cmake
#
# The interpreter under test is PYTHON behind a wrapper whose numpy import
# fails, so no second Python installation is needed.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/hide")
file(WRITE "${WORK}/hide/numpy.py" "raise ImportError('hidden by the Acustra configure check')\n")
file(WRITE "${WORK}/python3"
     "#!/bin/sh\nPYTHONPATH=\"${WORK}/hide\" exec \"${PYTHON}\" \"$@\"\n")
file(CHMOD "${WORK}/python3" PERMISSIONS
     OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE)

set(arguments -S "${SOURCE}" -DACUSTRA_BUILD_PLUGIN=OFF -DACUSTRA_BUILD_TOOLS=ON
    -DBUILD_TESTING=ON "-DPython3_EXECUTABLE=${WORK}/python3")

execute_process(COMMAND "${CMAKE_COMMAND}" ${arguments} -B "${WORK}/quiet"
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "the default configure failed without numpy:\n${errors}")
endif()
if(NOT errors MATCHES "cannot import numpy and scipy")
    message(FATAL_ERROR "a configure without numpy dropped the Python self-tests silently")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" ${arguments} -B "${WORK}/required"
                        -DACUSTRA_REQUIRE_PYTHON_TESTS=ON
                RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(result EQUAL 0)
    message(FATAL_ERROR "ACUSTRA_REQUIRE_PYTHON_TESTS configured without numpy")
endif()
file(REMOVE_RECURSE "${WORK}")
message(STATUS "a configure without numpy reports the skipped Python self-tests")
