// Arbeitsbereich: Personen (links bzw. oben), Kopf der Person, Reiter
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: home
    property var stackView
    property var persons: []
    property var personId: 0
    property var summary: ({})
    property int tab: 0
    background: Rectangle { color: Theme.paper }

    function reload() {
        persons = app.persons()
        let keep = personId || app.lastPerson()
        if (!persons.some(p => p.id === keep)) keep = persons.length ? persons[0].id : 0
        personId = keep
        summary = personId ? app.personSummary(personId) : ({})
    }
    function select(id) { personId = id; app.setLastPerson(id); summary = app.personSummary(id) }
    function measure(id) {
        stackView.push(measureComp, { personId: home.personId, vitalId: id || 0 })
    }
    function openSettings() { stackView.push(settingsComp) }
    function editMed(id) { stackView.push(medEditComp, { personId: home.personId, medId: id || 0 }) }
    function editAppointment(id) { stackView.push(apptEditComp, { personId: home.personId, apptId: id || 0 }) }
    function editPerson(newPerson) {
        stackView.push(personEditComp, { personId: newPerson ? 0 : home.personId })
    }
    Component.onCompleted: reload()
    Connections { target: app; function onDataChanged() { home.reload() } }

    Component { id: measureComp; MeasurePage { onClosed: (msg) => { stackView.pop(); if (msg) win.toast(msg) } } }
    Component { id: personEditComp; PersonEditPage {
        onClosed: (id, msg) => { stackView.pop(); if (id) home.select(id); if (msg) win.toast(msg) } } }
    Component { id: settingsComp; SettingsPage { onClosed: stackView.pop() } }
    Component { id: glucoseComp; GlucoseProfilePage { onClosed: (msg) => { stackView.pop(); if (msg) win.toast(msg) } } }
    Component { id: apptEditComp; AppointmentEditPage { onClosed: (msg) => { stackView.pop(); if (msg) win.toast(msg) } } }
    Component { id: medEditComp; MedEditPage { onClosed: (msg) => { stackView.pop(); if (msg) win.toast(msg) } } }
    // Krankenakte
    function openDocument(id) { stackView.push(docViewComp, { personId: home.personId, docId: id }) }
    function addDocument(how) { stackView.push(docEditComp, { personId: home.personId, docId: 0, startWith: how }) }
    function editDocument(id) { stackView.push(docEditComp, { personId: home.personId, docId: id }) }
    function shareDocuments(ids) { sharePopup.openFor(home.personId, ids) }
    Component { id: docViewComp; DocumentViewPage {
        onClosed: (msg) => { stackView.pop(); if (msg) win.toast(msg) }
        onEdit: (id) => home.editDocument(id)
        onShare: (ids) => home.shareDocuments(ids) } }
    Component { id: docEditComp; DocumentEditPage { onClosed: (msg) => { stackView.pop(); if (msg) win.toast(msg) } } }
    SharePopup { id: sharePopup; onShared: (msg) => win.toast(msg) }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---------------- Personen
        Rectangle {
            visible: win.wide
            Layout.preferredWidth: Theme.railWidth
            Layout.fillHeight: true
            color: Theme.surface
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.rule }
            ColumnLayout {
                anchors.fill: parent
                spacing: 0
                RowLayout {
                    Layout.margins: 16
                    spacing: 10
                    Image { source: "qrc:/icons/vitaparms.png"; sourceSize.width: 36; sourceSize.height: 36 }
                    Label { text: "VitaParms"; font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true }
                }
                Label { text: qsTr("Personen"); color: Theme.muted; font.pixelSize: Theme.fsS; Layout.leftMargin: 20; Layout.bottomMargin: 4 }
                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: home.persons
                    delegate: AbstractButton {
                        width: ListView.view.width
                        height: 64
                        onClicked: home.select(modelData.id)
                        readonly property bool current: modelData.id === home.personId
                        background: Rectangle {
                            color: current ? Theme.brandWash : parent.down ? Theme.surface2 : "transparent"
                            Rectangle { visible: current; width: 4; height: parent.height; color: Theme.brand }
                        }
                        contentItem: ColumnLayout {
                            anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 12
                            spacing: 0
                            Item { Layout.fillHeight: true }
                            Label { text: modelData.name; font.bold: current; color: Theme.ink; elide: Text.ElideRight; Layout.fillWidth: true }
                            Label { text: modelData.birthText ? qsTr("geb. %1").arg(modelData.birthText) : ""; visible: text !== ""
                                    color: Theme.muted; font.pixelSize: Theme.fsXs }
                            Item { Layout.fillHeight: true }
                        }
                    }
                }
                VButton { text: qsTr("Neue Person"); iconText: "+"; kind: "flat"; Layout.margins: 12; Layout.fillWidth: true
                          onClicked: home.editPerson(true) }
            }
        }

        // ---------------- Arbeitsbereich
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Kopfleiste
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 64
                color: Theme.surface
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 12
                    spacing: 8
                    Image { visible: !win.wide; source: "qrc:/icons/vitaparms.png"; sourceSize.width: 32; sourceSize.height: 32 }
                    VCombo {
                        visible: !win.wide
                        Layout.fillWidth: true
                        model: home.persons
                        textRole: "name"
                        currentIndex: home.persons.findIndex(p => p.id === home.personId)
                        onActivated: (i) => home.select(home.persons[i].id)
                    }
                    Item { Layout.fillWidth: win.wide }
                    // Abgleich
                    AbstractButton {
                        implicitWidth: syncRow.implicitWidth + 24; implicitHeight: 44
                        onClicked: app.syncNow()
                        visible: app.syncEnabled
                        background: Rectangle { radius: 22; color: parent.down ? Theme.brandWash : "transparent"; border.color: Theme.rule }
                        contentItem: Row {
                            id: syncRow; spacing: 8; anchors.centerIn: parent
                            BusyIndicator { running: app.syncBusy; visible: running; width: 24; height: 24; anchors.verticalCenter: parent.verticalCenter }
                            Rectangle { visible: !app.syncBusy; width: 10; height: 10; radius: 5; anchors.verticalCenter: parent.verticalCenter
                                        color: app.syncPending ? Theme.warn : Theme.ok }
                            Label { text: app.syncBusy ? qsTr("Abgleich …") : app.syncPending ? qsTr("Abgleichen") : qsTr("Abgeglichen")
                                    color: Theme.ink2; font.pixelSize: Theme.fsS; anchors.verticalCenter: parent.verticalCenter }
                        }
                    }
                    VButton { text: qsTr("Einstellungen"); kind: "flat"; onClicked: home.openSettings() }
                    VButton { objectName: "lockButton"; text: qsTr("Sperren"); kind: "flat"
                              onClicked: win.askUsbThen(function() { app.lock(); win.goStart() }, qsTr("Ohne Abgleich sperren")) }
                    VButton { objectName: "quitButton"; text: qsTr("Beenden"); kind: "flat"; onClicked: win.quitApp() }
                }
            }

            // Ohne Personen
            ColumnLayout {
                visible: home.persons.length === 0
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.margins: 40
                spacing: Theme.gap
                Label { text: qsTr("Noch keine Person angelegt"); font.pixelSize: Theme.fsXl; font.bold: true; color: Theme.ink }
                Label { text: qsTr("Legen Sie ein Stammblatt an, um Messwerte zu erfassen."); color: Theme.ink2 }
                VButton { text: qsTr("Neue Person anlegen"); kind: "primary"; onClicked: home.editPerson(true) }
                Item { Layout.fillHeight: true }
            }

            // Kopf der Person (außerhalb der Übersicht kompakt, damit Kurven und Listen Platz haben)
            ColumnLayout {
                readonly property bool compact: home.tab !== 0
                id: personHead
                visible: home.persons.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gapL; Layout.rightMargin: Theme.gapL; Layout.topMargin: Theme.gap
                spacing: 8
                RowLayout {
                    spacing: Theme.gap
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: home.summary.name || ""; font.pixelSize: Theme.fsXl; font.bold: true; color: Theme.ink
                                elide: Text.ElideRight; Layout.fillWidth: true }
                        Label {
                            visible: !personHead.compact
                            color: Theme.ink2
                            text: [home.summary.birthText ? qsTr("geb. %1").arg(home.summary.birthText) : "",
                                   home.summary.age >= 0 ? qsTr("%1 Jahre").arg(home.summary.age) : "",
                                   home.summary.sexText || ""].filter(s => s).join(", ")
                        }
                    }
                    VButton { text: qsTr("Messung erfassen"); iconText: "+"; kind: "primary"; onClicked: home.measure(0)
                              Layout.preferredHeight: Theme.touch + 4 }
                }
                // Pflegegrad und Ausweis
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: !personHead.compact && ((home.summary.careLevel || 0) > 0 || (home.summary.disabilityDegree || 0) > 0)
                    Badge { visible: (home.summary.careLevel || 0) > 0; text: home.summary.careText || ""; warn: home.summary.careSoon === true }
                    Badge { visible: (home.summary.disabilityDegree || 0) > 0; text: home.summary.disabilityText || ""; warn: home.summary.disabilitySoon === true }
                }
                Label { visible: !personHead.compact && !!home.summary.allergies; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2; textFormat: Text.StyledText
                        text: "<b>" + qsTr("Allergien:") + "</b> " + (home.summary.allergies || "").replace(/\n/g, "; ") }
                Rectangle {
                    visible: !!home.summary.emergency
                    Layout.fillWidth: true
                    implicitHeight: em.implicitHeight + 20
                    radius: 6
                    color: Theme.alertWash
                    Rectangle { width: 4; height: parent.height; color: Theme.alert; radius: 2 }
                    Label { id: em; anchors.verticalCenter: parent.verticalCenter; x: 16; width: parent.width - 28
                            wrapMode: personHead.compact ? Text.NoWrap : Text.WordWrap; elide: Text.ElideRight; textFormat: Text.StyledText; color: Theme.ink
                            text: "<b><font color='" + Theme.alert + "'>" + qsTr("Notfallmedikation:") + "</font></b> " + (home.summary.emergency || "") }
                }
            }

            // Reiter
            Item {
                visible: home.persons.length > 0
                Layout.fillWidth: true
                Layout.leftMargin: Theme.gapL; Layout.rightMargin: Theme.gapL; Layout.topMargin: 6
                implicitHeight: 52
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule }
                Flickable {
                    anchors.fill: parent
                    contentWidth: tabRow.implicitWidth
                    flickableDirection: Flickable.HorizontalFlick
                    clip: true
                    Row {
                        id: tabRow
                        height: parent.height
                        Repeater {
                            model: [qsTr("Übersicht"), qsTr("Messwerte"), qsTr("Verlaufskurven"), qsTr("Stammblatt"), qsTr("Medikamente"), qsTr("Termine"), qsTr("Krankenakte")]
                            delegate: AbstractButton {
                                height: tabRow.height
                                width: tl.implicitWidth + 36
                                onClicked: home.tab = index
                                contentItem: Label { id: tl; text: modelData; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                                                     font.bold: home.tab === index; color: home.tab === index ? Theme.ink : Theme.ink2 }
                                background: Rectangle {
                                    color: parent.down ? Theme.brandWash : "transparent"
                                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 3; color: Theme.brand; visible: home.tab === index }
                                }
                            }
                        }
                    }
                }
            }

            StackLayout {
                visible: home.persons.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: home.tab
                OverviewPage { personId: home.personId; active: home.tab === 0; onOpenChart: home.tab = 2; onOpenMeds: home.tab = 4; onOpenAppointments: home.tab = 5 }
                JournalPage { personId: home.personId; active: home.tab === 1; onEdit: (id) => home.measure(id)
                              onGlucoseProfile: stackView.push(glucoseComp, { personId: home.personId }) }
                ChartPage { personId: home.personId; active: home.tab === 2 }
                PersonPage { personId: home.personId; active: home.tab === 3; onEditRequested: home.editPerson(false) }
                MedsPage { personId: home.personId; active: home.tab === 4
                           onEdit: (id) => stackView.push(medEditComp, { personId: home.personId, medId: id }) }
                AppointmentsPage { personId: home.personId; active: home.tab === 5; onEdit: (id) => home.editAppointment(id) }
                DocumentsPage { personId: home.personId; active: home.tab === 6; onOpen: (id) => home.openDocument(id); onAdd: (how) => home.addDocument(how)
                                onShare: (ids) => home.shareDocuments(ids) }
            }
        }
    }
}
