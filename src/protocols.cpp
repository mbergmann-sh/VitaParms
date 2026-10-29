// SPDX-License-Identifier: MIT
#include "protocols.h"
#include "qtcompat.h"
#include "glucoseprofile.h"
#include "reportprinter.h"
#include "stool.h"
#include "fluid.h"
#include "vitaldefs.h"
#include "xlsxwriter.h"

#include <QCoreApplication>
#include <QLocale>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace Protocols {

static QString tr(const char *s) { return QCoreApplication::translate("Protocols", s); }

QString title(Kind k)
{
    switch (k) {
    case Kind::Stool: return tr("Stuhlprotokoll");
    case Kind::Glucose: return tr("BZ-Tagesprofile");
    case Kind::Fluid: return tr("Ein- und Ausfuhr (Bilanz)");
    }
    return {};
}

QString periodText(const Options &o)
{
    const QLocale loc;
    if (!o.from.isValid() && !o.to.isValid()) return tr("gesamter Zeitraum");
    return QStringLiteral("%1 – %2").arg(o.from.isValid() ? loc.toString(o.from, QStringLiteral("dd.MM.yyyy")) : QStringLiteral("…"),
                                         o.to.isValid() ? loc.toString(o.to, QStringLiteral("dd.MM.yyyy")) : QStringLiteral("…"));
}

static bool inRange(const Options &o, const QDate &d)
{
    return (!o.from.isValid() || d >= o.from) && (!o.to.isValid() || d <= o.to);
}

// ------------------------------------------------------------------ Stuhlprotokoll

Table stoolProtocol(const QList<VitalRecord> &records, const Options &o)
{
    const QLocale loc;
    Table t;
    t.title = title(Kind::Stool);
    t.heads = QStringList{ tr("Datum & Uhrzeit"), tr("Menge"), tr("Konsistenz (Typ 1–7)"), tr("Farbe"),
                tr("Beimengungen / Geruch"), tr("Begleitsymptome / Besonderheiten") };
    t.widths = { 15, 8, 25, 12, 17, 23 };

    QList<const VitalRecord *> all;
    for (const VitalRecord &r : records)
        if (r.stool) all << &r;
    std::sort(all.begin(), all.end(), [](const VitalRecord *a, const VitalRecord *b) { return a->measuredAt < b->measuredAt; });

    int hard = 0, normal = 0, soft = 0, none = 0;
    bool attention = false;
    QSet<QDate> days;
    for (const VitalRecord *r : std::as_const(all)) {
        const QDate d = r->measuredAt.date();
        if (!inRange(o, d)) continue;
        Table::Row row;
        row.when = r->measuredAt;
        const int type = *r->stool;
        row.cells = QStringList{ loc.toString(r->measuredAt, QStringLiteral("ddd dd.MM.yyyy HH:mm")),
                      r->stoolDetail.amount,
                      type >= 1 ? Stool::choiceText(type) : tr("Stuhlgang, Art nicht angegeben"),
                      r->stoolDetail.color, r->stoolDetail.admixture, r->stoolDetail.symptoms };
        switch (Stool::group(type)) {
        case Stool::Group::Hard: ++hard; row.alertCells << 2; break;
        case Stool::Group::Normal: ++normal; break;
        case Stool::Group::Soft: ++soft; if (type == 7) row.alertCells << 2; break;
        case Stool::Group::Unspecified: ++none; break;
        }
        if (Stool::needsAttention(r->stoolDetail)) {
            attention = true;
            row.flag = Table::Alert;
            row.alertCells << 3 << 4;
        }
        days.insert(d);
        t.rows << row;
        ++t.entries;
    }

    // Tage ohne Stuhlgang (ab dem ersten erfassten Stuhlgang, höchstens bis heute)
    int longest = 0;
    QDate firstDay = o.from, lastDay = std::min(o.to.isValid() ? o.to : QDate::currentDate(), QDate::currentDate());
    if (!all.isEmpty()) {
        const QDate firstStool = all.first()->measuredAt.date();
        if (!firstDay.isValid() || firstDay < firstStool) firstDay = firstStool;
        // letzter Stuhlgang vor dem Zeitraum: Zählung der Tage ohne läuft weiter
        QDate last;
        for (const VitalRecord *r : std::as_const(all))
            if (r->measuredAt.date() < firstDay) last = r->measuredAt.date();
        QSet<QDate> stoolDays;
        for (const VitalRecord *r : std::as_const(all)) stoolDays.insert(r->measuredAt.date());
        QList<Table::Row> empty;
        for (QDate d = firstDay; d.isValid() && d <= lastDay; d = d.addDays(1)) {
            if (stoolDays.contains(d)) {
                last = d;
                continue;
            }
            const int n = last.isValid() ? int(last.daysTo(d)) : 0;
            longest = std::max(longest, n);
            if (!o.showEmptyDays) continue;
            Table::Row row;
            row.when = QDateTime(d, QTime(0, 0));
            row.spanAfterFirst = true;
            row.cells = QStringList{ loc.toString(d, QStringLiteral("ddd dd.MM.yyyy")),
                          n >= Stool::kWarnDays ? tr("kein Stuhlgang eingetragen – %1. Tag ohne Stuhlgang").arg(n)
                          : n > 0 ? tr("kein Stuhlgang eingetragen (%1. Tag)").arg(n) : tr("kein Stuhlgang eingetragen") };
            row.flag = n >= Stool::kWarnDays ? Table::Alert : Table::Muted;
            if (row.flag == Table::Alert) row.alertCells << 1;
            empty << row;
        }
        t.rows << empty;
        std::stable_sort(t.rows.begin(), t.rows.end(), [](const Table::Row &a, const Table::Row &b) { return a.when < b.when; });
    }

    const int span = firstDay.isValid() && lastDay.isValid() && firstDay <= lastDay ? int(firstDay.daysTo(lastDay)) + 1 : 0;
    QStringList parts;
    parts << tr("%1 Stuhlgänge an %2 von %3 Tagen").arg(t.entries).arg(days.size()).arg(span);
    parts << tr("Typ 1–2 (hart): %1").arg(hard) << tr("Typ 3–4 (normal): %1").arg(normal)
          << tr("Typ 5–7 (weich bis flüssig): %1").arg(soft);
    if (none) parts << tr("ohne Angabe: %1").arg(none);
    if (longest == 1) parts << tr("längste Zeitspanne ohne Stuhlgang: 1 Tag");
    else if (longest > 1) parts << tr("längste Zeitspanne ohne Stuhlgang: %1 Tage").arg(longest);
    t.summary = t.entries ? parts.join(QStringLiteral(" · ")) : tr("Im Zeitraum ist kein Stuhlgang eingetragen.");
    t.notes << Stool::scaleNote();
    t.notes << tr("Ab dem %1. Tag ohne Stuhlgang ist die Zeile hervorgehoben.").arg(Stool::kWarnDays);
    if (attention) t.notes << Stool::attentionText();
    return t;
}

