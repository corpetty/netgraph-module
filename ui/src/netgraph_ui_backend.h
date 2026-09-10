#pragma once

#include "rep_netgraph_ui_source.h"     // NetgraphUiSimpleSource (repc-generated)
#include "logos_ui_plugin_context.h"    // LogosUiPluginContext

// The whole hand-written backend for the netgraph view. The *Plugin and
// *Interface classes (Q_PLUGIN_METADATA, initLogos wiring, QtRO registration,
// setBackend) are generated around it from the .rep + metadata.json.
//
// It derives:
//   - NetgraphUiSimpleSource — generated from netgraph_ui.rep; implement its
//     SLOTs and feed its PROPs (setSnapshotJson / setInfoJson / setLastError),
//     which auto-sync to every QML replica over QtRO.
//   - LogosUiPluginContext — supplies onContextReady() plus modules(), the
//     Qt-typed callers for the "netgraph_module" dependency. A UI plugin is a
//     view, not a module, so that is all the context carries.
//
// netgraph_module exposes no events (it is snapshot/poll by design), so there is
// nothing to subscribe to in onContextReady(); we use it only to prime the view
// with a first snapshot before the poll Timer's first tick.
class NetgraphUiBackend : public NetgraphUiSimpleSource,
                          public LogosUiPluginContext
{
public:
    // Forward the operator switch to netgraph_module and return its result
    // (1 = state changed, 0 = no-op / bad config), then refresh so the view
    // reflects the new state immediately.
    qlonglong setEnabled(QString config) override;

    // Pull one snapshot() + getInfo() from netgraph_module into the PROPs.
    void refresh() override;

    // Fires when ui-host hands the plugin its LogosAPI — the typed dependency
    // surface is live, so prime the first snapshot here.
    void onContextReady() override;
};
