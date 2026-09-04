#pragma once

// netgraph — a universal (pure-C++) Logos module that observes, in real time,
// every network connection the running Logos process tree holds, grouped by the
// service that opened it and the network it belongs to. Scope is the whole
// process tree: DNS lookups, plain HTTPS to a bootstrap host, and RPC endpoints
// all appear. Nothing is hidden.
//
// It declares an interface dependency on `connection_source` (the
// collectConnections() contract, see interfaces/connection_source.h) and binds
// it to each operator-configured module name at runtime, so it ships before any
// module implements the provider side (Collector B degrades to nothing).
//
// No Qt here. The sweep runs on a timer thread and (for Collector B) performs
// inter-module IPC through the bound wrappers; the SDK's LogosAPIClient marshals
// that IPC onto the module's main/event-loop thread, exactly as openmetrics'
// HTTP-thread scrape does. The impl header stays free of the generated
// logos_sdk.h — the generator parses it and expects plain C++.
//
// The sweep pipeline itself (discovery -> enumerate -> merge -> document) is the
// pure buildSnapshot() in sweep.cpp, tested against fakes; this class is the
// timer + Collector B + state shell around it.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <logos_module_context.h>  // LogosModuleContext base (gives modules())

#include "collector.h"        // netgraph::ISocketTable
#include "process_source.h"   // netgraph::IProcessSource

class NetgraphImpl : public LogosModuleContext {
public:
    NetgraphImpl() = default;
    ~NetgraphImpl();

    // The explicit user switch. Collection is OFF until turned on — this module
    // sees the complete connection graph of the host, a surveillance surface, so
    // it collects nothing until enabled (handoff constraint: off by default,
    // local only, behind an explicit switch).
    //
    // Config JSON:
    //   { "enabled": true,
    //     "sweep_ms": 1000,                       // default 1000; floor 250
    //     "sources":  ["blockchain_module", ...], // connection_source providers
    //     "include_host": true,                   // include host/ui-host sockets
    //     "root_pid": 0 }                         // 0 => derive host by ancestry
    //
    // enabled=true starts the sweep timer; enabled=false stops it and drops the
    // cached snapshot. Returns 1 on a state change, 0 on no-op or bad config.
    int64_t setEnabled(const std::string& configJson);

    // The direct-call snapshot: the last completed sweep's merged connection
    // array as JSON. The UI polls this and never opens a socket of its own
    // (openmetrics' scrape() analogue). Never blocks on a live sweep — it returns
    // the cached result under the lock.
    std::string snapshot();

    // Status: { enabled, sweeping, sweep_ms, include_host, sockets, attributed,
    //           derived, sources: [...] }.
    std::string getInfo();

private:
    struct Config {
        int      sweepMs = 1000;
        bool     includeHost = true;
        int64_t  rootPid = 0;
        std::vector<std::string> sources;
    };

    void startSweep();   // spawn the timer thread (caller must not hold m_mutex)
    void stopSweep();    // signal + join the timer thread (caller must not hold m_mutex)
    void runLoop();      // the timer thread body: sweep, wait, repeat
    void doSweepAndPublish(const Config& cfg);

    // Collector B: bind each configured connection_source provider and collect
    // its labels. Needs the SDK bound wrapper AND pid<->name attribution to place
    // rows (see DESIGN "Process-tree attribution"); returns empty until that
    // lands, which is correct — Collector A alone still produces a graph.
    std::vector<netgraph::ProviderLabel> collectProviderLabels(const Config& cfg);

    std::mutex m_mutex;                       // guards the fields below
    bool       m_enabled = false;
    Config     m_config;
    std::string m_snapshot = R"({"enabled":false,"swept_at":0,"connections":[]})";
    // last-sweep counters for getInfo
    int m_lastSockets = 0, m_lastAttributed = 0, m_lastDerived = 0;

    // sweep thread lifecycle
    std::thread             m_thread;
    std::condition_variable m_cv;
    bool                    m_stop = false;
    bool                    m_running = false;

    // collectors, created on first enable
    std::unique_ptr<netgraph::ISocketTable>   m_sockets;
    std::unique_ptr<netgraph::IProcessSource> m_procSource;
};
