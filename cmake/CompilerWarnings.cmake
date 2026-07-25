# agc_set_warnings(<target>) — the strict warning set applied to every
# first-party target, and NEVER to ext/ (emulator-setup-guide.md §5).
# -Wconversion / -Wsign-conversion / -Wswitch-enum catch real emulator bugs:
# the AGC is a 16-bit ones'-complement machine emulated with wider host types,
# and every implicit narrowing is a candidate sign/overflow bug.
function(agc_set_warnings target)
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
        -Wcast-qual -Wcast-align -Wpointer-arith -Wstrict-prototypes
        -Wmissing-prototypes -Wredundant-decls -Wundef -Wwrite-strings
        -Wdouble-promotion -Wformat=2 -Wswitch-enum -Wvla)
    if(AGC_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
endfunction()
