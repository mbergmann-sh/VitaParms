// Medikamente stellen: Vorlese-Assistent wie am Computer – Tagesdosierer oder Wochendosierer,
// Medikament für Medikament mit „Weiter“ bestätigen; danach Protokoll und Abbuchung vom Bestand
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "prepPage"
    property var personId: 0
    signal closed(string message)
    signal openStock()
    background: Rectangle { color: Theme.paper }

    property var summary: app.personSummary(personId)
    property var info: app.prepInfo(personId)
    property var voice: app.speechInfo()
    property int days: info.weekly ? 7 : 1
    property int slot: -1
    property var session: ({})
    readonly property var items: session.items || []
    property int index: 0
    property bool finished: false
    property var confirmed: []
    property var prepId: 0
    property var warnings: []
    property string logged: ""
    property bool ready: false
    readonly property var current: index >= 0 && index < items.length ? items[index] : ({})

    function speak(t, force) { if (force || autoRead.checked) app.say(t) }
    // abgebrochenen Durchgang protokollieren (bestätigte Medikamente werden abgebucht)
    function logAborted() {
        if (!finished && confirmed.length > 0)
            app.prepFinish(personId, days, slot, day.isoDate, confirmed, false, "")
    }
    function saveNote() { if (prepId) app.prepNote(prepId, note.text) }
    function restart() {
        if (!ready) return
        logAborted()
        saveNote()
        session = app.prepSession(personId, days, slot, day.isoDate)
        index = 0; finished = false; confirmed = []; prepId = 0; warnings = []; logged = ""
        note.text = ""
        if (items.length === 0) { speak(session.nothing || "", false); return }
        speak(session.intro + " " + items[0].speech, false)
    }
    // beim Weiterblättern die Karte ganz zeigen (die Einstellungen stehen darüber)
    function showCard() { cardTimer.restart() }
    Timer {   // nach dem Layout (Höhe der Karte ändert sich mit dem Medikament)
        id: cardTimer; interval: 80
        onTriggered: flick.contentY = Math.max(0, Math.min(prepCard.y + col.y - Theme.gapS, flick.contentHeight - flick.height))
    }
    function next() {
        if (finished) { restart(); flick.contentY = 0; return }
        if (confirmed.indexOf(index) < 0) confirmed = confirmed.concat([index])
        if (index + 1 < items.length) { index++; speak(items[index].speech, false); showCard(); return }
        finish()
    }
    function previous() {
        if (finished) { finished = false; index = items.length - 1; speak(items[index].speech, false); return }
        if (index > 0) { index--; speak(items[index].speech, false); showCard() }
    }
    function repeat() {
        if (finished) { speak(session.doneText, true); return }
        if (items.length) speak(current.speech, true)
    }
    function finish() {
        const r = app.prepFinish(personId, days, slot, day.isoDate, confirmed, true, note.text)
        if (!r.ok) { win.toast(app.lastError || qsTr("Das Protokoll konnte nicht gespeichert werden."), true); return }
        finished = true
        prepId = r.id
        warnings = r.warnings || []
        logged = r.logged || ""
        info = app.prepInfo(personId)
        doneSpeech.restart()
        showCard()
    }
    function close() {
        app.stopSpeech()
        if (finished) saveNote(); else logAborted()
        page.closed(finished ? qsTr("Medikamente sind gerichtet.") : "")
    }
    // Android startet die Sprachausgabe verzögert: Stimmen und Hinweis nach kurzer Zeit neu lesen
    Timer { interval: 1500; repeat: true; running: !page.voice.description && page.voice.available === true
            property int tries: 0
            onTriggered: { page.voice = app.speechInfo(); if (++tries > 10) stop() } }
    Timer { id: doneSpeech; interval: 900; onTriggered: if (page.finished) page.speak(page.session.doneText, true) }
    Component.onCompleted: { day.isoDate = info.start; ready = true; restart() }
    Component.onDestruction: app.stopSpeech()

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { objectName: "prepCloseButton"; text: qsTr("Beenden"); kind: "flat"; onClicked: page.close() }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 0
                Label { text: qsTr("Medikamente stellen"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true; Layout.minimumWidth: 0
                        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                Label { text: page.summary.name || ""; color: Theme.muted; font.pixelSize: Theme.fsS; Layout.fillWidth: true; Layout.minimumWidth: 0
                        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
            }
            VButton { text: qsTr("Protokoll"); kind: "flat"; onClicked: { page.saveNote(); logPopup.open() } }
        }
    }

    footer: Rectangle {
        implicitHeight: 84
        color: Theme.surface
        Rectangle { width: parent.width; height: 1; color: Theme.rule }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: Theme.gapL; anchors.rightMargin: Theme.gapL
            spacing: Theme.gap
            VButton { objectName: "prepPrevButton"; text: win.wide ? qsTr("◀ Zurück") : "◀"; implicitHeight: 60
                      enabled: page.items.length > 0 && (page.finished || page.index > 0); onClicked: page.previous() }
            Item { Layout.fillWidth: true }
            VButton { objectName: "prepRepeatButton"; text: win.wide ? qsTr("↻ Wiederholen") : "↻"; implicitHeight: 60
                      enabled: page.items.length > 0; onClicked: page.repeat() }
            VButton { objectName: "prepNextButton"; kind: "primary"; implicitHeight: 60; leftPadding: win.wide ? 56 : 20; rightPadding: win.wide ? 56 : 20
                      enabled: page.items.length > 0
                      text: page.finished ? qsTr("Von vorn") : page.index + 1 < page.items.length ? qsTr("Weiter ▶") : qsTr("Fertig ✔")
                      onClicked: page.next() }
            Item { Layout.fillWidth: true }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: col.implicitHeight + 48
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: page.width - 2 * Theme.gapL
            spacing: Theme.gap

            // Einstellungen
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                VChip { objectName: "prepDaily"; checkable: false; text: qsTr("täglich"); checked: page.days === 1
                        onClicked: { page.days = 1; page.restart() } }
                VChip { objectName: "prepWeekly"; checkable: false; text: qsTr("wöchentlich (7 Tage)"); checked: page.days === 7
                        onClicked: { page.days = 7; page.ready = false; day.isoDate = app.prepWeekStart(page.personId); page.ready = true; page.restart() } }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 900 ? 4 : 2
                columnSpacing: Theme.gap; rowSpacing: Theme.gapS
                VCombo { objectName: "prepSlotCombo"; label: qsTr("Zeitpunkt"); model: page.info.slotNames || []
                         Layout.fillWidth: true; Layout.preferredWidth: 10
                         onActivated: (i) => { page.slot = i - 1; page.restart() } }
                DateField { id: day; objectName: "prepDay"; label: page.days > 1 ? qsTr("ab") : qsTr("für"); allowEmpty: false
                            Layout.fillWidth: true; Layout.preferredWidth: 10
                            onIsoDateChanged: if (valid) page.restart() }
                VCombo { label: qsTr("Stimme"); model: page.voice.voices || []; textRole: "label"
                         visible: page.voice.available === true
                         Layout.fillWidth: true; Layout.preferredWidth: 10
                         currentIndex: Math.max(0, (page.voice.voices || []).findIndex(v => v.key === page.voice.voice))
                         onActivated: (i) => { app.setVoice(page.voice.voices[i].key); page.voice = app.speechInfo(); page.repeat() } }
                VCheck { id: autoRead; text: qsTr("automatisch vorlesen"); checked: page.voice.available === true
                           enabled: page.voice.available === true; font.pixelSize: Theme.fsM }
            }
            Label { visible: page.days > 1; text: qsTr("Wochendosierer: %1").arg(page.session.range || ""); color: Theme.ink2
                    Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label { visible: !!page.voice.info; text: "⚠ " + (page.voice.info || ""); color: Theme.warn; font.pixelSize: Theme.fsS
                    Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Label { text: page.info.last || ""; color: Theme.muted; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            // nicht zu stellen: nicht fällig, Bedarf, Sonstiges, Notfall
            Label {
                objectName: "prepDayInfo"
                Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.StyledText; font.pixelSize: Theme.fsS; color: Theme.ink2
                function esc(l) { return l.map(x => x.replace(/&/g, "&amp;").replace(/</g, "&lt;")).join(" · ") }
                readonly property var s: page.session
                text: [
                    (s.notDue || []).length ? "<b>" + (page.days > 1 ? qsTr("In diesen Tagen nicht fällig:") : qsTr("An diesem Tag nicht fällig:")) + "</b> " + esc(s.notDue) : "",
                    (s.asNeeded || []).length ? "<b>" + qsTr("Bedarfsmedikation:") + "</b> " + esc(s.asNeeded) : "",
                    (s.other || []).length ? "<b>" + qsTr("Sonstiges:") + "</b> " + esc(s.other) : "",
                    (s.emergency || []).length ? "<font color='" + Theme.alert + "'><b>" + qsTr("Notfallmedikation:") + "</b> " + esc(s.emergency) + "</font>" : ""
                ].filter(x => x).join("<br>")
                visible: text !== ""
            }

            // Karte: Medikament bzw. fertig
            Rectangle {
                id: prepCard
                objectName: "prepCard"
                Layout.fillWidth: true
                implicitHeight: card.implicitHeight + 48
                radius: Theme.radius + 4
                color: Theme.surface
                border.color: page.finished ? Theme.ok : Theme.rule
                border.width: page.finished ? 2 : 1
                ColumnLayout {
                    id: card
                    x: 24; y: 24; width: parent.width - 48
                    spacing: Theme.gap

                    // nichts zu stellen
                    Label { visible: page.items.length === 0; text: page.session.nothing || ""; font.pixelSize: Theme.fsXl; color: Theme.ink2
                            Layout.fillWidth: true; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter }

                    // Medikament
                    ColumnLayout {
                        visible: page.items.length > 0 && !page.finished
                        Layout.fillWidth: true
                        spacing: Theme.gapS
                        Label { objectName: "prepProgress"; text: qsTr("Medikament %1 von %2").arg(page.index + 1).arg(page.items.length)
                                font.bold: true; color: Theme.muted }
                        Label { objectName: "prepName"; text: page.current.name || ""; font.pixelSize: Theme.fsXxl; font.bold: true; color: Theme.ink
                                Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Label { visible: !!page.current.details; text: page.current.details || ""; font.pixelSize: Theme.fsL; color: Theme.ink2
                                Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        // Tagesdosierer: vier Zeitpunkte
                        GridLayout {
                            visible: page.days === 1
                            Layout.fillWidth: true
                            columns: 4; columnSpacing: 6; rowSpacing: 6
                            Repeater {
                                model: 4
                                delegate: Rectangle {
                                    readonly property string dose: (page.current.doses || [])[index] || ""
                                    readonly property bool on: (page.current.active || [])[index] === true
                                    Layout.fillWidth: true; Layout.preferredWidth: 10
                                    implicitHeight: 96
                                    radius: Theme.radius
                                    color: on ? Theme.brand : Theme.surface2
                                    border.color: Theme.rule
                                    opacity: page.slot >= 0 && page.slot !== index ? 0.45 : 1
                                    ColumnLayout {
                                        anchors.centerIn: parent; spacing: 2
                                        Label { text: (page.info.slotNames || [])[index + 1] || ""; color: parent.parent.on ? Theme.brandInk : Theme.muted
                                                font.pixelSize: Theme.fsS; Layout.alignment: Qt.AlignHCenter }
                                        Label { text: parent.parent.dose || "—"; color: parent.parent.on ? Theme.brandInk : Theme.muted
                                                font.pixelSize: parent.parent.dose ? Theme.fsXl : Theme.fsL; font.bold: !!parent.parent.dose
                                                Layout.alignment: Qt.AlignHCenter }
                                    }
                                }
                            }
                        }
                        // Wochendosierer: eine Zeile je Tag
                        GridLayout {
                            objectName: "prepWeekGrid"
                            visible: page.days > 1
                            Layout.fillWidth: true
                            columns: 5; columnSpacing: 4; rowSpacing: 4
                            Label { text: ""; Layout.preferredWidth: 110 }
                            Repeater {
                                model: 4
                                Label { text: (page.info.slotNames || [])[index + 1] || ""; color: Theme.muted; font.pixelSize: Theme.fsS
                                        horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true; Layout.preferredWidth: 10 }
                            }
                            Repeater {
                                model: page.days > 1 ? (page.current.week || []).length * 5 : 0
                                delegate: Rectangle {
                                    readonly property int row: Math.floor(index / 5)
                                    readonly property int c: index % 5
                                    readonly property var wd: (page.current.week || [])[row] || ({})
                                    readonly property string dose: c > 0 ? ((wd.doses || [])[c - 1] || "") : ""
                                    readonly property bool on: c > 0 && dose !== "" && (page.slot < 0 || page.slot === c - 1)
                                    Layout.fillWidth: c > 0; Layout.preferredWidth: c > 0 ? 10 : 110
                                    implicitHeight: 44
                                    radius: 6
                                    color: c === 0 ? "transparent" : on ? Theme.brand : Theme.surface2
                                    Label { anchors.fill: parent; anchors.leftMargin: 4; anchors.rightMargin: 4
                                            horizontalAlignment: c === 0 ? Text.AlignLeft : Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                            text: c === 0 ? (parent.wd.day || "") : (parent.dose || "—")
                                            font.bold: c === 0 || parent.dose !== ""; font.pixelSize: c === 0 ? Theme.fsM : Theme.fsL
                                            color: parent.on ? Theme.brandInk : c === 0 ? Theme.ink : Theme.muted }
                                }
                            }
                        }
                        Label { visible: !!page.current.total; text: page.current.total || ""; font.bold: true; color: Theme.ink }
                        Repeater {
                            model: page.current.hints || []
                            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; textFormat: Text.StyledText; font.pixelSize: Theme.fsM
                                    color: modelData.warn ? Theme.warn : Theme.ink2
                                    text: "<b>" + modelData.label + "</b> " + modelData.text.replace(/&/g, "&amp;").replace(/</g, "&lt;") }
                        }
                    }

                    // fertig
                    ColumnLayout {
                        visible: page.finished
                        Layout.fillWidth: true
                        spacing: Theme.gapS
                        Label { text: "✔"; font.pixelSize: 72; color: Theme.ok; Layout.alignment: Qt.AlignHCenter }
                        Label { objectName: "prepDoneLabel"; text: qsTr("Medikamente sind gerichtet"); font.pixelSize: Theme.fsXl; font.bold: true; color: Theme.ok
                                Layout.alignment: Qt.AlignHCenter }
                        Label { text: qsTr("%1 Medikament(e) für %2").arg(page.items.length).arg(page.summary.name || "")
                                        + (page.days > 1 ? "\n" + qsTr("Wochendosierer %1").arg(page.session.range || "") : "")
                                color: Theme.ink2; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Label { text: page.logged; color: Theme.muted; font.pixelSize: Theme.fsS; Layout.fillWidth: true; Layout.minimumWidth: 0
                        horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
                        VField { id: note; objectName: "prepNoteField"; label: qsTr("Bemerkung"); placeholder: qsTr("optional, z. B. „Wochendosierer KW 43“")
                                 Layout.fillWidth: true; field.onEditingFinished: page.saveNote() }
                        Rectangle {
                            objectName: "prepWarnings"
                            visible: page.warnings.length > 0
                            Layout.fillWidth: true
                            implicitHeight: warnCol.implicitHeight + 24
                            radius: Theme.radius
                            color: Theme.warnWash; border.color: Theme.warn
                            ColumnLayout {
                                id: warnCol
                                x: 14; y: 12; width: parent.width - 28
                                spacing: Theme.gapS
                                Repeater {
                                    model: page.warnings
                                    Label { text: modelData; color: Theme.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                }
                                VButton { text: qsTr("Bestand und Bestellungen"); onClicked: { page.saveNote(); page.openStock() } }
                            }
                        }
                    }
                }
            }
        }
    }

    // Protokoll: wann, von wem, was gestellt wurde
    Popup {
        id: logPopup
        modal: true
        anchors.centerIn: Overlay.overlay
        width: Math.min(820, page.width - 32); height: Math.min(680, page.height - 32)
        padding: 20
        property var entries: []
        onAboutToShow: entries = app.preparationLog(page.personId, 90)
        background: Rectangle { radius: Theme.radius + 4; color: Theme.surface; border.color: Theme.rule }
        Overlay.modal: Rectangle { color: "#80000000" }
        contentItem: ColumnLayout {
            spacing: Theme.gapS
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("Protokoll – letzte 90 Tage"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true }
                VButton { text: qsTr("Als PDF"); onClicked: { const f = app.exportReport("preplog", page.personId, { days: 90 }); if (f) app.openFile(f); else win.toast(app.lastError, true) } }
                VButton { text: qsTr("Schließen"); kind: "flat"; onClicked: logPopup.close() }
            }
            Label { visible: logPopup.entries.length === 0; text: qsTr("Noch keine Einträge."); color: Theme.muted }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true; spacing: 6
                model: logPopup.entries
                delegate: Rectangle {
                    width: ListView.view.width
                    implicitHeight: entry.implicitHeight + 20
                    radius: 8; color: Theme.surface2; border.color: Theme.rule
                    ColumnLayout {
                        id: entry
                        x: 12; y: 10; width: parent.width - 24; spacing: 2
                        Label { text: modelData.when + " · " + modelData.by; font.bold: true; color: Theme.ink; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Label { text: qsTr("für %1").arg(modelData.forText) + " – " + modelData.status
                                color: modelData.complete ? Theme.ok : Theme.warn; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Label { text: modelData.items.join("\n"); color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        Label { visible: !!modelData.note; text: modelData.note; color: Theme.muted; font.pixelSize: Theme.fsS; font.italic: true
                                Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
    }
}
