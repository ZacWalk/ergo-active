cmake_minimum_required(VERSION 3.21)

project(ergo-active LANGUAGES CXX VERSION 1.0.0)

if(NOT WIN32)
    message(FATAL_ERROR "ergo-active is a Win32 SDK application and only builds on Windows.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "ergo-active requires MSVC; no other toolchain is supported.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ergo-active is 64-bit only; configure with an x64 toolchain.")
endif()

enable_language(RC)

add_executable(ergo-active WIN32
    src/main.cpp
    src/ergo-active.rc
)

target_sources(ergo-active PRIVATE
    src/data.h
    src/resource.h
    src/ui-frame.h
    src/ui-task-bar-icon.h
    src/ui.h
    src/win.h
)

target_compile_features(ergo-active PRIVATE cxx_std_20)
target_include_directories(ergo-active PRIVATE src)
target_compile_definitions(ergo-active PRIVATE WIN32 _WINDOWS STRICT UNICODE _UNICODE)
target_compile_options(ergo-active PRIVATE /W3 /MP)

# The .rc embeds res/ergo-active.exe.manifest, so CMake must not add its own.
set_target_properties(ergo-active PROPERTIES
    OUTPUT_NAME ergo-active-64
    DEBUG_POSTFIX d
    LINK_FLAGS "/MANIFEST:NO"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/exe"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/exe"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/exe"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(ergo_active_tests tests/smoke.cpp)
    target_compile_features(ergo_active_tests PRIVATE cxx_std_20)
    add_test(NAME ergo_active_unit COMMAND ergo_active_tests)
    set_tests_properties(ergo_active_unit PROPERTIES TIMEOUT 30 LABELS ergo-active)
endif()
