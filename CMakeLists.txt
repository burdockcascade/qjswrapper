cmake_minimum_required(VERSION 3.28)
project(qjswrapper LANGUAGES CXX)

# Require C++23 standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QJSWRAPPER_BUILD_EXAMPLES "Build qjswrapper examples" ${PROJECT_IS_TOP_LEVEL})
option(QJSWRAPPER_BUILD_TESTS "Build qjswrapper unit tests" ${PROJECT_IS_TOP_LEVEL})
option(QJSWRAPPER_INCLUDE_QUICKJS "Fetch and build QuickJS dependency internally" ON)

# Include the FetchContent module
include(FetchContent)

if(QJSWRAPPER_INCLUDE_QUICKJS)
    FetchContent_Declare(
            quickjs-ng
            GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
            GIT_TAG        v0.15.1
    )
    FetchContent_MakeAvailable(quickjs-ng)
endif()

add_library(qjswrapper STATIC
        src/value.cpp
        src/module.cpp
        src/engine.cpp
)

target_include_directories(qjswrapper PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

# Modern target-based C++ standard enforcement
target_compile_features(qjswrapper PUBLIC cxx_std_23)

# Only link the internal qjs target if it was fetched
if(QJSWRAPPER_INCLUDE_QUICKJS)
    target_link_libraries(qjswrapper PUBLIC qjs)
else()
    target_compile_definitions(qjswrapper PUBLIC QJSWRAPPER_EXTERNAL_QUICKJS)
endif()

# -----------------------------------------------------------------------------
# Target: Unit Tests
# -----------------------------------------------------------------------------
if(QJSWRAPPER_BUILD_TESTS)

    # Conditionally declare Catch2 only if tests are enabled
    FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.4.0
    )
    set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CATCH_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CATCH_BUILD_EXTRA_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(Catch2)

    # Explicitly listing sources is safer and more reliable than file(GLOB)
    set(TEST_SOURCES
            tests/test_class.cpp
            tests/test_engine.cpp
            tests/test_module.cpp
            tests/test_value.cpp
    )

    add_executable(unit_tests ${TEST_SOURCES})

    target_link_libraries(unit_tests PRIVATE
            qjswrapper
            Catch2::Catch2WithMain
    )

endif()

# -----------------------------------------------------------------------------
# Targets: Examples
# -----------------------------------------------------------------------------
if(QJSWRAPPER_BUILD_EXAMPLES)

    # Conditionally declare and optimize Raylib build configurations
    set(BUILD_EXAMPLES OFF CACHE BOOL "Disable raylib examples" FORCE)

    FetchContent_Declare(
            raylib
            GIT_REPOSITORY https://github.com/raysan5/raylib.git
            GIT_TAG        6.0
    )
    FetchContent_MakeAvailable(raylib)

    set(EXAMPLES
            example_class
            example_global
            example_object
            example_simple
            example_value
    )

    foreach(example ${EXAMPLES})
        add_executable(${example} examples/${example}.cpp)
        target_link_libraries(${example} PRIVATE qjswrapper)
    endforeach()

    # Special example with raylib dependency
    add_executable(example_raylib examples/example_raylib.cpp)

    target_link_libraries(example_raylib PRIVATE
            qjswrapper
            raylib
    )

endif()