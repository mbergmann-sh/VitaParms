// SPDX-License-Identifier: MIT
#include "akteexport.h"
#include "appsettings.h"
#include "chartwidget.h"
#include "database.h"
#include "journalmodel.h"
#include "mailer.h"
#include "reportprinter.h"
#include "zipwriter.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVBoxLayout>

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

// ====================================================================== Dialog

AkteExportDialog::AkteExportDialog(const Patient &p, const QList<qint64> &selected,
                                   const QList<qint64> &filtered, const QList<qint64> &all, QWidget *parent)
    : QDialog(parent), m_patient(p), m_selected(selected), m_filtered(filtered), m_all(all)
{
    setWindowTitle(tr("Krankenakte packen / versenden – %1").arg(p.displayName()));
    auto *lay = new QVBoxLayout(this);

    // ---- Inhalt
    auto *content = new QGroupBox(tr("Inhalt"), this);
    auto *cl = new QVBoxLayout(content);
    m_rSelected = new QRadioButton(tr("Ausgewählte Dokumente (%1)").arg(selected.size()), content);
    m_rFiltered = new QRadioButton(tr("Alle angezeigten Dokumente – aktuelle Suche/Filter (%1)").arg(filtered.size()), content);
    m_rAll = new QRadioButton(tr("Alle Dokumente (%1)").arg(all.size()), content);
    m_rNone = new QRadioButton(tr("Keine Dokumente, nur Berichte"), content);
    for (QRadioButton *r : { m_rSelected, m_rFiltered, m_rAll, m_rNone }) cl->addWidget(r);
    m_rSelected->setEnabled(!selected.isEmpty());
    (selected.size() > 1 ? m_rSelected : filtered.size() < all.size() ? m_rFiltered : m_rAll)->setChecked(true);

    m_sheet = new QCheckBox(tr("Stammblatt (Daten, Diagnosen, Aufnahmewerte, Medikation)"), content);
    m_sheet->setChecked(true);
    m_medPlan = new QCheckBox(tr("Medikamentenplan"), content);
    m_medPlan->setChecked(true);
    auto *vrow = new QHBoxLayout;
    m_vitals = new QCheckBox(tr("Verlauf und Grafik der Vitalwerte der letzten"), content);
    m_days = new QSpinBox(content);
    m_days->setRange(0, 3650);
    m_days->setValue(90);
    m_days->setSpecialValueText(tr("alle"));
    m_days->setSuffix(tr(" Tage"));
    vrow->addWidget(m_vitals);
    vrow->addWidget(m_days);
    vrow->addStretch();
    cl->addWidget(m_sheet);
    cl->addWidget(m_medPlan);
    cl->addLayout(vrow);
    cl->addWidget(new QLabel(tr("Hinweis für das Inhaltsverzeichnis (optional):"), content));
    m_note = new QPlainTextEdit(content);
    m_note->setMaximumHeight(50);
    m_note->setPlaceholderText(tr("z. B. „Unterlagen zur Vorstellung in der Kardiologie am …“"));
    cl->addWidget(m_note);
    lay->addWidget(content);

    // ---- Schutz
    auto *prot = new QGroupBox(tr("Schutz der Gesundheitsdaten"), this);
    auto *pl = new QFormLayout(prot);
    m_encrypt = new QCheckBox(tr("Archiv mit Passwort verschlüsseln (AES-256) – empfohlen"), prot);
    m_encrypt->setChecked(true);
    pl->addRow(m_encrypt);
    auto *pwRow = new QHBoxLayout;
    m_pw1 = new QLineEdit(prot);
    m_pw1->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    auto *gen = new QPushButton(tr("&Erzeugen"), prot);
    gen->setToolTip(tr("Sicheres Zufallspasswort erzeugen und anzeigen"));
    pwRow->addWidget(m_pw1);
    pwRow->addWidget(gen);
    pl->addRow(tr("Passwort:"), pwRow);
    m_pw2 = new QLineEdit(prot);
    m_pw2->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    pl->addRow(tr("Wiederholen:"), m_pw2);
    m_pwInfo = new QLabel(tr("<small>Das Passwort bitte <b>getrennt</b> übermitteln (Telefon, SMS) – "
                             "nie in derselben E-Mail. Zum Öffnen braucht der Empfänger ein Programm "
                             "mit AES-ZIP-Unterstützung, z. B. 7-Zip (Windows), Keka (macOS) oder "
                             "das Archivprogramm von Linux; der Windows-Explorer allein kann "
                             "solche Archive nicht öffnen.</small>"), prot);
    m_pwInfo->setWordWrap(true);
    pl->addRow(m_pwInfo);
    lay->addWidget(prot);
    connect(gen, &QPushButton::clicked, this, &AkteExportDialog::generatePassword);
    connect(m_encrypt, &QCheckBox::toggled, this, &AkteExportDialog::updateState);

    // ---- E-Mail
    auto *mail = new QGroupBox(tr("E-Mail (nur beim Versand)"), this);
    auto *ml = new QFormLayout(mail);
    m_to = new QLineEdit(QSettings().value(QStringLiteral("akte/lastRecipient")).toString(), mail);
    m_to->setPlaceholderText(tr("praxis@example.de"));
    ml->addRow(tr("An:"), m_to);
    QString who = p.displayName();
    if (p.birthDate.isValid()) who += tr(", geb. %1").arg(QLocale().toString(p.birthDate, QStringLiteral("dd.MM.yyyy")));
    m_subject = new QLineEdit(tr("Unterlagen %1").arg(who), mail);
    ml->addRow(tr("Betreff:"), m_subject);
    m_body = new QPlainTextEdit(mail);
    m_body->setPlainText(tr("Guten Tag,\n\nanbei erhalten Sie Unterlagen von %1.\n\n"
                            "Das ZIP-Archiv ist mit einem Passwort geschützt, das ich Ihnen auf "
                            "anderem Weg mitteile. Zum Öffnen wird ein Programm benötigt, das "
                            "AES-verschlüsselte ZIP-Dateien unterstützt (z. B. 7-Zip).\n\n"
                            "Mit freundlichen Grüßen\n").arg(who));
    m_body->setMaximumHeight(130);
    ml->addRow(tr("Text:"), m_body);
    lay->addWidget(mail);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *save = buttons->addButton(tr("ZIP &speichern…"), QDialogButtonBox::ActionRole);
    QPushButton *send = buttons->addButton(tr("Per E-&Mail senden…"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close)->setText(tr("Schließen"));
    connect(save, &QPushButton::clicked, this, &AkteExportDialog::saveZip);
    connect(send, &QPushButton::clicked, this, &AkteExportDialog::sendMail);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);
    updateState();
    resize(620, 0);
}

