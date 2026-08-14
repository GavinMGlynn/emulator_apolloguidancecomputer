# The interactive DSKY. Built only when SDL3 is available: the core and the
# headless frontend are the parts CI needs, and neither depends on this.
find_package(SDL3 QUIET)
if(NOT SDL3_FOUND)
    message(STATUS "SDL3 not found — skipping the interactive DSKY frontend")
    return()
endif()

add_executable(agc_dsky main.c)
target_link_libraries(agc_dsky PRIVATE agc_core SDL3::SDL3)
agc_set_warnings(agc_dsky)

# Screenshots are how a headless run gets verified on the real output rather
# than a proxy, so libpng is worth having — but not worth requiring.
find_package(PNG QUIET)
if(PNG_FOUND)
    target_link_libraries(agc_dsky PRIVATE PNG::PNG)
    target_compile_definitions(agc_dsky PRIVATE AGC_HAVE_PNG)
else()
    message(STATUS "libpng not found — the DSKY frontend will build without --screenshot")
endif()
