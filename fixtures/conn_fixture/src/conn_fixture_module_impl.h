#pragma once

// conn_fixture_module — a doc-test fixture, NOT a shipping module.
//
// Its only job: while loaded, hold a real TCP connection open on a known
// loopback port so the netgraph observer has a *module-owned* connection to
// attribute. netgraph maps a swept socket's pid to a module name via
// core_service.getModuleStats(); that table lists only LOADED MODULES, so a
// plain background process (like the M0 doc-test's python fixture) can never be
// named. This module can: it is loaded, so its pid is in getModuleStats, and the
// socket it holds is owned by that pid — exactly the case the attribution
// doc-test asserts on.
//
// Universal (pure C++) module, same shape as netgraph_impl. One method.

#include <cstdint>
#include <mutex>
#include <vector>

#include <logos_module_context.h>  // LogosModuleContext base

class ConnFixtureModuleImpl : public LogosModuleContext {
public:
    ConnFixtureModuleImpl() = default;
    ~ConnFixtureModuleImpl();

    // Open a loopback TCP connection to ourselves on `port` (a listener, a
    // client connected to it, and the accepted server socket — all in this
    // process) and hold every socket open for the module's lifetime. Idempotent
    // per port. Returns this module process's pid on success (so the doc-test can
    // cross-check), or a negative errno-ish value on failure.
    int64_t openLoopback(int64_t port);

private:
    void closeAll();

    std::mutex        m_mutex;
    std::vector<int>  m_fds;   // listener + client + accepted, held open
};
