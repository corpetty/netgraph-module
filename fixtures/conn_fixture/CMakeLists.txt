cmake_minimum_required(VERSION 3.14)
project(ConnFixtureModulePlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LogosModule.cmake")
    include(cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. LOGOS_MODULE_BUILDER_ROOT is not set.")
endif()

# Universal module. Impl header/class follow the builder's default derivation
# from NAME: src/conn_fixture_module_impl.h / class ConnFixtureModuleImpl — so no
# codegen overrides are needed (unlike netgraph, whose files predate its rename).
logos_module(
    NAME conn_fixture_module
    SOURCES
        src/conn_fixture_module_impl.h
        src/conn_fixture_module_impl.cpp
)

# logos_module(NAME conn_fixture_module) creates the target
# `conn_fixture_module_module_plugin` (OUTPUT_NAME conn_fixture_module_plugin).
# The socket calls are POSIX; no extra libs needed on Linux/macOS.
