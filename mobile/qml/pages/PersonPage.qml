// Stammblatt (Ansicht)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Flickable {
    id: page
    property var personId: 0
    signal editRequested()
    property var d: ({})
    property var arms: app.armOptions()
    property var legs: app.legOptions()
    function reload() { d = personId ? app.personDetail(personId) : ({}) }
    onPersonIdChanged: reload()
    Connections { target: app; function onDataChanged() { page.reload() } }
    Component.onCompleted: reload()
    function fmt(iso) { if (!iso) return ""; const p = iso.split("-"); return p[2] + "." + p[1] + "." + p[0] }

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
            VButton { text: qsTr("Stammblatt bearbeiten"); kind: "primary"; onClicked: page.editRequested() }
            VButton { text: qsTr("Stammblatt als PDF"); onClicked: { const f = app.exportReport("sheet", page.personId); if (f) app.openFile(f); else win.toast(app.lastError, true) } }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: page.width > 900 ? 2 : 1
            columnSpacing: Theme.gapL * 2
            rowSpacing: Theme.gap

            InfoBlock {
                title: qsTr("Person")
                rows: [
                    [qsTr("Name"), [d.lastName, d.firstName].filter(s => s).join(", ")],
                    [qsTr("Geburtsdatum"), page.fmt(d.birthDate)],
                    [qsTr("Geschlecht"), ({ m: qsTr("männlich"), w: qsTr("weiblich"), d: qsTr("divers") })[d.sex] || ""],
                    [qsTr("Anschrift"), [d.street, [d.zip, d.city].filter(s => s).join(" ")].filter(s => s).join(", ")],
                    [qsTr("Telefon"), d.phone || ""],
                    [qsTr("Mobiltelefon"), d.mobile || ""]
                ]
            }
            InfoBlock {
                title: qsTr("Pflege, Ausweis und Vorsorge")
                rows: [
                    [qsTr("Pflegegrad"), d.careLevel > 0 ? qsTr("Pflegegrad %1").arg(d.careLevel) : qsTr("kein Pflegegrad")],
                    [qsTr("Nächste Überprüfung"), d.careLevel > 0 ? page.fmt(d.careReview) : ""],
                    [qsTr("Schwerbehindertenausweis"), d.disabilityDegree > 0 ? qsTr("GdB %1").arg(d.disabilityDegree) : qsTr("keiner")],
                    [qsTr("Merkzeichen"), d.disabilityDegree > 0 ? (d.disabilityMarks || []).join(", ") : ""],
                    [qsTr("Gültig bis"), d.disabilityDegree > 0 ? (d.disabilityValid ? page.fmt(d.disabilityValid) : qsTr("unbefristet")) : ""]
                ].concat((d.advance || []).map(l => [qsTr("Vorsorge"), l]))
            }
            InfoBlock {
                objectName: "contactsBlock"
                title: qsTr("Kontakte")
                visible: (d.contacts || []).length > 0
                rows: d.contacts || []
            }
            InfoBlock {
                title: qsTr("Diagnosen")
                rows: [["", d.diagnoses || qsTr("—")]]
            }
            InfoBlock {
                title: qsTr("Weitere Angaben")
                rows: (d.extra || []).filter(e => e.value).map(e => [e.label, e.value])
                      .concat([[qsTr("Bemerkungen"), d.notes || ""]])
                      .concat([[qsTr("Gliedmaßen"), [d.armLeft ? qsTr("Arm links: %1").arg(page.arms[d.armLeft]) : "",
                                                    d.armRight ? qsTr("Arm rechts: %1").arg(page.arms[d.armRight]) : "",
                                                    d.legLeft ? qsTr("Bein links: %1").arg(page.legs[d.legLeft]) : "",
                                                    d.legRight ? qsTr("Bein rechts: %1").arg(page.legs[d.legRight]) : ""].filter(s => s).join("; ")]])
            }
        }
    }
}
