// Touchscreen panel for a solar battery-swap station (Qt 6 / QtQuick).
// Bind `solarW`, `bays` and `nextArrival` from the station controller.
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root
    width: 800; height: 480
    color: "#0f172a"

    property string station: "Mocuba"
    property real solarW: 1840
    property string nextArrival: "GH-07 in 6 min"
    property var bays: [
        { id: 1, pct: 100, state: "ready" },
        { id: 2, pct: 100, state: "ready" },
        { id: 3, pct: 64,  state: "charging" },
        { id: 4, pct: 31,  state: "charging" },
        { id: 5, pct: 0,   state: "fault" },
        { id: 6, pct: 0,   state: "empty" }
    ]
    readonly property int ready: bays.filter(b => b.state === "ready").length

    function colorFor(s) {
        return s === "ready" ? "#22c55e" : s === "charging" ? "#f59e0b" : s === "fault" ? "#ef4444" : "#64748b"
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 24; spacing: 16

        RowLayout {
            Text { text: "Gondohealth · " + root.station; color: "white"; font.pixelSize: 28; font.bold: true }
            Item { Layout.fillWidth: true }
            Text { text: "☀ " + Math.round(root.solarW) + " W"; color: "#facc15"; font.pixelSize: 24 }
        }

        Text {
            text: root.ready + " packs ready · next drone " + root.nextArrival
            color: root.ready > 0 ? "#cbd5e1" : "#ef4444"; font.pixelSize: 20
        }

        GridLayout {
            columns: 3; rowSpacing: 12; columnSpacing: 12
            Layout.fillWidth: true; Layout.fillHeight: true

            Repeater {
                model: root.bays
                Rectangle {
                    required property var modelData
                    Layout.fillWidth: true; Layout.fillHeight: true
                    radius: 12; color: "#1e293b"
                    border.color: root.colorFor(modelData.state); border.width: 3

                    Rectangle { // charge level
                        anchors { left: parent.left; bottom: parent.bottom; margins: 6 }
                        width: (parent.width - 12) * modelData.pct / 100; height: 8; radius: 4
                        color: root.colorFor(modelData.state)
                    }
                    Column {
                        anchors.centerIn: parent
                        Text { text: "Bay " + modelData.id; color: "white"; font.pixelSize: 22; anchors.horizontalCenter: parent.horizontalCenter }
                        Text {
                            text: modelData.state === "charging" ? modelData.pct + "%" : modelData.state
                            color: root.colorFor(modelData.state); font.pixelSize: 18
                            anchors.horizontalCenter: parent.horizontalCenter
                        }
                    }
                }
            }
        }
    }
}
