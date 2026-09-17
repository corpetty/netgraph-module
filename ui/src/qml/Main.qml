import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// The netgraph view. Drives netgraph_ui's backend (which forwards to
// netgraph_module): a toggle arms collection, a Timer polls snapshot(), and the
// merged connection document is rendered as a table grouped by module or
// network. Every row is shown even when unlabelled (module: null) — "the
// unlabelled rows are the point"; unlabelled rows collect into an
// "(unattributed)" group rather than being hidden.
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
    readonly property color panelHi: "#1c2330"
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

    // ── Grouping ──
    // groupBy is "module" | "network" | "none". `collapsed` maps a group key to
    // true when that group is folded; it is reassigned (not mutated) so the
    // groupedRows binding re-runs.
    property string groupBy: "module"
    property var collapsed: ({})

    function groupKeyOf(c, by) {
        if (by === "network") return c.network ? String(c.network) : ""
        if (by === "module") return c.module ? String(c.module) : ""
        return ""
    }
    function groupLabelOf(key, by) {
        if (key !== "") return key
        return by === "network" ? "(no network)" : "(unattributed)"
    }
    function toggleCollapse(key) {
        var m = {}
        for (var k in root.collapsed) m[k] = root.collapsed[k]
        m[key] = !m[key]
        root.collapsed = m
    }

    // Flatten the connections into a single list of {type:"header"|"conn", …}
    // items so one ListView renders group headers and rows together and still
    // virtualizes. Groups are ordered alphabetically with the empty/unattributed
    // bucket last; a collapsed group contributes only its header.
    function buildGrouped(conns, by, collapsed) {
        if (by === "none")
            return conns.map(function (c) { return { type: "conn", conn: c } })

        var buckets = ({})
        var keys = []
        for (var i = 0; i < conns.length; i++) {
            var k = root.groupKeyOf(conns[i], by)
            if (buckets[k] === undefined) { buckets[k] = []; keys.push(k) }
            buckets[k].push(conns[i])
        }
        keys.sort(function (a, b) {
            if (a === "") return 1
            if (b === "") return -1
            return a < b ? -1 : (a > b ? 1 : 0)
        })

        var out = []
        for (var j = 0; j < keys.length; j++) {
            var key = keys[j]
            var rows = buckets[key]
            var isCollapsed = collapsed[key] === true
            out.push({ type: "header", key: key,
                       label: root.groupLabelOf(key, by),
                       count: rows.length, collapsed: isCollapsed })
            if (!isCollapsed)
                for (var r = 0; r < rows.length; r++)
                    out.push({ type: "conn", conn: rows[r] })
        }
        return out
    }
    readonly property var groupedRows: buildGrouped(root.connections, root.groupBy, root.collapsed)
    readonly property int groupCount: {
        var n = 0
        for (var i = 0; i < groupedRows.length; i++)
            if (groupedRows[i].type === "header") n++
        return n
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

        // ── Stats strip (from getInfo) + group-by selector ──
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

                // group-by segmented control
                Text { text: "group by"; color: root.dim; font.pixelSize: 12 }
                Row {
                    spacing: 0
                    Repeater {
                        model: [ { t: "module", v: "module" },
                                 { t: "network", v: "network" },
                                 { t: "none", v: "none" } ]
                        delegate: Button {
                            id: gb
                            required property var modelData
                            readonly property bool active: root.groupBy === gb.modelData.v
                            text: gb.modelData.t
                            onClicked: root.groupBy = gb.modelData.v
                            implicitHeight: 24
                            padding: 6
                            contentItem: Text {
                                text: gb.text
                                color: gb.active ? root.fg : root.dim
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                color: gb.active ? root.panelHi : "transparent"
                                border.color: root.line
                            }
                        }
                    }
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

        // ── Grouped connection rows ──
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.groupedRows
            spacing: 0

            delegate: Item {
                width: ListView.view.width
                implicitHeight: modelData.type === "header" ? 30 : 34

                // group header
                Rectangle {
                    anchors.fill: parent
                    visible: modelData.type === "header"
                    color: root.panelHi

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleCollapse(modelData.key)
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        anchors.rightMargin: 12
                        spacing: 8
                        Text {
                            text: modelData.collapsed ? "▸" : "▾"
                            color: root.dim; font.pixelSize: 12
                        }
                        Text {
                            text: modelData.label ? modelData.label : ""
                            color: (modelData.key === "") ? root.dim : root.fg
                            font.pixelSize: 13
                            font.bold: true
                            font.italic: modelData.key === ""
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Text {
                            text: (modelData.count ? modelData.count : 0)
                                  + (modelData.count === 1 ? " conn" : " conns")
                            color: root.dim; font.pixelSize: 12
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.line }
                }

                // connection row
                Item {
                    anchors.fill: parent
                    visible: modelData.type === "conn"

                    Rectangle {
                        anchors.fill: parent
                        color: index % 2 ? "transparent" : "#10161d"
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 6
                        spacing: 0

                        Text {
                            Layout.fillWidth: true; Layout.preferredWidth: 2.0
                            text: (modelData.conn && modelData.conn.module ? modelData.conn.module : "—")
                                  + (modelData.conn && modelData.conn.derived ? "  (derived)" : "")
                            color: (modelData.conn && modelData.conn.module) ? root.fg : root.dim
                            font.pixelSize: 12
                            font.italic: !(modelData.conn && modelData.conn.module)
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true; Layout.preferredWidth: 0.8
                            text: (modelData.conn && modelData.conn.pid) ? modelData.conn.pid : "—"
                            color: root.dim; font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true; Layout.preferredWidth: 3.0
                            text: modelData.conn ? (root.endpoint(modelData.conn.remote)
                                  + (modelData.conn.peer_id ? "  ·  " + String(modelData.conn.peer_id).substring(0, 12) : "")) : ""
                            color: root.fg; font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true; Layout.preferredWidth: 1.4
                            text: modelData.conn ? root.endpoint(modelData.conn.local) : ""
                            color: root.dim; font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true; Layout.preferredWidth: 1.0
                            text: (modelData.conn && modelData.conn.transport) ? modelData.conn.transport : "—"
                            color: root.dim; font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true; Layout.preferredWidth: 0.9
                            text: (modelData.conn && modelData.conn.direction) ? modelData.conn.direction : "—"
                            color: root.dim; font.pixelSize: 12
                        }
                        RowLayout {
                            Layout.fillWidth: true; Layout.preferredWidth: 1.2
                            spacing: 6
                            Text {
                                text: (modelData.conn && modelData.conn.state) ? modelData.conn.state : "—"
                                color: root.dim; font.pixelSize: 12
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Rectangle {
                                visible: modelData.conn && modelData.conn.host === true
                                radius: 3; color: "#30363d"
                                implicitWidth: hostTag.implicitWidth + 8; implicitHeight: 16
                                Text { id: hostTag; anchors.centerIn: parent; text: "host"; color: root.dim; font.pixelSize: 10 }
                            }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: root.line }
                }
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
                text: {
                    var n = root.connections.length
                    var base = n + " connection" + (n === 1 ? "" : "s")
                    return root.groupBy === "none" ? base
                        : base + " · " + root.groupCount + " " + root.groupBy
                          + " group" + (root.groupCount === 1 ? "" : "s")
                }
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
