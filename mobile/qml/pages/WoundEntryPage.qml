// Wundbeurteilung bzw. Verbandwechsel erfassen oder bearbeiten (mit Foto aus der Kamera)
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "woundEntryPage"
    property var woundId: 0
    property var entryId: 0
    property int startKind: 0                  // 0 Wundbeurteilung, 1 Verbandwechsel
    signal closed(string message)
    readonly property var c: app.woundChoices()
    property var e: app.woundEntryData(woundId, entryId, startKind)
    property int photoRev: e.photoRevision || 0
    property bool hasPhoto: e.hasPhoto === true
    readonly property bool full: kind.currentIndex === 0
    readonly property var gradeList: c.grades[e.woundKind] || []
    readonly property int groundSum: (gran.value + epi.value + fib.value + nec.value) * 5   // Stufen à 5 %
    readonly property real area: {
        const l = parseFloat(len.text.replace(",", ".")), w = parseFloat(wid.text.replace(",", "."))
        return l > 0 && w > 0 ? Math.round(l * w * 10) / 10 : 0
    }
    property string pendingCapture: ""
    property int captureTries: 0
    background: Rectangle { color: Theme.paper }

    function capture() {
        const p = app.startCapture()
        if (!p) { win.toast(app.lastError, true); return }
        pendingCapture = p
        captureTries = 0
    }
    function checkCapture() {
        if (pendingCapture === "") return
        const st = app.captureState(pendingCapture)
        if (st === "pending") { if (++captureTries < 20) captureTimer.restart(); return }
        const p = pendingCapture
        pendingCapture = ""
        if (st === "ok") takePhoto(p)
        else if (st === "error") win.toast(app.lastError, true)
    }
    function takePhoto(pathOrUrl) {
        const r = app.woundPhotoFrom(pathOrUrl)
        if (r) { photoRev = r; hasPhoto = true } else win.toast(app.lastError, true)
    }
    Timer { id: captureTimer; interval: 400; onTriggered: page.checkCapture() }
    Connections {
        target: Qt.application
        function onStateChanged() { if (Qt.application.state === Qt.ApplicationActive && page.pendingCapture !== "") captureTimer.restart() }
    }

    function save() {
        if (!date.valid || !next.valid) { win.toast(qsTr("Bitte ein gültiges Datum eingeben (TT.MM.JJJJ)."), true); return }
        if (page.full && page.groundSum > 100) { win.toast(qsTr("Der Wundgrund ergibt zusammen %1 % – höchstens 100 % sind möglich.").arg(page.groundSum), true); return }
        const v = {
            id: page.entryId, kind: kind.currentIndex, date: date.isoDate, time: time.text,
            length: len.text, width: wid.text, depth: dep.text, pockets: pockets.text,
            granulation: gran.value * 5, epithel: epi.value * 5, fibrin: fib.value * 5, necrosis: nec.value * 5,
            exposed: exposed.values, exudate: exudate.currentIndex - 1, exudateKind: exudateKind.text, odor: odor.currentIndex - 1,
            edge: edge.values, surroundings: surround.values, infection: infection.values,
            pain: pain.currentIndex > 0 ? pain.currentIndex - 1 : "", painChange: painChange.currentIndex > 0 ? painChange.currentIndex - 1 : "",
            grade: grade.currentIndex - 1, phase: phase.currentIndex - 1, trend: trend.currentIndex - 1,
            description: description.text, cleansing: cleansing.text, dressing: dressing.text, fixation: fixation.text,
            compression: compression.text, intervalDays: interval.value, nextChange: next.isoDate, notes: notes.text
        }
        if (app.saveWoundEntry(page.woundId, v))
            page.closed(page.full ? qsTr("Wundbeurteilung gespeichert.") : qsTr("Verbandwechsel gespeichert."))
        else win.toast(app.lastError, true)
    }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { text: qsTr("Abbrechen"); kind: "flat"; onClicked: page.closed("") }
            Label { text: (page.entryId ? qsTr("Eintrag bearbeiten") : page.full ? qsTr("Wundbeurteilung") : qsTr("Verbandwechsel"))
                    font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
            VButton { visible: page.entryId; text: qsTr("Löschen"); kind: "flat"; onClicked: confirmDelete.open() }
            VButton { objectName: "saveEntryButton"; text: qsTr("Speichern"); kind: "primary"; onClicked: page.save() }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: col.implicitHeight + 300
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: Math.min(1000, page.width - 2 * Theme.gapL)
            spacing: Theme.gap
            Label { text: page.e.woundTitle || ""; font.bold: true; color: Theme.ink; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 3 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VCombo { id: kind; objectName: "entryKind"; label: qsTr("Art"); model: page.c.entryKinds; currentIndex: page.e.kind; Layout.fillWidth: true }
                DateField { id: date; label: qsTr("Datum"); isoDate: page.e.date; allowEmpty: false; Layout.fillWidth: true }
                VField { id: time; label: qsTr("Uhrzeit"); text: page.e.time; inputMethodHints: Qt.ImhPreferNumbers; Layout.fillWidth: true }
            }

            // ---------------- Wundbeurteilung
            SectionTitle { visible: page.full; text: qsTr("Größe") }
            RowLayout {
                visible: page.full
                Layout.fillWidth: true
                spacing: Theme.gapS
                VField { id: len; objectName: "entryLength"; label: qsTr("Länge (cm)"); text: page.e.length; inputMethodHints: Qt.ImhFormattedNumbersOnly; Layout.fillWidth: true }
                VField { id: wid; objectName: "entryWidth"; label: qsTr("Breite (cm)"); text: page.e.width; inputMethodHints: Qt.ImhFormattedNumbersOnly; Layout.fillWidth: true }
                VField { id: dep; label: qsTr("Tiefe (cm)"); text: page.e.depth; inputMethodHints: Qt.ImhFormattedNumbersOnly; Layout.fillWidth: true }
                Label { text: page.area > 0 ? qsTr("Fläche %1 cm²").arg(page.area.toLocaleString(Qt.locale(), "f", 1)) : ""
                        color: Theme.ink2; Layout.preferredWidth: 160; Layout.alignment: Qt.AlignBottom; Layout.bottomMargin: 12 }
            }
            VField { id: pockets; visible: page.full; label: qsTr("Taschen, Unterminierung, Fisteln"); text: page.e.pockets
                     placeholder: qsTr("Uhrmethode (Kopf = 12 Uhr), z. B. Tasche 1,5 cm bei 3 Uhr"); Layout.fillWidth: true }
            SectionTitle { visible: page.full; text: qsTr("Wundgrund (%1 %)").arg(page.groundSum); color: page.groundSum > 100 ? Theme.alert : Theme.ink }
            GridLayout {
                visible: page.full
                Layout.fillWidth: true
                columns: page.width > 800 ? 4 : 2
                columnSpacing: Theme.gap; rowSpacing: Theme.gapS
                VStepper { id: gran; objectName: "entryGranulation"; label: qsTr("Granulation"); from: 0; to: 20; value: Math.round(page.e.granulation / 5); text: (value * 5) + " %" }
                VStepper { id: epi; label: qsTr("Epithel"); from: 0; to: 20; value: Math.round(page.e.epithel / 5); text: (value * 5) + " %" }
                VStepper { id: fib; label: qsTr("Fibrin/Beläge"); from: 0; to: 20; value: Math.round(page.e.fibrin / 5); text: (value * 5) + " %" }
                VStepper { id: nec; label: qsTr("Nekrose"); from: 0; to: 20; value: Math.round(page.e.necrosis / 5); text: (value * 5) + " %" }
            }
            ChoiceFlow { id: exposed; visible: page.full; label: qsTr("Freiliegend"); choices: page.c.exposed; values: page.e.exposed; Layout.fillWidth: true }
            GridLayout {
                visible: page.full
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VCombo { id: exudate; label: qsTr("Exsudat (Menge)"); model: ["–"].concat(page.c.exudate); currentIndex: page.e.exudate + 1; Layout.fillWidth: true }
                VCombo { id: odor; label: qsTr("Geruch"); model: ["–"].concat(page.c.odors); currentIndex: page.e.odor + 1; Layout.fillWidth: true }
            }
            SuggestField { id: exudateKind; visible: page.full; label: qsTr("Exsudat (Art)"); text: page.e.exudateKind; suggestions: page.c.exudateKinds; Layout.fillWidth: true }
            ChoiceFlow { id: edge; visible: page.full; label: qsTr("Wundrand"); choices: page.c.edges; values: page.e.edge; Layout.fillWidth: true }
            ChoiceFlow { id: surround; visible: page.full; label: qsTr("Wundumgebung"); choices: page.c.surroundings; values: page.e.surroundings; Layout.fillWidth: true }
            ChoiceFlow { id: infection; objectName: "entryInfection"; visible: page.full; label: qsTr("Infektionszeichen"); choices: page.c.infection; values: page.e.infection; Layout.fillWidth: true }
            GridLayout {
                visible: page.full
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VCombo { id: pain; label: qsTr("Schmerz in Ruhe (NRS 0–10)"); model: ["–", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"]
                         currentIndex: page.e.pain === "" ? 0 : parseInt(page.e.pain) + 1; Layout.fillWidth: true }
                VCombo { id: painChange; label: qsTr("Schmerz beim Verbandwechsel"); model: ["–", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10"]
                         currentIndex: page.e.painChange === "" ? 0 : parseInt(page.e.painChange) + 1; Layout.fillWidth: true }
                VCombo { id: grade; label: page.c.gradeSystems[page.e.woundKind] || qsTr("Klassifikation"); enabled: page.gradeList.length > 0
                         model: ["–"].concat(page.gradeList); currentIndex: page.e.grade + 1; Layout.fillWidth: true }
                VCombo { id: phase; label: qsTr("Heilungsphase"); model: ["–"].concat(page.c.phases); currentIndex: page.e.phase + 1; Layout.fillWidth: true }
            }

            // ---------------- Verlauf und Versorgung (immer)
            SectionTitle { text: qsTr("Verlauf und Versorgung") }
            VField { id: description; objectName: "entryDescription"; label: qsTr("Beschreibung"); text: page.e.description
                     placeholder: qsTr("kurz, z. B. Wunde sauber, Rand rosig"); Layout.fillWidth: true }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Label { text: qsTr("Tendenz"); font.pixelSize: Theme.fsS; color: Theme.ink2 }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.gapS
                    Repeater {
                        id: trendRep
                        model: page.c.trends
                        VChip { objectName: "trend_" + index; text: modelData; checkable: false; checked: trend.currentIndex === index + 1
                                dotColor: index === 0 ? Theme.ok : index === 2 ? Theme.alert : Theme.muted
                                onClicked: trend.currentIndex = checked ? 0 : index + 1 }
                    }
                }
                ComboBox { id: trend; visible: false; model: ["–"].concat(page.c.trends); currentIndex: page.e.trend + 1 }
            }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VField { id: cleansing; label: qsTr("Wundreinigung"); text: page.e.cleansing; placeholder: qsTr("z. B. Wundspüllösung, Débridement"); Layout.fillWidth: true }
                VField { id: dressing; objectName: "entryDressing"; label: qsTr("Wundauflage"); text: page.e.dressing; placeholder: qsTr("Wundauflage(n), Produkt"); Layout.fillWidth: true }
                VField { id: fixation; label: qsTr("Fixierung/Sekundärverband"); text: page.e.fixation; Layout.fillWidth: true }
                VField { id: compression; label: qsTr("Kompression/Entlastung"); text: page.e.compression; placeholder: qsTr("Kompression, Druckentlastung, Lagerung"); Layout.fillWidth: true }
                VStepper { id: interval; label: qsTr("Verbandwechsel"); from: 0; to: 30; value: page.e.intervalDays
                           text: value === 0 ? "–" : value === 1 ? qsTr("täglich") : qsTr("alle %1 Tage").arg(value)
                           onValueChanged: if (value > 0) next.isoDate = page.addDays(date.isoDate, value) }
                DateField { id: next; label: qsTr("Nächster Verbandwechsel"); isoDate: page.e.nextChange; Layout.fillWidth: true }
            }
            VArea { id: notes; label: qsTr("Bemerkung"); text: page.e.notes; minHeight: 80; Layout.fillWidth: true }

            // ---------------- Foto
            SectionTitle { text: qsTr("Foto") }
            Label { visible: page.e.photoConsent !== true; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.warn
                    text: qsTr("Fotos erst, wenn die Einwilligung zur Fotodokumentation vorliegt (Wunde bearbeiten).") }
            RowLayout {
                visible: page.e.photoConsent === true
                Layout.fillWidth: true
                spacing: Theme.gap
                Rectangle {
                    Layout.preferredWidth: 280; Layout.preferredHeight: 210
                    color: Theme.surface2; radius: 6; border.color: Theme.rule
                    Image { objectName: "entryPhoto"; anchors.fill: parent; anchors.margins: 4; fillMode: Image.PreserveAspectFit; cache: false
                            visible: page.hasPhoto; sourceSize.width: 560
                            source: page.hasPhoto ? "image://wound/draft?r=" + page.photoRev : "" }
                    Label { anchors.centerIn: parent; visible: !page.hasPhoto; text: qsTr("kein Foto"); color: Theme.muted }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Theme.gapS
                    VButton { text: page.hasPhoto ? qsTr("Neues Foto aufnehmen") : qsTr("Foto aufnehmen"); iconText: "+"; onClicked: page.capture() }
                    VButton { text: qsTr("Bild wählen …"); kind: "flat"; onClicked: fileDlg.open() }
                    VButton { visible: page.hasPhoto; text: qsTr("Foto entfernen"); kind: "flat"; onClicked: { page.photoRev = app.woundPhotoClear(); page.hasPhoto = false } }
                    Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: Theme.fsXs
                            text: qsTr("Das Foto wird verkleinert in der Datenbank gespeichert. Ein Maßband im Bild erleichtert den Vergleich.") }
                }
            }
            Label { visible: !!page.e.by; text: qsTr("Handzeichen: %1").arg(page.e.by || ""); color: Theme.muted; font.pixelSize: Theme.fsXs }
        }
    }
    function addDays(iso, n) {
        const p = (iso || "").split("-")
        const d = p.length === 3 ? new Date(parseInt(p[0]), parseInt(p[1]) - 1, parseInt(p[2])) : new Date()
        d.setDate(d.getDate() + n)
        const pad = (x) => (x < 10 ? "0" : "") + x
        return d.getFullYear() + "-" + pad(d.getMonth() + 1) + "-" + pad(d.getDate())
    }
    FileDialog {
        id: fileDlg
        title: qsTr("Wundfoto wählen")
        nameFilters: [qsTr("Bilder (*.jpg *.jpeg *.png *.heic *.webp)")]
        onAccepted: page.takePhoto(selectedFile.toString())
    }
    Confirm {
        id: confirmDelete
        title: qsTr("Eintrag löschen?")
        message: qsTr("Der Eintrag wird aus der Chronik gelöscht.")
        acceptText: qsTr("Löschen"); danger: true
        onAccepted: { if (app.deleteWoundEntry(page.entryId)) page.closed(qsTr("Eintrag gelöscht.")); else win.toast(app.lastError, true) }
    }
}