// ------------------------------------------------------------------ BZ-Tagesprofile

Table glucoseProfiles(const QList<VitalRecord> &records, const Options &o)
{
    const QLocale loc;
    const QList<GlucoseProfile::Point> pts = GlucoseProfile::points();
    const int dec = VitalDefs::decimals(VitalKey::Glucose);
    const QString unit = VitalDefs::unit(VitalKey::Glucose);
    Table t;
    t.title = title(Kind::Glucose);
    t.heads << tr("Datum");
    for (const auto &p : pts) t.heads << p.context + QStringLiteral("\n(") + p.time.toString(QStringLiteral("HH:mm")) + QLatin1Char(')');
    t.heads << tr("weitere Werte") << tr("Ø Tag");
    t.widths << 10;          // übrige Spalten: Breite nach Inhalt

    // Werte je Tag
    QMap<QDate, QList<const VitalRecord *>> byDay;
    for (const VitalRecord &r : records)
        if (r.glucose && inRange(o, r.measuredAt.date())) byDay[r.measuredAt.date()] << &r;

    const QString fasting2 = QCoreApplication::translate("AppSettings", "vor dem Frühstück");
    auto column = [&](const VitalRecord *r, const QList<QStringList> &cells) -> int {
        const QString c = r->context.trimmed();
        if (!c.isEmpty()) {
            for (int i = 0; i < pts.size(); ++i)
                if (c.compare(pts[i].context, Qt::CaseInsensitive) == 0) return i;
            if (c.compare(fasting2, Qt::CaseInsensitive) == 0) return 0;
            return -1;                                   // anderer Messzeitpunkt (z. B. nachts): „weitere Werte“
        }
        // ohne Messzeitpunkt: nächstgelegener Profilzeitpunkt (±75 min), wenn dort noch nichts steht
        int best = -1;
        int bestDiff = 75 * 60 + 1;
        for (int i = 0; i < pts.size(); ++i) {
            const int diff = std::abs(pts[i].time.secsTo(r->measuredAt.time()));
            if (diff < bestDiff && cells.value(i).isEmpty()) { best = i; bestDiff = diff; }
        }
        return best;
    };
    auto fmt = [&](double mgdl) { return loc.toString(VitalDefs::glucoseToDisplay(mgdl), 'f', dec); };
    auto isAlert = [](double mgdl) { return mgdl < 70 || mgdl > 250; };

    QList<double> colSum = vpFilledList<double>(pts.size() + 1, 0.0);
    QList<int> colN = vpFilledList<int>(pts.size() + 1, 0);
    double allSum = 0;
    int allN = 0, alerts = 0;
    for (auto it = byDay.constBegin(); it != byDay.constEnd(); ++it) {
        QList<const VitalRecord *> list = it.value();
        if (list.size() < std::max(1, o.minValues)) continue;
        std::sort(list.begin(), list.end(), [](const VitalRecord *a, const VitalRecord *b) { return a->measuredAt < b->measuredAt; });
        QList<QStringList> cells = vpFilledList<QStringList>(pts.size());
        QStringList more;
        QList<int> alertCols;
        bool moreAlert = false;
        double sum = 0;
        for (const VitalRecord *r : std::as_const(list)) {
            const int c = column(r, cells);
            const QString v = fmt(*r->glucose) + QStringLiteral(" (") + r->measuredAt.toString(QStringLiteral("HH:mm")) + QLatin1Char(')');
            if (c >= 0) {
                cells[c] << v;
                colSum[c] += *r->glucose; ++colN[c];
                if (isAlert(*r->glucose)) alertCols << c + 1;
            } else {
                more << v + (r->context.isEmpty() ? QString() : QStringLiteral(" ") + r->context);
                colSum[pts.size()] += *r->glucose; ++colN[pts.size()];
                moreAlert |= isAlert(*r->glucose);
            }
            if (isAlert(*r->glucose)) ++alerts;
            sum += *r->glucose;
            ++t.entries;
        }
        allSum += sum;
        allN += int(list.size());
        Table::Row row;
        row.when = QDateTime(it.key(), list.first()->measuredAt.time());
        row.cells << loc.toString(it.key(), QStringLiteral("ddd dd.MM.yyyy"));
        for (const QStringList &c : std::as_const(cells)) row.cells << c.join(QLatin1Char('\n'));
        row.cells << more.join(QLatin1Char('\n')) << fmt(sum / list.size());
        row.alertCells = alertCols;
        if (moreAlert) row.alertCells << pts.size() + 1;
        t.rows << row;
    }
    if (!t.rows.isEmpty()) {
        Table::Row mean, count;
        mean.flag = count.flag = Table::Summary;
        mean.cells << tr("Mittelwert");
        count.cells << tr("Anzahl");
        for (int i = 0; i <= pts.size(); ++i) {
            mean.cells << (colN[i] ? fmt(colSum[i] / colN[i]) : QString());
            count.cells << (colN[i] ? QString::number(colN[i]) : QString());
        }
        mean.cells << (allN ? fmt(allSum / allN) : QString());
        count.cells << QString::number(allN);
        t.footer << mean << count;
    }
    t.summary = t.rows.isEmpty()
        ? (o.minValues > 1 ? tr("Im Zeitraum gibt es keinen Tag mit mindestens %1 Blutzuckerwerten.").arg(o.minValues)
                           : tr("Im Zeitraum sind keine Blutzuckerwerte eingetragen."))
        : tr("Tage: %1, Blutzuckerwerte: %2, Einheit %3").arg(t.rows.size()).arg(t.entries).arg(unit)
              + (o.minValues > 1 ? QStringLiteral(" · ") + tr("nur Tage mit mindestens %1 Werten").arg(o.minValues) : QString());
    t.notes << tr("Zuordnung: nach dem Messzeitpunkt der Messung; Werte ohne Messzeitpunkt zum nächstgelegenen "
                  "Profilzeitpunkt (bis 75 Minuten Abstand), sonst unter „weitere Werte“. In Klammern die Uhrzeit.");
    if (alerts)
        t.notes << tr("Hervorgehoben: Werte unter %1 oder über %2 %3.")
                       .arg(fmt(70), fmt(250), unit);
    return t;
}

