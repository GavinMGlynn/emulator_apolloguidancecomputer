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
agc_add_suite(dsky_suite)
agc_add_suite(uplink_suite)
agc_add_suite(cdu_suite)
agc_add_suite(imu_suite)
agc_add_suite(telemetry_suite)

# The Block I core is a separate library and a separate machine, so its suite
# links that instead.
add_executable(block1_suite block1_suite.c)
target_link_libraries(block1_suite PRIVATE agc1_core unity)
agc_set_warnings(block1_suite)
add_test(NAME block1_suite COMMAND block1_suite)

find_package(Python3 COMPONENTS Interpreter)

# The committed control-pulse tables must stay in step with the reference model
# they were transcribed from. Skipped when ext/agcplusplus is not initialised,
# because CI does not need the oracles — only ext/unity.
if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/ext/agcplusplus/src/block2/subinstructions.cpp")
    add_test(NAME subinst_tables_are_current
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/gen_subinst_tables.py --check)
endif()

# The Block I tables, like the Block II ones, must stay in step with the model
# they were transcribed from.
if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/ext/agcplusplus/src/block1/subinstructions.cpp")
    add_test(NAME block1_tables_are_current
             COMMAND ${Python3_EXECUTABLE}
                     ${CMAKE_SOURCE_DIR}/tools/gen_block1_tables.py --check)
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

    # The instrument the probes are read with, checked against a rope built to
    # need both of its forms: a cell written non-zero and then cleared. The
    # clear is the moment `--sentinel A` cannot see, because the value it lands
    # on is the one that means "nothing has happened yet".
    add_test(NAME sentinel_forms
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/test_sentinel.py
                     --exe $<TARGET_FILE:agc_headless>)
endif()

# The third opinion on the pulse tables, and the only one that is hardware
# rather than a model: every subinstruction, every branch condition and all
# twelve timing pulses, read out of the gate netlists in ext/agc_simulation
# (generated from the original MIT logic flow diagrams) and diffed against
# src/core/cpu/subinst_tables.c. The memo is the primary source and
# ext/agcplusplus is the corrected reading of it; when those two disagree this
# is what decides, and it is what turned up the MP3A term on the carry gate
# (tools/oracle/FINDINGS.md #11).
#
# Skips cleanly when ext/agc_simulation is absent, so CI stays green with only
# ext/unity initialised.
if(Python3_Interpreter_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/ext/agc_simulation/modules/crosspoint_ii.v")
    add_test(NAME gate_level_crosspoint
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/oracle/gate_diff.py)
    set_tests_properties(gate_level_crosspoint PROPERTIES TIMEOUT 600)
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

# MIT's own instruction validation suite, run to completion and its verdict read
# off the panel rather than inferred from the absence of an alarm. The rope
# reports through the DSKY — a code in PROG, a sub-code in NOUN, OPR ERR lit,
# waiting for PRO — so this presses PRO at every stop and checks that the only
# codes displayed are the opening checkpoint and the closing PROG 77.
#
# Needs the assembled rope, so it skips cleanly when roms/ is empty, as CI's is.
if(Python3_Interpreter_FOUND)
    add_test(NAME mit_validation_suite
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/validation.py
                     --exe $<TARGET_FILE:agc_headless>)
    set_tests_properties(mit_validation_suite PROPERTIES TIMEOUT 900)
endif()

# The physical rope modules: dumps read back from real Apollo-era hardware,
# including one defective article and the same one repaired. Checks the rope
# image layout end to end (LM131R1 assembled from source against the dump taken
# off the article), odd parity across every module, and the PARITY FAIL alarm
# proving itself against damage nobody simulated.
#
# Needs the staged dumps, so it skips cleanly when bios/ is empty, as CI's is.
if(Python3_Interpreter_FOUND)
    add_test(NAME physical_rope_modules
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/rope_modules.py
                     --exe $<TARGET_FILE:agc_headless>)
    set_tests_properties(physical_rope_modules PROPERTIES TIMEOUT 600)
endif()

# The interactive DSKY, run headlessly under SDL's dummy video driver. A display
# that silently stops updating looks fine, so it is worth checking in CI that
# the panel still shows what the headless frontend reads off the same rope.
# Skips when SDL3 was not found or the rope is not assembled.
if(Python3_Interpreter_FOUND AND TARGET agc_dsky)
    add_test(NAME dsky_frontend
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/dsky_smoke.py
                     --exe $<TARGET_FILE:agc_dsky>)
    set_tests_properties(dsky_frontend PROPERTIES TIMEOUT 300)
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
