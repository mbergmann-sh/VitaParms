// SPDX-License-Identifier: MIT
#include "chartwidget.h"
#include "qtcompat.h"
#include "themes.h"
#include "stool.h"
#include "inr.h"
#include "fluid.h"

#include <QApplication>

#include <QFontMetricsF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <functional>

// ------------------------------------------------------------------ ChartSpec

void ChartSpec::rangeFor(Scale s, const QDate &ref, QDateTime &from, QDateTime &to)
{
    switch (s) {
    case Scale::Day:
        from = QDateTime(ref, QTime(0, 0));
        to   = QDateTime(ref.addDays(1), QTime(0, 0));
        break;
    case Scale::Week: {
        const QDate mon = ref.addDays(1 - ref.dayOfWeek());
        from = QDateTime(mon, QTime(0, 0));
        to   = QDateTime(mon.addDays(7), QTime(0, 0));
        break;
    }
    case Scale::Month: {
        const QDate first(ref.year(), ref.month(), 1);
        from = QDateTime(first, QTime(0, 0));
        to   = QDateTime(first.addMonths(1), QTime(0, 0));
        break;
    }
    case Scale::Year:
        from = QDateTime(QDate(ref.year(), 1, 1), QTime(0, 0));
        to   = QDateTime(QDate(ref.year() + 1, 1, 1), QTime(0, 0));
        break;
    case Scale::Custom:
        break;
    }
}

QString ChartSpec::scaleName() const
{
    switch (scale) {
    case Scale::Day:   return QObject::tr("Tagesverlauf");
    case Scale::Week:  return QObject::tr("Wochenverlauf");
    case Scale::Month: return QObject::tr("Monatsverlauf");
    case Scale::Year:  return QObject::tr("Jahresverlauf");
    case Scale::Custom: break;
    }
    return QObject::tr("Verlauf");
}

bool ChartSpec::isHidden(VitalKey k) const
{
    return hiddenKeys.contains(VitalDefs::key(k));
}

bool ChartSpec::aggregatesDays() const
{
    return from.isValid() && to.isValid() && from.daysTo(to) > 62;
}

QStringList ChartSpec::visiblePanelIds() const
{
    QStringList ids;
    for (const auto &pd : VitalDefs::panels()) {
        if (!panelIds.contains(pd.id)) continue;
        for (VitalKey k : pd.keys)
            if (!isHidden(k)) { ids << pd.id; break; }
    }
    return ids;
}

QString ChartSpec::periodText() const
{
    const QLocale loc;
    switch (scale) {
    case Scale::Day:
        return loc.toString(from.date(), QStringLiteral("dddd, dd.MM.yyyy"));
    case Scale::Week: {
        int year = 0;
        const int wk = from.date().weekNumber(&year);
        return QObject::tr("KW %1/%2: %3 – %4")
            .arg(wk).arg(year)
            .arg(loc.toString(from.date(), QStringLiteral("dd.MM.")),
                 loc.toString(to.date().addDays(-1), QStringLiteral("dd.MM.yyyy")));
    }
    case Scale::Month:
        return loc.toString(from.date(), QStringLiteral("MMMM yyyy"));
    case Scale::Year:
        return QString::number(from.date().year());
    case Scale::Custom:
        break;
    }
    return QStringLiteral("%1 – %2")
        .arg(loc.toString(from.date(), QStringLiteral("dd.MM.yyyy")),
             loc.toString(to.addSecs(-1).date(), QStringLiteral("dd.MM.yyyy")));
}

// ------------------------------------------------------------- Hilfsroutinen