Table fluidBalance(const QList<VitalRecord> &records, const Options &o, const Patient &p)
{
    const QLocale loc;
    Table t;
    t.title = title(Kind::Fluid);
    t.heads = QStringList{ tr("Datum & Uhrzeit"), tr("Einfuhr (ml)"), tr("Art"), tr("Ausfuhr (ml)"), tr("Art"), tr("Bemerkung") };
    t.widths = { 16, 10, 16, 10, 16, 32 };
    QList<const VitalRecord *> list;
    for (const VitalRecord &r : records)
        if ((r.fluidIn || r.fluidOut) && inRange(o, r.measuredAt.date())) list << &r;
    std::sort(list.begin(), list.end(), [](const VitalRecord *a, const VitalRecord *b) { return a->measuredAt < b->measuredAt; });
    const int lo = Fluid::minIntake(p), hi = Fluid::maxIntake(p);
    int sumIn = 0, sumOut = 0, nDays = 0, under = 0, over = 0;
    auto flush = [&](const QDate &d, const Fluid::Day &day) {
        Table::Row row;
        row.when = QDateTime(d, QTime(23, 59, 59));
        row.flag = Table::Summary;
        row.cells = QStringList{ tr("Summe %1").arg(loc.toString(d, QStringLiteral("dd.MM."))), loc.toString(day.in), QString(),
                      loc.toString(day.out), QString(), tr("Bilanz %1").arg(Fluid::signedMl(day.balance())) };
        if (d < QDate::currentDate() && ((lo && day.in < lo) || (hi && day.in > hi))) row.alertCells << 1;
        if (d < QDate::currentDate() && lo && day.in < lo) ++under;
        if (hi && day.in > hi) ++over;
        t.rows << row;
        sumIn += day.in; sumOut += day.out; ++nDays;
    };
    QDate cur;
    Fluid::Day day;
    for (const VitalRecord *r : std::as_const(list)) {
        const QDate d = r->measuredAt.date();
        if (cur.isValid() && d != cur) { flush(cur, day); day = {}; }
        cur = d;
        if (r->fluidIn) { day.in += *r->fluidIn; ++day.nIn; }
        if (r->fluidOut) { day.out += *r->fluidOut; ++day.nOut; }
        Table::Row row;
        row.when = r->measuredAt;
        row.cells = QStringList{ loc.toString(r->measuredAt, QStringLiteral("ddd dd.MM.yyyy HH:mm")),
                      r->fluidIn ? loc.toString(*r->fluidIn) : QString(), r->fluidIn ? r->fluidInKind : QString(),
                      r->fluidOut ? loc.toString(*r->fluidOut) : QString(), r->fluidOut ? r->fluidOutKind : QString(),
                      r->observations.simplified() };
        t.rows << row;
        ++t.entries;
    }
    if (cur.isValid()) flush(cur, day);
    if (nDays) {
        Table::Row mean;
        mean.flag = Table::Summary;
        mean.cells = QStringList{ tr("Ø je Tag (%1 Tage)").arg(nDays), loc.toString(qRound(double(sumIn) / nDays)), QString(),
                       loc.toString(qRound(double(sumOut) / nDays)), QString(),
                       tr("Bilanz %1").arg(Fluid::signedMl(qRound(double(sumIn - sumOut) / nDays))) };
        t.footer << mean;
    }
    t.summary = t.entries ? tr("%1 Einträge an %2 Tagen").arg(t.entries).arg(nDays)
                          : tr("Im Zeitraum ist keine Ein- oder Ausfuhr eingetragen.");
    if (const QString lim = Fluid::limitsText(p); !lim.isEmpty()) {
        t.summary += QStringLiteral(" · ") + lim;
        QStringList w;
        if (under == 1) w << tr("an 1 Tag weniger getrunken als vorgesehen");
        else if (under) w << tr("an %1 Tagen weniger getrunken als vorgesehen").arg(under);
        if (over == 1) w << tr("an 1 Tag mehr als die Begrenzung");
        else if (over) w << tr("an %1 Tagen mehr als die Begrenzung").arg(over);
        if (!w.isEmpty()) t.summary += QStringLiteral(" · ") + w.join(QStringLiteral(", "));
    }
    t.notes << tr("Bilanz = Einfuhr minus Ausfuhr. Nicht gemessene Ausfuhr (Schwitzen, Atmung, Stuhl) ist nicht enthalten.");
    if (lo || hi) t.notes << tr("Hervorgehoben: Tagessummen außerhalb der Trinkmengen aus dem Stammblatt (der laufende Tag zählt bei der Mindestmenge nicht).");
    return t;
}

