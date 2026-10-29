// Wunddokumentation: Wunden der Person, Hinweise, Flächenverlauf und Chronik
// (Wundbeurteilungen und Verbandwechsel, neueste zuerst)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Flickable {
    id: page
    objectName: "woundsPage"
    property var personId: 0
    signal editWound(var id, bool admission)
    signal editEntry(var woundId, var entryId, int kind)
    property bool showClosed: false
    property var wounds: []
    property var woundId: 0
    property var entries: []
    readonly property var current: wounds.find(w => w.id === woundId) || ({})
    function reload() {
        wounds = app.woundList(personId, showClosed)
        if (!wounds.some(w => w.id === woundId)) woundId = wounds.length ? wounds[0].id : 0
        entries = woundId ? app.woundEntries(woundId) : []
    }
    function select(id) { woundId = id; entries = app.woundEntries(id) }
    function output(xlsx, share) {
        if (share) {
            const r = app.shareReports(personId, [{ kind: "wound-" + woundId, xlsx: xlsx, options: {} }], {})
            if (!r.ok) win.toast(app.lastError, true)
            return
        }
        const f = app.createReport(personId, "wound-" + woundId, xlsx, 0, {})
        if (f) app.openFile(f); else win.toast(app.lastError, true)
    }
    onPersonIdChanged: { woundId = 0; reload() }
    onShowClosedChanged: reload()
    property bool active: true
    property bool stale: false
    onActiveChanged: if (active && stale) { stale = false; page.reload() }
    Connections { target: app; function onDataChanged() { if (page.active) page.reload(); else page.stale = true } }
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
            VButton { objectName: "newWoundButton"; text: qsTr("Neue Wunde"); iconText: "+"; kind: page.wounds.length ? "ghost" : "primary"
                      onClicked: page.editWound(0, false) }
            VButton { text: qsTr("Wunde bei Aufnahme"); iconText: "+"; onClicked: page.editWound(0, true) }
            VChip { text: qsTr("abgeheilte zeigen"); checked: page.showClosed; onClicked: page.showClosed = !page.showClosed }
        }
        Label {
            visible: page.wounds.length === 0
            Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
            text: qsTr("Noch keine Wunde erfasst. Wunden, die schon bei der Aufnahme bestanden, mit „Wunde bei Aufnahme“ anlegen – sie erscheinen dann auch im Stammblatt.")
        }
        // Wunden als Chips (rot: Hinweis mit Warnung)
        Flow {
            visible: page.wounds.length > 0
            Layout.fillWidth: true
            Layout.preferredWidth: 200
            Layout.preferredHeight: implicitHeight
            spacing: Theme.gapS
            Repeater {
                model: page.wounds
                VChip {
                    objectName: "wound_" + index
                    text: modelData.title
                    sub: modelData.status !== 0 ? modelData.statusText : (modelData.size || modelData.last)
                    checkable: false
                    checked: modelData.id === page.woundId
                    dotColor: modelData.status !== 0 ? Theme.muted : modelData.warn ? Theme.alert : modelData.notices.length ? Theme.warn : Theme.ok
                    onClicked: page.select(modelData.id)
                }
            }
        }

        // ---------------- gewählte Wunde
        Rectangle {
            visible: page.woundId !== 0
            Layout.fillWidth: true
            implicitHeight: head.implicitHeight + 32
            radius: Theme.radius
            color: Theme.surface; border.color: Theme.rule
            ColumnLayout {
                id: head
                x: 16; y: 16; width: parent.width - 32
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        Label { text: page.current.title || ""; font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label { text: page.current.summary || ""; color: Theme.ink2; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Repeater {
                            model: page.current.notices || []
                            Label { text: (modelData.warn ? "⚠ " : "• ") + modelData.text; color: modelData.warn ? Theme.alert : Theme.warn
                                    wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                        Label { visible: !!page.current.nextDue; text: qsTr("Nächste Wundbeurteilung spätestens am %1.").arg(page.current.nextDue || "")
                                color: Theme.ink2; font.pixelSize: Theme.fsS }
                    }
                    Rectangle {
                        visible: page.current.hasChart === true && page.width > 700
                        Layout.preferredWidth: 360; Layout.preferredHeight: 180
                        color: "white"; radius: 6; border.color: Theme.rule
                        Image { anchors.fill: parent; anchors.margins: 4; fillMode: Image.PreserveAspectFit; cache: false
                                source: parent.visible ? "image://wound/chart-" + page.woundId + "?r=" + app.dataRevision : "" }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.gapS
                    VButton { objectName: "assessButton"; visible: page.current.status === 0; text: qsTr("Wundbeurteilung"); iconText: "+"; kind: "primary"
                              onClicked: page.editEntry(page.woundId, 0, 0) }
                    VButton { objectName: "dressingButton"; visible: page.current.status === 0; text: qsTr("Verbandwechsel"); iconText: "+"
                              onClicked: page.editEntry(page.woundId, 0, 1) }
                    VButton { objectName: "editWoundButton"; text: qsTr("Wunde bearbeiten"); onClicked: page.editWound(page.woundId, false) }
                    VButton { visible: page.current.status === 0; text: qsTr("Abgeheilt"); kind: "flat"; onClicked: confirmHealed.open() }
                    VButton { text: qsTr("PDF öffnen"); kind: "flat"; onClicked: page.output(false, false) }
                    VButton { text: qsTr("Excel öffnen"); kind: "flat"; onClicked: page.output(true, false) }
                    VButton { text: qsTr("Teilen"); kind: "flat"; onClicked: page.output(false, true) }
                }
            }
        }
        Rectangle {        // schmale Geräte: Flächenverlauf unter dem Kopf
            visible: page.woundId !== 0 && page.current.hasChart === true && page.width <= 700
            Layout.fillWidth: true; Layout.preferredHeight: width / 2
            color: "white"; radius: 6; border.color: Theme.rule
            Image { anchors.fill: parent; anchors.margins: 4; fillMode: Image.PreserveAspectFit; cache: false
                    source: parent.visible ? "image://wound/chart-" + page.woundId + "?r=" + app.dataRevision : "" }
        }

        // ---------------- Chronik
        SectionTitle { visible: page.woundId !== 0; text: qsTr("Chronik (%1)").arg(page.entries.length) }
        Label { visible: page.woundId !== 0 && page.entries.length === 0; color: Theme.muted; wrapMode: Text.WordWrap; Layout.fillWidth: true
                text: qsTr("Noch keine Einträge. Beginnen Sie mit einer Wundbeurteilung (Größe, Wundgrund, Exsudat, Wundrand …); bei jedem Verbandwechsel genügt eine kurze Notiz mit Tendenz.") }
        Repeater {
            model: page.entries
            delegate: AbstractButton {
                objectName: "woundEntry_" + index
                Layout.fillWidth: true
                implicitHeight: er.implicitHeight + 24
                onClicked: page.editEntry(page.woundId, modelData.id, modelData.kind)
                background: Rectangle { color: parent.down ? Theme.brandWash : Theme.surface; border.color: Theme.rule }
                contentItem: RowLayout {
                    id: er
                    anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 16; anchors.rightMargin: 16
                    spacing: Theme.gap
                    ColumnLayout {
                        Layout.preferredWidth: 170; Layout.alignment: Qt.AlignTop; spacing: 2
                        Label { text: modelData.at; font.bold: true; color: Theme.ink }
                        Label { text: modelData.kindText; color: modelData.kind === 0 ? Theme.brand : Theme.ink2; font.bold: modelData.kind === 0; font.pixelSize: Theme.fsS }
                        Label { text: qsTr("Hz. %1").arg(modelData.by); color: Theme.muted; font.pixelSize: Theme.fsXs }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.alignment: Qt.AlignTop; spacing: 2
                        Label { text: modelData.summary; color: Theme.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label { visible: modelData.details.length > 0; text: modelData.details.join(" · "); color: Theme.ink2; font.pixelSize: Theme.fsS
                                wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    Image {
                        visible: modelData.hasPhoto
                        Layout.preferredWidth: 120; Layout.preferredHeight: 90; Layout.alignment: Qt.AlignTop
                        fillMode: Image.PreserveAspectFit; asynchronous: true
                        sourceSize.width: 240
                        source: modelData.hasPhoto ? "image://wound/" + modelData.id + "?r=" + app.dataRevision : ""
                    }
                }
            }
        }
        Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: Theme.fsXs; visible: page.wounds.length > 0
                text: qsTr("Nach dem Expertenstandard „Pflege von Menschen mit chronischen Wunden“: vollständige Wundbeurteilung spätestens alle vier Wochen, jeder Verbandwechsel mit Handzeichen. VitaParms ist kein Medizinprodukt – die Hinweise ersetzen keine fachliche Beurteilung.") }
    }
    Confirm {
        id: confirmHealed
        title: qsTr("Wunde abgeheilt?")
        message: qsTr("„%1“ heute als abgeheilt kennzeichnen? Die Dokumentation bleibt erhalten.").arg(page.current.title || "")
        acceptText: qsTr("Abgeheilt")
        onAccepted: { if (app.setWoundHealed(page.woundId)) win.toast(qsTr("Als abgeheilt gekennzeichnet.")); else win.toast(app.lastError, true) }
    }
}
