// Medikament anlegen oder bearbeiten
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    property var personId: 0
    property var medId: 0
    signal closed(string message)
    property var m: app.medication(personId, medId)
    background: Rectangle { color: Theme.paper }

    // Erinnerung an die Einnahme: nur Regelmedikation und Sonstiges; Tage nach Rhythmus oder frei wählbar
    readonly property bool canRemind: cat.currentIndex === 0 || cat.currentIndex === 3
    readonly property bool byRhythm: cat.currentIndex === 0 && rhythm.currentIndex > 0
    readonly property var rhythmNames: [qsTr("täglich"), qsTr("alle … Tage"), qsTr("an bestimmten Wochentagen"), qsTr("monatlich")]
    // Vorschau: Rhythmus in Worten, nächste Einnahmetage, Tagesmenge, automatische Kennzeichnung
    readonly property var preview: app.medicationPreview(page.basics())
    function basics() {
        return {
            name: name.text, substance: substance.text, strength: strength.text, form: form.text,
            morning: morning.text, noon: noon.text, evening: evening.text, night: night.text, unit: unit.text,
            category: cat.currentIndex, reason: reason.text, notes: notes.text, rhythmNote: rhythmNote.text,
            startDate: start.isoDate, endDate: end.isoDate,
            rhythmChoice: rhythm.currentIndex, rhythmInterval: interval.value, rhythmStart: rhythmStart.isoDate,
            rhythmWeekdays: page.wdState
        }
    }
    property var wdState: (page.m.rhythmWeekdays || []).map(d => d.on)
    property bool slotsTouched: page.m.remindSlotsStored === true
    readonly property var slotNames: [qsTr("morgens"), qsTr("mittags"), qsTr("abends"), qsTr("zur Nacht")]
    function hasDose(t) { t = (t || "").trim(); return t !== "" && t !== "0" && t !== "-" }
    function doseSlot(i) {
        const d = [morning.text, noon.text, evening.text, night.text]
        const any = d.some(hasDose)
        return any ? hasDose(d[i]) : i === 0
    }
    function freezeSlots() {           // ab jetzt wählt der Anwender die Tageszeiten selbst
        for (let i = 0; i < slotRep.count; ++i) { const it = slotRep.itemAt(i); it.on = it.on }
        page.slotsTouched = true
    }

    function save(force) {
        if (!force && app.pznStatus(pzn.text) < 0) { confirmPzn.open(); return }
        const v = page.basics()
        Object.assign(v, { id: page.medId, pzn: pzn.text, archived: archived.checked, stopReason: stopReason.text,
                           remind: remind.checked, remindDays: [], remindTimes: [], classes: [] })
        for (let c = 0; c < classRep.count; ++c) v.classes.push(classRep.itemAt(c).checked)
        for (let i = 0; i < dayRep.count; ++i) v.remindDays.push(dayRep.itemAt(i).checked)
        for (let j = 0; j < slotRep.count; ++j) if (slotRep.itemAt(j).on) v.remindTimes.push(slotRep.itemAt(j).time)
        if (!start.valid || !end.valid) { win.toast(qsTr("Bitte ein gültiges Datum eingeben (TT.MM.JJJJ)."), true); return }
        if (app.saveMedication(page.personId, v)) page.closed(qsTr("Medikament gespeichert."))
        else win.toast(app.lastError, true)
    }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { text: win.phone ? "✕" : qsTr("Abbrechen"); Accessible.name: qsTr("Abbrechen"); kind: "flat"; font.pixelSize: win.phone ? Theme.fsL + 6 : Theme.fsM; leftPadding: win.phone ? 8 : 18; rightPadding: win.phone ? 8 : 18; onClicked: page.closed("") }
            Label { text: page.medId ? qsTr("Medikament bearbeiten") : qsTr("Neues Medikament"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink
                    Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
            VButton { visible: page.medId; text: qsTr("Löschen"); kind: "flat"; onClicked: confirmDelete.open() }
            VButton { text: qsTr("Speichern"); kind: "primary"; onClicked: page.save(false) }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 300
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: Math.min(1000, page.width - 2 * Theme.gapL)
            spacing: Theme.gap
            VCombo { id: cat; objectName: "medCategory"; label: qsTr("Art"); model: app.medCategories(); currentIndex: page.m.category || 0; Layout.fillWidth: true }
            Label { text: app.medCategoryHint(cat.currentIndex); color: Theme.muted; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VField { id: name; label: qsTr("Präparat (Handelsname) *"); text: page.m.name; Layout.fillWidth: true }
                VField { id: substance; label: qsTr("Wirkstoff"); text: page.m.substance; Layout.fillWidth: true }
                VField { id: strength; label: qsTr("Stärke"); text: page.m.strength; placeholder: qsTr("z. B. 5 mg"); Layout.fillWidth: true }
                VField { id: form; label: qsTr("Darreichungsform"); text: page.m.form; placeholder: qsTr("z. B. Tablette"); Layout.fillWidth: true }
                VField { id: pzn; objectName: "pznField"; label: qsTr("PZN (Pharmazentralnummer)"); text: page.m.pzn || ""; Layout.fillWidth: true
                         inputMethodHints: Qt.ImhPreferNumbers; placeholder: qsTr("8 Ziffern, auf der Packung")
                         hint: app.pznStatus(text) < 0 ? qsTr("Ungültige PZN (8 Ziffern mit Prüfziffer).") : "" }
            }
            SectionTitle { text: qsTr("Dosierung"); visible: cat.currentIndex !== 3 }
            RowLayout {
                visible: cat.currentIndex !== 3
                Layout.fillWidth: true
                spacing: Theme.gapS
                VField { id: morning; label: qsTr("morgens"); text: page.m.morning; Layout.fillWidth: true; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                VField { id: noon; label: qsTr("mittags"); text: page.m.noon; Layout.fillWidth: true; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                VField { id: evening; label: qsTr("abends"); text: page.m.evening; Layout.fillWidth: true; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                VField { id: night; label: qsTr("zur Nacht"); text: page.m.night; Layout.fillWidth: true; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                VField { id: unit; label: qsTr("Einheit"); text: page.m.unit; Layout.preferredWidth: 140 }
            }
            Label { visible: cat.currentIndex === 1 || cat.currentIndex === 2; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: Theme.fsS
                    text: cat.currentIndex === 1 ? qsTr("Bedarfsmedikation: Dosierung als Einzel- bzw. Höchstmenge eintragen, Anlass und Mindestabstand unten bei „Anlass / Anwendung“.")
                                                 : qsTr("Notfallmedikation: erscheint hervorgehoben in der Übersicht und im Medikamentenplan. Anlass und Anwendung unten eintragen, z. B. „bei Brustschmerz 1–2 Hübe unter die Zunge, nach 10 Minuten ohne Besserung Notruf 112“.") }
            Label { visible: !!page.preview.amount && cat.currentIndex !== 3; text: qsTr("Menge: %1").arg(page.preview.amount || ""); color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }

            // Einnahme-Rhythmus (nur Regelmedikation)
            SectionTitle { text: qsTr("Einnahme-Rhythmus"); visible: cat.currentIndex === 0 }
            GridLayout {
                visible: cat.currentIndex === 0
                Layout.fillWidth: true
                columns: page.width > 800 ? 3 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gapS
                VCombo { id: rhythm; objectName: "rhythmCombo"; label: qsTr("Einnahme"); model: page.rhythmNames; currentIndex: page.m.rhythmChoice || 0; Layout.fillWidth: true }
                VStepper {
                    id: interval
                    visible: rhythm.currentIndex > 0
                    label: qsTr("Abstand")
                    from: rhythm.currentIndex === 1 ? 2 : 1; to: 99
                    value: Math.max(rhythm.currentIndex === 1 ? 2 : 1, page.m.rhythmInterval || 1)
                    text: rhythm.currentIndex === 1 ? qsTr("alle %1 Tage").arg(value)
                        : rhythm.currentIndex === 2 ? (value === 1 ? qsTr("jede Woche") : qsTr("alle %1 Wochen").arg(value))
                        : (value === 1 ? qsTr("jeden Monat") : qsTr("alle %1 Monate").arg(value))
                }
                DateField { id: rhythmStart; visible: rhythm.currentIndex > 0; label: qsTr("ab (erste Einnahme)"); allowEmpty: false
                            isoDate: page.m.rhythmStart; Layout.fillWidth: true }
            }
            Flow {
                visible: cat.currentIndex === 0 && rhythm.currentIndex === 2
                Layout.fillWidth: true
                spacing: Theme.gapS
                Repeater { id: wdRep; model: page.m.rhythmWeekdays
                    VChip { text: modelData.name; checked: modelData.on; autoExclusive: false
                            onClicked: { const w = page.wdState.slice(); w[index] = checked; page.wdState = w } } }
            }
            Label {
                visible: cat.currentIndex === 0 && rhythm.currentIndex > 0
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2; font.pixelSize: Theme.fsS
                text: qsTr("%1 – nächste Einnahmetage: %2").arg(page.preview.rhythmText || "").arg(page.preview.nextDays || qsTr("keine"))
                      + "\n" + qsTr("„Medikamente stellen“ nennt das Medikament nur an diesen Tagen.")
            }
            SectionTitle { text: qsTr("Erinnerung"); visible: page.canRemind }
            VCheck { id: remind; objectName: "remindCheck"; visible: page.canRemind; checked: page.m.remind === true
                       text: qsTr("An Einnahme erinnern (in die Terminliste übernehmen)"); font.pixelSize: Theme.fsM }
            ColumnLayout {
                visible: page.canRemind && remind.checked
                Layout.fillWidth: true
                spacing: Theme.gapS
                Label { text: page.byRhythm ? qsTr("an den Einnahmetagen laut Rhythmus (%1)").arg(page.preview.rhythmText || "") : qsTr("Erinnern an")
                        color: Theme.ink2; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                Flow {
                    visible: !page.byRhythm
                    Layout.fillWidth: true
                    spacing: Theme.gapS
                    Repeater { id: dayRep; model: page.m.remindDays
                        VChip { text: modelData.name; checked: modelData.on; autoExclusive: false } }
                }
                Label { text: qsTr("um"); color: Theme.ink2; font.pixelSize: Theme.fsS }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.gap
                    Repeater {
                        id: slotRep
                        model: 4
                        RowLayout {
                            property alias on: chip.checked
                            property alias time: tf.text
                            spacing: 6
                            VChip { id: chip; text: page.slotNames[index]; autoExclusive: false
                                    checked: page.slotsTouched ? page.m.remindSlots[index].on : page.doseSlot(index)
                                    onClicked: page.freezeSlots() }
                            VField { id: tf; text: page.m.remindSlots[index].time; enabled: chip.checked; Layout.preferredWidth: 110
                                     inputMethodHints: Qt.ImhPreferNumbers; placeholder: "08:00" }
                        }
                    }
                }
                Label { text: qsTr("Die Erinnerungen erscheinen in der Terminliste und werden von VitaParms am Computer angezeigt.")
                        color: Theme.muted; font.pixelSize: Theme.fsXs; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            VField { id: rhythmNote; label: cat.currentIndex === 1 || cat.currentIndex === 2 ? qsTr("Anlass / Anwendung") : qsTr("Bemerkung zur Einnahme")
                     text: page.m.rhythmNote; Layout.fillWidth: true }
            GridLayout {
                Layout.fillWidth: true
                columns: page.width > 800 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VField { id: reason; label: qsTr("Grund (Indikation)"); text: page.m.reason; Layout.fillWidth: true }
                VField { id: notes; label: qsTr("Hinweise"); text: page.m.notes; Layout.fillWidth: true }
                DateField { id: start; label: qsTr("Einnahme von"); isoDate: page.m.startDate; Layout.fillWidth: true }
                DateField { id: end; label: qsTr("bis (leer = dauerhaft)"); isoDate: page.m.endDate; Layout.fillWidth: true }
            }
            SectionTitle { text: qsTr("Kennzeichnung") }
            Label { text: qsTr("Wird aus Wirkstoff und Name vorgeschlagen; ein Haken legt die Kennzeichnung fest."); color: Theme.muted; font.pixelSize: Theme.fsS; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Repeater {
                id: classRep
                model: page.m.classes
                delegate: VCheck {
                    readonly property var pv: (page.preview.classes || [])[index] || ({})
                    readonly property bool overridden: modelData.on !== modelData.auto
                    Layout.fillWidth: true
                    font.pixelSize: Theme.fsM
                    checked: overridden ? modelData.on : !!pv.auto
                    text: modelData.name + (pv.auto && pv.reason ? "  (" + (pv.uncertain ? qsTr("ggf. ") : "") + pv.reason + ")" : "")
                }
            }
            VCheck { id: archived; objectName: "archivedCheck"; text: qsTr("Abgesetzt (nicht mehr eingenommen, ins Archiv)"); checked: page.m.archived === true; font.pixelSize: Theme.fsM }
            VField { id: stopReason; visible: archived.checked; label: qsTr("Grund des Absetzens"); text: page.m.stopReason || ""
                     placeholder: qsTr("z. B. Nebenwirkung, Therapie beendet, ersetzt durch …"); Layout.fillWidth: true
                     hint: !end.isoDate ? qsTr("Als Ende wird heute eingetragen.") : "" }
        }
    }
    Confirm {
        id: confirmPzn
        title: qsTr("PZN ungültig")
        message: qsTr("Die PZN „%1“ ist ungültig (8 Ziffern mit Prüfziffer). Trotzdem übernehmen?").arg(pzn.text)
        acceptText: qsTr("Übernehmen")
        onAccepted: page.save(true)
    }
    Confirm {
        id: confirmDelete
        title: qsTr("Medikament löschen?")
        message: qsTr("Statt zu löschen können Sie das Medikament auch als „abgesetzt“ archivieren – dann bleibt es im Verlauf sichtbar.")
        acceptText: qsTr("Löschen"); danger: true
        onAccepted: { if (app.deleteMedication(page.personId, page.medId)) page.closed(qsTr("Medikament gelöscht.")); else win.toast(app.lastError, true) }
    }
}
