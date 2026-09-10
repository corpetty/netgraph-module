cmake_minimum_required(VERSION 3.14)
project(NetgraphUiPlugin LANGUAGES CXX)

if(DEFINED ENV{LOGOS_MODULE_BUILDER_ROOT})
    include($ENV{LOGOS_MODULE_BUILDER_ROOT}/cmake/LogosModule.cmake)
else()
    message(FATAL_ERROR "LogosModule.cmake not found. Set LOGOS_MODULE_BUILDER_ROOT.")
endif()

# Universal UI module: we author the .rep view contract + the *Backend class.
# REP_FILE runs repc; the generated *Plugin glue in generated_code/ is compiled
# automatically. No Qt Widgets — the view runs in the host app, the plugin runs
# headlessly in ui-host.
logos_module(
    NAME netgraph_ui
    REP_FILE src/netgraph_ui.rep
    SOURCES
        src/netgraph_ui_backend.h
        src/netgraph_ui_backend.cpp
    INCLUDE_DIRS
        src
)