namespace {

// Messpunkt; bei Tageswerten: v = Mittel, lo/hi = Min/Max, n = Anzahl Messungen
struct Pt { QDateTime t; double v; const VitalRecord *rec; double lo = 0, hi = 0; int n = 1; };

QList<Pt> seriesPoints(const ChartSpec &spec, VitalKey k)
{
    QList<Pt> pts;
    for (const VitalRecord &r : spec.records) {
        if (r.measuredAt < spec.from || r.measuredAt >= spec.to) continue;
        if (const auto v = VitalDefs::value(r, k))
            pts.append({ r.measuredAt, *v, &r });
    }
    std::sort(pts.begin(), pts.end(), [](const Pt &a, const Pt &b) { return a.t < b.t; });
    for (Pt &p : pts) p.lo = p.hi = p.v;
    return pts;
}

// Tageswerte: alle Messungen eines Kalendertags → ein Punkt (Mittag) mit Mittel, Min, Max
QList<Pt> dailyPoints(const QList<Pt> &raw)
{
    QList<Pt> out;
    for (const Pt &p : raw) {
        const QDateTime noon(p.t.date(), QTime(12, 0));
        if (!out.isEmpty() && out.last().t == noon) {
            Pt &d = out.last();
            d.v += p.v;                      // vorerst Summe
            d.lo = std::min(d.lo, p.v);
            d.hi = std::max(d.hi, p.v);
            ++d.n;
        } else {
            out.append({ noon, p.v, nullptr, p.v, p.v, 1 });
        }
    }
    for (Pt &d : out) d.v /= d.n;
    return out;
}

// "Schöne" Achsenteilung (1-2-2,5-5-10)
QList<double> niceTicks(double lo, double hi, int target, double &nlo, double &nhi)
{
    if (!(hi > lo)) {
        const double pad = std::max(1.0, std::abs(hi) * 0.05);
        lo -= pad; hi += pad;
    }
    const double rough = (hi - lo) / std::max(1, target - 1);
    const double mag = std::pow(10.0, std::floor(std::log10(rough)));
    const double n = rough / mag;
    const double step = (n <= 1 ? 1 : n <= 2 ? 2 : n <= 2.5 ? 2.5 : n <= 5 ? 5 : 10) * mag;
    nlo = std::floor(lo / step) * step;
    nhi = std::ceil(hi / step) * step;
    QList<double> t;
    for (double v = nlo; v <= nhi + step * 0.5; v += step)
        t << (std::abs(v) < step * 1e-9 ? 0.0 : v);
    return t;
}

struct TimeAxis {
    QList<QDateTime> ticks;
    QString fmt;
    QList<QDateTime> dayLines;  // Tagesgrenzen bei Stundenteilung
    QList<QDateTime> minor;     // feines Raster ohne Beschriftung
};

TimeAxis timeAxis(const QDateTime &from, const QDateTime &to, qreal width, const QFontMetricsF &fm)
{
    TimeAxis ax;
    const qint64 span = from.secsTo(to);
    struct Cand { qint64 secs; int months; QString fmt; };
    const QList<Cand> cands = {
        { 3600, 0, QStringLiteral("HH:mm") },      { 2 * 3600, 0, QStringLiteral("HH:mm") },
        { 3 * 3600, 0, QStringLiteral("HH:mm") },  { 6 * 3600, 0, QStringLiteral("HH:mm") },
        { 12 * 3600, 0, QStringLiteral("dd.MM. HH:mm") },
        { 86400, 0, QStringLiteral("ddd dd.MM.") }, { 86400, 0, QStringLiteral("dd.MM.") },
        { 2 * 86400, 0, QStringLiteral("dd.MM.") }, { 3 * 86400, 0, QStringLiteral("dd.MM.") },
        { 7 * 86400, 0, QStringLiteral("dd.MM.") }, { 14 * 86400, 0, QStringLiteral("dd.MM.") },
        { 0, 1, QStringLiteral("MMM yy") },         { 0, 3, QStringLiteral("MMM yy") },
        { 0, 6, QStringLiteral("MMM yy") },         { 0, 12, QStringLiteral("yyyy") },
    };
    const QLocale loc;
    Cand chosen = cands.last();
    for (const Cand &c : cands) {
        // Ab mehreren Tagen werden Tage beschriftet, nicht Uhrzeiten
        if (span > 2 * 86400 && !c.months && c.secs < 86400) continue;
        const double approxSecs = c.months ? c.months * 30.4 * 86400 : double(c.secs);
        const double n = span / approxSecs;
        const qreal lw = fm.horizontalAdvance(loc.toString(QDateTime(QDate(2026, 12, 30), QTime(23, 59)), c.fmt)) * 1.35;
        if (n * lw <= width && n <= 40) { chosen = c; break; }
    }
    ax.fmt = chosen.fmt;

    if (chosen.months) {
        QDate d(from.date().year(), from.date().month(), 1);
        if (chosen.months == 12) d = QDate(from.date().year(), 1, 1);
        for (; QDateTime(d, QTime(0, 0)) <= to; d = d.addMonths(chosen.months))
            if (QDateTime(d, QTime(0, 0)) >= from) ax.ticks << QDateTime(d, QTime(0, 0));
    } else if (chosen.secs >= 86400) {
        QDate d = from.date();
        if (chosen.secs == 7 * 86400 || chosen.secs == 14 * 86400)
            d = d.addDays(1 - d.dayOfWeek());           // Montag
        const int stepDays = int(chosen.secs / 86400);
        for (; QDateTime(d, QTime(0, 0)) <= to; d = d.addDays(stepDays))
            if (QDateTime(d, QTime(0, 0)) >= from) ax.ticks << QDateTime(d, QTime(0, 0));
        if (stepDays == 1 && span <= 8 * 86400)   // Wochenansicht: 6-Stunden-Raster
            for (QDateTime t = QDateTime(from.date(), QTime(0, 0)); t < to; t = t.addSecs(6 * 3600))
                if (t > from && t.time().hour() != 0) ax.minor << t;
    } else {
        const int stepH = int(chosen.secs / 3600);
        QDateTime t(from.date(), QTime(from.time().hour() - from.time().hour() % stepH, 0));
        for (; t <= to; t = t.addSecs(chosen.secs))
            if (t >= from) ax.ticks << t;
        if (span > 86400)
            for (QDate d = from.date().addDays(1); QDateTime(d, QTime(0, 0)) < to; d = d.addDays(1))
                ax.dayLines << QDateTime(d, QTime(0, 0));
    }
    return ax;
}

void drawMarker(QPainter &p, MarkerShape s, const QPointF &c, qreal r, const QColor &col)
{
    p.setBrush(col);
    switch (s) {
    case MarkerShape::Circle:
        p.drawEllipse(c, r, r);
        break;
    case MarkerShape::Square:
        p.drawRect(QRectF(c.x() - r * 0.85, c.y() - r * 0.85, r * 1.7, r * 1.7));
        break;
    case MarkerShape::Triangle: {
        const QPointF t[3] = { { c.x(), c.y() - r * 1.15 }, { c.x() - r, c.y() + r * 0.75 }, { c.x() + r, c.y() + r * 0.75 } };
        p.drawPolygon(t, 3);
        break;
    }
    case MarkerShape::TriangleDown: {
        const QPointF t[3] = { { c.x(), c.y() + r * 1.15 }, { c.x() - r, c.y() - r * 0.75 }, { c.x() + r, c.y() - r * 0.75 } };
        p.drawPolygon(t, 3);
        break;
    }
    case MarkerShape::Diamond: {
        const QPointF t[4] = { { c.x(), c.y() - r * 1.2 }, { c.x() + r, c.y() }, { c.x(), c.y() + r * 1.2 }, { c.x() - r, c.y() } };
        p.drawPolygon(t, 4);
        break;
    }
    case MarkerShape::Cross: {
        QPen old = p.pen();
        QPen pen(col, old.widthF() * 1.4);
        p.setPen(pen);
        p.drawLine(QPointF(c.x() - r, c.y() - r), QPointF(c.x() + r, c.y() + r));
        p.drawLine(QPointF(c.x() - r, c.y() + r), QPointF(c.x() + r, c.y() - r));
        p.setPen(old);
        break;
    }
    }
}

qreal g_screenFontScale = 1.0;      // Tablet: größere Schrift am Bildschirm

QFont scaledFont(const QFont &base, qreal pt, bool bold = false)
{
    QFont f = base;
    f.setPointSizeF(pt);
    f.setBold(bold);
    return f;
}

} // namespace

// ------------------------------------------------------------ ChartRenderer

void ChartSpec::setPatient(const Patient &p)
{
    const Inr::Range r = Inr::range(p);
    inrLow = r.low;
    inrHigh = r.high;
    fluidMin = Fluid::minIntake(p);
    fluidMax = Fluid::maxIntake(p);
}

QString SeriesStats::label() const
{
    return note.isEmpty() ? VitalDefs::label(key) : VitalDefs::label(key) + QStringLiteral(" (") + note + QLatin1Char(')');
}

QString SeriesStats::stoolNote() const
{
    if (key != VitalKey::Stool) return {};
    QString t = QObject::tr("an %1 von %2 Tagen").arg(days).arg(periodDays);
    if (longestGap > 0) t += QStringLiteral(", ") + (longestGap == 1 ? QObject::tr("längste Zeitspanne ohne: 1 Tag")
                                                                : QObject::tr("längste Zeitspanne ohne: %1 Tage").arg(longestGap));
    return t;
}

QList<SeriesStats> ChartRenderer::statistics(const ChartSpec &spec)
{
    QList<SeriesStats> out;
    const auto panels = VitalDefs::panels();
    for (const auto &panel : panels) {
        if (!spec.panelIds.contains(panel.id)) continue;
        for (VitalKey k : panel.keys) {
            if (spec.isHidden(k)) continue;
            const auto pts = seriesPoints(spec, k);
            SeriesStats s;
            s.key = k;
            s.count = pts.size();
            if (k == VitalKey::Stool) {
                const QDate fromD = spec.from.date();
                const QDate lastD = std::min(spec.to.addMSecs(-1).date(), QDate::currentDate());
                const auto all = Stool::perDay(spec.records);
                s.periodDays = std::max(0, int(fromD.daysTo(lastD)) + 1);
                double sum = 0;
                s.min = 99;
                for (const Pt &p : pts) {
                    const int t = int(p.v);
                    if (t < 1) continue;
                    ++s.typed;
                    s.min = std::min(s.min, p.v);
                    s.max = std::max(s.max, p.v);
                    sum += p.v;
                }
                if (s.typed) s.mean = sum / s.typed; else s.min = 0;
                for (auto it = all.constBegin(); it != all.constEnd(); ++it)
                    if (it.key() >= fromD && it.key() <= lastD) ++s.days;
                for (const Stool::Gap &g : Stool::gaps(all, fromD, lastD, 1)) {
                    const QDate a = std::max(g.first, fromD), b = std::min(g.last, lastD);
                    s.longestGap = std::max(s.longestGap, int(a.daysTo(b)) + 1);
                }
                s.note = s.stoolNote();
                out << s;
                continue;
            }
            if (k == VitalKey::FluidIn || k == VitalKey::FluidOut) {
                // Kennwerte der Tagessummen
                const auto days = Fluid::perDay(spec.records, spec.from.date(), spec.to.addMSecs(-1).date());
                QList<double> sums;
                for (const Fluid::Day &d : days) {
                    const int v = k == VitalKey::FluidIn ? d.in : d.out;
                    if ((k == VitalKey::FluidIn ? d.nIn : d.nOut) > 0) sums << v;
                }
                if (!sums.isEmpty()) {
                    s.min = *std::min_element(sums.begin(), sums.end());
                    s.max = *std::max_element(sums.begin(), sums.end());
                    double sum = 0;
                    for (double v : sums) sum += v;
                    s.mean = sum / sums.size();
                }
                s.note = QObject::tr("Tagessummen an %1 Tagen").arg(sums.size());
                if (sums.isEmpty()) s.count = 0;
                out << s;
                continue;
            }
            if (!pts.isEmpty()) {
                s.min = s.max = pts.first().v;
                double sum = 0;
                for (const Pt &p : pts) {
                    s.min = std::min(s.min, p.v);
                    s.max = std::max(s.max, p.v);
                    sum += p.v;
                }
                s.mean = sum / pts.size();
            }
            if (k == VitalKey::Inr && !pts.isEmpty() && spec.inrHigh > spec.inrLow) {
                int in = 0;
                for (const Pt &p : pts) in += p.v >= spec.inrLow - 1e-9 && p.v <= spec.inrHigh + 1e-9;
                s.note = QObject::tr("%1 von %2 im Zielbereich %3–%4").arg(in).arg(pts.size())
                             .arg(QLocale().toString(spec.inrLow, 'f', 1), QLocale().toString(spec.inrHigh, 'f', 1));
            }
            out << s;
        }
    }
    return out;
}


