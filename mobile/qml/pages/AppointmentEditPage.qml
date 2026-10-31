// Termin anlegen oder bearbeiten – mit Wiederholung (Serien), Priorität und Erinnerung
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "appointmentEditPage"
    property var apptId: 0
    property var personId: 0
    signal closed(string message)
    property var rec: app.appointment(apptId, personId)
    property var rule: rec.rule || ({})
    property var persons: app.apptPersons()
    property var owners: app.apptOwners()
    property var leads: app.apptLeadTimes()
    property var priorities: app.apptPriorities()
    property var repeats: app.apptRepeatChoices()
    property int priority: rec.priority !== undefined ? rec.priority : 1
    property int weekdays: rule.weekdays || 0
    property string prevStart: rec.startDate || ""
    background: Rectangle { color: Theme.paper }

    readonly property int repeatChoice: repeatBox.currentIndex >= 0 ? repeats[repeatBox.currentIndex].value : 0
    readonly property bool repeating: repeatChoice !== 0
    readonly property bool weekly: repeatChoice === 3 || repeatChoice === 6
    readonly property int endChoice: endBox.currentIndex
    property var monthModes: app.apptMonthModes(startDate.isoDate)

    function collectRule() {
        return { repeat: repeatChoice, interval: intervalStep.value, weekdays: weekdays,
                 monthMode: monthBox.currentIndex >= 0 && monthModes.length ? monthModes[monthBox.currentIndex].value : 0,
                 endMode: endChoice, until: untilDate.isoDate, count: countStep.value }
    }
    function collect() {
        return { id: page.apptId, title: titleField.text, location: locField.text, notes: notesArea.text,
                 priority: page.priority, allDay: allDay.checked,
                 startDate: startDate.isoDate, startTime: startTime.text, endDate: endDate.isoDate, endTime: endTime.text,
                 patientId: personBox.currentIndex >= 0 ? persons[personBox.currentIndex].id : 0,
                 owner: ownerBox.currentIndex >= 0 ? owners[ownerBox.currentIndex] : "",
                 remindMinutes: leadBox.currentIndex >= 0 ? leads[leadBox.currentIndex].value : -1,
                 remindVisual: remindVisual.checked, remindSound: remindSound.checked,
                 done: doneCheck.checked, rule: collectRule() }
    }
    property string pendingScope: ""
    function save() {
        if (titleField.text.trim() === "") { win.toast(qsTr("Bitte einen Titel angeben."), true); titleField.field.forceActiveFocus(); return }
        if (rec.isSeries) {
            scopeDlg.deleting = false
            scopeDlg.allowOnlyThis = !app.apptRuleChanged(collect())
            scopeDlg.open()
            return
        }
        doSave("")
    }
    function doSave(scope) {
        if (app.saveAppointment(collect(), scope)) page.closed(qsTr("Termin gespeichert."))
        else win.toast(app.lastError, true)
    }
    function remove() {
        if (rec.isSeries) { scopeDlg.deleting = true; scopeDlg.allowOnlyThis = true; scopeDlg.open() }
        else confirmDelete.open()
    }
    function doDelete(scope) {
        if (app.deleteAppointment(page.apptId, scope)) page.closed(qsTr("Termin gelöscht."))
        else win.toast(app.lastError, true)
    }
    // Wöchentlich mit genau einem Wochentag: dieser wandert mit dem Startdatum mit
    function onStartChanged() {
        const iso = startDate.isoDate
        if (!iso || iso === prevStart) return
        if (!rec.isSeries && prevStart) {
            const oldBit = 1 << (new Date(prevStart + "T12:00:00").getDay() + 6) % 7
            const newBit = 1 << (new Date(iso + "T12:00:00").getDay() + 6) % 7
            if (weekdays === oldBit || weekdays === 0) weekdays = newBit
            if (endDate.isoDate && endDate.isoDate < iso) endDate.isoDate = iso
        }
        prevStart = iso
    }

    Component.onCompleted: {
        titleField.text = rec.title || ""
        locField.text = rec.location || ""
        notesArea.text = rec.notes || ""
        allDay.checked = rec.allDay === true
        startDate.isoDate = rec.startDate || ""
        startTime.text = rec.startTime || ""
        endDate.isoDate = rec.endDate || ""
        endTime.text = rec.endTime || ""
        personBox.currentIndex = Math.max(0, persons.findIndex(p => p.id === rec.patientId))
        ownerBox.currentIndex = Math.max(0, owners.indexOf(rec.owner))
        leadBox.currentIndex = Math.max(0, leads.findIndex(l => l.value === rec.remindMinutes))
        remindVisual.checked = rec.remindVisual !== false
        remindSound.checked = rec.remindSound !== false
        doneCheck.checked = rec.done === true
        repeatBox.currentIndex = Math.max(0, repeats.findIndex(r => r.value === rule.repeat))
        intervalStep.value = rule.interval || 1
        endBox.currentIndex = rule.endMode || 0
        untilDate.isoDate = rule.until || ""
        countStep.value = rule.count || 10
        monthBox.currentIndex = Math.max(0, monthModes.findIndex(m => m.value === rule.monthMode))
        if (!apptId) titleField.field.forceActiveFocus()
    }

    Confirm {
        id: confirmDelete
        title: qsTr("Termin löschen")
        message: qsTr("Termin „%1“ löschen?").arg(page.rec.title || "")
        acceptText: qsTr("Löschen"); danger: true
        onAccepted: page.doDelete("")
    }
    ScopeDialog {
        id: scopeDlg
        apptTitle: page.rec.title || ""
        onChosen: (scope) => deleting ? page.doDelete(scope) : page.doSave(scope)
    }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            spacing: Theme.gapS
            VButton { text: win.phone ? "✕" : qsTr("Abbrechen"); Accessible.name: qsTr("Abbrechen"); kind: "flat"; font.pixelSize: win.phone ? Theme.fsL + 6 : Theme.fsM; leftPadding: win.phone ? 8 : 18; rightPadding: win.phone ? 8 : 18; onClicked: page.closed("") }
            Label { text: page.apptId ? qsTr("Termin bearbeiten") : qsTr("Neuer Termin"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink
                    Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
            VButton { visible: page.apptId; text: qsTr("Löschen"); kind: "flat"; onClicked: page.remove() }
            VButton { objectName: "saveAppointmentButton"; text: qsTr("Speichern"); kind: "primary"; onClicked: page.save() }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 320      // Platz für die Bildschirmtastatur
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: Math.min(900, page.width - 2 * Theme.gapL)
            spacing: Theme.gap

            VField { id: titleField; objectName: "apptTitle"; label: qsTr("Titel"); Layout.fillWidth: true
                     placeholder: qsTr("z. B. Hausarzt – Blutabnahme") }

            // ---- Zeit
            VCheck { id: allDay; text: qsTr("ganztägig"); font.pixelSize: Theme.fsM }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                DateField { id: startDate; objectName: "apptStartDate"; label: qsTr("Beginn"); width: 200; allowEmpty: false
                            onIsoDateChanged: page.onStartChanged() }
                VField { id: startTime; objectName: "apptStartTime"; label: " "; width: 120; placeholder: "HH:MM"
                         inputMethodHints: Qt.ImhPreferNumbers; visible: !allDay.checked }
                Item { width: 24; height: 1 }
                DateField { id: endDate; label: qsTr("Ende"); width: 200; allowEmpty: false }
                VField { id: endTime; label: " "; width: 120; placeholder: "HH:MM"
                         inputMethodHints: Qt.ImhPreferNumbers; visible: !allDay.checked }
            }

            // ---- Wiederholung
            SectionTitle { text: qsTr("Wiederholung") }
            Label {
                visible: page.rec.isSeries === true
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                text: qsTr("Dieser Termin gehört zu einer Serie: %1. Beim Speichern wird gefragt, ob die Änderung nur für ihn, für alle folgenden oder für die ganze Serie gilt.").arg(page.rec.seriesText || "")
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                VCombo { id: repeatBox; objectName: "apptRepeat"; width: 320; model: page.repeats; textRole: "text"
                         onActivated: {
                             if ((page.repeatChoice === 6 || page.repeatChoice === 7) && intervalStep.value < 2) intervalStep.value = 2
                             if ((page.repeatChoice === 3 || page.repeatChoice === 6) && page.weekdays === 0)
                                 page.weekdays = 1 << (new Date(startDate.isoDate + "T12:00:00").getDay() + 6) % 7
                         } }
                VStepper {
                    id: intervalStep
                    objectName: "apptInterval"
                    visible: page.repeating && page.repeatChoice !== 2
                    from: (page.repeatChoice === 6 || page.repeatChoice === 7) ? 2 : 1
                    to: 99
                    label: " "
                    text: {
                        const n = value
                        switch (page.repeatChoice) {
                        case 1: case 7: return n === 1 ? qsTr("jeden Tag") : qsTr("alle %1 Tage").arg(n)
                        case 3: case 6: return n === 1 ? qsTr("jede Woche") : qsTr("alle %1 Wochen").arg(n)
                        case 4: return n === 1 ? qsTr("jeden Monat") : qsTr("alle %1 Monate").arg(n)
                        case 5: return n === 1 ? qsTr("jedes Jahr") : qsTr("alle %1 Jahre").arg(n)
                        }
                        return String(n)
                    }
                }
            }
            Flow {
                visible: page.weekly
                Layout.fillWidth: true
                spacing: Theme.gapS
                Repeater {
                    model: app.weekdayNames()
                    VChip { text: modelData; checkable: false; checked: (page.weekdays & (1 << index)) !== 0
                            onClicked: page.weekdays = page.weekdays ^ (1 << index) }
                }
            }
            VCombo { id: monthBox; visible: page.repeatChoice === 4; Layout.preferredWidth: 360
                     model: page.monthModes; textRole: "text" }
            Flow {
                visible: page.repeating
                Layout.fillWidth: true
                spacing: Theme.gapS
                VCombo { id: endBox; width: 280; label: qsTr("Ende der Serie")
                         model: [qsTr("ohne Ende"), qsTr("bis einschließlich"), qsTr("nach Anzahl Terminen")] }
                DateField { id: untilDate; visible: page.endChoice === 1; label: " "; width: 200; allowEmpty: false }
                VStepper { id: countStep; visible: page.endChoice === 2; label: " "; from: 1; to: 999
                           text: qsTr("%1 Termine").arg(value) }
            }
            Label {
                objectName: "apptPreview"
                visible: page.repeating && text !== ""
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                text: page.repeating ? app.apptRulePreview(page.collectRule(), startDate.isoDate) : ""
            }

            // ---- Angaben
            SectionTitle { text: qsTr("Angaben") }
            GridLayout {
                Layout.fillWidth: true
                columns: win.wide ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gap
                VField { id: locField; label: qsTr("Ort"); Layout.fillWidth: true; Layout.preferredWidth: 10 }
                VCombo { id: personBox; label: qsTr("Person"); Layout.fillWidth: true; Layout.preferredWidth: 10; model: page.persons; textRole: "name" }
                VCombo { id: ownerBox; label: qsTr("Zuständig"); Layout.fillWidth: true; Layout.preferredWidth: 10; model: page.owners
                         visible: app.isAdmin && page.owners.length > 1 }
            }
            Label { text: qsTr("Priorität"); color: Theme.ink2; font.pixelSize: Theme.fsS }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                Repeater {
                    model: page.priorities
                    VChip { text: modelData.text; checkable: false; checked: page.priority === modelData.value
                            dotColor: modelData.color; onClicked: page.priority = modelData.value }
                }
            }
            VArea { id: notesArea; label: qsTr("Notiz"); Layout.fillWidth: true; minHeight: 110 }

            // ---- Erinnerung
            SectionTitle { text: qsTr("Erinnerung") }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                VCombo { id: leadBox; objectName: "apptLead"; width: 280; model: page.leads; textRole: "text" }
                VCheck { id: remindVisual; text: qsTr("Benachrichtigung"); enabled: leadBox.currentIndex > 0; font.pixelSize: Theme.fsM }
                VCheck { id: remindSound; text: qsTr("Ton"); enabled: leadBox.currentIndex > 0; font.pixelSize: Theme.fsM }
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: Theme.fsS
                text: page.rec.remindEmail ? qsTr("Die E-Mail-Erinnerung dieses Termins verschickt VitaParms am Computer.")
                                           : qsTr("Das Tablet erinnert mit einer Benachrichtigung – auch wenn VitaParms im Hintergrund ist.")
            }
            VCheck { id: doneCheck; text: qsTr("erledigt"); font.pixelSize: Theme.fsM; visible: page.apptId }
        }
    }
}
