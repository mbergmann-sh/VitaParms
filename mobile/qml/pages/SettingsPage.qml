// Einstellungen: Abgleich, Darstellung, Messzeitpunkte, Sperre
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    signal closed()
    signal openAdmin()
    property var cfg: app.syncConfig()
    background: Rectangle { color: Theme.paper }
    property var web: app.websiteConfig()
    Connections { target: app; function onSyncStatusChanged() { page.cfg = app.syncConfig() }
                  function onWebsiteChanged() { page.web = app.websiteConfig() } }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            VButton { text: qsTr("Zurück"); kind: "flat"; onClicked: page.closed() }
            Label { text: qsTr("Einstellungen"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink; Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter }
            Item { width: 100 }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 300
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: Math.min(900, page.width - 2 * Theme.gapL)
            spacing: Theme.gap

            SectionTitle { text: qsTr("Verwaltung") }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: app.isAdmin ? qsTr("Eigenes Passwort, Anmeldekonten, Wiederherstellungsschlüssel, Sicherung und Demo-Datenbank.")
                                      : qsTr("Eigenes Passwort ändern, Demo-Datenbank.") }
            VButton { objectName: "adminButton"; text: qsTr("Verwaltung öffnen"); onClicked: page.openAdmin() }

            SectionTitle { text: qsTr("Sprache und Hilfe") }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                VChip { objectName: "langChipDe"; text: "Deutsch"; checkable: false; checked: app.language === "de"; onClicked: app.setLanguage("de") }
                VChip { objectName: "langChipEn"; text: "English"; checkable: false; checked: app.language === "en"; onClicked: app.setLanguage("en") }
                VButton { objectName: "gettingStartedButton"; text: qsTr("Erste Schritte"); kind: "flat"
                          onClicked: if (!app.openGettingStarted()) win.toast(app.lastError, true) }
            }

            SectionTitle { text: qsTr("Abgleich mit VitaParms am Computer") }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: qsTr("Die verschlüsselte Datenbank wird über Ihre eigene Website ausgetauscht. Änderungen beider Seiten werden zusammengeführt; bei gleichzeitiger Änderung derselben Angabe gilt die jüngere.") }
            CheckBox { id: syncOn; text: qsTr("Abgleich verwenden"); checked: page.cfg.enabled === true; font.pixelSize: Theme.fsM }
            CheckBox { id: syncAuto; text: qsTr("Automatisch – nach Änderungen, beim Öffnen und alle 10 Minuten"); checked: page.cfg.autoSync === true
                       enabled: syncOn.checked; font.pixelSize: Theme.fsM }
            VField { id: url; label: qsTr("Adresse der Website"); text: page.cfg.url || ""; Layout.fillWidth: true; enabled: syncOn.checked
                     inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase }
            VField { id: key; label: qsTr("Upload-Schlüssel"); text: page.cfg.uploadKey || ""; Layout.fillWidth: true; enabled: syncOn.checked
                     inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                VButton { text: qsTr("Übernehmen"); kind: "primary"
                          onClicked: { if (app.saveSyncConfig({ enabled: syncOn.checked, autoSync: syncAuto.checked, url: url.text, uploadKey: key.text })) win.toast(qsTr("Gespeichert.")); else win.toast(app.lastError, true) } }
                VButton { text: app.syncBusy ? qsTr("Wird abgeglichen …") : qsTr("Jetzt abgleichen"); enabled: syncOn.checked && !app.syncBusy; onClicked: app.syncNow() }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted
                    text: page.cfg.last ? qsTr("Zuletzt abgeglichen: %1 – %2").arg(page.cfg.last).arg(page.cfg.lastResult) : (page.cfg.lastResult || "") }

            Label { objectName: "tlsInfo"; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: Theme.fsS
                    color: page.cfg.tlsOk === false ? Theme.alert : Theme.muted; text: page.cfg.tls || "" }

            SectionTitle { text: qsTr("Website (Ansicht im Browser)") }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: qsTr("Die Website zeigt Messwerte, Kurven, Medikamente, Termine und Berichte zum Ansehen im Browser – mit dem Master-Passwort verschlüsselt. Von hier aus lässt sie sich wie am Computer mit dem aktuellen Stand aktualisieren.") }
            CheckBox { id: webOn; objectName: "webEnabled"; text: qsTr("Website vom Tablet aus aktualisieren"); checked: page.web.enabled === true; font.pixelSize: Theme.fsM }
            GridLayout {
                visible: webOn.checked
                Layout.fillWidth: true
                columns: page.width > 900 ? 2 : 1
                columnSpacing: Theme.gap; rowSpacing: Theme.gapS
                VField { id: webUrl; label: qsTr("Adresse der Website"); text: page.web.url || ""; Layout.fillWidth: true
                         inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase }
                VField { id: webKey; label: qsTr("Upload-Schlüssel"); text: page.web.uploadKey || ""; Layout.fillWidth: true
                         inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData }
                VField { id: webPw; objectName: "webPassword"; label: page.web.hasPassword ? qsTr("Master-Passwort (leer = unverändert)") : qsTr("Master-Passwort der Website")
                         echoMode: TextInput.Password; Layout.fillWidth: true
                         inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData }
                VField { id: webPw2; objectName: "webPassword2"; label: qsTr("Passwort wiederholen"); echoMode: TextInput.Password; Layout.fillWidth: true
                         inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText | Qt.ImhSensitiveData }
            }
            CheckBox { id: webAfter; visible: webOn.checked; text: qsTr("Nach jedem Abgleich automatisch aktualisieren"); checked: page.web.afterSync === true; font.pixelSize: Theme.fsM }
            Label { visible: webOn.checked; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: Theme.fsS
                    text: qsTr("Das Master-Passwort ist dasselbe wie am Computer (VitaParms → Extras → Website). Es bleibt verschlüsselt in der Datenbank dieses Tablets.") }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                VButton { text: qsTr("Übernehmen"); kind: "primary"
                          onClicked: { if (app.saveWebsiteConfig({ enabled: webOn.checked, url: webUrl.text, uploadKey: webKey.text, password: webPw.text, repeat: webPw2.text, afterSync: webAfter.checked })) { webPw.text = ""; webPw2.text = ""; page.web = app.websiteConfig(); win.toast(qsTr("Gespeichert.")) } else win.toast(app.lastError, true) } }
                VButton { objectName: "publishWebsiteButton"; text: page.web.busy ? qsTr("Wird aktualisiert …") : qsTr("Website jetzt aktualisieren")
                          enabled: page.web.ready === true && !page.web.busy
                          onClicked: if (!app.publishWebsite()) win.toast(app.lastError, true) }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted
                    text: page.web.last ? (page.web.lastResult || qsTr("Zuletzt aktualisiert: %1").arg(page.web.last))
                                        : (webOn.checked && !page.web.ready ? (page.web.why || "") : "") }

            SectionTitle { text: qsTr("Sprachausgabe (Medikamente stellen)") }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: qsTr("VitaParms nutzt die Sprachausgabe von Android. Bleibt es stumm: „Probe vorlesen“ tippen und – falls nötig – „Neu starten“. Die Angaben darunter helfen bei der Fehlersuche.") }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                VButton { objectName: "speechTestButton"; text: qsTr("Probe vorlesen"); onClicked: { app.testSpeech(); speechDiag.refresh() } }
                VButton { text: qsTr("Neu starten"); kind: "flat"; onClicked: { app.restartSpeech(); speechDiag.refresh(); speechTimer.restart() } }
                VButton { text: qsTr("Aktualisieren"); kind: "flat"; onClicked: speechDiag.refresh() }
            }
            Label { id: speechDiag; objectName: "speechDiagnostics"; Layout.fillWidth: true; wrapMode: Text.WordWrap
                    font.pixelSize: Theme.fsS; color: Theme.muted
                    function refresh() { text = app.speechDiagnostics() }
                    Component.onCompleted: refresh() }
            Timer { id: speechTimer; interval: 2000; onTriggered: speechDiag.refresh() }

            SectionTitle { text: qsTr("Abgleich über das USB-Kabel") }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: qsTr("Ohne Website: Tablet per USB-Kabel an den Computer anschließen und die Datenbank direkt abgleichen.") }
            VButton { objectName: "usbSyncButton"; text: qsTr("USB-Abgleich mit dem Computer"); onClicked: win.openUsbSync() }

            SectionTitle { text: qsTr("Erinnerungen") }
            CheckBox { objectName: "quarterCheck"; text: qsTr("An den Quartalswechsel erinnern (KV-Karte in den Praxen einlesen lassen)")
                       checked: app.quarterReminderEnabled(); onToggled: app.setQuarterReminderEnabled(checked); font.pixelSize: Theme.fsM }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: app.notificationsAllowed() ? qsTr("Benachrichtigungen sind erlaubt: Termine und das BZ-Tagesprofil erinnern auch, wenn VitaParms im Hintergrund ist.")
                                                     : qsTr("Benachrichtigungen sind nicht erlaubt – Erinnerungen erscheinen nur, solange VitaParms geöffnet ist.") }
            VButton { visible: !app.notificationsAllowed(); text: qsTr("Benachrichtigungen erlauben"); onClicked: app.requestNotifications() }

            SectionTitle { text: qsTr("Darstellung") }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.gapS
                Repeater {
                    model: [{ t: qsTr("wie das System"), v: "system" }, { t: qsTr("hell"), v: "light" }, { t: qsTr("dunkel"), v: "dark" }]
                    VChip { text: modelData.t; checked: app.theme === modelData.v; onClicked: app.theme = modelData.v }
                }
            }
            Label { text: qsTr("Blutzucker"); color: Theme.ink2 }
            Flow {
                spacing: Theme.gapS
                Repeater {
                    model: ["mg/dl", "mmol/l"]
                    VChip { text: modelData; checked: app.glucoseUnit() === modelData; onClicked: { app.setGlucoseUnit(modelData); checked = true } }
                }
            }

            SectionTitle { text: qsTr("Sicherheit") }
            Label { text: qsTr("Automatisch sperren nach"); color: Theme.ink2 }
            Flow {
                spacing: Theme.gapS
                Repeater {
                    model: [{ t: qsTr("nie"), v: 0 }, { t: qsTr("5 Minuten"), v: 5 }, { t: qsTr("10 Minuten"), v: 10 }, { t: qsTr("30 Minuten"), v: 30 }]
                    VChip { text: modelData.t; checked: lockMin.value === modelData.v; onClicked: { app.setAutoLockMinutes(modelData.v); lockMin.value = modelData.v } }
                }
                QtObject { id: lockMin; property int value: app.autoLockMinutes() }
            }

            SectionTitle { text: qsTr("Messzeitpunkte für Tagesprofile") }
            VArea { id: ctx; label: qsTr("Ein Eintrag pro Zeile"); text: app.contexts().join("\n"); Layout.fillWidth: true; minHeight: 160 }
            VButton { text: qsTr("Messzeitpunkte speichern"); onClicked: { app.setContexts(ctx.text.split("\n").map(s => s.trim()).filter(s => s)); win.toast(qsTr("Gespeichert.")) } }

            SectionTitle { text: qsTr("Über VitaParms") }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                    text: qsTr("VitaParms %1 – persönliches Gesundheitstagebuch, kein Medizinprodukt. Open Source (MIT-Lizenz). Angemeldet als %2.").arg(app.version).arg(app.userName) }
        }
    }
}
