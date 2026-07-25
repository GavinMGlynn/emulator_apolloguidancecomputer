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

# Differential test against the runnable oracle: same rope, same number of
# timing pulses, compare registers and memory. This is what catches an
# instruction that computes the wrong answer at exactly the right speed — the
# failure mode that hid a broken divide behind green timing probes and five
# booting flight ropes (tools/oracle/FINDINGS.md #40).
#
# It builds the oracle out of ext/agcplusplus on first use and skips cleanly
# when that submodule is absent, so CI (which initialises only ext/unity) stays
# green without it. The quick sweep runs here; the full one is ~2 minutes and is
# run by hand.
if(Python3_Interpreter_FOUND)
    add_test(NAME oracle_differential
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/oracle/differential.py
                     --ours $<TARGET_FILE:agc_headless>
                     --oracle ${CMAKE_BINARY_DIR}/oracle
                     --quick)
    set_tests_properties(oracle_differential PROPERTIES TIMEOUT 600)
endif()

# The widest net in the project: the whole of erasable memory plus the register
# file after 200,000 MCTs of real flight software, hashed. The probes check what
# we thought to check; this notices a divergence anywhere in 2048 words, in
# software far more intricate than anything we would write by hand. It is also
# the identity harness any future optimization needs — "faster and
# bit-identical" is only a claim if something would have noticed.
#
# Ropes are not committed (tools/build_ropes.sh rebuilds them from the Virtual
# AGC submodule), so this skips cleanly when roms/ is empty and CI stays green.
if(Python3_Interpreter_FOUND)
    add_test(NAME rope_state_hashes
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/hash_ropes.py
                     --exe $<TARGET_FILE:agc_headless>)
    set_tests_properties(rope_state_hashes PROPERTIES TIMEOUT 900)
endif()
