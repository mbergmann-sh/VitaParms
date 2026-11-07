// Terminkollisions-Check (nicht light): Termine mehrerer Personen vergleichen, Überschneidungen mit
// Ausweichterminen anzeigen, Ergebnis als PDF (A4 hoch)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "conflictsPage"
    property var personId: 0
    signal closed(string message)
    signal editAppointment(var id)
    background: Rectangle { color: Theme.paper }

    property var persons: app.persons()
    property var selected: persons.map(p => p.id)          // Vorgabe: alle Personen
    property int buffer: 0
    property bool weekdays: true
    property var result: ({})
    property bool checked: false

    function toggle(id) {
        const l = selected.slice()
        const i = l.indexOf(id)
        if (i >= 0) l.splice(i, 1); else l.push(id)
        selected = l
    }
    function pad(n) { return (n < 10 ? "0" : "") + n }
    function iso(d) { return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) }
    function options() {
        return { persons: selected, from: from.isoDate, to: to.isoDate, buffer: buffer, weekdays: weekdays,
                 dayStart: "08:00", dayEnd: "18:00" }
    }
    function runCheck() { result = app.apptConflicts(options()); checked = !result.error }
    Connections { target: app; function onDataChanged() { if (page.checked) page.runCheck() } }
    Component.onCompleted: { const t = new Date(); from.isoDate = iso(t); t.setDate(t.getDate() + 60); to.isoDate = iso(t) }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { objectName: "conflictsCloseButton"; text: win.phone ? "✕" : qsTr("Schließen"); kind: "flat"; onClicked: page.closed("") }
            Label { text: qsTr("Terminkollisionen"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink
                    Layout.fillWidth: true; Layout.minimumWidth: 0; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
            VButton { objectName: "conflictsPdfButton"; text: qsTr("Als PDF"); kind: "flat"; enabled: page.checked
                      onClicked: { const f = app.exportReport("conflicts", page.selected[0] || page.personId, page.options())
                                   if (f) app.openFile(f); else win.toast(app.lastError, true) } }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 64
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: page.width - 2 * Theme.gapL
            spacing: Theme.gap

            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: qsTr("Vergleicht die Termine der gewählten Personen und meldet Überschneidungen – mit Vorschlägen für freie Ausweichtermine.") }
            SectionTitle { text: qsTr("Personen (mindestens zwei)") }
            Flow {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                spacing: Theme.gapS
                Repeater {
                    model: page.persons
                    VChip { objectName: "conflictPerson_" + index; text: modelData.name; checkable: false
                            checked: page.selected.indexOf(modelData.id) >= 0; onClicked: page.toggle(modelData.id) }
                }
            }
            SectionTitle { text: qsTr("Zeitraum") }
            Flow {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                spacing: Theme.gap
                DateField { id: from; objectName: "conflictFrom"; label: qsTr("von"); allowEmpty: false; width: win.phone ? 150 : 200 }
                DateField { id: to; objectName: "conflictTo"; label: qsTr("bis"); allowEmpty: false; width: win.phone ? 150 : 200 }
            }
            Label { text: qsTr("Mindestabstand (z. B. für die Fahrt)"); font.pixelSize: Theme.fsS; color: Theme.ink2 }
            Flow {
                Layout.fillWidth: true; Layout.preferredWidth: 1
                spacing: Theme.gapS
                Repeater {
                    model: [0, 15, 30, 60]
                    VChip { objectName: "conflictBuffer_" + modelData; checkable: false
                            text: modelData === 0 ? qsTr("keiner") : qsTr("%1 min").arg(modelData)
                            checked: page.buffer === modelData; onClicked: page.buffer = modelData }
                }
            }
            VCheck { objectName: "conflictWeekdays"; text: qsTr("Ausweichtermine nur Montag bis Freitag, 8 bis 18 Uhr")
                     checked: page.weekdays; onToggled: page.weekdays = checked; Layout.fillWidth: true }
            VButton { objectName: "conflictCheckButton"; text: qsTr("Prüfen"); kind: "primary"; enabled: page.selected.length >= 2
                      onClicked: page.runCheck() }

            Label {
                objectName: "conflictSummary"
                visible: page.result.error !== undefined
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                color: page.result.error ? Theme.alert : Theme.ink
                font.bold: true
                text: page.result.error ? page.result.error
                      : (page.result.conflicts || []).length === 0
                        ? qsTr("Keine Überschneidungen – %1 Termine geprüft.").arg(page.result.checked || 0)
                        : qsTr("%1 Kollision(en) – %2 Termine geprüft.").arg((page.result.conflicts || []).length).arg(page.result.checked || 0)
            }
            Repeater {
                model: page.result.conflicts || []
                delegate: Rectangle {
                    objectName: "conflictCard_" + index
                    Layout.fillWidth: true
                    implicitHeight: card.implicitHeight + 24
                    radius: Theme.radius
                    color: Theme.surface; border.color: modelData.overlap > 0 ? Theme.alert : Theme.warn
                    ColumnLayout {
                        id: card
                        x: 12; y: 12; width: parent.width - 24
                        spacing: 6
                        Label { text: "⚠ " + modelData.overlapText; color: modelData.overlap > 0 ? Theme.alert : Theme.warn; font.bold: true
                                Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Repeater {
                            model: [ { w: modelData.aWhen, t: modelData.aTitle, p: modelData.aPerson, m: !modelData.moveB },
                                     { w: modelData.bWhen, t: modelData.bTitle, p: modelData.bPerson, m: modelData.moveB } ]
                            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink; textFormat: Text.StyledText
                                    text: "<b>" + modelData.w + "</b> · " + modelData.t.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                                          + " – " + modelData.p.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                                          + (modelData.m ? " <i>(" + qsTr("verschieben") + ")</i>" : "") }
                        }
                        Label { text: modelData.moveText + ":\n" + modelData.alternatives; color: Theme.ink2; font.pixelSize: Theme.fsS
                                Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        VButton { text: qsTr("Termin öffnen"); kind: "flat"; onClicked: page.editAppointment(modelData.moveId) }
                    }
                }
            }
        }
    }
}
