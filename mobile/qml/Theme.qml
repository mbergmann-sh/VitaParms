// VitaParms Tablet – Farben, Schrift und Maße (wie die Website: ruhig, kontrastreich, gut lesbar)
pragma Singleton
import QtQuick

QtObject {
    readonly property bool dark: app.dark

    // Farben
    readonly property color paper:     dark ? "#0F171D" : "#F3F6F8"
    readonly property color surface:   dark ? "#16212A" : "#FFFFFF"
    readonly property color surface2:  dark ? "#1C2933" : "#F8FAFB"
    readonly property color ink:       dark ? "#E6EDF2" : "#16222B"
    readonly property color ink2:      dark ? "#B8C4CD" : "#45535E"
    readonly property color muted:     dark ? "#93A1AC" : "#6B7782"
    readonly property color rule:      dark ? "#26333D" : "#D9E0E6"
    readonly property color ruleStrong: dark ? "#3A4955" : "#B9C4CD"
    readonly property color brand:     dark ? "#5DA9E0" : "#1F78B9"
    readonly property color brandInk:  dark ? "#0E171D" : "#FFFFFF"
    readonly property color brandWash: dark ? "#183247" : "#E3EFF8"
    readonly property color alert:     dark ? "#F2867B" : "#B3261E"
    readonly property color alertWash: dark ? "#3A1D1A" : "#FBE9E7"
    readonly property color warn:      dark ? "#F0B35E" : "#9A5200"
    readonly property color warnWash:  dark ? "#33261A" : "#FFF2E0"
    readonly property color ok:        dark ? "#6CCB9B" : "#1E7A4C"

    // Schrift (Atkinson Hyperlegible – für gute Lesbarkeit entworfen)
    readonly property string family: "Atkinson Hyperlegible"
    readonly property int fsXs: 14
    readonly property int fsS: 15
    readonly property int fsM: 17
    readonly property int fsL: 20
    readonly property int fsXl: 26
    readonly property int fsXxl: 34
    readonly property int fsValue: 40

    // Kurvenfarben: sehr dunkle Farben (Gewicht) im Dunkelmodus aufhellen – wie die Kurven selbst
    function series(c) {
        const col = Qt.lighter(c, 1.0)
        return dark && col.hslSaturation < 0.16 && col.hslLightness < 0.36 ? "#C8C8C8" : c
    }

    // Maße
    readonly property int touch: 52          // Mindesthöhe von Bedienelementen
    readonly property int radius: 10
    readonly property int gap: 16
    readonly property int gapS: 8
    readonly property int gapL: 24
    readonly property int railWidth: 300
}
