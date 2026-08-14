# Apollo Guidance Computer emulator — cycle-correct at the control-pulse level
# (see docs/COMPLETION_PLAN.md). The core is a static library that knows nothing
# about any frontend; frontends depend on the core, never the reverse
# (emulator-setup-guide.md §4).
cmake_minimum_required(VERSION 3.21)

project(agc
    VERSION 0.0.1
    DESCRIPTION "Cycle-correct Apollo Guidance Computer (Block II) emulator"
    LANGUAGES C)

# C17, not C23, and deliberately: every compiler we build on implements C17
# completely, while C23 support varies between them. Asking for a standard the
# CI toolchains only half-have means a developer with a newer compiler builds a
# different language from the one CI builds — a C23 construct compiles clean on
# their machine and fails the pedantic build on the runner, which is exactly how
# the Block I core landed with binary literals in it. Nothing here needs C23.
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(AGC_WERROR "Treat warnings as errors" OFF)

include(cmake/CompilerWarnings.cmake)

# --- Vendored third-party (ext/ submodules), built without our warning set ----
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ext/unity/src/unity.c")
    message(FATAL_ERROR
        "ext/unity is missing. Run: git submodule update --init ext/unity")
endif()

add_library(unity STATIC ext/unity/src/unity.c)
# SYSTEM so Unity's assert macros don't trip our strict warning set in tests.
target_include_directories(unity SYSTEM PUBLIC ext/unity/src)

# Unity picks its own noreturn spelling, and picks wrong for the compiler the
# Windows job actually runs: clang with the MSVC driver defines _MSC_VER but not
# __GNUC__, so with a current Windows SDK it falls past both special cases and
# includes <stdnoreturn.h>. That header defines `noreturn` as a macro, and the
# UCRT declares exit() as `__declspec(noreturn)` — which then expands to
# `__declspec(_Noreturn)` and fails to parse. C23 never reached that branch,
# which is why the standard change surfaced it. PUBLIC because the suites
# include unity.h themselves, and `#ifndef UNITY_NORETURN` is Unity's own
# documented way to be overridden — vendored code stays untouched.
if(WIN32 AND NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_definitions(unity PUBLIC UNITY_NORETURN=_Noreturn)
endif()

# --- First-party targets ------------------------------------------------------
enable_testing()
add_subdirectory(src/core)
add_subdirectory(src/core_block1)
add_subdirectory(src/frontend/headless)
add_subdirectory(src/frontend/sdl)
add_subdirectory(tests)
