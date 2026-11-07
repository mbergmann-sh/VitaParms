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
    property int days: 0                 // 0 alle, n letzte n Tage, -1 Zeitraum von–bis
    property string fromDay: { const d = new Date(); d.setDate(d.getDate() - 29); return Qt.formatDate(d, "yyyy-MM-dd") }
    property string toDay: Qt.formatDate(new Date(), "yyyy-MM-dd")
    // Optionen für PDF, Excel und Protokolle: gleicher Zeitraum wie die Liste
    function rangeOptions(extra) {
        const o = extra || {}
        if (days === -1) { o.from = fromDay; o.to = toDay } else o.days = days
        return o
    }
    function reload() {
        const l = days === -1 ? app.journal(personId, search.text, 0, fromDay, toDay) : app.journal(personId, search.text, days)
        for (let i = 0; i < l.length; ++i) l[i].showDay = i === 0 || l[i].day !== l[i - 1].day
        entries = l
    }
    onPersonIdChanged: if (active) reload(); else stale = true
    onDaysChanged: reload()
    onFromDayChanged: if (days === -1) reload()
    onToDayChanged: if (days === -1) reload()
    // nur der sichtbare Reiter lädt sofort neu; die anderen beim nächsten Anzeigen
    property bool active: true
    property bool stale: false
    onActiveChanged: if (active && stale) { stale = false; page.reload() }
    Connections { target: app; function onDataChanged() { if (page.active) page.reload(); else page.stale = true } }
    Component.onCompleted: reload()

    // Verlauf als PDF: Wertegruppen wählen – je Gruppe chronologisch, Zeitraum wie die Liste
    property var printGroups: []           // zuletzt gewählt (leer: alle mit Werten)
    Popup {
        id: printSheet
        objectName: "historyPrintSheet"
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(520, page.width - 24)
        padding: 20
        property var groups: []
        onAboutToShow: {
            groups = app.historyGroups(page.personId, page.rangeOptions())
            const pre = page.printGroups
            checked = groups.map(g => g.hasData && (pre.length === 0 || pre.indexOf(g.id) >= 0))
        }
        property var checked: []
        background: Rectangle { color: Theme.surface; radius: Theme.radius; border.color: Theme.rule }
        contentItem: ColumnLayout {
            spacing: Theme.gapS
            Label { text: qsTr("Verlauf als PDF"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2; font.pixelSize: Theme.fsS
                    text: qsTr("Jede gewählte Wertegruppe erscheint für sich, chronologisch, mit Mittelwert, Minimum und Maximum. Zeitraum wie in der Liste gewählt.") }
            Repeater {
                id: groupRep
                model: printSheet.groups
                VCheck {
                    objectName: "historyGroup_" + modelData.id
                    text: modelData.title + (modelData.hasData ? "" : "  " + qsTr("(keine Werte)"))
                    enabled: modelData.hasData
                    checked: printSheet.checked[index] === true
                    onToggled: { const c = printSheet.checked.slice(); c[index] = checked; printSheet.checked = c }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.gapS
                Item { Layout.fillWidth: true }
                VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: printSheet.close() }
                VButton {
                    objectName: "historyPdfButton"
                    text: qsTr("PDF erstellen"); kind: "primary"
                    onClicked: {
                        const ids = printSheet.groups.filter((g, i) => printSheet.checked[i] === true).map(g => g.id)
                        if (ids.length === 0) { win.toast(qsTr("Bitte mindestens eine Wertegruppe wählen."), true); return }
                        page.printGroups = ids
                        printSheet.close()
                        const f = app.exportReport("journal", page.personId, page.rangeOptions({ groups: ids }))
                        if (f) app.openFile(f); else win.toast(app.lastError, true)
                    }
                }
            }
        }
    }

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
                model: [{ t: qsTr("Alle"), d: 0 }, { t: qsTr("30 Tage"), d: 30 }, { t: qsTr("90 Tage"), d: 90 }, { t: qsTr("1 Jahr"), d: 365 },
                        { t: qsTr("Zeitraum"), d: -1 }]
                VChip { objectName: "range_" + modelData.d; text: modelData.t; checked: page.days === modelData.d; onClicked: page.days = modelData.d; autoExclusive: false }
            }
            Label { text: qsTr("Messungen: %1").arg(page.entries.length); color: Theme.muted; height: Theme.touch; verticalAlignment: Text.AlignVCenter }
            VButton { objectName: "glucoseProfileButton"; text: qsTr("BZ-Tagesprofil"); onClicked: page.glucoseProfile() }
            VButton { objectName: "bpProfileButton"; text: qsTr("RR-Tagesprofil"); onClicked: page.bpProfile() }
            VButton { objectName: "journalPdfButton"; text: qsTr("PDF"); onClicked: printSheet.open() }
            VButton { text: qsTr("Excel"); onClicked: { const f = app.exportReport("journalXlsx", page.personId, page.rangeOptions()); if (f) app.openFile(f); else win.toast(app.lastError, true) } }
            // Protokolle aus den erfassten Werten (Zeitraum wie oben gewählt; „Alle“ = gesamter Zeitraum)
            VButton {
                id: protoBtn
                objectName: "protocolsButton"
                text: qsTr("Protokolle …")
                onClicked: protoMenu.popup(protoBtn, 0, protoBtn.height)
                Menu {
                    id: protoMenu
                    function run(kind) {
                        const f = app.exportReport(kind, page.personId, page.rangeOptions())
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
        // Zeitraum von–bis (einschließlich)
        Flow {
            visible: page.days === -1
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            spacing: Theme.gapS
            DateField { objectName: "journalFrom"; label: qsTr("Von"); allowEmpty: false; width: win.phone ? 150 : 170
                        isoDate: page.fromDay; onIsoDateChanged: if (valid && isoDate) page.fromDay = isoDate }
            DateField { objectName: "journalTo"; label: qsTr("Bis"); allowEmpty: false; width: win.phone ? 150 : 170
                        isoDate: page.toDay; onIsoDateChanged: if (valid && isoDate) page.toDay = isoDate }
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
                // Geometrie über die Innenabstände des Knopfs – keine Anker am contentItem (sonst Layout-Schleife)
                leftPadding: 16; rightPadding: 16; topPadding: 12; bottomPadding: 12
                onClicked: page.edit(modelData.id)
                background: Rectangle { color: parent.down ? Theme.brandWash : Theme.surface; border.color: Theme.rule }
                // ohne Layouts: zwei Spalten, Breite vom Knopf – keine Layout-Schleife, Werte linksbündig
                contentItem: Item {
                    id: entry
                    implicitHeight: Math.max(timeCol.implicitHeight, valCol.implicitHeight)
                    Column {
                        id: timeCol
                        width: win.phone ? 76 : 120
                        Label { text: modelData.time; font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink }
                        Label { text: modelData.context; color: Theme.muted; font.pixelSize: Theme.fsXs; wrapMode: Text.WordWrap; width: parent.width }
                    }
                    Column {
                        id: valCol
                        x: timeCol.width + Theme.gap
                        width: entry.width - x
                        spacing: 6
                        Flow {
                            id: valFlow
                            width: parent.width
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
                                wrapMode: Text.WordWrap; width: parent.width }
                    }
                }
            }
            }
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
