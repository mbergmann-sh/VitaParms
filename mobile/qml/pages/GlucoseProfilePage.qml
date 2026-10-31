// BZ-Tagesprofil (sieben Zeitpunkte) bzw. RR-Tagesprofil (sechs Zeitpunkte) eines Tages – Erinnerung und Erfassung
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: bp ? "bpProfilePage" : "glucoseProfilePage"
    property var personId: 0
    property bool bp: false        // true: RR-Tagesprofil
    property var summary: app.personSummary(personId)
    property var points: app.glucoseProfilePoints(bp).map(p => Object.assign({}, p))
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
    property int openCount: app.glucoseProfileOpen(personId, day.isoDate, bp)
    Connections { target: app; function onDataChanged() { page.openCount = app.glucoseProfileOpen(page.personId, day.isoDate, page.bp) } }

    function start() {
        for (let i = 0; i < rep.count; ++i) {
            const t = rep.itemAt(i).timeText.match(/^(\d{1,2})[:.](\d{2})$/)
            if (!t) { win.toast(qsTr("Bitte die Uhrzeiten prüfen (HH:MM)."), true); return }
            points[i].time = pad(parseInt(t[1])) + ":" + t[2]
            points[i].active = rep.itemAt(i).active
        }
        const n = app.startGlucoseProfile(personId, day.isoDate, points, sound.checked, bp)
        if (n < 0) { win.toast(app.lastError, true); return }
        if (n === 0) { win.toast(qsTr("Für diesen Tag liegt kein gewählter Zeitpunkt in der Zukunft."), true); return }
        page.closed((bp ? qsTr("RR-Tagesprofil: %1 Erinnerung(en) gestellt.") : qsTr("BZ-Tagesprofil: %1 Erinnerung(en) gestellt.")).arg(n))
    }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { text: win.phone ? "✕" : qsTr("Abbrechen"); Accessible.name: qsTr("Abbrechen"); kind: "flat"; font.pixelSize: win.phone ? Theme.fsL + 6 : Theme.fsM; leftPadding: win.phone ? 8 : 18; rightPadding: win.phone ? 8 : 18; onClicked: page.closed("") }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                Label { text: page.bp ? qsTr("RR-Tagesprofil") : qsTr("BZ-Tagesprofil"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true; Layout.minimumWidth: 0
                        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                Label { text: page.summary.name || ""; color: Theme.muted; font.pixelSize: Theme.fsS; Layout.fillWidth: true; Layout.minimumWidth: 0
                        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
            }
            VButton { visible: page.openCount > 0; text: win.wide ? qsTr("Tagesprofil beenden") : qsTr("Beenden"); kind: "flat"
                      onClicked: { const n = app.stopGlucoseProfile(page.personId, day.isoDate, page.bp); win.toast(qsTr("%1 offene Erinnerung(en) für diesen Tag gelöscht.").arg(n)) } }
            VButton { objectName: "profileStartButton"; text: win.wide ? qsTr("Tagesprofil starten") : qsTr("Starten"); kind: "primary"; onClicked: page.start() }
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
                text: page.bp ? qsTr("%1 erinnert zu den gewählten Zeitpunkten an die Blutdruckmessung und öffnet auf Wunsch gleich das Erfassungsfenster mit dem passenden Messzeitpunkt. Messen Sie nach 5 Minuten Ruhe im Sitzen und immer am selben Arm. Die Erinnerungen stehen als Termine im Terminplaner (und kommen mit dem Abgleich auf den Computer).").arg(app.appName)
                              : qsTr("%1 erinnert zu den gewählten Zeitpunkten an die Blutzuckermessung und öffnet auf Wunsch gleich das Erfassungsfenster mit dem passenden Messzeitpunkt. Die Erinnerungen stehen als Termine im Terminplaner (und kommen mit dem Abgleich auf den Computer).").arg(app.appName)
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
                    CheckBox { id: cb; checked: modelData.active; Accessible.name: modelData.label }
                    Label { text: modelData.label; font.pixelSize: Theme.fsM; color: cb.checked ? Theme.ink : Theme.muted
                            wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.minimumWidth: 0
                            MouseArea { anchors.fill: parent; onClicked: cb.toggle() } }
                    VField { id: tf; text: modelData.time; enabled: cb.checked; Layout.fillWidth: false; Layout.preferredWidth: win.phone ? 96 : 120; Layout.maximumWidth: 130; placeholder: "HH:MM"
                             inputMethodHints: Qt.ImhPreferNumbers }
                }
            }
            VCheck { id: sound; text: qsTr("Erinnerung mit Ton"); checked: true; font.pixelSize: Theme.fsM }
        }
    }
}
