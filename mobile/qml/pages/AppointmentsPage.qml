// Terminplaner: Termine nach Tagen, Zeitraum wählbar, erledigt abhaken, antippen zum Bearbeiten
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Flickable {
    id: page
    objectName: "appointmentsPage"
    property var personId: 0
    signal edit(var id)
    property string range: "upcoming"
    property bool allPersons: false
    property bool showDone: false
    property var items: []
    property var quarter: ({})
    function reload() { items = app.appointmentList(allPersons ? 0 : personId, range, showDone); quarter = app.quarterBanner() }
    onPersonIdChanged: reload()
    onRangeChanged: reload()
    onAllPersonsChanged: reload()
    onShowDoneChanged: reload()
    // nur der sichtbare Reiter lädt sofort neu; die anderen beim nächsten Anzeigen
    property bool active: true
    property bool stale: false
    onActiveChanged: if (active && stale) { stale = false; page.reload() }
    Connections { target: app; function onDataChanged() { if (page.active) page.reload(); else page.stale = true } }
    Component.onCompleted: reload()

    // Tage für die Zwischenüberschriften
    readonly property var days: {
        const d = []
        for (const a of items) if (d.indexOf(a.day) < 0) d.push(a.day)
        return d
    }

    contentHeight: col.implicitHeight + 48
    clip: true
    ColumnLayout {
        id: col
        x: Theme.gapL; y: Theme.gap
        width: page.width - 2 * Theme.gapL
        spacing: Theme.gap

        // Quartalswechsel: Hinweis bis zur Bestätigung
        Rectangle {
            objectName: "quarterBanner"
            visible: !!page.quarter.title
            Layout.fillWidth: true
            implicitHeight: qrow.implicitHeight + 24
            radius: Theme.radius
            color: Theme.warnWash
            Rectangle { width: 5; height: parent.height; radius: 2; color: Theme.warn }
            RowLayout {
                id: qrow
                anchors.fill: parent; anchors.leftMargin: 18; anchors.rightMargin: 12
                spacing: Theme.gap
                Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink; textFormat: Text.StyledText
                        text: "<b>" + (page.quarter.title || "") + "</b><br>" + (page.quarter.message || "") }
                VButton { text: qsTr("Erledigt"); kind: "primary"; onClicked: app.confirmQuarter() }
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.gapS
            VButton { objectName: "newAppointmentButton"; text: qsTr("Neuer Termin"); iconText: "+"; kind: "primary"; onClicked: page.edit(0) }
            Repeater {
                model: [{ t: qsTr("Kommende"), v: "upcoming" }, { t: qsTr("Heute"), v: "today" }, { t: qsTr("Diese Woche"), v: "week" },
                        { t: qsTr("Dieser Monat"), v: "month" }, { t: qsTr("Vergangene"), v: "past" }]
                VChip { text: modelData.t; checkable: false; checked: page.range === modelData.v; onClicked: page.range = modelData.v }
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.gapS
            VChip { objectName: "allPersonsChip"; text: qsTr("Alle Personen"); checkable: false; checked: page.allPersons; onClicked: page.allPersons = !page.allPersons }
            VChip { text: qsTr("Erledigte zeigen"); checkable: false; checked: page.showDone; onClicked: page.showDone = !page.showDone }
        }

        Label {
            visible: page.items.length === 0
            Layout.fillWidth: true; wrapMode: Text.WordWrap
            text: page.range === "past" ? qsTr("Keine vergangenen Termine in den letzten 90 Tagen.")
                                        : qsTr("Keine Termine in diesem Zeitraum. Tippen Sie auf „Neuer Termin“.")
            color: Theme.muted
        }

        Repeater {
            model: page.days
            delegate: ColumnLayout {
                readonly property var list: page.items.filter(a => a.day === modelData)
                Layout.fillWidth: true
                spacing: 8
                Label { text: list.length ? list[0].dayText : ""; font.bold: true; color: Theme.ink2; Layout.topMargin: 6 }
                Repeater {
                    model: list
                    delegate: AbstractButton {
                        id: row
                        Layout.fillWidth: true
                        implicitHeight: Math.max(Theme.touch + 12, rc.implicitHeight + 24)
                        onClicked: page.edit(modelData.id)
                        opacity: modelData.done || modelData.past ? 0.62 : 1
                        background: Rectangle {
                            radius: Theme.radius
                            color: row.down ? Theme.brandWash : Theme.surface
                            border.color: Theme.rule
                            Rectangle { width: 6; height: parent.height - 2; y: 1; x: 1; radius: 3; color: modelData.priorityColor }
                        }
                        contentItem: RowLayout {
                            id: rc
                            anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 12
                            spacing: 14
                            Label { text: modelData.time; color: Theme.ink2; font.pixelSize: Theme.fsS
                                    Layout.preferredWidth: 120; Layout.alignment: Qt.AlignVCenter; wrapMode: Text.WordWrap }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    Layout.fillWidth: true; elide: Text.ElideRight
                                    text: (modelData.series ? "↻ " : "") + modelData.title
                                    font.bold: modelData.priority === 3 && !modelData.done
                                    font.strikeout: modelData.done
                                    color: Theme.ink
                                }
                                Label {
                                    Layout.fillWidth: true; elide: Text.ElideRight
                                    visible: text !== ""
                                    text: [modelData.location, page.allPersons ? modelData.person : "",
                                           modelData.priority !== 1 ? modelData.priorityName : "",
                                           modelData.reminder ? qsTr("Erinnerung: %1").arg(modelData.reminder) : ""].filter(s => s).join(" · ")
                                    color: Theme.muted; font.pixelSize: Theme.fsS
                                }
                            }
                            // erledigt / offen
                            AbstractButton {
                                objectName: "doneToggle"
                                implicitWidth: Theme.touch; implicitHeight: Theme.touch
                                onClicked: app.setAppointmentDone(modelData.id, !modelData.done)
                                background: Rectangle {
                                    radius: width / 2
                                    color: modelData.done ? Theme.ok : "transparent"
                                    border.width: 2; border.color: modelData.done ? Theme.ok : Theme.ruleStrong
                                }
                                contentItem: Label { text: "✓"; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                                     color: modelData.done ? "white" : Theme.muted; font.bold: true }
                                ToolTip.text: modelData.done ? qsTr("wieder offen") : qsTr("erledigt")
                            }
                        }
                    }
                }
            }
        }
    }
}
