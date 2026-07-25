add_executable(agc_headless main.c)
target_link_libraries(agc_headless PRIVATE agc_core)
agc_set_warnings(agc_headless)
