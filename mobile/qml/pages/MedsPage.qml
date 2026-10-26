// Medikamentenplan: gruppiert nach Regel-, Bedarfs-, Notfallmedikation und Sonstigem
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Flickable {
    id: page
    property var personId: 0
    signal edit(var id)
    property bool archived: false
    property var meds: []
    property var cats: app.medCategories()
    function reload() { meds = app.medications(personId, archived) }
    onPersonIdChanged: reload()
    onArchivedChanged: reload()
    Connections { target: app; function onDataChanged() { page.reload() } }
    Component.onCompleted: reload()

    contentHeight: col.implicitHeight + 48
    clip: true
    ColumnLayout {
        id: col
        x: Theme.gapL; y: Theme.gap
        width: page.width - 2 * Theme.gapL
        spacing: Theme.gap
        Flow {
            Layout.fillWidth: true
            spacing: Theme.gapS
            VButton { text: qsTr("Neues Medikament"); iconText: "+"; kind: "primary"; onClicked: page.edit(0) }
            VButton { text: qsTr("Medikamentenplan als PDF"); onClicked: { const f = app.exportReport("medplan", page.personId); if (f) app.openFile(f); else win.toast(app.lastError, true) } }
            VButton { text: qsTr("Liste für die Apotheke"); onClicked: { const f = app.exportReport("pharmacy", page.personId); if (f) app.openFile(f); else win.toast(app.lastError, true) } }
            VChip { text: qsTr("Historie zeigen"); checked: page.archived; onClicked: page.archived = !page.archived }
        }
        // Wechselwirkungen prüft die Apotheke – Hinweis auf die Medikationsberatung
        Rectangle {
            Layout.fillWidth: true
            visible: page.meds.length > 0
            implicitHeight: hint.implicitHeight + 24
            radius: Theme.radius
            color: Theme.brandWash
            Label {
                id: hint
                x: 16; width: parent.width - 32; anchors.verticalCenter: parent.verticalCenter
                wrapMode: Text.WordWrap; color: Theme.ink2
                readonly property int regular: page.meds.filter(m => m.category === 0 && !m.archived).length
                text: (regular >= 5
                       ? qsTr("%1 Arzneimittel in der Regelmedikation: Bei fünf oder mehr dauerhaft verordneten Arzneimitteln besteht alle zwölf Monate Anspruch auf eine kostenlose erweiterte Medikationsberatung in der Apotheke.").arg(regular) + " "
                       : "")
                      + qsTr("Wechselwirkungen prüft die Apotheke – die „Liste für die Apotheke“ enthält alles, was sie dafür braucht.")
            }
        }
        Label { visible: page.meds.length === 0; text: qsTr("Noch keine Medikamente eingetragen."); color: Theme.muted }
        Repeater {
            model: [0, 1, 2, 3]
            delegate: ColumnLayout {
                readonly property var list: page.meds.filter(m => m.category === modelData)
                visible: list.length > 0
                Layout.fillWidth: true
                spacing: 0
                SectionTitle {
                    text: page.cats[modelData] + "  (" + list.length + ")"
                    color: modelData === 2 ? Theme.alert : Theme.ink
                }
                Repeater {
                    model: list
                    delegate: AbstractButton {
                        Layout.fillWidth: true
                        implicitHeight: row.implicitHeight + 24
                        onClicked: page.edit(modelData.id)
                        opacity: modelData.archived ? 0.55 : 1
                        background: Rectangle { color: parent.down ? Theme.brandWash : Theme.surface; border.color: Theme.rule }
                        contentItem: RowLayout {
                            id: row
                            anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 16; anchors.rightMargin: 16
                            spacing: Theme.gap
                            ColumnLayout {
                                Layout.preferredWidth: 130; Layout.alignment: Qt.AlignTop; spacing: 0
                                Label { text: modelData.category === 0 ? modelData.scheme : (modelData.category === 3 ? "" : qsTr("bei Bedarf"))
                                        font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
                                Label { text: modelData.unit; color: Theme.muted; font.pixelSize: Theme.fsXs }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Label { text: modelData.name + (modelData.strength ? " " + modelData.strength : "") + (modelData.form ? " (" + modelData.form + ")" : "")
                                        font.bold: true; color: Theme.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                Label { visible: !!modelData.substance; text: modelData.substance; color: Theme.ink2; font.pixelSize: Theme.fsS }
                                Label { visible: !!modelData.schedule; text: modelData.schedule; color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                Label { visible: !!modelData.reminderText; text: "🔔 " + qsTr("Erinnerung: %1").arg(modelData.reminderText); color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                Label { visible: !!modelData.reason || !!modelData.notes; text: [modelData.reason, modelData.notes].filter(s => s).join(" – ")
                                        color: Theme.muted; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                Label { visible: modelData.archived || !!modelData.endDate; text: modelData.status + (modelData.stopReason ? " – " + modelData.stopReason : ""); color: Theme.warn; font.pixelSize: Theme.fsXs }
                            }
                        }
                    }
                }
            }
        }
    }
}
