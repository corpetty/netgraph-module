#include "netgraph_ui_backend.h"

// Generated umbrella: LogosModules (behind modules()) built from
// metadata.json#dependencies — here the Qt-typed caller for netgraph_module.
#include "logos_sdk.h"

qlonglong NetgraphUiBackend::setEnabled(QString config)
{
    // netgraph_module::setEnabled(const std::string&) -> int64_t, surfaced by
    // the generator as the Qt-typed caller setEnabled(QString) -> qlonglong.
    const qlonglong changed = modules().netgraph_module.setEnabled(config);
    setLastError(QString());
    // Reflect the flip (enabled/disabled, cleared snapshot) without waiting for
    // the view's next poll tick.
    refresh();
    return changed;
}

void NetgraphUiBackend::refresh()
{
    // snapshot() never blocks on a live sweep — it returns the last completed
    // sweep's cached document. getInfo() is the status line the header reads.
    setSnapshotJson(modules().netgraph_module.snapshot());
    setInfoJson(modules().netgraph_module.getInfo());
    setLastError(QString());
}

void NetgraphUiBackend::onContextReady()
{
    // Prime the view before the poll Timer's first tick so it never renders a
    // blank frame. Collection is off until the operator turns it on, so this
    // just yields the disabled document ({"enabled":false,"connections":[]}).
    refresh();
}
