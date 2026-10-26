// Übersicht: letzte Messwerte mit Verlaufsspur, nächste Termine, Regelmedikation
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Flickable {
    id: page
    property var personId: 0
    signal openChart()
    signal openMeds()
    signal openAppointments()
    property var tiles: []
    property var appts: []
    property var meds: []
    function reload() {
        tiles = app.latest(personId)
        appts = app.upcoming(personId, 14)
        meds = app.medications(personId, false).filter(m => m.category === 0)
    }
    onPersonIdChanged: reload()
    Connections { target: app; function onDataChanged() { page.reload() } }
    Component.onCompleted: reload()

    contentHeight: col.implicitHeight + 48
    clip: true
    ColumnLayout {
        id: col
        x: Theme.gapL; y: Theme.gap
        width: page.width - 2 * Theme.gapL
        spacing: Theme.gap

        SectionTitle { text: qsTr("Letzte Messwerte") }
        Label { visible: page.tiles.length === 0; text: qsTr("Noch keine Messungen. Tippen Sie auf „Messung erfassen“."); color: Theme.muted }
        GridLayout {
            id: tileGrid
            Layout.fillWidth: true
            columns: Math.max(1, Math.floor(width / 250))
            columnSpacing: 0; rowSpacing: 0
            Repeater {
                model: page.tiles
                delegate: AbstractButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: tileGrid.width / tileGrid.columns
                    implicitHeight: 176
                    padding: 16
                    onClicked: page.openChart()
                    background: Rectangle { color: parent.down ? Theme.brandWash : Theme.surface; border.color: Theme.rule }
                    contentItem: ColumnLayout {
                        spacing: 2
                        RowLayout {
                            spacing: 6
                            Row { spacing: 4; Layout.alignment: Qt.AlignVCenter; Repeater { model: modelData.colors; Rectangle { width: 10; height: 10; radius: 5; color: Theme.series(modelData) } } }
                            Label { text: modelData.label; color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; elide: Text.ElideRight }
                        }
                        RowLayout {
                            spacing: 6
                            Label { text: modelData.value; font.pixelSize: Theme.fsXxl; font.bold: true; color: Theme.ink }
                            Label { text: modelData.unit; color: Theme.muted; font.pixelSize: Theme.fsS; Layout.alignment: Qt.AlignBaseline }
                        }
                        Label { text: modelData.when; color: Theme.muted; font.pixelSize: Theme.fsXs; elide: Text.ElideRight; Layout.fillWidth: true }
                        Item { Layout.fillHeight: true }
                        Sparkline { series: modelData.series; Layout.fillWidth: true; Layout.preferredHeight: 34 }
                    }
                }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: page.width > 900 ? 2 : 1
            columnSpacing: Theme.gapL
            rowSpacing: Theme.gap
            ColumnLayout {
                Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
                spacing: 0
                SectionTitle { text: qsTr("Nächste Termine") }
                Label { visible: page.appts.length === 0; text: qsTr("Keine Termine in den nächsten 14 Tagen."); color: Theme.muted; topPadding: 6 }
                VButton { text: qsTr("Alle Termine"); kind: "flat"; leftPadding: 0; onClicked: page.openAppointments() }
                Repeater {
                    model: page.appts
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.rule }
                        RowLayout {
                            Layout.topMargin: 10; Layout.bottomMargin: 10
                            spacing: Theme.gap
                            Label { text: modelData.when; color: Theme.ink2; Layout.preferredWidth: 150; font.bold: modelData.priority >= 2 }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Label { text: modelData.title; color: Theme.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                Label { visible: !!modelData.location; text: modelData.location; color: Theme.muted; font.pixelSize: Theme.fsS }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.alignment: Qt.AlignTop
                spacing: 0
                SectionTitle { text: qsTr("Regelmedikation") }
                Label { visible: page.meds.length === 0; text: qsTr("Keine Regelmedikation eingetragen."); color: Theme.muted; topPadding: 6 }
                Repeater {
                    model: page.meds
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.rule }
                        RowLayout {
                            Layout.topMargin: 10; Layout.bottomMargin: 10
                            spacing: Theme.gap
                            Label { text: modelData.scheme; font.bold: true; color: Theme.ink; Layout.preferredWidth: 110 }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Label { text: modelData.name + (modelData.strength ? " " + modelData.strength : ""); color: Theme.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                Label { visible: !!modelData.rhythm; text: modelData.rhythm; color: Theme.muted; font.pixelSize: Theme.fsS }
                            }
                        }
                    }
                }
                VButton { text: qsTr("Zum Medikamentenplan"); kind: "flat"; onClicked: page.openMeds(); Layout.topMargin: 6 }
            }
        }
    }
}
