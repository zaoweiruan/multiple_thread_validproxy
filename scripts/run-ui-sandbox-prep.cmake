# scripts/run-ui-sandbox-prep.cmake - ctest driver for prepare-ui-sandbox.bat.
#
# Why this wrapper exists: ctest quotes EVERY argument when it builds the
# child command line, so `add_test(... COMMAND cmd /c "path\to\x.bat")`
# reaches cmd.exe as `cmd "/c" "path\to\x.bat"`. cmd.exe's quote-stripping
# rule (first char is a quote AND != 2 quotes total -> strip the first and
# last quote) then turns that into `/c" "path\to\x.bat`, which aborts with
# "The syntax of the command is incorrect." before the bat ever runs.
#
# execute_process() assembles the command line itself, so the bat is invoked
# with clean args. The bat remains the single source of truth for sandbox
# preparation (shared with scripts/build-and-test.bat); no logic duplicated.
#
# Usage (from CMakeLists.txt):
#   add_test(NAME UI_SANDBOX_PREP
#            COMMAND ${CMAKE_COMMAND} -DSANDBOX_BAT=<native path> -P
#                    ${CMAKE_SOURCE_DIR}/scripts/run-ui-sandbox-prep.cmake)

if(NOT DEFINED SANDBOX_BAT)
    message(FATAL_ERROR "SANDBOX_BAT must point to prepare-ui-sandbox.bat")
endif()
if(NOT EXISTS "${SANDBOX_BAT}")
    message(FATAL_ERROR "SANDBOX_BAT not found: ${SANDBOX_BAT}")
endif()

execute_process(
    COMMAND cmd /c "${SANDBOX_BAT}"
    RESULT_VARIABLE rc
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "prepare-ui-sandbox.bat failed with exit code ${rc}")
endif()