void AkteExportDialog::updateState()
{
    const bool on = m_encrypt->isChecked();
    m_pw1->setEnabled(on);
    m_pw2->setEnabled(on);
}

void AkteExportDialog::generatePassword()
{
    static const QString alphabet = QStringLiteral("abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789");
    QString pw;
    for (int i = 0; i < 16; ++i) {
        if (i && i % 4 == 0) pw += QLatin1Char('-');
        pw += alphabet.at(int(QRandomGenerator::system()->bounded(alphabet.size())));
    }
    m_encrypt->setChecked(true);
    m_pw1->setEchoMode(QLineEdit::Normal);
    m_pw2->setEchoMode(QLineEdit::Normal);
    m_pw1->setText(pw);
    m_pw2->setText(pw);
    m_pw1->selectAll();
    m_pw1->setFocus();
}

bool AkteExportDialog::collect(AkteExport::Options &o)
{
    o.documentIds = m_rSelected->isChecked() ? m_selected
                  : m_rFiltered->isChecked() ? m_filtered
                  : m_rAll->isChecked()      ? m_all : QList<qint64>();
    o.includeSheet = m_sheet->isChecked();
    o.includeMedPlan = m_medPlan->isChecked();
    o.includeVitals = m_vitals->isChecked();
    o.vitalsDays = m_days->value();
    o.note = m_note->toPlainText().trimmed();
    if (o.documentIds.isEmpty() && !o.includeSheet && !o.includeMedPlan && !o.includeVitals) {
        QMessageBox::information(this, windowTitle(), tr("Das Archiv wäre leer – bitte Inhalte auswählen."));
        return false;
    }
    o.password.clear();
    if (m_encrypt->isChecked()) {
        if (m_pw1->text() != m_pw2->text()) {
            QMessageBox::warning(this, windowTitle(), tr("Die Passwörter stimmen nicht überein."));
            return false;
        }
        if (m_pw1->text().size() < 8) {
            QMessageBox::warning(this, windowTitle(), tr("Bitte ein Passwort mit mindestens 8 Zeichen verwenden "
                                                         "(oder „Erzeugen“ wählen)."));
            return false;
        }
        o.password = m_pw1->text();
    }
    return true;
}

