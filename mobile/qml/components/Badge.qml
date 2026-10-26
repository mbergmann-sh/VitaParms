// Hinweis-Plakette (Pflegegrad, Schwerbehindertenausweis)
import QtQuick
import QtQuick.Controls
import ".."

Rectangle {
    property alias text: l.text
    property bool warn: false
    implicitWidth: l.implicitWidth + 24
    implicitHeight: l.implicitHeight + 12
    radius: 6
    color: warn ? Theme.warnWash : Theme.surface
    border.color: warn ? Theme.warn : Theme.ruleStrong
    Label { id: l; anchors.centerIn: parent; color: warn ? Theme.warn : Theme.ink2; font.pixelSize: Theme.fsS }
}
