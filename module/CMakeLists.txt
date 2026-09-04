cmake_minimum_required(VERSION 3.14)
project(NetgraphModulePlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LogosModule.cmake")
    include(cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. LOGOS_MODULE_BUILDER_ROOT is not set.")
endif()

# Universal module. The builder runs `logos-cpp-generator --from-header` for the
# Qt glue and, because metadata.json declares `interface_dependencies`,
# `--interface connection_source=interfaces/connection_source.h=IConnectionSource`
# to produce the bound `ConnectionSource` wrapper + `bind_connection_source`
# factory.
#
# The platform socket tables and pid sources are self-guarded (#if __linux__ /
# __APPLE__), so all are listed here and each compiles to nothing off its host.
logos_module(
    NAME netgraph
    SOURCES
        src/netgraph_impl.h
        src/netgraph_impl.cpp
        src/collector.h
        src/process_source.h
        src/proc_net_parse.h
        src/proc_net_parse.cpp
        src/merge.h
        src/merge.cpp
        src/sweep.h
        src/sweep.cpp
        src/socket_table_factory.cpp
        src/fake_sources.cpp
        src/linux_socket_table.cpp
        src/linux_process_source.cpp
        src/macos_socket_table.cpp
        src/macos_process_source.cpp
)

# The sweep runs on a std::thread; link the platform threads library.
find_package(Threads REQUIRED)
target_link_libraries(netgraph_module_plugin PRIVATE Threads::Threads)