namespace {

// Stuhlgang: Balken je Tag (Anzahl), Abschnitte nach Bristol-Gruppe gefärbt; Zeitspannen
// von mindestens drei Tagen ohne Stuhlgang hinterlegt. Kurze Zeiträume (Tagesansicht):
// jede Eintragung als Marke zur Uhrzeit.
struct StoolCtx {
    qreal k; bool dark; bool screen;
    QColor ink, grid, gridStrong, muted, paper;
    QFont titleFont, legendFont, axisFont;
    qreal yLabelW, plotLeft, plotW;
    const TimeAxis *ax;
    std::function<qreal(const QDateTime &)> xOf;
};

void paintStoolPanel(QPainter &p, const QRectF &rect, const QRectF &pr, const StoolCtx &c, const ChartSpec &spec,
                     QList<ChartRenderer::Hit> *hits, QList<ChartRenderer::LegendHit> *legend)
{
    const qreal k = c.k;
    const QLocale loc;
    const QFontMetricsF fmTitle(c.titleFont, p.device()), fmLegend(c.legendFont, p.device()), fmAxis(c.axisFont, p.device());
    const bool off = spec.isHidden(VitalKey::Stool);
    QString title = VitalDefs::label(VitalKey::Stool);
    for (const auto &pd : VitalDefs::panels())
        if (pd.id == QLatin1String("stool")) title = pd.title;
    // Titel + Legende (Bristol-Gruppen, Hinterlegung)
    p.setFont(c.titleFont);
    p.setPen(c.ink);
    const qreal titleH = fmTitle.height() + 4 * k;
    p.drawText(QRectF(pr.left(), pr.top(), pr.width(), titleH), Qt::AlignLeft | Qt::AlignVCenter, title);
    qreal lx = pr.left() + fmTitle.horizontalAdvance(title) + 14 * k;
    const qreal legendStart = lx;
    const qreal cy = pr.top() + titleH / 2;
    p.setFont(c.legendFont);
    struct Item { QColor col; QString text; bool wash; };
    QColor washCol = Stool::groupColor(Stool::Group::Hard, c.dark);
    washCol.setAlpha(c.dark ? 70 : 48);
    const QList<Item> items = {
        { Stool::groupColor(Stool::Group::Hard, c.dark),   QObject::tr("Typ 1–2 hart"), false },
        { Stool::groupColor(Stool::Group::Normal, c.dark), QObject::tr("Typ 3–4 normal"), false },
        { Stool::groupColor(Stool::Group::Soft, c.dark),   QObject::tr("Typ 5–7 weich/flüssig"), false },
        { Stool::groupColor(Stool::Group::Unspecified, c.dark), QObject::tr("ohne Angabe"), false },
        { washCol, QObject::tr("≥ %1 Tage ohne Stuhlgang").arg(Stool::kWarnDays), true },
    };
    for (const Item &it : items) {
        const qreal tw = fmLegend.horizontalAdvance(it.text);
        if (lx + 14 * k + tw > pr.right()) break;
        p.setPen(it.wash ? QPen(c.muted, 0.5 * k) : Qt::NoPen);
        p.setBrush(off ? c.muted : it.col);
        p.drawRect(QRectF(lx, cy - 4 * k, 9 * k, 8 * k));
        QFont lf = c.legendFont;
        lf.setStrikeOut(off);
        p.setFont(lf);
        p.setPen(off ? c.muted : c.ink);
        p.drawText(QPointF(lx + 13 * k, cy + fmLegend.ascent() / 2 - fmLegend.descent() / 2), it.text);
        lx += 13 * k + tw + 12 * k;
    }
    p.setFont(c.legendFont);
    if (legend) legend->append({ QRectF(legendStart - 3 * k, pr.top(), lx - legendStart, titleH), VitalKey::Stool });

    const qreal xLabelH = fmAxis.height() + 4 * k;
    const qreal topPad = fmAxis.height() * 0.6;
    const QRectF plot(c.plotLeft, pr.top() + titleH + topPad, c.plotW, pr.height() - titleH - xLabelH - topPad);
    if (plot.height() < 10 * k) return;

    p.setPen(QPen(c.gridStrong, 0.6 * k));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);
    p.setFont(c.axisFont);
    for (const QDateTime &t : c.ax->ticks) {
        const qreal x = c.xOf(t);
        p.setPen(QPen(c.grid, 0.5 * k));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const QString lbl = loc.toString(t, c.ax->fmt);
        const qreal w = fmAxis.horizontalAdvance(lbl);
        const qreal tx = std::clamp(x - w / 2, plot.left() - c.yLabelW * 0.5, rect.right() - w);
        p.setPen(c.muted);
        p.drawText(QPointF(tx, plot.bottom() + 2 * k + fmAxis.ascent()), lbl);
    }
    for (const QDateTime &t : c.ax->dayLines) {
        const qreal x = c.xOf(t);
        p.setPen(QPen(c.gridStrong, 0.8 * k, Qt::DashLine));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }

    const QDate fromD = spec.from.date(), lastD = spec.to.addMSecs(-1).date();
    const QMap<QDate, Stool::Day> all = Stool::perDay(spec.records);
    QMap<QDate, Stool::Day> days;
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
        if (it.key() >= fromD && it.key() <= lastD) days.insert(it.key(), it.value());

    if (off || (days.isEmpty() && Stool::gaps(all, fromD, lastD).isEmpty())) {
        p.setPen(c.muted);
        p.setFont(c.legendFont);
        p.drawText(plot, Qt::AlignCenter, off
            ? QObject::tr("Alle Kurven dieses Bereichs sind ausgeblendet – zum Einblenden auf die Legende klicken")
            : QObject::tr("Kein Stuhlgang im Zeitraum eingetragen"));
        return;
    }

