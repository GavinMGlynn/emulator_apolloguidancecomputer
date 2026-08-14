# The emulator core: a static library with zero frontend dependencies.
add_library(agc_core STATIC
    agc.c
    cpu/cpu.c
    cpu/pulses.c
    cpu/subinst.c
    cpu/subinst_tables.c
    dsky/dsky.c
    io/channels.c
    io/counters.c
    io/uplink.c
    memory/memory.c
    timing/scaler.c)

target_include_directories(agc_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
agc_set_warnings(agc_core)
