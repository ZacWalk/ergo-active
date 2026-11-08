cmake_minimum_required(VERSION 3.21)

project(ergo-active LANGUAGES CXX VERSION 1.1.0)

if(NOT WIN32)
    message(FATAL_ERROR "ergo-active is a Win32 desktop application and only builds on Windows.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "ergo-active requires MSVC; no other toolchain is supported.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "ergo-active is 64-bit only; configure with an x64 toolchain.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")

# The application and the static LGPL platform layer use the same CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Develop against a sibling checkout when one exists; a fresh checkout fetches
# the published revision recorded in cmake/dd-dependencies.json.
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_LIBDESKTOP
   AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../libdesktop/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_LIBDESKTOP "${CMAKE_CURRENT_SOURCE_DIR}/../libdesktop"
        CACHE PATH "Local libdesktop checkout to build against")
endif()

include(FetchContent)
FetchContent_Declare(libdesktop
    GIT_REPOSITORY https://github.com/ZacWalk/libdesktop.git
    GIT_TAG        04109ee82a500db38d439fa281bbc3f43124adfe
)
FetchContent_MakeAvailable(libdesktop)

libdesktop_add_app(ergo-active
    SOURCES
        src/main.cpp
        src/app-services.h
        src/data.h
        src/dashboard-layout.h
        src/dashboard-policy.h
        src/desktop-services.h
        src/ui-frame.h
        src/win-services.h
    ICON        src/res/ergo-active.ico
    DESCRIPTION "ergo-active break reminder"
    VERSION     1.1.0.0
    OUTPUT_NAME ergo-active-64
)

target_include_directories(ergo-active PRIVATE src)
target_link_libraries(ergo-active PRIVATE libdesktop::ui)
target_compile_options(ergo-active PRIVATE /W3 /MP /EHsc /utf-8)

set_target_properties(ergo-active PROPERTIES
    DEBUG_POSTFIX d
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

    add_executable(ergo_active_usage_tests tests/usage.cpp)
    target_compile_features(ergo_active_usage_tests PRIVATE cxx_std_20)
    target_include_directories(ergo_active_usage_tests PRIVATE src)
    target_compile_options(ergo_active_usage_tests PRIVATE /W3 /EHsc /utf-8)
    add_test(NAME ergo_active_usage COMMAND ergo_active_usage_tests)
    set_tests_properties(ergo_active_usage PROPERTIES TIMEOUT 30 LABELS ergo-active)

    add_executable(ergo_active_dashboard_tests tests/dashboard.cpp)
    target_compile_features(ergo_active_dashboard_tests PRIVATE cxx_std_20)
    target_include_directories(ergo_active_dashboard_tests PRIVATE src)
    target_compile_definitions(ergo_active_dashboard_tests PRIVATE ERGO_ACTIVE_TESTS)
    target_link_libraries(ergo_active_dashboard_tests PRIVATE libdesktop::ui)
    target_compile_options(ergo_active_dashboard_tests PRIVATE /W3 /EHsc /utf-8)
    add_test(NAME ergo_active_dashboard COMMAND ergo_active_dashboard_tests)
    set_tests_properties(ergo_active_dashboard PROPERTIES TIMEOUT 30 LABELS ergo-active)

    add_test(NAME ergo_active_manifest COMMAND "${CMAKE_COMMAND}"
        "-DEXE=$<TARGET_FILE:ergo-active>" -P "${PROJECT_SOURCE_DIR}/tests/manifest.cmake")
    set_tests_properties(ergo_active_manifest PROPERTIES TIMEOUT 30 LABELS ergo-active)
endif()