    p.save();
    p.setClipRect(plot);
    // Hinterlegung: Zeitspannen ohne Stuhlgang
    for (const Stool::Gap &g : Stool::gaps(all, fromD, lastD)) {
        const qreal x0 = std::max(plot.left(), c.xOf(QDateTime(g.first, QTime(0, 0))));
        const qreal x1 = std::min(plot.right(), c.xOf(QDateTime(g.last.addDays(1), QTime(0, 0))));
        if (x1 <= x0) continue;
        const QRectF band(x0, plot.top(), x1 - x0, plot.height());
        p.fillRect(band, washCol);
        const QString txt = QObject::tr("%1 Tage ohne").arg(g.days());
        p.setFont(c.axisFont);
        if (fmAxis.horizontalAdvance(txt) + 6 * k < band.width()) {
            p.setPen(c.ink);
            p.drawText(band.adjusted(3 * k, 2 * k, -3 * k, 0), Qt::AlignHCenter | Qt::AlignTop, txt);
        }
        if (hits)
            hits->append({ band.center(), QObject::tr("%1 Tage ohne Stuhlgang (%2 – %3)")
                                              .arg(g.days())
                                              .arg(loc.toString(g.first, QStringLiteral("dd.MM.yyyy")),
                                                   loc.toString(g.last, QStringLiteral("dd.MM.yyyy"))) });
    }

    const bool shortSpan = spec.from.daysTo(spec.to) <= 2;
    if (shortSpan) {
        // Einzelne Eintragungen: Marke zur Uhrzeit, Höhe = Bristol-Typ (ohne Angabe: Mitte)
        const qreal y0 = plot.bottom() - 6 * k;
        const qreal yh = plot.height() - 18 * k;
        auto yOf = [&](int t) { return y0 - yh * (t >= 1 ? (t - 1) / 6.0 : 0.5); };
        p.setFont(c.axisFont);
        for (int t = 1; t <= 7; t += 3) {
            p.setPen(QPen(c.grid, 0.5 * k));
            p.drawLine(QPointF(plot.left(), yOf(t)), QPointF(plot.right(), yOf(t)));
        }
        p.restore();
        p.setFont(c.axisFont);
        for (int t = 1; t <= 7; t += 3) {
            p.setPen(c.muted);
            p.drawText(QRectF(rect.left(), yOf(t) - fmAxis.height() / 2, c.yLabelW - 4 * k, fmAxis.height()),
                       Qt::AlignRight | Qt::AlignVCenter, QObject::tr("Typ %1").arg(t));
        }
        for (const VitalRecord &r : spec.records) {
            if (!r.stool || r.measuredAt < spec.from || r.measuredAt >= spec.to) continue;
            const QPointF pt(c.xOf(r.measuredAt), yOf(*r.stool));
            p.setPen(QPen(c.paper, 0.8 * k));
            p.setBrush(Stool::groupColor(Stool::group(*r.stool), c.dark));
            p.drawRect(QRectF(pt.x() - 4.5 * k, pt.y() - 4.5 * k, 9 * k, 9 * k));
            if (hits)
                hits->append({ pt, loc.toString(r.measuredAt, QStringLiteral("ddd dd.MM.yyyy HH:mm")) + QLatin1Char('\n')
                                       + QObject::tr("Stuhlgang: %1").arg(Stool::choiceText(*r.stool)) });
        }
        return;
    }

    // Balken je Tag
    int maxN = 3;
    for (const Stool::Day &d : days) maxN = std::max(maxN, d.count);
    const qreal unitH = (plot.height() - 4 * k) / maxN;
    p.restore();
    p.setFont(c.axisFont);
    const int stepN = maxN <= 6 ? 1 : 2;
    for (int n = 0; n <= maxN; n += stepN) {
        const qreal y = plot.bottom() - n * unitH;
        p.setPen(QPen(c.grid, 0.5 * k));
        if (n > 0) p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(c.muted);
        p.drawText(QRectF(rect.left(), y - fmAxis.height() / 2, c.yLabelW - 4 * k, fmAxis.height()),
                   Qt::AlignRight | Qt::AlignVCenter, QString::number(n) + QStringLiteral("×"));
    }
    p.save();
    p.setClipRect(plot);
    const qreal dayW = c.xOf(QDateTime(fromD.addDays(1), QTime(0, 0))) - c.xOf(QDateTime(fromD, QTime(0, 0)));
    const qreal gapW = dayW > 8 * k ? std::min(dayW * 0.22, 6 * k) : 0;
    const qreal sep = dayW > 4 * k ? 1.0 * k : 0;
    for (auto it = days.constBegin(); it != days.constEnd(); ++it) {
        const qreal x0 = c.xOf(QDateTime(it.key(), QTime(0, 0))) + gapW / 2;
        const qreal w = std::max(1.0 * k, dayW - gapW);
        QList<int> types = it.value().types;
        std::sort(types.begin(), types.end());
        qreal y = plot.bottom();
        for (int i = 0; i < types.size(); ++i) {
            const QRectF seg(x0, y - unitH + (i ? sep : 0), w, unitH - (i ? sep : 0));
            p.fillRect(seg, Stool::groupColor(Stool::group(types[i]), c.dark));
            y -= unitH;
        }
        if (hits) {
            QStringList t;
            for (int ty : std::as_const(types)) t << Stool::shortText(ty);
            hits->append({ QPointF(x0 + w / 2, plot.bottom() - types.size() * unitH / 2),
                           loc.toString(it.key(), QStringLiteral("ddd dd.MM.yyyy")) + QLatin1Char('\n')
                               + QObject::tr("%1× Stuhlgang").arg(types.size()) + QStringLiteral(": ") + t.join(QStringLiteral(", ")) });
        }
    }
    p.restore();
}

