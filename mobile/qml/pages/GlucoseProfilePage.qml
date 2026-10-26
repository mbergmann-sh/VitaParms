// BZ-Tagesprofil: sieben Zeitpunkte eines Tages – Erinnerung und Erfassung (wie am Computer)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "glucoseProfilePage"
    property var personId: 0
    property var summary: app.personSummary(personId)
    property var points: app.glucoseProfilePoints().map(p => Object.assign({}, p))
    signal closed(string message)
    background: Rectangle { color: Theme.paper }

    function pad(n) { return (n < 10 ? "0" : "") + n }
    function isoOf(d) { return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate()) }
    // nach dem letzten Zeitpunkt des Tages: morgen vorschlagen
    function suggestedDay() {
        const now = new Date()
        const nowHm = pad(now.getHours()) + ":" + pad(now.getMinutes())
        let last = ""
        for (const p of points) if (p.active && p.time > last) last = p.time
        const d = new Date()
        if (nowHm > last) d.setDate(d.getDate() + 1)
        return isoOf(d)
    }
    property int openCount: app.glucoseProfileOpen(personId, day.isoDate)
    Connections { target: app; function onDataChanged() { page.openCount = app.glucoseProfileOpen(page.personId, day.isoDate) } }

    function start() {
        for (let i = 0; i < rep.count; ++i) {
            const t = rep.itemAt(i).timeText.match(/^(\d{1,2})[:.](\d{2})$/)
            if (!t) { win.toast(qsTr("Bitte die Uhrzeiten prüfen (HH:MM)."), true); return }
            points[i].time = pad(parseInt(t[1])) + ":" + t[2]
            points[i].active = rep.itemAt(i).active
        }
        const n = app.startGlucoseProfile(personId, day.isoDate, points, sound.checked)
        if (n < 0) { win.toast(app.lastError, true); return }
        if (n === 0) { win.toast(qsTr("Für diesen Tag liegt kein gewählter Zeitpunkt in der Zukunft."), true); return }
        page.closed(qsTr("BZ-Tagesprofil: %1 Erinnerung(en) gestellt.").arg(n))
    }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: page.closed("") }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                Label { text: qsTr("BZ-Tagesprofil"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.alignment: Qt.AlignHCenter }
                Label { text: page.summary.name || ""; color: Theme.muted; font.pixelSize: Theme.fsS; Layout.alignment: Qt.AlignHCenter }
            }
            VButton { visible: page.openCount > 0; text: qsTr("Tagesprofil beenden"); kind: "flat"
                      onClicked: { const n = app.stopGlucoseProfile(page.personId, day.isoDate); win.toast(qsTr("%1 offene Erinnerung(en) für diesen Tag gelöscht.").arg(n)) } }
            VButton { objectName: "profileStartButton"; text: qsTr("Tagesprofil starten"); kind: "primary"; onClicked: page.start() }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 300
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: Math.min(800, page.width - 2 * Theme.gapL)
            spacing: Theme.gap
            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                text: qsTr("VitaParms erinnert zu den gewählten Zeitpunkten an die Blutzuckermessung und öffnet auf Wunsch gleich das Erfassungsfenster mit dem passenden Messzeitpunkt. Die Erinnerungen stehen als Termine im Terminplaner (und kommen mit dem Abgleich auf den Computer).")
            }
            DateField { id: day; label: qsTr("Tag"); allowEmpty: false; Layout.preferredWidth: 220
                        Component.onCompleted: isoDate = page.suggestedDay() }
            Label { visible: page.openCount > 0; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.warn
                    text: qsTr("Für diesen Tag sind bereits %1 Erinnerung(en) gestellt – „Tagesprofil starten“ ersetzt sie.").arg(page.openCount) }
            SectionTitle { text: qsTr("Zeitpunkte") }
            Repeater {
                id: rep
                model: page.points
                delegate: RowLayout {
                    property alias timeText: tf.text
                    property alias active: cb.checked
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    CheckBox { id: cb; checked: modelData.active; text: modelData.label; font.pixelSize: Theme.fsM; Layout.fillWidth: true }
                    VField { id: tf; text: modelData.time; enabled: cb.checked; Layout.fillWidth: false; Layout.preferredWidth: 120; Layout.maximumWidth: 130; placeholder: "HH:MM"
                             inputMethodHints: Qt.ImhPreferNumbers }
                }
            }
            CheckBox { id: sound; text: qsTr("Erinnerung mit Ton"); checked: true; font.pixelSize: Theme.fsM }
        }
    }
}