Table build(Kind k, const QList<VitalRecord> &records, const Options &o, const Patient &p)
{
    switch (k) {
    case Kind::Stool: return stoolProtocol(records, o);
    case Kind::Glucose: return glucoseProfiles(records, o);
    case Kind::Fluid: return fluidBalance(records, o, p);
    }
    return {};
}

// ------------------------------------------------------------------ Ausgabe

QString toHtml(const Table &t, const QString &periodText)
{
    auto esc = [](const QString &s) { return s.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")); };
    QString h = QStringLiteral("<p><b>%1</b> %2</p>").arg(tr("Zeitraum:"), esc(periodText));
    if (!t.summary.isEmpty()) h += QStringLiteral("<p>%1</p>").arg(esc(t.summary));
    h += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\" border=\"1\" "
                        "style=\"border-collapse: collapse; border-color: #8899aa;\"><thead><tr>");
    for (int i = 0; i < t.heads.size(); ++i)
        h += QStringLiteral("<th bgcolor=\"#e8eef4\" align=\"left\"%1>%2</th>")
                 .arg(t.widths.value(i) > 0 ? QStringLiteral(" width=\"%1%\"").arg(int(t.widths.value(i))) : QString(), esc(t.heads[i]));
    h += QStringLiteral("</tr></thead>");
    auto row = [&](const Table::Row &r) {
        QString bg;
        if (r.flag == Table::Summary) bg = QStringLiteral(" bgcolor=\"#f1f4f7\"");
        else if (r.flag == Table::Alert && r.spanAfterFirst) bg = QStringLiteral(" bgcolor=\"#fdf0e0\"");
        QString s = QStringLiteral("<tr%1>").arg(bg);
        const int n = r.spanAfterFirst ? 2 : int(t.heads.size());
        for (int c = 0; c < n; ++c) {
            QString v = esc(r.cells.value(c));
            if (r.alertCells.contains(c)) v = QStringLiteral("<b><font color=\"#B35C00\">%1</font></b>").arg(v);
            else if (r.flag == Table::Muted && c > 0) v = QStringLiteral("<i><font color=\"#777777\">%1</font></i>").arg(v);
            if (r.flag == Table::Summary) v = QStringLiteral("<b>%1</b>").arg(v);
            const QString span = r.spanAfterFirst && c == 1 ? QStringLiteral(" colspan=\"%1\"").arg(t.heads.size() - 1) : QString();
            s += QStringLiteral("<td valign=\"top\"%1>%2</td>").arg(span, v);
        }
        return s + QStringLiteral("</tr>");
    };
    for (const Table::Row &r : t.rows) h += row(r);
    for (const Table::Row &r : t.footer) h += row(r);
    h += QStringLiteral("</table>");
    for (const QString &n : t.notes) h += QStringLiteral("<p><small>%1</small></p>").arg(esc(n));
    return h;
}

