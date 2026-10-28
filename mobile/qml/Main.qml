// VitaParms Tablet – Hauptfenster: Anmeldung/Einrichtung → Arbeitsbereich
import QtQuick
import QtQuick.Controls
import QtQuick.Window
import "components"
import "pages"

ApplicationWindow {
    id: win
    visible: true
    width: 1280
    height: 800
    title: "VitaParms"
    color: Theme.paper
    font.family: Theme.family
    font.pixelSize: Theme.fsM

    readonly property bool wide: width >= 900
    function toast(msg, error) { toastItem.show(msg, error) }

    onActiveFocusItemChanged: app.userActivity()

    function startPage() {
        if (!app.languageChosen) return languagePage
        if (!app.disclaimerAccepted) return disclaimerPage
        if (!app.hasDatabase) return setupPage
        return app.loggedIn ? homePage : loginPage
    }
    function goStart() { stack.replace(null, startPage()) }
    function popPage() { stack.pop() }
    function showLanguagePage() { stack.push(languagePage) }   // Bildschirmfotos (Autopilot)
    function openUsbSync() { stack.push(usbSyncPage) }
    function openImport() { stack.push(setupPage, { mode: 3, fromMenu: true }) }   // Sicherung wiederherstellen

    // Vor dem Beenden bzw. Sperren an den USB-Abgleich erinnern (einstellbar auf der USB-Seite)
    property bool usbSynced: false       // in dieser Sitzung schon abgeglichen
    property bool exitConfirmed: false
    property var afterUsbQuestion: null
    function askUsbThen(action, skipText) {
        if (!app.loggedIn || !app.usbAskOnExit) { action(); return }
        win.afterUsbQuestion = action
        usbQuestion.rejectText = skipText
        usbQuestion.open()
    }
    // „Beenden“: ggf. erst nach dem USB-Abgleich fragen, dann sperren (ausstehenden Abgleich senden) und schließen
    function quitApp() {
        win.askUsbThen(function() { win.exitConfirmed = true; if (app.loggedIn) app.lock(); Qt.quit() }, qsTr("Ohne Abgleich beenden"))
    }
    onClosing: (close) => {
        if (win.exitConfirmed || !app.loggedIn || !app.usbAskOnExit) return
        close.accepted = false
        win.askUsbThen(function() { win.exitConfirmed = true; Qt.quit() }, qsTr("Ohne Abgleich beenden"))
    }
    // BZ-Tagesprofil: fällige Messung anbieten; Quartalswechsel: Erinnerung (je Sitzung und Tag einmal)
    property var shownProfile: []
    property string quarterShown: ""
    function checkDue() {
        if (!app.loggedIn || profileQuestion.opened || quarterQuestion.opened || usbQuestion.opened) return
        const m = app.dueProfileMeasurement()
        if (m.id && win.shownProfile.indexOf(m.id) < 0) {
            win.shownProfile = win.shownProfile.concat([m.id])
            profileQuestion.due = m
            profileQuestion.open()
            return
        }
        const q = app.quarterDue()
        const tag = q.key ? q.key + "@" + new Date().toDateString() : ""
        if (q.key && win.quarterShown !== tag) {
            win.quarterShown = tag
            quarterQuestion.title = q.title
            quarterQuestion.message = q.message
            quarterQuestion.open()
        }
    }
    function measureProfile(m) {
        stack.push(profileMeasureComp, { personId: m.patientId, presetContext: m.context,
                                         presetObservation: m.subject, profileApptId: m.id })
    }
    Timer { interval: 60000; repeat: true; running: app.loggedIn; triggeredOnStart: true; onTriggered: win.checkDue() }
    Component { id: profileMeasureComp; MeasurePage { onClosed: (msg) => { stack.pop(); if (msg) win.toast(msg) } } }
    Confirm {
        id: profileQuestion
        objectName: "profileQuestion"
        property var due: ({})
        title: (due.subject || "") + " – " + (due.label || "")
        message: qsTr("Zeit für die Blutzuckermessung (%1).").arg(due.person || "")
        acceptText: qsTr("Messung erfassen")
        rejectText: qsTr("Später")
        onAccepted: win.measureProfile(due)
    }
    Confirm {
        id: quarterQuestion
        objectName: "quarterQuestion"
        acceptText: qsTr("Erledigt – nicht mehr erinnern")
        rejectText: qsTr("Morgen wieder erinnern")
        onAccepted: app.confirmQuarter()
        onRejected: app.snoozeQuarter()
    }
    Confirm {
        id: usbQuestion
        objectName: "usbQuestion"
        title: qsTr("Mit dem Computer abgleichen?")
        message: qsTr("Die Werte dieses Tablets über das USB-Kabel mit VitaParms am Computer abgleichen? Am Computer dazu „Datei → Mit Tablet über USB abgleichen…“ wählen und den Code eingeben, den das Tablet gleich anzeigt.")
        acceptText: qsTr("USB-Abgleich öffnen")
        onAccepted: win.openUsbSync()
        onRejected: if (win.afterUsbQuestion) win.afterUsbQuestion()
    }

    StackView {
        id: stack
        objectName: "stack"
        anchors.fill: parent
        initialItem: win.startPage()
        onCurrentItemChanged: app.userActivity()
        // ruhige Übergänge ohne Schwung
        pushEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 } }
        pushExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 140 } }
        popEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 } }
        popExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 140 } }
        replaceEnter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 140 } }
        replaceExit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 140 } }
    }

    PointHandler { onActiveChanged: if (active) app.userActivity() }
    Connections { target: Qt.application; function onStateChanged() { if (Qt.application.state === Qt.ApplicationActive) win.checkDue() } }

    Component { id: languagePage; LanguagePage { onDone: win.goStart() } }
    Component { id: disclaimerPage; DisclaimerPage { onDone: win.goStart() } }
    Component { id: setupPage; SetupPage { onDone: win.goStart() } }
    Component { id: loginPage; LoginPage { onDone: win.goStart(); onSetupRequested: stack.push(setupPage)
                                         onImportRequested: stack.push(setupPage, { mode: 3, fromMenu: true }) } }
    Component { id: usbSyncPage; UsbSyncPage { onClosed: stack.pop() } }
    Component { id: homePage; HomePage { objectName: "home"; stackView: stack } }

    Connections {
        target: app
        function onLocked() { win.goStart(); win.toast(qsTr("Aus Sicherheitsgründen gesperrt – bitte erneut anmelden.")) }
        function onUsbFinished(ok, message) { if (ok) win.usbSynced = true }
        function onSessionChanged() { if (!app.loggedIn) win.usbSynced = false; else Qt.callLater(win.checkDue) }
        function onSyncFinished(ok, message) { if (app.loggedIn) win.toast(ok ? message : qsTr("Abgleich: %1").arg(message), !ok) }
        function onWebsiteFinished(ok, message) { if (app.loggedIn) win.toast(ok ? qsTr("Website aktualisiert.") : qsTr("Website: %1").arg(message), !ok) }
    }

    Toast { id: toastItem }
}
