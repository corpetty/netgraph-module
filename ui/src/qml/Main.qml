import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The netgraph view. Drives netgraph_ui's backend (which forwards to
// netgraph_module): a toggle arms collection, a Timer polls snapshot(), and the
// merged connection document is rendered as a table. Every row is shown even
// when unlabelled (module: null) — "the unlabelled rows are the point".
Rectangle {
    id: root
    width: 900
    height: 560
    color: "#0d1117"

    // ── Palette ──
    readonly property color fg: "#e6edf3"
    readonly property color dim: "#8b949e"
    readonly property color line: "#21262d"
    readonly property color panel: "#161b22"
    readonly property color accentOn: "#56d364"
    readonly property color accentOff: "#f0883e"
    readonly property color derivedTint: "#d29922"

    // ── Backend: the typed replica. SLOTs return async (logos.watch); PROPs
    //    (snapshotJson/infoJson/lastError) auto-sync over QtRO. ──
    readonly property var backend: logos.module("netgraph_ui")
    property bool ready: false

    Connections {
        target: logos
        function onViewModuleReadyChanged(moduleName, isReady) {
            if (moduleName === "netgraph_ui")
                root.ready = isReady && root.backend !== null
        }
    }
    Component.onCompleted: {
        root.ready = root.backend !== null && logos.isViewModuleReady("netgraph_ui")
    }

    // ── Parsed state (recomputed as the PROPs sync) ──
    function safeParse(s, fallback) {
        if (!s) return fallback
        try { return JSON.parse(s) } catch (e) { return fallback }
    }
    readonly property var snap: root.ready ? safeParse(backend.snapshotJson, {}) : ({})
    readonly property var info: root.ready ? safeParse(backend.infoJson, {}) : ({})
    readonly property var connections: snap.connections ? snap.connections : []
    readonly property bool collecting: info.enabled === true
    readonly property string lastError: root.ready && backend ? backend.lastError : ""

    function endpoint(e) {
        if (!e) return "—"
        var hostOrAddr = (e.host && e.host.length) ? e.host : (e.addr ? e.addr : "*")
        var port = (e.port === 0 || e.port) ? e.port : "*"
        return hostOrAddr + ":" + port
    }
    function pushEnabled(on) {
        if (!backend) return
        var cfg = JSON.stringify({
            enabled: on,
            sweep_ms: sweepSpin.value,
            include_host: hostCheck.checked,
            root_pid: 0
        })
        logos.watch(backend.setEnabled(cfg), function (v) {}, function (e) {})
    }

    // ── Poll cadence: the view drives it (netgraph_module emits no events).
    //    Track the sweep interval when known, floor at 500ms. ──
    Timer {
        interval: Math.max(500, root.info.sweep_ms ? root.info.sweep_ms : 1500)
        running: root.ready
        repeat: true
        onTriggered: if (root.backend) root.backend.refresh()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 14

        // ── Header: title + collection switch + controls ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            ColumnLayout {
                spacing: 2
                Text {
                    text: "netgraph"
                    color: root.fg
                    font.pixelSize: 22
                    font.bold: true
                }
                Text {
                    text: root.ready ? (root.collecting ? "collecting — local only, nothing exported"
                                                         : "off — no connections are being read")
                                     : "connecting to backend…"
                    color: root.ready ? (root.collecting ? root.accentOn : root.dim) : root.accentOff
                    font.pixelSize: 12
                }
            }

            Item { Layout.fillWidth: true }

            Label { text: "sweep (ms)"; color: root.dim; font.pixelSize: 12 }
            SpinBox {
                id: sweepSpin
                from: 250; to: 60000; stepSize: 250; value: 1000
                editable: true
                enabled: root.ready
                onValueModified: if (root.collecting) root.pushEnabled(true)
            }

            CheckBox {
                id: hostCheck
                text: "include host"
                checked: true
                enabled: root.ready
                contentItem: Text {
                    text: hostCheck.text; color: root.dim; font.pixelSize: 12
                    leftPadding: hostCheck.indicator.width + 6
                    verticalAlignment: Text.AlignVCenter
                }
                onToggled: if (root.collecting) root.pushEnabled(true)
            }

            Switch {
                id: collectSwitch
                text: checked ? "On" : "Off"
                enabled: root.ready
                checked: root.collecting
                onToggled: root.pushEnabled(checked)
                contentItem: Text {
                    text: collectSwitch.text
                    color: collectSwitch.checked ? root.accentOn : root.dim
                    font.pixelSize: 13; font.bold: true
                    leftPadding: collectSwitch.indicator.width + 8
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // ── Stats strip (from getInfo) ──
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 44
            color: root.panel
            radius: 6
            border.color: root.line

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 16
                anchors.rightMargin: 16
                spacing: 28

                Repeater {
                    model: [
                        { k: "sockets",    v: root.info.sockets },
                        { k: "attributed", v: root.info.attributed },
                        { k: "derived",    v: root.info.derived },
                        { k: "sweeping",   v: root.info.sweeping === true ? "yes" : "no" }
                    ]
                    RowLayout {
                        spacing: 6
                        Text { text: modelData.k; color: root.dim; font.pixelSize: 12 }
                        Text {
                            text: (modelData.v === undefined || modelData.v === null) ? "—" : modelData.v
                            color: root.fg; font.pixelSize: 14; font.bold: true
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Text {
                    visible: root.lastError.length > 0
                    text: "⚠ " + root.lastError
                    color: root.accentOff; font.pixelSize: 12
                }
            }
        }

        // ── Column header ──
        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            Repeater {
                model: [
                    { t: "module",    w: 2.0 },
                    { t: "pid",       w: 0.8 },
                    { t: "remote",    w: 3.0 },
                    { t: "local",     w: 1.4 },
                    { t: "transport", w: 1.0 },
                    { t: "dir",       w: 0.9 },
                    { t: "state",     w: 1.2 }
                ]
                Text {
                    Layout.fillWidth: true
                    Layout.preferredWidth: modelData.w
                    text: modelData.t
                    color: root.dim
                    font.pixelSize: 11
                    font.capitalization: Font.AllUppercase
                    elide: Text.ElideRight
                }
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: root.line }

        // ── Connection rows ──
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.connections
            spacing: 0

            delegate: Rectangle {
                width: ListView.view.width
                implicitHeight: 34
                color: index % 2 ? "transparent" : "#10161d"

                RowLayout {
                    anchors.fill: parent
                    spacing: 0

                    // module (or — when unlabelled), tinted if a derived edge
                    Text {
                        Layout.fillWidth: true; Layout.preferredWidth: 2.0
                        leftPadding: 2
                        text: (modelData.module ? modelData.module : "—")
                              + (modelData.derived ? "  (derived)" : "")
                        color: modelData.module ? root.fg : root.dim
                        font.pixelSize: 12
                        font.italic: !modelData.module
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true; Layout.preferredWidth: 0.8
                        text: modelData.pid ? modelData.pid : "—"
                        color: root.dim; font.pixelSize: 12
                    }
                    Text {
                        Layout.fillWidth: true; Layout.preferredWidth: 3.0
                        text: root.endpoint(modelData.remote)
                              + (modelData.peer_id ? "  ·  " + String(modelData.peer_id).substring(0, 12) : "")
                        color: root.fg; font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true; Layout.preferredWidth: 1.4
                        text: root.endpoint(modelData.local)
                        color: root.dim; font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true; Layout.preferredWidth: 1.0
                        text: modelData.transport ? modelData.transport : "—"
                        color: root.dim; font.pixelSize: 12
                    }
                    Text {
                        Layout.fillWidth: true; Layout.preferredWidth: 0.9
                        text: modelData.direction ? modelData.direction : "—"
                        color: root.dim; font.pixelSize: 12
                    }
                    RowLayout {
                        Layout.fillWidth: true; Layout.preferredWidth: 1.2
                        spacing: 6
                        Text {
                            text: modelData.state ? modelData.state : "—"
                            color: root.dim; font.pixelSize: 12
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Rectangle {
                            visible: modelData.host === true
                            radius: 3; color: "#30363d"
                            implicitWidth: hostTag.implicitWidth + 8; implicitHeight: 16
                            Text { id: hostTag; anchors.centerIn: parent; text: "host"; color: root.dim; font.pixelSize: 10 }
                        }
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.line }
            }

            // Empty state
            Text {
                anchors.centerIn: parent
                visible: root.connections.length === 0
                width: parent.width * 0.7
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: root.dim
                font.pixelSize: 13
                text: !root.ready ? "connecting to backend…"
                      : root.collecting ? "no connections in the last sweep yet…"
                      : "collection is off. Flip the switch to start observing the process tree."
            }
        }

        // ── Footer ──
        RowLayout {
            Layout.fillWidth: true
            Text {
                text: root.connections.length + " connection" + (root.connections.length === 1 ? "" : "s")
                color: root.dim; font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Text {
                text: "local only · no export · no telemetry"
                color: root.dim; font.pixelSize: 11
            }
        }
    }
}
