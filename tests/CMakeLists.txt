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

# The committed control-pulse tables must stay in step with the reference model
# they were transcribed from.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/ext/agcplusplus/src/block2/subinstructions.cpp")
    add_test(NAME subinst_tables_are_current
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/gen_subinst_tables.py --check)
endif()