bool AkteExportDialog::buildTo(const QString &path, const AkteExport::Options &o)
{
    QProgressDialog prog(tr("Krankenakte wird gepackt …"), QString(), 0, 100, this);
    prog.setWindowModality(Qt::WindowModal);
    prog.setMinimumDuration(300);
    QString err;
    const bool ok = AkteExport::build(path, m_patient, o, &err, [&](int cur, int total) {
        prog.setMaximum(total);
        prog.setValue(cur);
        QApplication::processEvents();
    });
    prog.close();
    if (!ok)
        QMessageBox::critical(this, windowTitle(), tr("Das Archiv konnte nicht erstellt werden:\n%1").arg(err));
    return ok;
}

void AkteExportDialog::saveZip()
{
    AkteExport::Options o;
    if (!collect(o)) return;
    QString dir = QSettings().value(QStringLiteral("paths/export")).toString();
    if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(this, tr("Krankenakte speichern"),
        QDir(dir).filePath(AkteExport::defaultArchiveName(m_patient)), tr("ZIP-Archiv (*.zip)"));
    if (path.isEmpty()) return;
    QSettings().setValue(QStringLiteral("paths/export"), QFileInfo(path).absolutePath());
    if (!buildTo(path, o)) return;
    QMessageBox::information(this, windowTitle(),
        tr("Das Archiv wurde gespeichert:\n%1%2").arg(QDir::toNativeSeparators(path),
            o.password.isEmpty() ? tr("\n\nAchtung: Das Archiv ist nicht verschlüsselt.") : QString()));
    Mailer::revealFile(path);
}

void AkteExportDialog::sendMail()
{
    AkteExport::Options o;
    if (!collect(o)) return;
    if (o.password.isEmpty()
        && QMessageBox::warning(this, windowTitle(),
               tr("Sie möchten Gesundheitsdaten <b>unverschlüsselt</b> per E-Mail versenden.<br><br>"
                  "Normale E-Mails können unterwegs mitgelesen werden. Wirklich ohne Passwort senden?"),
               QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                            .filePath(QStringLiteral("VitaParms-Versand"));
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(AkteExport::defaultArchiveName(m_patient));
    if (!buildTo(path, o)) return;
    QSettings().setValue(QStringLiteral("akte/lastRecipient"), m_to->text().trimmed());

    QString body = m_body->toPlainText();
    if (o.password.isEmpty()) {
        // Absatz mit Passwort-Hinweis entfernen
        QStringList paras = body.split(QStringLiteral("\n\n"));
        paras.erase(std::remove_if(paras.begin(), paras.end(),
                                   [](const QString &x) { return x.contains(QStringLiteral("Passwort")); }),
                    paras.end());
        body = paras.join(QStringLiteral("\n\n"));
    }
    const auto r = Mailer::composeWithAttachment(m_to->text().trimmed(), m_subject->text(), body, path);
    QString msg;
    switch (r) {
    case Mailer::Result::Opened:
        msg = tr("Die E-Mail wurde im Mailprogramm vorbereitet. Bitte prüfen und dort absenden.");
        break;
    case Mailer::Result::OpenedWithoutAttachment:
        msg = tr("Das Mailprogramm unterstützt keine automatischen Anhänge. Bitte die Datei\n%1\n"
                 "aus dem geöffneten Ordner in die E-Mail ziehen.").arg(QDir::toNativeSeparators(path));
        break;
    case Mailer::Result::Failed:
        msg = tr("Es konnte kein Mailprogramm geöffnet werden. Das Archiv liegt hier:\n%1")
                  .arg(QDir::toNativeSeparators(path));
        break;
    }
    if (!o.password.isEmpty())
        msg += tr("\n\nDenken Sie daran, dem Empfänger das Passwort auf anderem Weg mitzuteilen.");
    msg += tr("\n\nDie Versandkopie bleibt im Ordner „VitaParms-Versand“ – nach dem Versand "
              "können Sie sie löschen.");
    QMessageBox::information(this, windowTitle(), msg);
}
