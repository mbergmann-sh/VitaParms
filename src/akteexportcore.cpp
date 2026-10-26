// SPDX-License-Identifier: MIT
#include "akteexportcore.h"
#include "appsettings.h"
#include "chartwidget.h"
#include "database.h"
#include "journalmodel.h"
#include "reportprinter.h"
#include "zipwriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <algorithm>

namespace AkteExport {

static QString tr(const char *s) { return QCoreApplication::translate("AkteExport", s); }

static QString cleanName(QString s, int maxLen = 60)
{
    s.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]")), QStringLiteral(" "));
    s = s.simplified();
    s.replace(QLatin1Char(' '), QLatin1Char('_'));
    if (s.size() > maxLen) s = s.left(maxLen);
    while (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char('_'))) s.chop(1);
    return s;
}

QString defaultArchiveName(const Patient &p)
{
    return tr("Krankenakte_%1_%2.zip")
        .arg(cleanName(p.lastName + QLatin1Char('_') + p.firstName), QDate::currentDate().toString(Qt::ISODate));
}

QString archiveFileName(int index, const MedicalDocument &d)
{
    QString n = QStringLiteral("%1_%2").arg(index, 3, 10, QLatin1Char('0'))
                    .arg(d.docDate.isValid() ? d.docDate.toString(Qt::ISODate) : tr("ohne-Datum"));
    if (!d.category.isEmpty()) n += QLatin1Char('_') + cleanName(d.category, 25);
    if (!d.title.isEmpty())    n += QLatin1Char('_') + cleanName(d.title, 60);
    return n + QStringLiteral(".pdf");
}

bool build(const QString &zipPath, const Patient &p, const Options &o, QString *error,
           const std::function<void(int, int)> &progress)
{
    Database &db = Database::instance();
    QTemporaryDir tmp;
    if (!tmp.isValid()) {
        if (error) *error = tr("Temporärer Ordner konnte nicht angelegt werden.");
        return false;
    }
    const QString root = cleanName(tr("Krankenakte_") + p.lastName + QLatin1Char('_') + p.firstName, 80)
                       + QLatin1Char('/');
    const int total = o.documentIds.size() + 5;
    int step = 0;
    auto tick = [&] { if (progress) progress(++step, total); };

    ZipWriter zip;
    zip.setPassword(o.password);
    QList<QPair<QString, QString>> reports;
    QList<QPair<QString, QByteArray>> reportFiles;
    auto readFile = [](const QString &f) { QFile x(f); return x.open(QIODevice::ReadOnly) ? x.readAll() : QByteArray(); };

    // ---- Berichte aus VitaParms
    int no = 1;
    if (o.includeSheet) {
        const QString f = tmp.filePath(QStringLiteral("s.pdf"));
        ReportPrinter::patientSheetToPdf(f, p, db.medications(p.id, false), db.admissionVitals(p.id));
        const QString name = tr("%1_Stammblatt.pdf").arg(no++, 2, 10, QLatin1Char('0'));
        reportFiles.append({ name, readFile(f) });
        reports.append({ name, tr("Persönliche Daten, Diagnosen, Aufnahmewerte, aktuelle Medikation") });
    }
    tick();
    if (o.includeMedPlan) {
        const QString f = tmp.filePath(QStringLiteral("m.pdf"));
        ReportPrinter::medicationPlanToPdf(f, p, db.medications(p.id, false));
        const QString name = tr("%1_Medikamentenplan.pdf").arg(no++, 2, 10, QLatin1Char('0'));
        reportFiles.append({ name, readFile(f) });
        reports.append({ name, tr("Aktueller Medikamentenplan (Stand %1)")
                                   .arg(QLocale().toString(QDate::currentDate(), QStringLiteral("dd.MM.yyyy"))) });
    }
    tick();
    if (o.includeVitals) {
        QDateTime from;
        const QDateTime to(QDate::currentDate().addDays(1), QTime(0, 0));
        if (o.vitalsDays > 0) from = QDateTime(QDate::currentDate().addDays(1 - o.vitalsDays), QTime(0, 0));
        const auto recs = db.vitals(p.id, from, to, true);
        if (!from.isValid()) {
            from = to.addDays(-30);
            for (const VitalRecord &r : recs) if (r.measuredAt < from) from = QDateTime(r.measuredAt.date(), QTime(0, 0));
        }
        const QString period = QStringLiteral("%1 – %2")
            .arg(QLocale().toString(from.date(), QStringLiteral("dd.MM.yyyy")),
                 QLocale().toString(QDate::currentDate(), QStringLiteral("dd.MM.yyyy")));

        JournalModel model;
        model.setRecords(recs, db.vitalExtraKeys(p.id));
        QString f = tmp.filePath(QStringLiteral("v.pdf"));
        ReportPrinter::journalToPdf(f, p, model, period);
        QString name = tr("%1_Verlauf_Vitalwerte.pdf").arg(no++, 2, 10, QLatin1Char('0'));
        reportFiles.append({ name, readFile(f) });
        reports.append({ name, tr("Alle Messwerte %1 (%2 Messungen)").arg(period).arg(recs.size()) });

        ChartSpec spec;
        spec.scale = ChartSpec::Scale::Custom;
        spec.from = from;
        spec.to = to;
        spec.records = recs;
        for (const auto &panel : VitalDefs::panels()) {
            bool has = false;
            for (const VitalRecord &r : recs)
                for (VitalKey k : panel.keys)
                    if (VitalDefs::value(r, k)) { has = true; break; }
            if (has && panel.id != QLatin1String("bmi")) spec.panelIds << panel.id;
        }
        if (!spec.panelIds.isEmpty()) {
            f = tmp.filePath(QStringLiteral("g.pdf"));
            ReportPrinter::chartsToPdf(f, p, spec);
            name = tr("%1_Verlaufsgrafik.pdf").arg(no++, 2, 10, QLatin1Char('0'));
            reportFiles.append({ name, readFile(f) });
            reports.append({ name, tr("Verlaufskurven mit Kennwerten %1").arg(period) });
        }
    }
    tick();

    // ---- Dokumente
    QList<MedicalDocument> docs;
    for (qint64 id : o.documentIds)
        if (const auto d = db.document(id)) docs << *d;

    // ---- Inhaltsverzeichnis zuerst
    {
        const QString f = tmp.filePath(QStringLiteral("i.pdf"));
        QString note = o.note;
        ReportPrinter::documentIndexToPdf(f, p, docs, reports, note);
        zip.addFile(root + tr("00_Inhaltsverzeichnis.pdf"), readFile(f));
    }
    tick();
    for (const auto &[name, data] : reportFiles)
        zip.addFile(root + name, data);
    tick();

    for (int i = 0; i < docs.size(); ++i) {
        const QByteArray data = db.documentData(docs[i].id);
        if (data.isEmpty()) continue;
        zip.addFile(root + tr("Dokumente/") + archiveFileName(i + 1, docs[i]), data);
        tick();
    }
    return zip.save(zipPath, error);
}

} // namespace AkteExport
