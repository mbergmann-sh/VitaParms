// Kleine Verlaufsspur (letzte 30 Tage) für die Übersicht
import QtQuick
import ".."

Canvas {
    id: c
    property var series: []      // [{color, points:[{x 0..1, y}]}]
    onSeriesChanged: requestPaint()
    property bool dark: Theme.dark
    onDarkChanged: requestPaint()
    onWidthChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        let lo = Infinity, hi = -Infinity
        for (const s of series) for (const p of s.points) { lo = Math.min(lo, p.y); hi = Math.max(hi, p.y) }
        if (!(hi >= lo)) return
        if (hi === lo) { hi += 1; lo -= 1 }
        const X = (x) => 3 + x * (width - 6), Y = (y) => height - 4 - (y - lo) / (hi - lo) * (height - 8)
        for (const s of series) {
            if (!s.points.length) continue
            ctx.strokeStyle = String(Theme.series(s.color)); ctx.lineWidth = 2; ctx.lineJoin = "round"
            ctx.beginPath()
            s.points.forEach((p, i) => i ? ctx.lineTo(X(p.x), Y(p.y)) : ctx.moveTo(X(p.x), Y(p.y)))
            ctx.stroke()
            const l = s.points[s.points.length - 1]
            ctx.fillStyle = String(Theme.series(s.color)); ctx.beginPath(); ctx.arc(X(l.x), Y(l.y), 3.5, 0, 2 * Math.PI); ctx.fill()
        }
    }
}