bool toXlsx(const Table &t, const Patient &p, const QString &periodText, const QString &path, QString *error)
{
    XlsxWriter x;
    x.setTitle(QStringLiteral("VitaParms – %1 %2").arg(t.title, p.displayName()));
    const int sh = x.addSheet(t.title.left(31));
    x.setLandscape(sh, true);
    x.setCell(sh, 0, 0, QStringLiteral("VitaParms – %1").arg(t.title), XlsxWriter::Title);
    QString who = p.displayName();
    if (p.birthDate.isValid()) who += QStringLiteral(", ") + tr("geb. %1").arg(QLocale().toString(p.birthDate, QStringLiteral("dd.MM.yyyy")));
    x.setCell(sh, 1, 0, who);
    x.setCell(sh, 2, 0, tr("Zeitraum: %1").arg(periodText) + (t.summary.isEmpty() ? QString() : QStringLiteral(" – ") + t.summary));
    int row = 4;
    for (int c = 0; c < t.heads.size(); ++c) {
        x.setCell(sh, row, c, QString(t.heads[c]).replace(QLatin1Char('\n'), QLatin1Char(' ')), XlsxWriter::Header);
        x.setColumnWidth(sh, c, std::max(10.0, t.widths.value(c, 10) * 1.3));
    }
    x.freezeRows(sh, row + 1);
    ++row;
    for (const Table::Row &r : t.rows) {
        for (int c = 0; c < r.cells.size(); ++c)
            x.setCell(sh, row, c, r.cells[c], r.cells[c].contains(QLatin1Char('\n')) ? XlsxWriter::Wrap : XlsxWriter::Default);
        ++row;
    }
    for (const Table::Row &r : t.footer) {
        for (int c = 0; c < r.cells.size(); ++c) x.setCell(sh, row, c, r.cells[c], XlsxWriter::Header);
        ++row;
    }
    ++row;
    for (const QString &n : t.notes) x.setCell(sh, row++, 0, n);
    return x.save(path, error);
}

bool toPdf(const Table &t, const Patient &p, const QString &periodText, const QString &path)
{
    return ReportPrinter::htmlReportToPdf(path, p, t.title, toHtml(t, periodText), true);
}

} // namespace Protocols
