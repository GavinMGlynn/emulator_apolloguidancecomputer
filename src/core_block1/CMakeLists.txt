# The Block I core: a separate static library, sharing no code with the Block II
# core on purpose. They are different machines, and parameterising one into the
# other would compromise the reference core the whole project rests on.
add_library(agc1_core STATIC
    agc1.c
    pulses.c
    subinst.c
    subinst_tables.c)

target_include_directories(agc1_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
agc_set_warnings(agc1_core)
