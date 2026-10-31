import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

// Abschnittsüberschrift – bricht auf schmalen Bildschirmen (Smartphone) um, statt das Layout zu verbreitern
Label {
    font.family: Theme.family
    font.pixelSize: Theme.fsL
    font.bold: true
    color: Theme.ink
    topPadding: 6
    bottomPadding: 4
    wrapMode: Text.WordWrap
    Layout.fillWidth: true
}
