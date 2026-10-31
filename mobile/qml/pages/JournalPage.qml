// Messwerte: alle Messungen, neueste zuerst, nach Tagen gruppiert
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Item {
    id: page
    property var personId: 0
    signal edit(var id)
    signal glucoseProfile()
    signal bpProfile()
    property var entries: []
    property int days: 0
    function reload() {
        const l = app.journal(personId, search.text, days)
        for (let i = 0; i < l.length; ++i) l[i].showDay = i === 0 || l[i].day !== l[i - 1].day
        entries = l
    }
    onPersonIdChanged: reload()
    onDaysChanged: reload()
    // nur der sichtbare Reiter lädt sofort neu; die anderen beim nächsten Anzeigen
    property bool active: true
    property bool stale: false
    onActiveChanged: if (active && stale) { stale = false; page.reload() }
    Connections { target: app; function onDataChanged() { if (page.active) page.reload(); else page.stale = true } }
    Component.onCompleted: reload()

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.gapL; anchors.rightMargin: Theme.gapL; anchors.topMargin: Theme.gap
        spacing: Theme.gap
        Flow {
            Layout.fillWidth: true
            spacing: Theme.gapS
            VField { id: search; width: Math.min(300, page.width - 2 * Theme.gapL); placeholder: qsTr("Suchen in Beobachtungen …")
                     field.onTextChanged: searchTimer.restart() }
            Repeater {
                model: [{ t: qsTr("Alle"), d: 0 }, { t: qsTr("30 Tage"), d: 30 }, { t: qsTr("90 Tage"), d: 90 }, { t: qsTr("1 Jahr"), d: 365 }]
                VChip { text: modelData.t; checked: page.days === modelData.d; onClicked: page.days = modelData.d; autoExclusive: false }
            }
            Label { text: qsTr("Messungen: %1").arg(page.entries.length); color: Theme.muted; height: Theme.touch; verticalAlignment: Text.AlignVCenter }
            VButton { objectName: "glucoseProfileButton"; text: qsTr("BZ-Tagesprofil"); onClicked: page.glucoseProfile() }
            VButton { objectName: "bpProfileButton"; text: qsTr("RR-Tagesprofil"); onClicked: page.bpProfile() }
            VButton { text: qsTr("PDF"); onClicked: { const f = app.exportReport("journal", page.personId, { days: page.days }); if (f) app.openFile(f); else win.toast(app.lastError, true) } }
            VButton { text: qsTr("Excel"); onClicked: { const f = app.exportReport("journalXlsx", page.personId, { days: page.days }); if (f) app.openFile(f); else win.toast(app.lastError, true) } }
            // Protokolle aus den erfassten Werten (Zeitraum wie oben gewählt; „Alle“ = gesamter Zeitraum)
            VButton {
                id: protoBtn
                objectName: "protocolsButton"
                text: qsTr("Protokolle …")
                onClicked: protoMenu.popup(protoBtn, 0, protoBtn.height)
                Menu {
                    id: protoMenu
                    function run(kind) {
                        const f = app.exportReport(kind, page.personId, { days: page.days })
                        if (f) app.openFile(f); else win.toast(app.lastError, true)
                    }
                    MenuItem { text: qsTr("Stuhlprotokoll (PDF)"); font.pixelSize: Theme.fsM; height: Theme.touch; onTriggered: protoMenu.run("stool") }
                    MenuItem { text: qsTr("Stuhlprotokoll (Excel)"); font.pixelSize: Theme.fsM; height: Theme.touch; onTriggered: protoMenu.run("stoolXlsx") }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Ein- und Ausfuhr (PDF)"); font.pixelSize: Theme.fsM; height: Theme.touch; onTriggered: protoMenu.run("fluid") }
                    MenuItem { text: qsTr("Ein- und Ausfuhr (Excel)"); font.pixelSize: Theme.fsM; height: Theme.touch; onTriggered: protoMenu.run("fluidXlsx") }
                    MenuSeparator {}
                    MenuItem { text: qsTr("BZ-Tagesprofile (PDF)"); font.pixelSize: Theme.fsM; height: Theme.touch; onTriggered: protoMenu.run("glucose") }
                    MenuItem { text: qsTr("BZ-Tagesprofile (Excel)"); font.pixelSize: Theme.fsM; height: Theme.touch; onTriggered: protoMenu.run("glucoseXlsx") }
                    MenuSeparator {}
                    MenuItem { text: qsTr("RR-Tagesprofile (PDF)"); font.pixelSize: Theme.fsM; height: Theme.touch; onTriggered: protoMenu.run("bpProfile") }
                    MenuItem { text: qsTr("RR-Tagesprofile (Excel)"); font.pixelSize: Theme.fsM; height: Theme.touch; onTriggered: protoMenu.run("bpProfileXlsx") }
                }
            }
        }
        Timer { id: searchTimer; interval: 300; onTriggered: page.reload() }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: page.entries
            spacing: 0
            delegate: Column {
              width: list.width
              Label {
                visible: modelData.showDay
                width: list.width
                text: modelData.dayText
                font.bold: true; color: Theme.ink2; font.pixelSize: Theme.fsS
                topPadding: 18; bottomPadding: 6
              }
              AbstractButton {
                width: list.width
                implicitHeight: entry.implicitHeight + 24
                onClicked: page.edit(modelData.id)
                background: Rectangle { color: parent.down ? Theme.brandWash : Theme.surface; border.color: Theme.rule }
                contentItem: RowLayout {
                    id: entry
                    anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 16; anchors.rightMargin: 16
                    spacing: Theme.gap
                    ColumnLayout {
                        Layout.preferredWidth: win.phone ? 76 : 120; Layout.alignment: Qt.AlignTop
                        spacing: 0
                        Label { text: modelData.time; font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
                        Label { text: modelData.context; color: Theme.muted; font.pixelSize: Theme.fsXs; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Flow {
                            id: valFlow
                            Layout.fillWidth: true
                            spacing: 18
                            Repeater {
                                model: modelData.values
                                delegate: Row {
                                    spacing: 6
                                    Rectangle { width: 4; height: 22; radius: 2; color: Theme.series(modelData.color); anchors.verticalCenter: parent.verticalCenter }
                                    Label { id: vl; text: modelData.label; color: Theme.muted; font.pixelSize: Theme.fsS; anchors.verticalCenter: parent.verticalCenter }
                                    Label { id: vt; text: modelData.text; color: modelData.alert || Theme.ink; font.pixelSize: Theme.fsL; font.bold: true; anchors.verticalCenter: parent.verticalCenter }
                                    // Einheit samt Art (z. B. „ml · Suppe/flüssige Speisen“) bricht auf schmalen Bildschirmen um
                                    Label { text: modelData.unit; color: Theme.muted; font.pixelSize: Theme.fsXs; anchors.verticalCenter: parent.verticalCenter
                                            wrapMode: Text.WordWrap
                                            width: Math.min(Math.ceil(um.advanceWidth) + 2, Math.max(60, valFlow.width - 4 - vl.implicitWidth - vt.implicitWidth - 18))
                                            TextMetrics { id: um; font.family: Theme.family; font.pixelSize: Theme.fsXs; text: modelData.unit } }
                                }
                            }
                        }
                        Label { visible: !!modelData.observations; text: modelData.observations; color: Theme.ink2
                                wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                }
            }
            }
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
