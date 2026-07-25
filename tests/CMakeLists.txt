# One executable and one CTest entry per suite, so a crash in one suite does not
# take down the rest (emulator-setup-guide.md §6).
function(agc_add_suite name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE agc_core unity)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    agc_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
endfunction()

agc_add_suite(memory_suite)
agc_add_suite(cpu_suite)
agc_add_suite(timing_suite)

find_package(Python3 COMPONENTS Interpreter)

# The committed control-pulse tables must stay in step with the reference model
# they were transcribed from. Skipped when ext/agcplusplus is not initialised,
# because CI does not need the oracles — only ext/unity.
if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/ext/agcplusplus/src/block2/subinstructions.cpp")
    add_test(NAME subinst_tables_are_current
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/gen_subinst_tables.py --check)
endif()

# The probe regression: self-contained rope images run through the headless
# frontend, checked against their goldens *and* against the instruction timings
# in AGC4 Memo #9. The probes and goldens are committed, so this needs no
# submodule and no downloaded rope — it runs on every CI platform and both
# build types, and the numbers are emulated timing pulses, so they must come
# out bit-identical everywhere.
if(Python3_Interpreter_FOUND)
    add_test(NAME probe_regression
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/regress.py
                     --exe $<TARGET_FILE:agc_headless>)
endif()
