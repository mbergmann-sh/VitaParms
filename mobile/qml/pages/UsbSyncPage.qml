// USB-Abgleich: das Tablet wartet, der Computer verbindet sich über das USB-Kabel (adb)
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"
import ".."

Page {
    id: page
    objectName: "usbSyncPage"
    signal closed()
    property string lastResult: ""
    property bool lastOk: true
    background: Rectangle { color: Theme.paper }

    Component.onCompleted: if (!app.startUsbSync()) { page.lastOk = false; page.lastResult = app.lastError }
    Component.onDestruction: app.stopUsbSync()

    Connections {
        target: app
        function onUsbFinished(ok, message) { page.lastOk = ok; page.lastResult = message; win.toast(message, !ok) }
        function onUsbReplaced() { win.goStart() }
    }

    header: ToolBar {
        implicitHeight: 64
        background: Rectangle { color: Theme.surface; Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.rule } }
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 16; anchors.rightMargin: 16
            VButton { text: qsTr("Zurück"); kind: "flat"; onClicked: page.closed() }
            Label { text: qsTr("USB-Abgleich mit dem Computer"); font.pixelSize: Theme.fsL; font.bold: true; color: Theme.ink
                    Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight }
            Item { width: 100 }
        }
    }

    Flickable {
        anchors.fill: parent
        contentHeight: col.implicitHeight + 96
        clip: true
        ColumnLayout {
            id: col
            x: Theme.gapL; y: Theme.gap
            width: Math.min(900, page.width - 2 * Theme.gapL)
            spacing: Theme.gap

            // ---- Code und Zustand
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: codeCol.implicitHeight + 48
                radius: Theme.radius + 4
                color: Theme.surface
                border.color: app.usbConnected ? Theme.brand : Theme.rule
                border.width: app.usbConnected ? 2 : 1
                ColumnLayout {
                    id: codeCol
                    anchors.centerIn: parent
                    width: parent.width - 48
                    spacing: 8
                    Label { text: qsTr("Code für den Computer – nur für die erste Einrichtung nötig"); color: Theme.ink2; Layout.alignment: Qt.AlignHCenter }
                    Label {
                        objectName: "usbCodeLabel"
                        text: app.usbActive ? app.usbCode : "– – –"
                        font.pixelSize: Theme.fsXxl * 2
                        font.bold: true
                        font.letterSpacing: 6
                        color: Theme.ink
                        Layout.alignment: Qt.AlignHCenter
                    }
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 10
                        Rectangle { width: 12; height: 12; radius: 6; color: app.usbConnected ? Theme.brand : Theme.muted }
                        Label { objectName: "usbStatusLabel"; text: app.usbStatus; color: Theme.ink2; wrapMode: Text.WordWrap
                                Layout.maximumWidth: codeCol.width - 30 }
                    }
                }
            }

            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                visible: page.lastResult !== ""
                text: page.lastResult
                color: page.lastOk ? Theme.ink : Theme.alert
                font.bold: true
            }

            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.ink2
                visible: !app.loggedIn
                text: qsTr("Sie sind nicht angemeldet. Der Computer kann seine Datenbank auf dieses Tablet übertragen (erste Einrichtung). Zum Abgleichen bitte vorher anmelden.")
            }

            CheckBox {
                objectName: "usbAskExitCheck"
                Layout.fillWidth: true
                text: qsTr("Beim Beenden und Sperren fragen, ob abgeglichen werden soll")
                checked: app.usbAskOnExit
                onToggled: app.usbAskOnExit = checked
                font.pixelSize: Theme.fsM
            }

            SectionTitle { text: qsTr("So geht es") }
            Repeater {
                model: [
                    qsTr("Einmalig USB-Debugging einschalten: Einstellungen → Über das Tablet → 7 × auf „Build-Nummer“ tippen. Danach Einstellungen → System → Entwickleroptionen → „USB-Debugging“ einschalten."),
                    qsTr("Tablet mit dem USB-Kabel an den Computer anschließen. Die Rückfrage „USB-Debugging zulassen?“ bestätigen (am besten mit „Immer von diesem Computer zulassen“)."),
                    qsTr("Am Computer in VitaParms „Datei → Mit Tablet über USB abgleichen…“ wählen und „Verbinden“. Verwenden beide Geräte dieselbe Datenbank, ist kein Code nötig – VitaParms muss hier nur geöffnet und angemeldet sein. Den Code oben braucht es für die erste Einrichtung oder eine andere Datenbank."),
                    qsTr("Am Computer „Abgleichen“ wählen. Neue und geänderte Einträge beider Geräte werden zusammengeführt; bei gleichzeitiger Änderung gilt die jüngere.")
                ]
                delegate: RowLayout {
                    Layout.fillWidth: true
                    spacing: 14
                    Rectangle {
                        Layout.alignment: Qt.AlignTop
                        width: 32; height: 32; radius: 16
                        color: Theme.brandWash
                        Label { anchors.centerIn: parent; text: index + 1; font.bold: true; color: Theme.brand }
                    }
                    Label { text: modelData; wrapMode: Text.WordWrap; color: Theme.ink; Layout.fillWidth: true }
                }
            }
            Label {
                Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.muted; font.pixelSize: Theme.fsS
                text: qsTr("Übertragen wird nur die verschlüsselte Datenbank; ohne Kontopasswort lässt sie sich nicht öffnen. Verbinden kann sich nur ein Computer über das USB-Kabel: ohne Code nur mit derselben Datenbank, mit dem Code, solange VitaParms läuft (der Code bleibt bis zum Beenden gleich).")
            }
        }
    }
}
