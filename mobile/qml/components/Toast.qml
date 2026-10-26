// Kurze Rückmeldung am unteren Rand
import QtQuick
import QtQuick.Controls
import ".."

Rectangle {
    id: t
    property bool isError: false
    function show(msg, error) { label.text = msg; isError = !!error; opacity = 1; timer.restart() }
    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 28
    width: Math.min(parent.width - 32, label.implicitWidth + 40)
    height: label.implicitHeight + 24
    radius: Theme.radius
    color: isError ? Theme.alert : Theme.ink
    opacity: 0
    visible: opacity > 0
    z: 1000
    Behavior on opacity { NumberAnimation { duration: 180 } }
    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, t.parent.width - 72)
        wrapMode: Text.WordWrap
        font.family: Theme.family; font.pixelSize: Theme.fsM
        color: Theme.paper
    }
    Timer { id: timer; interval: t.isError ? 6000 : 3000; onTriggered: t.opacity = 0 }
}
