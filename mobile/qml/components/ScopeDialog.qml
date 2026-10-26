// Terminserie: Für welche Termine gilt das Ändern bzw. Löschen?
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

Popup {
    id: d
    property bool deleting: false
    property bool allowOnlyThis: true
    property string apptTitle: ""
    signal chosen(string scope)          // "only", "following", "all"
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(600, parent ? parent.width - 48 : 600)
    padding: 24
    background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
    Overlay.modal: Rectangle { color: "#80000000" }
    contentItem: ColumnLayout {
        spacing: Theme.gap
        Label { text: d.deleting ? qsTr("Termin löschen") : qsTr("Terminserie ändern")
                font.family: Theme.family; font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
            font.family: Theme.family; font.pixelSize: Theme.fsM
            text: (d.deleting ? qsTr("„%1“ gehört zu einer Terminserie. Welche Termine sollen gelöscht werden?")
                              : qsTr("„%1“ gehört zu einer Terminserie. Für welche Termine sollen die Änderungen gelten?")).arg(d.apptTitle)
                  + (d.allowOnlyThis ? "" : "\n\n" + qsTr("Eine geänderte Wiederholung gilt immer für mehrere Termine."))
        }
        VButton { objectName: "scopeOnly"; visible: d.allowOnlyThis; text: qsTr("Nur diesen Termin"); kind: "primary"; Layout.fillWidth: true
                  onClicked: { d.close(); d.chosen("only") } }
        VButton { objectName: "scopeFollowing"; text: qsTr("Diesen und alle folgenden"); Layout.fillWidth: true
                  kind: d.allowOnlyThis ? "ghost" : "primary"; onClicked: { d.close(); d.chosen("following") } }
        VButton { objectName: "scopeAll"; text: qsTr("Alle Termine der Serie"); Layout.fillWidth: true
                  onClicked: { d.close(); d.chosen("all") } }
        VButton { text: qsTr("Abbrechen"); kind: "flat"; Layout.alignment: Qt.AlignRight; onClicked: d.close() }
    }
}
