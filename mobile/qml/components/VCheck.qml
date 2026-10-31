// Ankreuzfeld, dessen Text bei wenig Platz umbricht (Smartphone) – statt die Seite zu verbreitern
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ".."

CheckBox {
    id: c
    Layout.fillWidth: true
    Layout.minimumWidth: 0
    font.family: Theme.family
    font.pixelSize: Theme.fsM
    contentItem: Text {
        text: c.text
        font: c.font
        wrapMode: Text.WordWrap
        color: c.palette.windowText
        opacity: c.enabled ? 1 : 0.5
        verticalAlignment: Text.AlignVCenter
        leftPadding: c.indicator && !c.mirrored ? c.indicator.width + c.spacing : 0
        rightPadding: c.indicator && c.mirrored ? c.indicator.width + c.spacing : 0
    }
}