// Zeitachse (senkrechte Linien, Beschriftung, Tagesgrenzen) in einem Sonder-Bereich
void drawTimeGrid(QPainter &p, const QRectF &rect, const QRectF &plot, const StoolCtx &c)
{
    const qreal k = c.k;
    const QLocale loc;
    const QFontMetricsF fmAxis(c.axisFont, p.device());
    p.setPen(QPen(c.gridStrong, 0.6 * k));
    p.setBrush(Qt::NoBrush);
    p.drawRect(plot);
    p.setFont(c.axisFont);
    for (const QDateTime &t : c.ax->ticks) {
        const qreal x = c.xOf(t);
        p.setPen(QPen(c.grid, 0.5 * k));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        const QString lbl = loc.toString(t, c.ax->fmt);
        const qreal w = fmAxis.horizontalAdvance(lbl);
        const qreal tx = std::clamp(x - w / 2, plot.left() - c.yLabelW * 0.5, rect.right() - w);
        p.setPen(c.muted);
        p.drawText(QPointF(tx, plot.bottom() + 2 * k + fmAxis.ascent()), lbl);
    }
    for (const QDateTime &t : c.ax->dayLines) {
        const qreal x = c.xOf(t);
        p.setPen(QPen(c.gridStrong, 0.8 * k, Qt::DashLine));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
}

// Ein- und Ausfuhr: je Tag zwei Balken (Einfuhr, Ausfuhr) und die Bilanz als Raute; Mindest- und
// Höchst-Trinkmenge als gestrichelte Linien. Tagesansicht: aufsummierter Verlauf über den Tag.
void paintFluidPanel(QPainter &p, const QRectF &rect, const QRectF &pr, const StoolCtx &c, const ChartSpec &spec,
                     QList<ChartRenderer::Hit> *hits, QList<ChartRenderer::LegendHit> *legend)
{
    const qreal k = c.k;
    const QLocale loc;
    const QFontMetricsF fmTitle(c.titleFont, p.device()), fmLegend(c.legendFont, p.device()), fmAxis(c.axisFont, p.device());
    const bool offIn = spec.isHidden(VitalKey::FluidIn), offOut = spec.isHidden(VitalKey::FluidOut);
    QString title = VitalDefs::label(VitalKey::FluidIn);
    for (const auto &pd : VitalDefs::panels())
        if (pd.id == QLatin1String("fluid")) title = pd.title;
    const QColor colIn = VitalDefs::color(VitalKey::FluidIn), colOut = VitalDefs::color(VitalKey::FluidOut);
    const QColor colMin = Inr::zoneColor(Inr::Zone::InRange, c.dark), colMax = Inr::zoneColor(Inr::Zone::High, c.dark);

    p.setFont(c.titleFont);
    p.setPen(c.ink);
    const qreal titleH = fmTitle.height() + 4 * k;
    p.drawText(QRectF(pr.left(), pr.top(), pr.width(), titleH), Qt::AlignLeft | Qt::AlignVCenter, title);
    qreal lx = pr.left() + fmTitle.horizontalAdvance(title) + 14 * k;
    const qreal cy = pr.top() + titleH / 2;
    auto legendItem = [&](const QColor &col, const QString &text, bool off, int kind, std::optional<VitalKey> key) {
        const qreal tw = fmLegend.horizontalAdvance(text);
        if (lx + 18 * k + tw > pr.right()) return;
        const qreal x0 = lx;
        if (kind == 0) {            // Balken
            p.setPen(Qt::NoPen);
            p.setBrush(off ? c.muted : col);
            p.drawRect(QRectF(lx, cy - 4 * k, 9 * k, 8 * k));
        } else if (kind == 1) {     // Raute
            p.setPen(QPen(c.paper, 0.6 * k));
            drawMarker(p, MarkerShape::Diamond, QPointF(lx + 5 * k, cy), 3.2 * k, col);
        } else {                    // gestrichelte Linie
            p.setPen(QPen(col, 1.2 * k, Qt::DashLine));
            p.drawLine(QPointF(lx, cy), QPointF(lx + 14 * k, cy));
            lx += 5 * k;
        }
        QFont lf = c.legendFont;
        lf.setStrikeOut(off);
        p.setFont(lf);
        p.setPen(off ? c.muted : c.ink);
        p.drawText(QPointF(lx + 13 * k, cy + fmLegend.ascent() / 2 - fmLegend.descent() / 2), text);
        if (legend && key) legend->append({ QRectF(x0 - 3 * k, pr.top(), 13 * k + tw + 6 * k, titleH), *key });
        lx += 13 * k + tw + 12 * k;
    };
    legendItem(colIn, VitalDefs::header(VitalKey::FluidIn), offIn, 0, VitalKey::FluidIn);
    legendItem(colOut, VitalDefs::header(VitalKey::FluidOut), offOut, 0, VitalKey::FluidOut);
    if (spec.from.daysTo(spec.to) > 2) legendItem(c.ink, QObject::tr("Bilanz"), false, 1, std::nullopt);
    if (spec.fluidMin > 0) legendItem(colMin, QObject::tr("mind. %1 ml").arg(loc.toString(spec.fluidMin)), false, 2, std::nullopt);
    if (spec.fluidMax > 0) legendItem(colMax, QObject::tr("höchstens %1 ml").arg(loc.toString(spec.fluidMax)), false, 2, std::nullopt);
    p.setFont(c.legendFont);

    const qreal xLabelH = fmAxis.height() + 4 * k;
    const qreal topPad = fmAxis.height() * 0.6;
    const QRectF plot(c.plotLeft, pr.top() + titleH + topPad, c.plotW, pr.height() - titleH - xLabelH - topPad);
    if (plot.height() < 10 * k) return;
    drawTimeGrid(p, rect, plot, c);

    const QDate fromD = spec.from.date(), lastD = spec.to.addMSecs(-1).date();
    const auto days = Fluid::perDay(spec.records, fromD, lastD);
    if ((offIn && offOut) || days.isEmpty()) {
        p.setPen(c.muted);
        p.setFont(c.legendFont);
        p.drawText(plot, Qt::AlignCenter, offIn && offOut
            ? QObject::tr("Alle Kurven dieses Bereichs sind ausgeblendet – zum Einblenden auf die Legende klicken")
            : QObject::tr("Keine Ein- oder Ausfuhr im Zeitraum eingetragen"));
        return;
    }
    const bool shortSpan = spec.from.daysTo(spec.to) <= 2;

    // Wertebereich
    double lo = 0, hi = std::max({ 500, spec.fluidMin, spec.fluidMax });
    struct Step { QDateTime t; int cumIn, cumOut; const VitalRecord *r; };
    QList<Step> steps;
    if (shortSpan) {
        QList<const VitalRecord *> recs;
        for (const VitalRecord &r : spec.records)
            if ((r.fluidIn || r.fluidOut) && r.measuredAt >= spec.from && r.measuredAt < spec.to) recs << &r;
        std::sort(recs.begin(), recs.end(), [](const VitalRecord *a, const VitalRecord *b) { return a->measuredAt < b->measuredAt; });
        int ci = 0, co = 0;
        QDate day;
        for (const VitalRecord *r : std::as_const(recs)) {
            if (r->measuredAt.date() != day) { day = r->measuredAt.date(); ci = co = 0; }   // je Tag neu summieren
            ci += r->fluidIn.value_or(0);
            co += r->fluidOut.value_or(0);
            steps << Step{ r->measuredAt, ci, co, r };
            hi = std::max(hi, double(std::max(ci, co)));
        }
    } else {
        for (const Fluid::Day &d : days) {
            if (!offIn) hi = std::max(hi, double(d.in));
            if (!offOut) hi = std::max(hi, double(d.out));
            if (!offIn && !offOut) lo = std::min(lo, double(d.balance()));
        }
    }
    double ylo, yhi;
    const int targetTicks = std::clamp(int(plot.height() / (fmAxis.height() * 1.6)), 3, 6);
    const QList<double> yt = niceTicks(lo, hi * 1.05, targetTicks, ylo, yhi);
    auto yOf = [&](double v) { return plot.bottom() - plot.height() * (v - ylo) / (yhi - ylo); };
    p.setFont(c.axisFont);
    for (double v : yt) {
        const qreal y = yOf(v);
        p.setPen(QPen(v == 0 ? c.gridStrong : c.grid, (v == 0 ? 0.8 : 0.5) * k));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(c.muted);
        p.drawText(QRectF(rect.left(), y - fmAxis.height() / 2, c.yLabelW - 4 * k, fmAxis.height()),
                   Qt::AlignRight | Qt::AlignVCenter, loc.toString(qRound(v)));
    }
    p.save();
    p.setClipRect(plot.adjusted(-4 * k, -4 * k, 4 * k, 4 * k));
    for (const auto &[ml, col] : { std::pair{ spec.fluidMin, colMin }, std::pair{ spec.fluidMax, colMax } }) {
        if (ml <= 0 || offIn) continue;
        p.setPen(QPen(col, 1.1 * k, Qt::DashLine));
        p.drawLine(QPointF(plot.left(), yOf(ml)), QPointF(plot.right(), yOf(ml)));
    }
    if (shortSpan) {
        // aufsummierte Ein- und Ausfuhr als Treppen
        for (int which = 0; which < 2; ++which) {
            if ((which == 0 && offIn) || (which == 1 && offOut)) continue;
            const QColor col = which == 0 ? colIn : colOut;
            QPainterPath path;
            QDate day;
            double last = 0;
            for (const Step &st : std::as_const(steps)) {
                const double v = which == 0 ? st.cumIn : st.cumOut;
                const QPointF pt(c.xOf(st.t), yOf(v));
                if (st.t.date() != day) { day = st.t.date(); path.moveTo(c.xOf(QDateTime(day, QTime(0, 0))), yOf(0)); last = 0; }
                path.lineTo(pt.x(), yOf(last));
                path.lineTo(pt);
                last = v;
            }
            p.setPen(QPen(col, 2.0 * k, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPath(path);
            for (const Step &st : std::as_const(steps)) {
                const VitalRecord *r = st.r;
                const std::optional<int> amount = which == 0 ? r->fluidIn : r->fluidOut;
                if (!amount) continue;
                const QPointF pt(c.xOf(st.t), yOf(which == 0 ? st.cumIn : st.cumOut));
                p.setPen(QPen(c.paper, 0.6 * k));
                drawMarker(p, MarkerShape::Square, pt, 2.8 * k, col);
                if (hits) {
                    const QString kind = which == 0 ? r->fluidInKind : r->fluidOutKind;
                    hits->append({ pt, loc.toString(st.t, QStringLiteral("ddd dd.MM.yyyy HH:mm")) + QLatin1Char('\n')
                                           + VitalDefs::label(which == 0 ? VitalKey::FluidIn : VitalKey::FluidOut) + QStringLiteral(": ")
                                           + loc.toString(*amount) + QStringLiteral(" ml") + (kind.isEmpty() ? QString() : QStringLiteral(" (") + kind + QLatin1Char(')'))
                                           + QLatin1Char('\n') + QObject::tr("bis dahin am Tag: %1 ml").arg(loc.toString(which == 0 ? st.cumIn : st.cumOut)) });
                }
            }
        }
    } else {
        const qreal dayW = c.xOf(QDateTime(fromD.addDays(1), QTime(0, 0))) - c.xOf(QDateTime(fromD, QTime(0, 0)));
        const qreal gapW = dayW > 8 * k ? std::min(dayW * 0.2, 6 * k) : 0;
        const int nBars = (offIn ? 0 : 1) + (offOut ? 0 : 1);
        const qreal barW = std::max(0.8 * k, (dayW - gapW) / std::max(1, nBars));
        for (auto it = days.constBegin(); it != days.constEnd(); ++it) {
            const Fluid::Day &d = it.value();
            qreal x = c.xOf(QDateTime(it.key(), QTime(0, 0))) + gapW / 2;
            const qreal xc = x + (dayW - gapW) / 2;
            if (!offIn) {
                if (d.nIn) p.fillRect(QRectF(x, yOf(d.in), barW, yOf(0) - yOf(d.in)), colIn);
                x += barW;
            }
            if (!offOut && d.nOut) p.fillRect(QRectF(x, yOf(d.out), barW, yOf(0) - yOf(d.out)), colOut);
            if (!offIn && !offOut && d.nIn && d.nOut && dayW > 5 * k) {
                p.setPen(QPen(c.paper, 0.6 * k));
                drawMarker(p, MarkerShape::Diamond, QPointF(xc, yOf(d.balance())), std::min(3.2 * k, dayW * 0.35), c.ink);
            }
            if (hits) {
                QString t = loc.toString(it.key(), QStringLiteral("ddd dd.MM.yyyy"));
                if (d.nIn) t += QLatin1Char('\n') + VitalDefs::label(VitalKey::FluidIn) + QStringLiteral(": ") + loc.toString(d.in) + QStringLiteral(" ml");
                if (d.nOut) t += QLatin1Char('\n') + VitalDefs::label(VitalKey::FluidOut) + QStringLiteral(": ") + loc.toString(d.out) + QStringLiteral(" ml");
                if (d.nIn && d.nOut) t += QLatin1Char('\n') + QObject::tr("Bilanz: %1").arg(Fluid::signedMl(d.balance()));
                hits->append({ QPointF(xc, yOf(std::max(d.in, d.out) / 2.0)), t });
            }
        }
    }
    p.restore();
}

} // namespace

void ChartRenderer::paint(QPainter &p, const QRectF &rect, const ChartSpec &spec,
                          QList<Hit> *hits, bool screen, QList<LegendHit> *legend)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const qreal dpi = p.device() ? p.device()->logicalDpiY() : 96.0;
    const qreal k = dpi / 72.0;          // Pixel je typografischem Punkt
    const QLocale loc;

    // Am Bildschirm folgt das Diagramm einem dunklen Theme; Druck/PDF/Excel bleiben "Papier".
    const bool dark = screen && Themes::isDark();
    const QColor ink = dark ? QColor(0xd4, 0xd4, 0xd4) : QColor(0x22, 0x22, 0x22);
    const QColor grid = dark ? QColor(0x3a, 0x3a, 0x3a) : QColor(0xDD, 0xDD, 0xDD);
    const QColor gridStrong = dark ? QColor(0x5a, 0x5a, 0x5a) : QColor(0xB0, 0xB0, 0xB0);
    const QColor muted = dark ? QColor(0x9a, 0x9a, 0x9a) : QColor(0x66, 0x66, 0x66);
    const QColor paper = dark ? QApplication::palette().color(QPalette::Base) : QColor(Qt::white);
    auto seriesColor = [dark](VitalKey key) {
        const QColor c = VitalDefs::color(key);
        return (dark && c.hslSaturation() < 40 && c.lightness() < 90) ? QColor(0xc8, 0xc8, 0xc8) : c;   // nur Dunkelgrau → Hellgrau
    };
    p.fillRect(rect, paper);

    // Am Bildschirm bleiben Bereiche mit lauter ausgeblendeten Kurven sichtbar (zum
    // Wiedereinblenden über die Legende), im Druck entfallen sie.
    const QStringList shownIds = screen ? spec.panelIds : spec.visiblePanelIds();
    QList<VitalDefs::Panel> panels;
    for (const auto &pd : VitalDefs::panels())
        if (shownIds.contains(pd.id))
            panels << pd;
    const bool daily = spec.aggregatesDays();

    QFont base = p.font();
    const qreal fs = screen ? g_screenFontScale : 1.0;
    const QFont titleFont = scaledFont(base, (screen ? 9.5 : 9.0) * fs, true);
    const QFont legendFont = scaledFont(base, 8.0 * fs);
    const QFont axisFont = scaledFont(base, 7.5 * fs);
    p.setFont(axisFont);
    const QFontMetricsF fmAxis(axisFont, p.device());
    const QFontMetricsF fmTitle(titleFont, p.device());
    const QFontMetricsF fmLegend(legendFont, p.device());

    if (panels.isEmpty() || !spec.from.isValid() || !spec.to.isValid()) {
        p.setPen(muted);
        p.setFont(legendFont);
        p.drawText(rect, Qt::AlignCenter, QObject::tr("Bitte mindestens einen Bereich auswählen."));
        p.restore();
        return;
    }

    const qreal gap = 10 * k;
    const qreal panelH = (rect.height() - gap * (panels.size() - 1)) / panels.size();

    // Gemeinsame Breite der y-Beschriftung, damit alle Bereiche bündig sind
    qreal yLabelW = fmAxis.horizontalAdvance(QStringLiteral("0000,0")) + 6 * k;

    const qreal plotLeft = rect.left() + yLabelW;
    const qreal plotRight = rect.right() - 6 * k;
    const qreal plotW = plotRight - plotLeft;
    const TimeAxis ax = timeAxis(spec.from, spec.to, plotW, fmAxis);
    const qint64 span = spec.from.msecsTo(spec.to);
    auto xOf = [&](const QDateTime &t) {
        return plotLeft + plotW * double(spec.from.msecsTo(t)) / double(span);
    };

    for (int pi = 0; pi < panels.size(); ++pi) {
        const auto &panel = panels[pi];
        const QRectF pr(rect.left(), rect.top() + pi * (panelH + gap), rect.width(), panelH);
        if (panel.id == QLatin1String("stool")) {
            const StoolCtx sc{ k, dark, screen, ink, grid, gridStrong, muted, paper, titleFont, legendFont, axisFont,
                               yLabelW, plotLeft, plotW, &ax, xOf };
            paintStoolPanel(p, rect, pr, sc, spec, hits, legend);
            continue;
        }
        if (panel.id == QLatin1String("fluid")) {
            const StoolCtx sc{ k, dark, screen, ink, grid, gridStrong, muted, paper, titleFont, legendFont, axisFont,
                               yLabelW, plotLeft, plotW, &ax, xOf };
            paintFluidPanel(p, rect, pr, sc, spec, hits, legend);
            continue;
        }

        // Titel + Legende
        p.setFont(titleFont);
        p.setPen(ink);
        const qreal titleH = fmTitle.height() + 4 * k;
        p.drawText(QRectF(pr.left(), pr.top(), pr.width(), titleH), Qt::AlignLeft | Qt::AlignVCenter,
                   panel.title);
        qreal lx = pr.left() + fmTitle.horizontalAdvance(panel.title) + 14 * k;
        p.setFont(legendFont);
        for (VitalKey key : panel.keys) {
            const bool off = spec.isHidden(key);
            if (off && !screen) continue;
            const QString txt = VitalDefs::header(key);
            const qreal cy = pr.top() + titleH / 2;
            const QColor col = off ? muted : seriesColor(key);
            p.setPen(QPen(col, 1.6 * k, off ? Qt::DotLine : Qt::SolidLine));
            p.drawLine(QPointF(lx, cy), QPointF(lx + 16 * k, cy));
            if (!off) {
                p.setPen(QPen(paper, 0.6 * k));
                drawMarker(p, VitalDefs::marker(key), QPointF(lx + 8 * k, cy), 2.8 * k, col);
            }
            QFont lf = legendFont;
            lf.setStrikeOut(off);
            p.setFont(lf);
            p.setPen(off ? muted : ink);
            const qreal tw = fmLegend.horizontalAdvance(txt);
            p.drawText(QPointF(lx + 20 * k, cy + fmLegend.ascent() / 2 - fmLegend.descent() / 2), txt);
            p.setFont(legendFont);
            if (legend)
                legend->append({ QRectF(lx - 3 * k, pr.top(), 20 * k + tw + 6 * k, titleH), key });
            lx += 20 * k + tw + 14 * k;
        }
        if (daily) {
            const QString note = QObject::tr("Tageswerte: Mittelwert, senkrechter Strich = Minimum bis Maximum");
            if (lx + fmLegend.horizontalAdvance(note) < pr.right()) {
                p.setPen(muted);
                p.drawText(QPointF(lx, pr.top() + titleH / 2 + fmLegend.ascent() / 2 - fmLegend.descent() / 2), note);
            }
        }

        const qreal xLabelH = fmAxis.height() + 4 * k;
        const qreal topPad = fmAxis.height() * 0.6;
        const QRectF plot(plotLeft, pr.top() + titleH + topPad, plotW, pr.height() - titleH - xLabelH - topPad);
        if (plot.height() < 10 * k) continue;

        // Daten sammeln
        QList<QPair<VitalKey, QList<Pt>>> data;
        double lo = std::numeric_limits<double>::max(), hi = std::numeric_limits<double>::lowest();
        bool allHidden = true;
        for (VitalKey key : panel.keys) {
            if (spec.isHidden(key)) continue;
            allHidden = false;
            const auto raw = seriesPoints(spec, key);
            const auto pts = daily ? dailyPoints(raw) : raw;
            for (const Pt &q : pts) { lo = std::min(lo, q.lo); hi = std::max(hi, q.hi); }
            data.append({ key, pts });
        }
        const bool empty = lo > hi;

        // Rahmen + Gitter
        p.setPen(QPen(gridStrong, 0.6 * k));
        p.setBrush(Qt::NoBrush);
        p.drawRect(plot);

        // x-Achse
        p.setFont(axisFont);
        for (const QDateTime &t : ax.minor) {
            const qreal x = xOf(t);
            p.setPen(QPen(QColor(0xEE, 0xEE, 0xEE), 0.4 * k));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        for (const QDateTime &t : ax.dayLines) {
            const qreal x = xOf(t);
            p.setPen(QPen(gridStrong, 0.8 * k, Qt::DashLine));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
        for (const QDateTime &t : ax.ticks) {
            const qreal x = xOf(t);
            p.setPen(QPen(grid, 0.5 * k));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
            const QString lbl = loc.toString(t, ax.fmt);
            const qreal w = fmAxis.horizontalAdvance(lbl);
            qreal tx = x - w / 2;
            tx = std::clamp(tx, plot.left() - yLabelW * 0.5, rect.right() - w);
            p.setPen(muted);
            p.drawText(QPointF(tx, plot.bottom() + 2 * k + fmAxis.ascent()), lbl);
        }

        if (empty) {
            p.setPen(muted);
            p.setFont(legendFont);
            p.drawText(plot, Qt::AlignCenter, allHidden
                ? QObject::tr("Alle Kurven dieses Bereichs sind ausgeblendet – zum Einblenden auf die Legende klicken")
                : QObject::tr("Keine Messwerte im Zeitraum"));
            continue;
        }

        // INR: Zielbereich immer sichtbar; Ödeme: feste Skala Grad 0–4
        const bool inrBand = panel.id == QLatin1String("inr") && spec.inrHigh > spec.inrLow;
        if (inrBand) { lo = std::min(lo, spec.inrLow); hi = std::max(hi, spec.inrHigh); }
        if (panel.id == QLatin1String("edema")) { lo = 0; hi = Fluid::kEdemaMax; }
        if (panel.id == QLatin1String("pain")) { lo = 0; hi = 10; }

        // y-Achse
        double ylo, yhi;
        const int targetTicks = std::clamp(int(plot.height() / (fmAxis.height() * 1.6)), 3, 7);
        const double pad = (panel.id == QLatin1String("edema") || panel.id == QLatin1String("pain")) ? 0.0 : (hi - lo) * 0.08;
        const QList<double> yt = niceTicks(lo - pad, hi + pad, targetTicks, ylo, yhi);
        auto yOf = [&](double v) { return plot.bottom() - plot.height() * (v - ylo) / (yhi - ylo); };
        int ydec = 0;
        if (yt.size() > 1) {
            const double st = yt[1] - yt[0];
            ydec = st < 0.1 ? 2 : st < 1 ? 1 : 0;
        }
        for (double v : yt) {
            const qreal y = yOf(v);
            p.setPen(QPen(grid, 0.5 * k));
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
            const QString lbl = loc.toString(v, 'f', ydec);
            p.setPen(muted);
            p.drawText(QRectF(rect.left(), y - fmAxis.height() / 2, yLabelW - 4 * k, fmAxis.height()),
                       Qt::AlignRight | Qt::AlignVCenter, lbl);
        }

        if (inrBand) {
            const qreal y1 = yOf(spec.inrHigh), y0 = yOf(spec.inrLow);
            QColor band = Inr::zoneColor(Inr::Zone::InRange, dark);
            band.setAlpha(dark ? 45 : 30);
            p.fillRect(QRectF(plot.left(), y1, plot.width(), y0 - y1), band);
            band.setAlpha(160);
            p.setPen(QPen(band, 0.8 * k, Qt::DashLine));
            p.drawLine(QPointF(plot.left(), y1), QPointF(plot.right(), y1));
            p.drawLine(QPointF(plot.left(), y0), QPointF(plot.right(), y0));
            p.setFont(axisFont);
            p.setPen(Inr::zoneColor(Inr::Zone::InRange, dark));
            p.drawText(QRectF(plot.left() + 4 * k, y1 + 1 * k, plot.width() - 8 * k, fmAxis.height()), Qt::AlignRight | Qt::AlignTop,
                       QObject::tr("Zielbereich %1–%2").arg(loc.toString(spec.inrLow, 'f', 1), loc.toString(spec.inrHigh, 'f', 1)));
        }

        // Kurven
        p.save();
        p.setClipRect(plot.adjusted(-4 * k, -4 * k, 4 * k, 4 * k));
        for (const auto &[key, pts] : data) {
            const QColor col = seriesColor(key);
            if (daily) {                     // Spannweite des Tages als senkrechter Strich
                QColor band = col;
                band.setAlpha(110);
                p.setPen(QPen(band, 1.1 * k, Qt::SolidLine, Qt::FlatCap));
                for (const Pt &q : pts)
                    if (q.n > 1 && q.hi > q.lo)
                        p.drawLine(QPointF(xOf(q.t), yOf(q.lo)), QPointF(xOf(q.t), yOf(q.hi)));
            }
            if (pts.size() > 1) {
                // Lücken von mehr als einer Woche (Tageswerte) werden nicht überbrückt
                QPainterPath path;
                path.moveTo(xOf(pts[0].t), yOf(pts[0].v));
                for (int i = 1; i < pts.size(); ++i) {
                    const QPointF pt(xOf(pts[i].t), yOf(pts[i].v));
                    if (daily && pts[i - 1].t.daysTo(pts[i].t) > 7) path.moveTo(pt);
                    else path.lineTo(pt);
                }
                p.setPen(QPen(col, (daily ? 1.1 : 1.4) * k, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
            }
            const qreal mr = (pts.size() > 150 ? 1.3 : pts.size() > 60 ? 1.8 : 2.6) * k;
            if (pts.size() > 150) p.setPen(Qt::NoPen);          // dichte Reihen: Linie nicht unterbrechen
            else p.setPen(QPen(paper, 0.6 * k));
            for (const Pt &q : pts) {
                const QPointF c(xOf(q.t), yOf(q.v));
                drawMarker(p, VitalDefs::marker(key), c, mr, col);
                if (hits) {
                    const int d = VitalDefs::decimals(key);
                    const QString u = QLatin1Char(' ') + VitalDefs::unit(key);
                    QString tip;
                    if (q.rec) {
                        tip = loc.toString(q.t, QStringLiteral("ddd dd.MM.yyyy HH:mm"));
                        if (!q.rec->context.isEmpty()) tip += QStringLiteral(" (") + q.rec->context + QLatin1Char(')');
                        tip += QLatin1Char('\n') + VitalDefs::label(key) + QStringLiteral(": ") + loc.toString(q.v, 'f', d) + u;
                    } else {
                        tip = loc.toString(q.t.date(), QStringLiteral("ddd dd.MM.yyyy")) + QLatin1Char('\n')
                            + VitalDefs::label(key) + QStringLiteral(": ");
                        if (q.n == 1)
                            tip += loc.toString(q.v, 'f', d) + u;
                        else
                            tip += QObject::tr("Ø %1 (%2 – %3), %4 Messungen")
                                       .arg(loc.toString(q.v, 'f', d) + u, loc.toString(q.lo, 'f', d), loc.toString(q.hi, 'f', d))
                                       .arg(q.n);
                    }
                    hits->append({ c, tip });
                }
            }
        }
        p.restore();
    }
    p.restore();
}

QImage ChartRenderer::renderImage(const ChartSpec &spec, const QSize &size, qreal dpi)
{
    QImage img(size, QImage::Format_ARGB32_Premultiplied);
    const int dpm = qRound(dpi / 0.0254);
    img.setDotsPerMeterX(dpm);
    img.setDotsPerMeterY(dpm);
    img.fill(Qt::white);
    QPainter p(&img);
    paint(p, QRectF(QPointF(0, 0), QSizeF(size)).adjusted(8, 8, -8, -8), spec);
    return img;
}

// -------------------------------------------------------------- ChartWidget

ChartWidget::ChartWidget(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void ChartWidget::setSpec(const ChartSpec &spec)
{
    m_spec = spec;
    m_hover = -1;
    // Jeder Bereich braucht Platz für Titel/Legende, x-Beschriftung und eine lesbare Zeichenfläche.
    // Ist das Fenster zu niedrig, wird gescrollt – sonst blieben nur die Legenden sichtbar.
    const int n = std::max<qsizetype>(1, spec.panelIds.size());
    const qreal fh = QFontMetricsF(font()).height() * g_screenFontScale;
    const int perPanel = int(fh * 3.2 + 95);    // Zeichenfläche mindestens ~85 px
    setMinimumHeight(std::max(240, int(n * perPanel + (n - 1) * 10 + 16)));
    update();
}

void ChartWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Themes::isDark() ? QApplication::palette().color(QPalette::Base) : QColor(Qt::white));
    m_hits.clear();
    m_legend.clear();
    ChartRenderer::paint(p, QRectF(rect()).adjusted(10, 8, -10, -8), m_spec, &m_hits, true, &m_legend);
    if (m_hover >= 0 && m_hover < m_hits.size()) {
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(Themes::isDark() ? QColor(255, 255, 255, 180) : QColor(0, 0, 0, 160), 1.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(m_hits[m_hover].pos, 7, 7);
    }
}

void ChartWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton)
        for (const auto &l : std::as_const(m_legend))
            if (l.rect.contains(vpPos(e))) {
                emit legendClicked(l.key);
                return;
            }
    QWidget::mousePressEvent(e);
}

void ChartWidget::mouseMoveEvent(QMouseEvent *e)
{
    bool overLegend = false;
    for (const auto &l : std::as_const(m_legend))
        if (l.rect.contains(vpPos(e))) { overLegend = true; break; }
    setCursor(overLegend ? Qt::PointingHandCursor : Qt::ArrowCursor);
    if (overLegend) {
        QToolTip::showText(vpGlobalPos(e).toPoint(), tr("Klicken: Kurve ein- oder ausblenden"), this);
        return;
    }

    int best = -1;
    qreal bestD = 12.0 * 12.0;
    const QPointF pos = vpPos(e);
    for (int i = 0; i < m_hits.size(); ++i) {
        const QPointF d = m_hits[i].pos - pos;
        const qreal dd = d.x() * d.x() + d.y() * d.y();
        if (dd < bestD) { bestD = dd; best = i; }
    }
    if (best != m_hover) {
        m_hover = best;
        update();
        if (best >= 0)
            QToolTip::showText(vpGlobalPos(e).toPoint(), m_hits[best].text, this);
        else
            QToolTip::hideText();
    }
}

void ChartWidget::leaveEvent(QEvent *)
{
    if (m_hover >= 0) { m_hover = -1; update(); }
}

void ChartRenderer::setScreenFontScale(qreal f)
{
    g_screenFontScale = f > 0 ? f : 1.0;
}
