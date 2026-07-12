cmake_minimum_required(VERSION 3.16)

project(
    apollo-compiler
    VERSION 0.1.0
    DESCRIPTION "Multi-language compiler toolchain targeting the Gemini Virtual Machine"
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS
    ON
    CACHE BOOL "Write compile_commands.json for Clang tooling and IDEs" FORCE)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_TESTING "Build and register tests" ON)

include(GNUInstallDirs)

add_subdirectory(src)

if (BUILD_TESTING)
    include(CTest)
    add_subdirectory(tests)
endif ()
