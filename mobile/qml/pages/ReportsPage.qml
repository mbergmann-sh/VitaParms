// Berichte: alle Ausgaben der Person als PDF bzw. Excel öffnen oder teilen – einzeln oder mehrere
// zusammen, auf Wunsch als verschlüsseltes ZIP-Archiv (wie „Berichte“ auf der Website)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Flickable {
    id: page
    objectName: "reportsPage"
    property var personId: 0
    property bool active: false
    property bool stale: true
    property var items: []
    property var selected: []            // Schlüssel "kind" bzw. "kind:xlsx"
    property int days: 30
    function reload() {
        if (!active) { stale = true; return }
        stale = false
        items = app.reportCatalog(personId)
        selected = selected.filter(k => items.some(it => k.split(":")[0] === it.kind && it.available))
    }
    function key(kind, xlsx) { return xlsx ? kind + ":xlsx" : kind }
    function toggle(k) {
        const s = selected.slice()
        const i = s.indexOf(k)
        if (i < 0) s.push(k); else s.splice(i, 1)
        selected = s
    }
    function itemFor(kind) { return items.find(it => it.kind === kind) || ({}) }
    function open(it, xlsx) {
        const f = app.createReport(personId, it.kind, xlsx, days, it.options || {})
        if (!f) { win.toast(app.lastError, true); return }
        if (!app.openFile(f)) win.toast(xlsx ? qsTr("Keine App zum Anzeigen von Excel-Dateien gefunden.")
                                             : qsTr("Keine App zum Anzeigen von PDF-Dateien gefunden."), true)
    }
    function share(list) {
        const r = app.shareReports(personId, list, { days: page.days, zip: zipCheck.checked, password: zipCheck.checked ? password.text : "" })
        if (!r.ok) win.toast(app.lastError, true)
        return r
    }
    function shareSelected() {
        return share(selected.map(k => { const p = k.split(":"); const it = itemFor(p[0]); return { kind: p[0], xlsx: p[1] === "xlsx", options: it.options || {} } }))
    }
    onPersonIdChanged: { selected = []; reload() }
    Connections { target: app; function onDataChanged() { page.reload() } }
    onActiveChanged: if (active && stale) reload()
    Component.onCompleted: reload()

    contentHeight: col.implicitHeight + 48
    clip: true
    ColumnLayout {
        id: col
        x: Theme.gapL; y: Theme.gap
        width: page.width - 2 * Theme.gapL
        spacing: Theme.gap

        Label {
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
            text: qsTr("Berichte öffnen sich in der Anzeige-App des Tablets (dort auch drucken). „Teilen“ gibt sie per E-Mail, Messenger oder Cloud weiter – Gesundheitsdaten bitte nur verschlüsselt oder an vertrauenswürdige Empfänger senden.")
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.gapS
            Label { text: qsTr("Zeitraum:"); color: Theme.ink2 }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                Repeater {
                    model: [[7, qsTr("7 Tage")], [30, qsTr("30 Tage")], [90, qsTr("90 Tage")], [365, qsTr("1 Jahr")], [0, qsTr("alles")]]
                    VChip { objectName: "period" + modelData[0]; checkable: false; text: modelData[1]; checked: page.days === modelData[0]
                            onClicked: page.days = modelData[0] }
                }
            }
        }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: Theme.fsS
                text: qsTr("Der Zeitraum gilt für Verlauf, Kurven und Protokolle; Verlaufskurven zeigen die Woche, den Monat bzw. das Jahr bis heute.") }

        Repeater {
            model: {
                const g = []
                for (const it of page.items) if (g.indexOf(it.group) < 0) g.push(it.group)
                return g
            }
            delegate: ColumnLayout {
                readonly property string group: modelData
                Layout.fillWidth: true
                spacing: 0
                SectionTitle { text: parent.group }
                Repeater {
                    model: page.items.filter(it => it.group === parent.group)
                    delegate: Rectangle {
                        objectName: "reportCard"
                        Layout.fillWidth: true
                        implicitHeight: rrow.implicitHeight + 24
                        color: Theme.surface; border.color: Theme.rule
                        opacity: modelData.available ? 1 : 0.5
                        RowLayout {
                            id: rrow
                            x: 16; y: 12; width: parent.width - 32
                            spacing: Theme.gap
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 2
                                Label { text: modelData.title; font.bold: true; color: Theme.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                                Label { text: modelData.available ? modelData.description : qsTr("Keine Einträge vorhanden."); color: Theme.ink2
                                        font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                            Flow {
                                Layout.preferredWidth: page.width > 900 ? 560 : 300
                                spacing: Theme.gapS
                                layoutDirection: Qt.RightToLeft
                                enabled: modelData.available
                                VChip { objectName: "pick_" + modelData.kind; checkable: false; text: qsTr("PDF") + (page.selected.indexOf(modelData.kind) >= 0 ? " ✓" : "")
                                        checked: page.selected.indexOf(modelData.kind) >= 0; onClicked: page.toggle(modelData.kind) }
                                VChip { visible: modelData.xlsx; checkable: false; text: qsTr("Excel") + (page.selected.indexOf(modelData.kind + ":xlsx") >= 0 ? " ✓" : "")
                                        checked: page.selected.indexOf(modelData.kind + ":xlsx") >= 0; onClicked: page.toggle(modelData.kind + ":xlsx") }
                                Label { text: qsTr("auswählen:"); color: Theme.muted; font.pixelSize: Theme.fsS; height: Theme.touch - 6; verticalAlignment: Text.AlignVCenter }
                                VButton { objectName: "share_" + modelData.kind; text: qsTr("Teilen"); kind: "flat"
                                          onClicked: page.share([{ kind: modelData.kind, xlsx: false, options: modelData.options || {} }]) }
                                VButton { visible: modelData.xlsx; text: qsTr("Excel öffnen"); kind: "flat"; onClicked: page.open(modelData, true) }
                                VButton { objectName: "open_" + modelData.kind; text: qsTr("Öffnen"); onClicked: page.open(modelData, false) }
                            }
                        }
                    }
                }
            }
        }

        // mehrere Berichte zusammen teilen
        Rectangle {
            objectName: "shareBox"
            Layout.fillWidth: true
            implicitHeight: sb.implicitHeight + 32
            radius: Theme.radius
            color: Theme.brandWash
            ColumnLayout {
                id: sb
                x: 16; y: 16; width: parent.width - 32
                spacing: Theme.gapS
                Label { text: page.selected.length ? qsTr("%1 Bericht(e) ausgewählt").arg(page.selected.length)
                                                    : qsTr("Mehrere Berichte zusammen teilen: oben „PDF“ bzw. „Excel“ antippen.")
                        font.bold: page.selected.length > 0; color: Theme.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                CheckBox { id: zipCheck; objectName: "reportsZip"; text: qsTr("als ZIP-Archiv mit Passwort (AES-256)"); font.pixelSize: Theme.fsM }
                VField { id: password; objectName: "reportsPassword"; visible: zipCheck.checked; label: qsTr("Passwort (mindestens 8 Zeichen)")
                         echoMode: TextInput.Password; Layout.fillWidth: true; Layout.maximumWidth: 480 }
                Label { visible: zipCheck.checked; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2; font.pixelSize: Theme.fsS
                        text: qsTr("Das Passwort teilen Sie auf anderem Weg mit (z. B. am Telefon). Öffnen lässt sich das Archiv u. a. mit 7-Zip, WinZip oder Keka.") }
                Flow {
                    Layout.fillWidth: true; spacing: Theme.gapS
                    VButton { objectName: "shareSelectedButton"; text: qsTr("Ausgewählte teilen"); kind: "primary"; enabled: page.selected.length > 0
                              onClicked: page.shareSelected() }
                    VButton { text: qsTr("Auswahl aufheben"); kind: "flat"; visible: page.selected.length > 0; onClicked: page.selected = [] }
                }
            }
        }
    }
}
